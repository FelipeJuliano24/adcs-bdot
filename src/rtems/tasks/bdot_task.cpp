#include "bdot_task.h"

#include "../../drivers/utils/adcs.h"
#include "../../drivers/telemetry/adcs_telemetry.h"
#include "../../drivers/utils/HK_ADCS_STATUS.h"
#include "../../drivers/i3g4250d/i3g4250d.h"

namespace {

/* The control loop runs at 10 Hz. */
constexpr float BDOT_PERIOD_SECONDS = 0.1f;

/*
 * m = -K * dB/dt
 *
 * With a 50 uT field changing by roughly 50 uT/s, the default gain commands
 * 0.2 A*m^2.  Tune this value for the satellite inertia and torquer hardware.
 */
constexpr float BDOT_DEFAULT_GAIN_AM2_S_PER_TESLA = 4000.0f;
constexpr float BDOT_MAX_GAIN_AM2_S_PER_TESLA = 1000000.0f;
constexpr float BDOT_MAX_DIPOLE_AM2 = 0.2f;
constexpr uint32_t BDOT_TELEMETRY_PERIOD_CYCLES = 10U;

struct bdot_controller_t {
    float previous_bx;
    float previous_by;
    float previous_bz;
    bool has_previous_sample;
};

/* Updated by the PUS command handler through adcs_set_bdot_gain(). */
volatile float bdot_gain_am2_s_per_tesla = BDOT_DEFAULT_GAIN_AM2_S_PER_TESLA;
volatile uint8_t bdot_enabled = 1U;

float clamp(float value, float limit)
{
    if (value > limit) {
        return limit;
    }
    if (value < -limit) {
        return -limit;
    }
    return value;
}

void bdot_reset(bdot_controller_t *controller)
{
    controller->previous_bx = 0.0f;
    controller->previous_by = 0.0f;
    controller->previous_bz = 0.0f;
    controller->has_previous_sample = false;
}

/*
 * Returns true only after two valid samples are available.  The first sample
 * establishes the reference, so commanding zero avoids a start-up pulse.
 */
bool bdot_update(
    bdot_controller_t *controller,
    float bx,
    float by,
    float bz,
    float period_seconds,
    float gain,
    float *bdot_x_out,
    float *bdot_y_out,
    float *bdot_z_out,
    float *mx,
    float *my,
    float *mz)
{
    if (!controller->has_previous_sample) {
        controller->previous_bx = bx;
        controller->previous_by = by;
        controller->previous_bz = bz;
        controller->has_previous_sample = true;
        return false;
    }

    const float bdot_x = (bx - controller->previous_bx) / period_seconds;
    const float bdot_y = (by - controller->previous_by) / period_seconds;
    const float bdot_z = (bz - controller->previous_bz) / period_seconds;

    controller->previous_bx = bx;
    controller->previous_by = by;
    controller->previous_bz = bz;

    *bdot_x_out = bdot_x;
    *bdot_y_out = bdot_y;
    *bdot_z_out = bdot_z;
    *mx = clamp(-gain * bdot_x, BDOT_MAX_DIPOLE_AM2);
    *my = clamp(-gain * bdot_y, BDOT_MAX_DIPOLE_AM2);
    *mz = clamp(-gain * bdot_z, BDOT_MAX_DIPOLE_AM2);
    return true;
}

} // namespace

/*
 * Default board hooks.  They make this task safe to link before the hardware
 * drivers exist: no magnetic-field sample means no torquer command.  A board
 * driver can override either weak symbol with its real implementation.
 */
extern "C" bool __attribute__((weak))
bdot_read_magnetic_field_tesla(float *bx, float *by, float *bz)
{
    (void) bx;
    (void) by;
    (void) bz;
    return false;
}

extern "C" void __attribute__((weak))
bdot_set_magnetic_dipole_am2(float mx, float my, float mz)
{
    (void) mx;
    (void) my;
    (void) mz;
}

extern "C" void bdot_set_gain(float gain_am2_s_per_tesla)
{
    if (gain_am2_s_per_tesla >= 0.0f &&
        gain_am2_s_per_tesla <= BDOT_MAX_GAIN_AM2_S_PER_TESLA) {
        bdot_gain_am2_s_per_tesla = gain_am2_s_per_tesla;
    }
}

extern "C" void bdot_set_enabled(uint8_t enabled)
{
    bdot_enabled = enabled != 0U ? 1U : 0U;
}

/* Interface used by the existing PUS SET_BDOT_GAIN command. */
extern "C" void adcs_set_bdot_gain(float gain)
{
    bdot_set_gain(gain);
}

extern "C" void adcs_set_bdot_enabled(uint8_t enabled)
{
    bdot_set_enabled(enabled);
}

/* Existing SET_MODE command: mode zero disables B-dot, any other mode enables it. */
extern "C" void adcs_set_mode(uint8_t mode)
{
    bdot_set_enabled(mode);
}

