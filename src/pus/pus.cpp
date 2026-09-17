#include <stdint.h>
#include "pus_packet.h"
#include "pus.h"
#include "pus_crc.h"
#include "pus_dispatch.h"
#include "pus_parser.h"
#include "../drivers/telemetry/adcs_telemetry.h"

// função principal de entrada (ex: chamada na UART)
extern "C" void pus_handle_rx(const uint8_t *rx, uint16_t size) {

    pus_packet_t pkt;
    
    int ret = pus_parse(rx, size, &pkt);

    if (ret != 0) {
        pus_packet_t error_pkt;

        /* Preserve the parser reason and frame length for ground diagnosis. */
        adcs_build_parser_error_telemetry(&error_pkt, ret, size);
        adcs_send_telemetry(&error_pkt);

        // erro → pode logar ou mandar NACK
        send_ack(2); // erro genérico
        return;
    }

    // pacote válido → despacha
    pus_dispatch(&pkt);
}
