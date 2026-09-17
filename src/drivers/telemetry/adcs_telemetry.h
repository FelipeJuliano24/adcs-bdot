#ifndef ADCS_TELEMETRY_H
#define ADCS_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#include "../../pus/pus_packet.h"

// =========================
// APID (ADCS apenas)
// =========================
#define APID_ADCS 0x01

// =========================
// SERVICES (somente o que o ADCS usa)
// =========================
#define PUS_SERVICE_HK  3
#define PUS_SERVICE_TM  8

// =========================
// HOUSEKEEPING (saúde do sistema)
// =========================
#define HK_ADCS_STATUS      0x01
#define HK_ADCS_POWER       0x02
#define HK_ADCS_TEMPERATURE 0x03
#define HK_ADCS_FAULTS      0x04

// =========================
// ADCS TELEMETRY (dados de controle)
// =========================
#define ADCS_TM_QUATERNION   0x10
#define ADCS_TM_GYRO         0x11
#define ADCS_TM_BFIELD       0x12
#define ADCS_TM_CONTROL_MODE 0x13
#define ADCS_TM_BDOT         0x14
#define ADCS_TM_PARSER_ERROR 0x15

#define ADCS_GYRO_STATUS_VALID 0x01
#define ADCS_GYRO_STATUS_ERROR 0xff

/* Values stored in byte 0 of an ADCS_TM_BDOT packet. */
#define ADCS_BDOT_STATUS_NO_FIELD     0x00
#define ADCS_BDOT_STATUS_ACTIVE       0x01
#define ADCS_BDOT_STATUS_INITIALIZING 0x02
#define ADCS_BDOT_STATUS_DETUMBLED    0x03
#define ADCS_BDOT_STATUS_GYRO_ERROR   0x04
#define ADCS_BDOT_STATUS_DISABLED     0x05

/* ADCS_TM_GYRO: status byte followed by X, Y and Z angular rates in dps. */
#define ADCS_TM_GYRO_PAYLOAD_SIZE 13U

/*
 * ADCS_TM_BDOT payload, encoded in the native IEEE-754 float format used by
 * the current PUS telemetry implementation:
 *   [0]      B-dot status
 *   [1]      gyro status
 *   [2..13]  Gx, Gy, Gz                 (dps)
 *   [14..25] Bx, By, Bz                 (tesla)
 *   [26..37] dBx/dt, dBy/dt, dBz/dt     (tesla/s)
 *   [38..49] Mx, My, Mz                 (A*m^2)
 *   [50..53] K gain                     (A*m^2*s/T)
 */
#define ADCS_TM_BDOT_PAYLOAD_SIZE 54U

/*
 * ADCS_TM_PARSER_ERROR payload:
 *   [0] parser return value, as signed 8-bit two's-complement
 *   [1] received frame size MSB
 *   [2] received frame size LSB
 */
#define ADCS_TM_PARSER_ERROR_PAYLOAD_SIZE 3U

void adcs_build_bdot_telemetry(
    pus_packet_t *pkt,
    uint8_t status,
    uint8_t gyro_status,
    const float gyro_dps[3],
    const float magnetic_field_tesla[3],
    const float magnetic_field_rate_tesla_per_second[3],
    const float magnetic_dipole_am2[3],
    float gain_am2_s_per_tesla);

void adcs_build_gyro_telemetry(
    pus_packet_t *pkt,
    uint8_t gyro_status,
    const float gyro_dps[3]);

void adcs_build_parser_error_telemetry(
    pus_packet_t *pkt,
    int parser_status,
    uint16_t received_size);

/* Queue a fully-built ADCS telemetry packet for downlink transmission. */
bool adcs_send_telemetry(const pus_packet_t *pkt);

#endif