extern "C" rtems_task bdot_task(rtems_task_argument argument)
{
    (void) argument;

    bdot_controller_t controller{};
    bdot_reset(&controller);

    const rtems_interval ticks_per_second = rtems_clock_get_ticks_per_second();
    const rtems_interval period_ticks =
        (ticks_per_second >= 10U) ? ticks_per_second / 10U : 1U;
    uint32_t telemetry_cycle = 0U;

    float magnetic_field_tesla[3] = {0.0f, 0.0f, 0.0f};
    float magnetic_field_rate_tesla_per_second[3] = {0.0f, 0.0f, 0.0f};
    float magnetic_dipole_am2[3] = {0.0f, 0.0f, 0.0f};
    float gyro_dps[3] = {0.0f, 0.0f, 0.0f};
    uint8_t bdot_status = ADCS_BDOT_STATUS_NO_FIELD;
    uint8_t gyro_status = ADCS_GYRO_STATUS_ERROR;

    /* Keep the torquers off until the first complete B-dot estimate exists. */
    bdot_set_magnetic_dipole_am2(0.0f, 0.0f, 0.0f);

    while (true) {
        float bx = 0.0f;
        float by = 0.0f;
        float bz = 0.0f;
        const bool gyro_is_valid = gyro_i3g4250d_read_dps(
            &gyro_dps[0],
            &gyro_dps[1],
            &gyro_dps[2]);
        const float gyro_magnitude_squared =
            gyro_dps[0] * gyro_dps[0] +
            gyro_dps[1] * gyro_dps[1] +
            gyro_dps[2] * gyro_dps[2];
        const float detumble_threshold_squared =
            (float) ROTATION_THRESHOLD_DPS * ROTATION_THRESHOLD_DPS;

        gyro_status = gyro_is_valid ? ADCS_GYRO_STATUS_VALID : ADCS_GYRO_STATUS_ERROR;
        if (!gyro_is_valid) {
            gyro_dps[0] = 0.0f;
            gyro_dps[1] = 0.0f;
            gyro_dps[2] = 0.0f;
        }

        if (bdot_enabled == 0U) {
            /* A telecommand disabled detumbling: clear history and torquers. */
            bdot_reset(&controller);
            bdot_set_magnetic_dipole_am2(0.0f, 0.0f, 0.0f);
            magnetic_field_tesla[0] = 0.0f;
            magnetic_field_tesla[1] = 0.0f;
            magnetic_field_tesla[2] = 0.0f;
            magnetic_field_rate_tesla_per_second[0] = 0.0f;
            magnetic_field_rate_tesla_per_second[1] = 0.0f;
            magnetic_field_rate_tesla_per_second[2] = 0.0f;
            magnetic_dipole_am2[0] = 0.0f;
            magnetic_dipole_am2[1] = 0.0f;
            magnetic_dipole_am2[2] = 0.0f;
            bdot_status = ADCS_BDOT_STATUS_DISABLED;
        } else if (!bdot_read_magnetic_field_tesla(&bx, &by, &bz)) {
            /* A gap in measurements must not produce a stale torque command. */
            bdot_reset(&controller);
            bdot_set_magnetic_dipole_am2(0.0f, 0.0f, 0.0f);
            magnetic_field_tesla[0] = 0.0f;
            magnetic_field_tesla[1] = 0.0f;
            magnetic_field_tesla[2] = 0.0f;
            magnetic_field_rate_tesla_per_second[0] = 0.0f;
            magnetic_field_rate_tesla_per_second[1] = 0.0f;
            magnetic_field_rate_tesla_per_second[2] = 0.0f;
            magnetic_dipole_am2[0] = 0.0f;
            magnetic_dipole_am2[1] = 0.0f;
            magnetic_dipole_am2[2] = 0.0f;
            bdot_status = ADCS_BDOT_STATUS_NO_FIELD;
        } else {
            float mx;
            float my;
            float mz;

            if (bdot_update(
                    &controller,
                    bx,
                    by,
                    bz,
                    BDOT_PERIOD_SECONDS,
                    bdot_gain_am2_s_per_tesla,
                    &magnetic_field_rate_tesla_per_second[0],
                    &magnetic_field_rate_tesla_per_second[1],
                    &magnetic_field_rate_tesla_per_second[2],
                    &mx,
                    &my,
                    &mz)) {
                if (gyro_is_valid &&
                    gyro_magnitude_squared < detumble_threshold_squared) {
                    /* Gyro confirms detumbling: prevent noise-driven torquer pulses. */
                    magnetic_dipole_am2[0] = 0.0f;
                    magnetic_dipole_am2[1] = 0.0f;
                    magnetic_dipole_am2[2] = 0.0f;
                    bdot_status = ADCS_BDOT_STATUS_DETUMBLED;
                    bdot_set_magnetic_dipole_am2(0.0f, 0.0f, 0.0f);
                } else {
                    magnetic_dipole_am2[0] = mx;
                    magnetic_dipole_am2[1] = my;
                    magnetic_dipole_am2[2] = mz;
                    bdot_status = gyro_is_valid
                        ? ADCS_BDOT_STATUS_ACTIVE
                        : ADCS_BDOT_STATUS_GYRO_ERROR;
                    bdot_set_magnetic_dipole_am2(mx, my, mz);
                }
            } else {
                magnetic_field_rate_tesla_per_second[0] = 0.0f;
                magnetic_field_rate_tesla_per_second[1] = 0.0f;
                magnetic_field_rate_tesla_per_second[2] = 0.0f;
                magnetic_dipole_am2[0] = 0.0f;
                magnetic_dipole_am2[1] = 0.0f;
                magnetic_dipole_am2[2] = 0.0f;
                bdot_status = ADCS_BDOT_STATUS_INITIALIZING;
                bdot_set_magnetic_dipole_am2(0.0f, 0.0f, 0.0f);
            }

            magnetic_field_tesla[0] = bx;
            magnetic_field_tesla[1] = by;
            magnetic_field_tesla[2] = bz;
        }

        ++telemetry_cycle;
        if (telemetry_cycle >= BDOT_TELEMETRY_PERIOD_CYCLES) {
            pus_packet_t telemetry_packet;

            telemetry_cycle = 0U;
            adcs_build_bdot_telemetry(
                &telemetry_packet,
                bdot_status,
                gyro_status,
                gyro_dps,
                magnetic_field_tesla,
                magnetic_field_rate_tesla_per_second,
                magnetic_dipole_am2,
                bdot_gain_am2_s_per_tesla);
            adcs_send_telemetry(&telemetry_packet);
        }

        rtems_task_wake_after(period_ticks);
    }
}
