#ifndef TELEMETRY_H
#define TELEMETRY_H

#include <stdint.h>

#include "../../pus/pus_packet.h"

#define PUS_FRAME_HEADER_SIZE 5U
#define PUS_FRAME_CRC_SIZE 2U
#define PUS_FRAME_MAX_SIZE (PUS_FRAME_HEADER_SIZE + MAX_DATA_SIZE + PUS_FRAME_CRC_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

/* Serialise one PUS packet into [APID|length|service|subtype|data|CRC]. */
uint16_t pus_build_tm(uint8_t *tx, const pus_packet_t *pkt);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_H */
