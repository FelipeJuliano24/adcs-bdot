#ifndef PUS_PARSER_H
#define PUS_PARSER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parser and transport-framing errors sent in ADCS_TM_PARSER_ERROR. */
#define PUS_PARSE_ERROR_FRAME_TOO_SHORT -1
#define PUS_PARSE_ERROR_CRC             -2
#define PUS_PARSE_ERROR_LENGTH          -3
#define PUS_PARSE_ERROR_SERVICE         -4
#define PUS_PARSE_ERROR_DATA_TOO_LARGE  -5
#define PUS_PARSE_ERROR_INVALID_ARGUMENT -6
#define PUS_PARSE_ERROR_TRANSPORT        -7

int pus_parse(const uint8_t *rx, uint16_t size, pus_packet_t *pkt);

#ifdef __cplusplus
}
#endif

#endif /* PUS_PARSER_H */
