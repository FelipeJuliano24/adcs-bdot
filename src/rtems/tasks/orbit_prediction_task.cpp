#include "orbit_prediction_task.h"

#include <cmath>
#include <cstring>
#include <ctime>

extern "C" {
#include <predict/predict.h>
}

namespace {

constexpr rtems_interval ORBIT_PREDICTION_PERIOD_SECONDS = 30U;
constexpr uint32_t MINIMUM_VALID_UTC_YEAR = 2020U;
constexpr double RADIANS_TO_DEGREES = 57.2957795130823208768;

/*
 * Supply the mission TLE through the CMake cache variables
 * ORBIT_PREDICTION_TLE_LINE_1 and ORBIT_PREDICTION_TLE_LINE_2.
 *
 * Empty defaults intentionally disable propagation.  An arbitrary example TLE
 * would report a convincing, but incorrect, CubeSat location.
 */
#ifndef ORBIT_PREDICTION_TLE_LINE_1
#define ORBIT_PREDICTION_TLE_LINE_1 ""
#endif

#ifndef ORBIT_PREDICTION_TLE_LINE_2
#define ORBIT_PREDICTION_TLE_LINE_2 ""
#endif

struct location_t {
    float latitude_degrees;
    float longitude_degrees;
    uint8_t status;
};

struct geo_point_t {
    double latitude_degrees;
    double longitude_degrees;
};

/*
 * Coarse continental Brazil geofence.  It rejects the surrounding oceans and
 * countries while keeping the implementation deterministic and allocation-free.
 * It is intentionally used only for a dummy notification event, not control.
 */
constexpr geo_point_t BRAZIL_GEOFENCE[] = {
    { -33.75, -53.39 }, { -31.78, -53.74 }, { -30.18, -50.21 },
    { -28.48, -48.65 }, { -25.95, -48.35 }, { -23.98, -46.35 },
    { -22.91, -43.17 }, { -20.28, -40.20 }, { -18.10, -39.20 },
    { -15.00, -38.80 }, { -12.00, -38.70 }, {  -9.00, -35.00 },
    {  -5.00, -35.00 }, {  -1.00, -48.00 }, {   1.30, -50.80 },
    {   4.50, -51.70 }, {   5.30, -57.50 }, {   3.50, -60.00 },
    {   2.20, -60.50 }, {   1.20, -66.70 }, {  -1.00, -69.50 },
    {  -4.50, -73.00 }, {  -8.00, -73.50 }, { -10.50, -70.00 },
    { -12.50, -65.00 }, { -15.00, -60.00 }, { -17.50, -58.50 },
    { -20.00, -58.00 }, { -22.00, -57.00 }, { -24.00, -58.00 },
    { -26.00, -57.00 }, { -28.00, -57.50 }, { -30.00, -57.00 }
};

rtems_id location_lock = RTEMS_ID_NONE;
location_t latest_location = { 0.0f, 0.0f, ORBIT_PREDICTION_STATUS_UNAVAILABLE };

bool is_tle_configured()
{
    const char *const line_1 = ORBIT_PREDICTION_TLE_LINE_1;
    const char *const line_2 = ORBIT_PREDICTION_TLE_LINE_2;

    return std::strlen(line_1) >= 69U && std::strlen(line_2) >= 69U &&
        line_1[0] == '1' && line_2[0] == '2';
}

bool current_unix_time(uint64_t *timestamp)
{
    if (!timestamp) {
        return false;
    }

    rtems_time_of_day tod;
    if (rtems_clock_get_tod(&tod) != RTEMS_SUCCESSFUL ||
        tod.year < MINIMUM_VALID_UTC_YEAR) {
        return false;
    }

    const time_t now = time(nullptr);
    if (now <= 0) {
        return false;
    }

    *timestamp = static_cast<uint64_t>(now);
    return true;
}

void publish_location(float latitude_degrees, float longitude_degrees, uint8_t status)
{
    if (location_lock == RTEMS_ID_NONE) {
        return;
    }

    if (rtems_semaphore_obtain(location_lock, RTEMS_WAIT, RTEMS_NO_TIMEOUT) ==
        RTEMS_SUCCESSFUL) {
        latest_location.latitude_degrees = latitude_degrees;
        latest_location.longitude_degrees = longitude_degrees;
        latest_location.status = status;
        (void) rtems_semaphore_release(location_lock);
    }
}

bool is_inside_brazil(double latitude_degrees, double longitude_degrees)
{
    if (latitude_degrees < -34.0 || latitude_degrees > 6.0 ||
        longitude_degrees < -75.0 || longitude_degrees > -33.0) {
        return false;
    }

    bool inside = false;
    size_t previous = sizeof(BRAZIL_GEOFENCE) / sizeof(BRAZIL_GEOFENCE[0]) - 1U;

    for (size_t current = 0U;
         current < sizeof(BRAZIL_GEOFENCE) / sizeof(BRAZIL_GEOFENCE[0]);
         previous = current++) {
        const geo_point_t &first = BRAZIL_GEOFENCE[current];
        const geo_point_t &second = BRAZIL_GEOFENCE[previous];
        const bool crosses_latitude =
            (first.latitude_degrees > latitude_degrees) !=
            (second.latitude_degrees > latitude_degrees);

        if (crosses_latitude &&
            longitude_degrees <
                (second.longitude_degrees - first.longitude_degrees) *
                    (latitude_degrees - first.latitude_degrees) /
                    (second.latitude_degrees - first.latitude_degrees) +
                    first.longitude_degrees) {
            inside = !inside;
        }
    }

    return inside;
}

void wait_for_next_prediction()
{
    const rtems_interval ticks_per_second = rtems_clock_get_ticks_per_second();
    const rtems_interval period_ticks =
        ticks_per_second == 0U ? 1U : ticks_per_second * ORBIT_PREDICTION_PERIOD_SECONDS;

    (void) rtems_task_wake_after(period_ticks);
}

} // namespace

