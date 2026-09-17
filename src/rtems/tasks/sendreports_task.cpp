#include <rtems.h>
#include <rtems/bspIo.h>

#include "../../drivers/i2c/telemetry_i2c.h"
#include "../../drivers/telemetry/telemetry.h"
#include "../queues/queues.h"

namespace {

constexpr rtems_interval I2C_REINITIALISATION_TICKS = 1000U;
constexpr uint32_t I2C_FAILURE_LOG_INTERVAL = 100U;

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
            RTEMS_WAIT,
            RTEMS_NO_TIMEOUT);

        if (queue_status != RTEMS_SUCCESSFUL || received_size != sizeof(packet)) {
            continue;
        }

        const uint16_t frame_size = pus_build_tm(frame, &packet);
        if (frame_size == 0U) {
            continue;
        }

        i2c_status = telemetry_i2c_write(frame, frame_size);
        if (i2c_status != TELEMETRY_I2C_SUCCESS) {
            /* Reset the peripheral before the next frame after NACK/error/timeout. */
            ++consecutive_i2c_failures;
            if (consecutive_i2c_failures == 1U ||
                consecutive_i2c_failures % I2C_FAILURE_LOG_INTERVAL == 0U) {
                printk("Telemetry I2C transmit failed: %d\n", (int) i2c_status);
            }
            i2c_status = telemetry_i2c_init();
        } else {
            consecutive_i2c_failures = 0U;
        }
    }
}
