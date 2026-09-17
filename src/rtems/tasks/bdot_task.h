#ifndef BDOT_TASK_H
#define BDOT_TASK_H

#include <rtems.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * B-dot task entry point.  The task executes at 10 Hz and commands a
 * magnetic dipole from the derivative of the measured magnetic field.
 */
rtems_task bdot_task(rtems_task_argument argument);

/*
 * Board integration points.
 *
 * bdot_task.cpp supplies weak, safe defaults.  The board-specific
 * magnetometer and magnetic-torquer drivers should provide strong versions
 * of these functions.  Magnetic field is expressed in tesla and dipole
 * command in A*m^2.
 */
bool bdot_read_magnetic_field_tesla(float *bx, float *by, float *bz);
void bdot_set_magnetic_dipole_am2(float mx, float my, float mz);

/* B-dot gain unit: A*m^2*s/T.  Invalid gains are ignored. */
void bdot_set_gain(float gain_am2_s_per_tesla);

/* Enable (non-zero) or disable (zero) the B-dot controller. */
void bdot_set_enabled(uint8_t enabled);

#ifdef __cplusplus
}
#endif

#endif /* BDOT_TASK_H */
