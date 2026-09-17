#include <rtems.h>
#include <rtems/bspIo.h>

#include "../../drivers/i2c/telemetry_i2c.h"
#include "../../drivers/telemetry/telemetry.h"
#include "../../drivers/telemetry/adcs_telemetry.h"
#include "../../pus/pus.h"
#include "../../pus/pus_parser.h"
#include "../queues/queues.h"

namespace {

constexpr rtems_interval I2C_REINITIALISATION_TICKS = 1000U;
constexpr uint32_t I2C_FAILURE_LOG_INTERVAL = 100U;
constexpr rtems_interval I2C_TC_POLL_TICKS = 100U;
constexpr rtems_interval I2C_WORKER_TICKS = 10U;

void report_i2c_receive_error(int error, uint16_t received_size)
{
    pus_packet_t error_packet;

    adcs_build_parser_error_telemetry(&error_packet, error, received_size);
    (void) adcs_send_telemetry(&error_packet);
}

telemetry_i2c_status_t poll_obdh_telecommand(void)
{
    uint8_t length_bytes[2] = {0U, 0U};
    telemetry_i2c_status_t status = telemetry_i2c_read_register(
        TELEMETRY_I2C_TC_LENGTH_REGISTER,
        length_bytes,
        sizeof(length_bytes));

    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    const uint16_t frame_size =
        ((uint16_t) length_bytes[0] << 8U) | (uint16_t) length_bytes[1];
    if (frame_size == 0U) {
        return TELEMETRY_I2C_SUCCESS;
    }

    if (frame_size < PUS_FRAME_HEADER_SIZE + PUS_FRAME_CRC_SIZE ||
        frame_size > TELEMETRY_I2C_TC_MAX_FRAME_SIZE) {
        report_i2c_receive_error(PUS_PARSE_ERROR_LENGTH, frame_size);
        return TELEMETRY_I2C_SUCCESS;
    }

    uint8_t frame[TELEMETRY_I2C_TC_MAX_FRAME_SIZE];
    status = telemetry_i2c_read_register(
        TELEMETRY_I2C_TC_DATA_REGISTER,
        frame,
        frame_size);
    if (status == TELEMETRY_I2C_SUCCESS) {
        pus_handle_rx(frame, frame_size);
    }

    return status;
}

} // namespace

/*
 * Telemetry downlink task.
 *
 * Every producer (B-dot, housekeeping and PUS parser errors) sends a
 * pus_packet_t to queue_tx.  This task turns it into a PUS wire frame and
 * sends that complete frame in one I2C master transaction.
 */
extern "C" rtems_task send_reports_task(rtems_task_argument argument)
{
    (void) argument;

    telemetry_i2c_status_t i2c_status = TELEMETRY_I2C_BUS_ERROR;
    uint32_t consecutive_i2c_failures = 0U;
    rtems_interval last_tc_poll = rtems_clock_get_ticks_since_boot();
    while (i2c_status != TELEMETRY_I2C_SUCCESS) {
        i2c_status = telemetry_i2c_init();
        if (i2c_status != TELEMETRY_I2C_SUCCESS) {
            printk("Telemetry I2C init failed: %d\n", (int) i2c_status);
            rtems_task_wake_after(I2C_REINITIALISATION_TICKS);
        }
    }

    while (true) {
        pus_packet_t packet;
        size_t received_size = 0U;
        uint8_t frame[PUS_FRAME_MAX_SIZE];

        const rtems_status_code queue_status = rtems_message_queue_receive(
            queue_tx,
            &packet,
            &received_size,
            RTEMS_NO_WAIT,
            0U);

        if (queue_status == RTEMS_SUCCESSFUL && received_size == sizeof(packet)) {
            const uint16_t frame_size = pus_build_tm(frame, &packet);
            if (frame_size != 0U) {
                i2c_status = telemetry_i2c_write(frame, frame_size);
            }

            if (i2c_status == TELEMETRY_I2C_SUCCESS) {
                consecutive_i2c_failures = 0U;
            }
        }

        const rtems_interval now = rtems_clock_get_ticks_since_boot();
        if (now - last_tc_poll >= I2C_TC_POLL_TICKS) {
            last_tc_poll = now;
            i2c_status = poll_obdh_telecommand();
        }

        if (i2c_status != TELEMETRY_I2C_SUCCESS) {
            /* Reset the peripheral before the next frame after NACK/error/timeout. */
            ++consecutive_i2c_failures;
            if (consecutive_i2c_failures == 1U ||
                consecutive_i2c_failures % I2C_FAILURE_LOG_INTERVAL == 0U) {
                printk("Telemetry I2C transmit failed: %d\n", (int) i2c_status);
            }
            i2c_status = telemetry_i2c_init();
        }

        rtems_task_wake_after(I2C_WORKER_TICKS);
    }
}
