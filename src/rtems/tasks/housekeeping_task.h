#ifndef HOUSEKEEPING_TASK_H
#define HOUSEKEEPING_TASK_H

#include <rtems.h>

#ifdef __cplusplus
extern "C" {
#endif

rtems_task housekeeping_task(rtems_task_argument argument);

#ifdef __cplusplus
}
#endif

#endif /* HOUSEKEEPING_TASK_H */
