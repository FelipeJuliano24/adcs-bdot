#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <rtems.h>
#include "adcs_telemetry.h"
#include "../../pus/pus_packet.h"
#include "../utils/HK_ADCS_STATUS.h"
#include "../i3g4250d/i3g4250d.h"
#include "../../rtems/queues/queues.h"

namespace {

void initialise_tm_packet(pus_packet_t *pkt, uint8_t subtype, uint16_t length)
{
    memset(pkt, 0, sizeof(*pkt));
    pkt->apid = APID_ADCS;
    pkt->service = PUS_SERVICE_TM;
    pkt->subtype = subtype;
    pkt->length = length;
}

void put_float(uint8_t *destination, float value)
{
    memcpy(destination, &value, sizeof(value));
}

} // namespace

void adcs_build_bdot_telemetry(
    pus_packet_t *pkt,
    uint8_t status,
    uint8_t gyro_status,
    const float gyro_dps[3],
    const float magnetic_field_tesla[3],
    const float magnetic_field_rate_tesla_per_second[3],
    const float magnetic_dipole_am2[3],
    float gain_am2_s_per_tesla)
{
    if (!pkt || !gyro_dps || !magnetic_field_tesla ||
        !magnetic_field_rate_tesla_per_second || !magnetic_dipole_am2) {
        return;
    }

    initialise_tm_packet(pkt, ADCS_TM_BDOT, ADCS_TM_BDOT_PAYLOAD_SIZE);
    pkt->data[0] = status;
    pkt->data[1] = gyro_status;

    for (uint8_t axis = 0; axis < 3U; ++axis) {
        put_float(&pkt->data[2U + axis * sizeof(float)], gyro_dps[axis]);
        put_float(&pkt->data[14U + axis * sizeof(float)], magnetic_field_tesla[axis]);
        put_float(&pkt->data[26U + axis * sizeof(float)],
                  magnetic_field_rate_tesla_per_second[axis]);
        put_float(&pkt->data[38U + axis * sizeof(float)], magnetic_dipole_am2[axis]);
    }

    put_float(&pkt->data[50U], gain_am2_s_per_tesla);
}

void adcs_build_gyro_telemetry(
    pus_packet_t *pkt,
    uint8_t gyro_status,
    const float gyro_dps[3])
{
    if (!pkt || !gyro_dps) {
        return;
    }

    initialise_tm_packet(pkt, ADCS_TM_GYRO, ADCS_TM_GYRO_PAYLOAD_SIZE);
    pkt->data[0] = gyro_status;

    for (uint8_t axis = 0; axis < 3U; ++axis) {
        put_float(&pkt->data[1U + axis * sizeof(float)], gyro_dps[axis]);
    }
}

void adcs_build_parser_error_telemetry(
    pus_packet_t *pkt,
    int parser_status,
    uint16_t received_size)
{
    if (!pkt) {
        return;
    }

    initialise_tm_packet(
        pkt,
        ADCS_TM_PARSER_ERROR,
        ADCS_TM_PARSER_ERROR_PAYLOAD_SIZE);
    pkt->data[0] = (uint8_t) parser_status;
    pkt->data[1] = (uint8_t) (received_size >> 8);
    pkt->data[2] = (uint8_t) received_size;
}

bool adcs_send_telemetry(const pus_packet_t *pkt)
{
    if (!pkt || queue_tx == RTEMS_ID_NONE) {
        return false;
    }

    return rtems_message_queue_send(queue_tx, pkt, sizeof(*pkt)) == RTEMS_SUCCESSFUL;
}

extern "C" int check_telemetry(pus_packet_t *pkt, bool hk, uint8_t subtype) {

    if (!pkt) return -1;

    /* Never transmit stale stack bytes for telemetry fields not implemented yet. */
    memset(pkt, 0, sizeof(*pkt));

    pkt->apid = APID_ADCS;

    if (hk) {

        pkt->service = PUS_SERVICE_HK;

        switch (subtype) {

            case HK_ADCS_STATUS:
            {
                const uint8_t adcs_status = get_status();

                pkt->subtype = HK_ADCS_STATUS;
                pkt->length = 2;
                pkt->data[0] = adcs_status;
                pkt->data[1] = adcs_status == 0xff
                    ? ADCS_GYRO_STATUS_ERROR
                    : ADCS_GYRO_STATUS_VALID;
                break;
            }

            case HK_ADCS_POWER:
                pkt->subtype = HK_ADCS_POWER;
                pkt->length = 2;
                /* Power-monitor driver is not available yet. */
                pkt->data[0] = 0xff;
                pkt->data[1] = 0xff;
                break;

            case HK_ADCS_TEMPERATURE:
                pkt->subtype = HK_ADCS_TEMPERATURE;
                pkt->length = 2;
                /* Temperature driver is not available yet. */
                pkt->data[0] = 0xff;
                pkt->data[1] = 0xff;
                break;

            case HK_ADCS_FAULTS:
            {
                const uint8_t adcs_status = get_status();

                pkt->subtype = HK_ADCS_FAULTS;
                pkt->length = 1;
                /* Bit 0: I3G4250D/SPI communication unavailable. */
                pkt->data[0] = adcs_status == 0xff ? 0x01 : 0x00;
                break;
            }

            default:
                return -2;
        }

    } else {

        pkt->service = PUS_SERVICE_TM;

        switch (subtype) {

            case ADCS_TM_QUATERNION:
                pkt->subtype = ADCS_TM_QUATERNION;
                pkt->length = 16;
                break;

            case ADCS_TM_GYRO:
            {
                float gyro_dps[3] = {0.0f, 0.0f, 0.0f};
                const uint8_t gyro_status = gyro_i3g4250d_read_dps(
                    &gyro_dps[0],
                    &gyro_dps[1],
                    &gyro_dps[2])
                    ? ADCS_GYRO_STATUS_VALID
                    : ADCS_GYRO_STATUS_ERROR;

                adcs_build_gyro_telemetry(pkt, gyro_status, gyro_dps);
                break;
            }

            case ADCS_TM_BFIELD:
                pkt->subtype = ADCS_TM_BFIELD;
                pkt->length = 12;
                break;

            case ADCS_TM_CONTROL_MODE:
                pkt->subtype = ADCS_TM_CONTROL_MODE;
                pkt->length = 1;
                pkt->data[0] = 0;
                break;

            default:
                return -3;
        }
    }

    return 0;
}
