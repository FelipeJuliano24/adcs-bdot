#ifndef ORBIT_PREDICTION_TASK_H
#define ORBIT_PREDICTION_TASK_H

#include <stdbool.h>
#include <stdint.h>

#include <rtems.h>

/*
 * This event is deliberately not consumed yet.  It is emitted by the orbit
 * task on an outside-to-inside transition through the Brazil geofence, so a
 * future subsystem can subscribe to the same bit without changing prediction.
 */
#define ORBIT_PREDICTION_EVENT_ENTERED_BRAZIL RTEMS_EVENT_1

/* Status byte appended to every housekeeping packet with the coordinates. */
#define ORBIT_PREDICTION_STATUS_UNAVAILABLE       0x00U
#define ORBIT_PREDICTION_STATUS_VALID             0x01U
#define ORBIT_PREDICTION_STATUS_INSIDE_BRAZIL     0x02U
#define ORBIT_PREDICTION_STATUS_INVALID_TLE       0x03U
#define ORBIT_PREDICTION_STATUS_INVALID_TIME      0x04U
#define ORBIT_PREDICTION_STATUS_PROPAGATION_ERROR 0x05U

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the lock used to safely share the latest prediction with telemetry. */
rtems_status_code orbit_prediction_init(void);

/* Periodically propagates the configured TLE and updates the latest location. */
rtems_task orbit_prediction_task(rtems_task_argument argument);

/*
 * Gets the latest sub-satellite latitude/longitude in degrees (WGS-84).
 * Coordinates are zero when the return value is false; status always explains
 * the state if a non-null status pointer is supplied.
 */
bool orbit_prediction_get_latest(
    float *latitude_degrees,
    float *longitude_degrees,
    uint8_t *status);

#ifdef __cplusplus
}
#endif

#endif /* ORBIT_PREDICTION_TASK_H */
