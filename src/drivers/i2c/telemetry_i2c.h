#ifndef TELEMETRY_I2C_H
#define TELEMETRY_I2C_H

#include <stddef.h>
#include <stdint.h>

/*
 * STM32F4 I2C1 telemetry link configuration.
 *
 * Default pins are I2C1 SCL=PB6 and SDA=PB7 (AF4).  The slave address is a
 * seven-bit address, so do not include the I2C read/write bit here.
 */
#ifndef TELEMETRY_I2C_SLAVE_ADDRESS
#define TELEMETRY_I2C_SLAVE_ADDRESS 0x42U
#endif

#ifndef TELEMETRY_I2C_BITRATE_HZ
#define TELEMETRY_I2C_BITRATE_HZ 100000U
#endif

#ifndef TELEMETRY_I2C_GPIO_PORT
#define TELEMETRY_I2C_GPIO_PORT 1U /* GPIOB */
#endif

#ifndef TELEMETRY_I2C_SCL_PIN
#define TELEMETRY_I2C_SCL_PIN 6U
#endif

#ifndef TELEMETRY_I2C_SDA_PIN
#define TELEMETRY_I2C_SDA_PIN 7U
#endif

/*
 * OBDH telecommand mailbox protocol (the STM32F4 remains I2C master):
 *   0x00..0x01  uint16_t PUS frame length, big-endian; zero means empty.
 *   0x02..       complete PUS frame, starting at APID MSB.
 *
 * The OBDH must retain the mailbox contents until the whole frame has been
 * read, then clear the length to zero.
 */
#define TELEMETRY_I2C_TC_LENGTH_REGISTER 0x00U
#define TELEMETRY_I2C_TC_DATA_REGISTER   0x02U
#define TELEMETRY_I2C_TC_MAX_FRAME_SIZE 128U

typedef enum {
    TELEMETRY_I2C_SUCCESS = 0,
    TELEMETRY_I2C_INVALID_ARGUMENT,
    TELEMETRY_I2C_TIMEOUT,
    TELEMETRY_I2C_NACK,
    TELEMETRY_I2C_BUS_ERROR
} telemetry_i2c_status_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Configure GPIOB PB6/PB7 and the STM32F4 I2C1 peripheral as an I2C master. */
telemetry_i2c_status_t telemetry_i2c_init(void);

/* Write one complete telemetry frame as a single I2C transaction. */
telemetry_i2c_status_t telemetry_i2c_write(const uint8_t *data, size_t length);

/* Read bytes from an auto-incrementing OBDH mailbox register. */
telemetry_i2c_status_t telemetry_i2c_read_register(
    uint8_t register_address,
    uint8_t *data,
    size_t length);

#ifdef __cplusplus
}
#endif

#endif /* TELEMETRY_I2C_H */