extern "C" rtems_status_code orbit_prediction_init(void)
{
    if (location_lock != RTEMS_ID_NONE) {
        return RTEMS_SUCCESSFUL;
    }

    return rtems_semaphore_create(
        rtems_build_name('O', 'R', 'B', 'L'),
        1U,
        RTEMS_BINARY_SEMAPHORE | RTEMS_PRIORITY | RTEMS_INHERIT_PRIORITY,
        0U,
        &location_lock);
}

extern "C" bool orbit_prediction_get_latest(
    float *latitude_degrees,
    float *longitude_degrees,
    uint8_t *status)
{
    if (!latitude_degrees || !longitude_degrees || !status) {
        return false;
    }

    *latitude_degrees = 0.0f;
    *longitude_degrees = 0.0f;
    *status = ORBIT_PREDICTION_STATUS_UNAVAILABLE;

    if (location_lock == RTEMS_ID_NONE) {
        return false;
    }

    if (rtems_semaphore_obtain(location_lock, RTEMS_WAIT, RTEMS_NO_TIMEOUT) !=
        RTEMS_SUCCESSFUL) {
        return false;
    }

    *latitude_degrees = latest_location.latitude_degrees;
    *longitude_degrees = latest_location.longitude_degrees;
    *status = latest_location.status;
    (void) rtems_semaphore_release(location_lock);

    return *status == ORBIT_PREDICTION_STATUS_VALID ||
        *status == ORBIT_PREDICTION_STATUS_INSIDE_BRAZIL;
}

extern "C" rtems_task orbit_prediction_task(rtems_task_argument argument)
{
    (void) argument;

    if (!is_tle_configured()) {
        while (true) {
            publish_location(0.0f, 0.0f, ORBIT_PREDICTION_STATUS_INVALID_TLE);
            wait_for_next_prediction();
        }
    }

    static predict_orbital_elements_t orbital_elements = {};
    static predict_sgp4 sgp4 = {};
    static predict_sdp4 sdp4 = {};
    const predict_orbital_elements_t *const parsed_tle = predict_parse_tle(
        &orbital_elements,
        &sgp4,
        &sdp4,
        ORBIT_PREDICTION_TLE_LINE_1,
        ORBIT_PREDICTION_TLE_LINE_2);

    if (!parsed_tle) {
        while (true) {
            publish_location(0.0f, 0.0f, ORBIT_PREDICTION_STATUS_INVALID_TLE);
            wait_for_next_prediction();
        }
    }

    bool was_inside_brazil = false;

    while (true) {
        uint64_t unix_time = 0U;
        struct predict_position position = {};

        if (!current_unix_time(&unix_time)) {
            was_inside_brazil = false;
            publish_location(0.0f, 0.0f, ORBIT_PREDICTION_STATUS_INVALID_TIME);
        } else if (predict_orbit(
                       parsed_tle,
                       &position,
                       julian_from_timestamp(unix_time)) != 0 ||
                   position.decayed || !std::isfinite(position.latitude) ||
                   !std::isfinite(position.longitude)) {
            was_inside_brazil = false;
            publish_location(0.0f, 0.0f, ORBIT_PREDICTION_STATUS_PROPAGATION_ERROR);
        } else {
            const float latitude_degrees = static_cast<float>(
                position.latitude * RADIANS_TO_DEGREES);
            const float longitude_degrees = static_cast<float>(
                position.longitude * RADIANS_TO_DEGREES);
            const bool is_now_inside_brazil =
                is_inside_brazil(latitude_degrees, longitude_degrees);
            const uint8_t status = is_now_inside_brazil
                ? ORBIT_PREDICTION_STATUS_INSIDE_BRAZIL
                : ORBIT_PREDICTION_STATUS_VALID;

            publish_location(latitude_degrees, longitude_degrees, status);

            if (is_now_inside_brazil && !was_inside_brazil) {
                /* Intentionally no receiver or action is attached to this event. */
                (void) rtems_event_send(
                    RTEMS_SELF,
                    ORBIT_PREDICTION_EVENT_ENTERED_BRAZIL);
            }
            was_inside_brazil = is_now_inside_brazil;
        }

        wait_for_next_prediction();
    }
}
