#include <stdint.h>
#include <string.h>
#include "pus_packet.h"
#include "pus_crc.h"
#include "pus_dispatch.h"
#include "../drivers/utils/adcs.h"
#include "../drivers/telemetry/adcs_telemetry.h"

extern "C" int check_telemetry(pus_packet_t *pkt, bool hk, uint8_t subtype);

namespace {

void send_packet(const pus_packet_t *pkt)
{
    adcs_send_telemetry(pkt);
}

} // namespace

void send_ack(uint8_t result)
{
    pus_packet_t pkt = {};

    pkt.apid = APID_ADCS;
    pkt.service = PUS_SERVICE_TC_VERIFICATION;
    pkt.subtype = 1U;
    pkt.length = 1U;
    pkt.data[0] = result;
    send_packet(&pkt);
}

void send_hk(void)
{
    pus_packet_t pkt;

    if (check_telemetry(&pkt, true, HK_ADCS_STATUS) == 0) {
        send_packet(&pkt);
    }
}

void send_test(void)
{
    pus_packet_t pkt = {};

    pkt.apid = APID_ADCS;
    pkt.service = PUS_SERVICE_TEST;
    pkt.subtype = 1U;
    pkt.length = 1U;
    pkt.data[0] = 0U;
    send_packet(&pkt);
}

void pus_dispatch(pus_packet_t *pkt) {

    if (!pkt) {
        send_ack(2);
        return;
    }

    // 1. VERIFICAÇÃO DE TC (ACK de recepção)
    send_ack(0); // acceptance OK

    switch (pkt->service) {

        // -----------------------------
        // TEST SERVICE
        // -----------------------------
        case PUS_SERVICE_TEST:
            send_test();
            send_ack(1); // completion OK
            break;

        // -----------------------------
        // HOUSEKEEPING
        // -----------------------------
        case PUS_SERVICE_HK:
            send_hk();
            send_ack(1);
            break;

        // -----------------------------
        // COMMAND SERVICE (principal)
        // -----------------------------
        case PUS_SERVICE_COMMAND:

            switch (pkt->subtype) {

                case SUBTYPE_SET_MODE:
                    if (pkt->length != 1U) {
                        send_ack(2);
                        break;
                    }
                    adcs_set_mode(pkt->data[0]);
                    send_ack(1);
                    break;

                case SUBTYPE_SET_BDOT_GAIN:
                {
                    float gain;

                    if (pkt->length != sizeof(gain)) {
                        send_ack(2);
                        break;
                    }
                    memcpy(&gain, pkt->data, sizeof(gain));
                    adcs_set_bdot_gain(gain);
                    send_ack(1);
                    break;
                }

                case SUBTYPE_SET_BDOT_ENABLED:
                    if (pkt->length != 1U || pkt->data[0] > 1U) {
                        send_ack(2);
                        break;
                    }
                    adcs_set_bdot_enabled(pkt->data[0]);
                    send_ack(1);
                    break;

                default:
                    send_ack(2); // unknown command
                    break;
            }

            break;

        // -----------------------------
        // INVALID SERVICE
        // -----------------------------
        default:
            send_ack(2); // service not supported
            break;
    }
}
