#ifndef PUS_H
#define PUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parse and dispatch one complete PUS telecommand frame. */
void pus_handle_rx(const uint8_t *rx, uint16_t size);

#ifdef __cplusplus
}
#endif

#endif /* PUS_H */
