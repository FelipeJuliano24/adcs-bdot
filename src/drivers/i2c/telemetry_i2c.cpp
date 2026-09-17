#include "telemetry_i2c.h"

#include <bsp.h>
#include <bsp/io.h>
#include <bsp/rcc.h>
#include <bsp/stm32_i2c.h>

namespace {

constexpr uint32_t I2C_TIMEOUT_ITERATIONS = 500000U;
constexpr uintptr_t STM32F4_I2C1_BASE_ADDRESS = 0x40005400U;
constexpr uint8_t I2C_READ_BIT = 0x01U;
constexpr uint32_t I2C_ERROR_FLAGS =
    STM32F4_I2C_SR1_BERR |
    STM32F4_I2C_SR1_ARLO |
    STM32F4_I2C_SR1_AF |
    STM32F4_I2C_SR1_OVR |
    STM32F4_I2C_SR1_TIMEOUT;

const stm32f4_gpio_config telemetry_i2c_pins[] = {
    { { STM32F4_GPIO_PIN(TELEMETRY_I2C_GPIO_PORT, TELEMETRY_I2C_SCL_PIN),
        STM32F4_GPIO_PIN(TELEMETRY_I2C_GPIO_PORT, TELEMETRY_I2C_SCL_PIN),
        STM32F4_GPIO_MODE_AF,
        STM32F4_GPIO_OTYPE_OPEN_DRAIN,
        STM32F4_GPIO_OSPEED_50_MHZ,
        STM32F4_GPIO_NO_PULL,
        1U,
        STM32F4_GPIO_AF_I2C1,
        0U } },
    { { STM32F4_GPIO_PIN(TELEMETRY_I2C_GPIO_PORT, TELEMETRY_I2C_SDA_PIN),
        STM32F4_GPIO_PIN(TELEMETRY_I2C_GPIO_PORT, TELEMETRY_I2C_SDA_PIN),
        STM32F4_GPIO_MODE_AF,
        STM32F4_GPIO_OTYPE_OPEN_DRAIN,
        STM32F4_GPIO_OSPEED_50_MHZ,
        STM32F4_GPIO_NO_PULL,
        1U,
        STM32F4_GPIO_AF_I2C1,
        0U } },
    STM32F4_GPIO_CONFIG_TERMINAL
};

volatile stm32f4_i2c *const telemetry_i2c1 =
    reinterpret_cast<volatile stm32f4_i2c *>(STM32F4_I2C1_BASE_ADDRESS);

telemetry_i2c_status_t status_from_errors(uint32_t status)
{
    if ((status & STM32F4_I2C_SR1_AF) != 0U) {
        return TELEMETRY_I2C_NACK;
    }
    return TELEMETRY_I2C_BUS_ERROR;
}

void stop_and_clear_errors(volatile stm32f4_i2c *i2c)
{
    i2c->cr1 |= STM32F4_I2C_CR1_STOP;
    i2c->sr1 &= ~I2C_ERROR_FLAGS;
}

telemetry_i2c_status_t wait_for_status(volatile stm32f4_i2c *i2c, uint32_t flag)
{
    for (uint32_t count = 0U; count < I2C_TIMEOUT_ITERATIONS; ++count) {
        const uint32_t status = i2c->sr1;

        if ((status & I2C_ERROR_FLAGS) != 0U) {
            const telemetry_i2c_status_t result = status_from_errors(status);
            stop_and_clear_errors(i2c);
            return result;
        }

        if ((status & flag) != 0U) {
            return TELEMETRY_I2C_SUCCESS;
        }
    }

    stop_and_clear_errors(i2c);
    return TELEMETRY_I2C_TIMEOUT;
}

telemetry_i2c_status_t wait_for_bus_idle(volatile stm32f4_i2c *i2c)
{
    for (uint32_t count = 0U; count < I2C_TIMEOUT_ITERATIONS; ++count) {
        if ((i2c->sr2 & STM32F4_I2C_SR2_BUSY) == 0U) {
            return TELEMETRY_I2C_SUCCESS;
        }
    }

    return TELEMETRY_I2C_TIMEOUT;
}

void clear_address_flag(volatile stm32f4_i2c *i2c)
{
    (void) i2c->sr1;
    (void) i2c->sr2;
}

void restore_receive_configuration(volatile stm32f4_i2c *i2c)
{
    i2c->cr1 &= ~STM32F4_I2C_CR1_POS;
    i2c->cr1 |= STM32F4_I2C_CR1_ACK;
}

} // namespace

extern "C" telemetry_i2c_status_t telemetry_i2c_init(void)
{
    if (TELEMETRY_I2C_SLAVE_ADDRESS > 0x7fU ||
        TELEMETRY_I2C_BITRATE_HZ == 0U ||
        TELEMETRY_I2C_BITRATE_HZ > 100000U) {
        return TELEMETRY_I2C_INVALID_ARGUMENT;
    }

    volatile stm32f4_i2c *const i2c = telemetry_i2c1;

    stm32f4_gpio_set_clock(
        STM32F4_GPIO_PIN(TELEMETRY_I2C_GPIO_PORT, TELEMETRY_I2C_SCL_PIN),
        true);
    stm32f4_gpio_set_config_array(telemetry_i2c_pins);
    stm32f4_rcc_set_clock(STM32F4_RCC_I2C1, true);
    stm32f4_rcc_set_reset(STM32F4_RCC_I2C1, true);
    stm32f4_rcc_set_reset(STM32F4_RCC_I2C1, false);

    /* Standard-mode timing: CCR = PCLK1 / (2 * I2C clock), TRISE = PCLK1/MHz + 1. */
    const uint32_t peripheral_clock_mhz = STM32F4_PCLK1 / 1000000U;
    const uint32_t clock_control = STM32F4_PCLK1 / (2U * TELEMETRY_I2C_BITRATE_HZ);

    if (peripheral_clock_mhz < 2U ||
        peripheral_clock_mhz > 0x3fU ||
        clock_control == 0U ||
        clock_control > STM32F4_I2C_CCR_CCR_MAX) {
        return TELEMETRY_I2C_INVALID_ARGUMENT;
    }

    i2c->cr1 = STM32F4_I2C_CR1_SWRST;
    i2c->cr1 = 0U;
    i2c->cr2 = STM32F4_I2C_CR2_FREQ( peripheral_clock_mhz );
    i2c->ccr = STM32F4_I2C_CCR_CCR(clock_control);
    i2c->trise = STM32F4_I2C_TRISE(peripheral_clock_mhz + 1U);
    i2c->sr1 = 0U;
    i2c->cr1 = STM32F4_I2C_CR1_ACK | STM32F4_I2C_CR1_PE;

    return TELEMETRY_I2C_SUCCESS;
}

extern "C" telemetry_i2c_status_t telemetry_i2c_write(const uint8_t *data, size_t length)
{
    if (!data || length == 0U) {
        return TELEMETRY_I2C_INVALID_ARGUMENT;
    }

    volatile stm32f4_i2c *const i2c = telemetry_i2c1;
    telemetry_i2c_status_t status = wait_for_bus_idle(i2c);

    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    i2c->cr1 |= STM32F4_I2C_CR1_START;
    status = wait_for_status(i2c, STM32F4_I2C_SR1_SB);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    i2c->dr = (uint32_t) (TELEMETRY_I2C_SLAVE_ADDRESS << 1U);
    status = wait_for_status(i2c, STM32F4_I2C_SR1_ADDR);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    /* Reading SR1 followed by SR2 clears ADDR and enters transmitter mode. */
    clear_address_flag(i2c);

    for (size_t index = 0U; index < length; ++index) {
        status = wait_for_status(i2c, STM32F4_I2C_SR1_TxE);
        if (status != TELEMETRY_I2C_SUCCESS) {
            return status;
        }
        i2c->dr = data[index];
    }

    status = wait_for_status(i2c, STM32F4_I2C_SR1_BTF);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    i2c->cr1 |= STM32F4_I2C_CR1_STOP;
    return TELEMETRY_I2C_SUCCESS;
}

extern "C" telemetry_i2c_status_t telemetry_i2c_read_register(
    uint8_t register_address,
    uint8_t *data,
    size_t length)
{
    if (!data || length == 0U) {
        return TELEMETRY_I2C_INVALID_ARGUMENT;
    }

    volatile stm32f4_i2c *const i2c = telemetry_i2c1;
    telemetry_i2c_status_t status = wait_for_bus_idle(i2c);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    /* Select the OBDH mailbox register with a write transaction. */
    i2c->cr1 |= STM32F4_I2C_CR1_START;
    status = wait_for_status(i2c, STM32F4_I2C_SR1_SB);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    i2c->dr = (uint32_t) (TELEMETRY_I2C_SLAVE_ADDRESS << 1U);
    status = wait_for_status(i2c, STM32F4_I2C_SR1_ADDR);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }
    clear_address_flag(i2c);

    status = wait_for_status(i2c, STM32F4_I2C_SR1_TxE);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }
    i2c->dr = register_address;

    status = wait_for_status(i2c, STM32F4_I2C_SR1_BTF);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    /* Repeated START followed by the slave read address. */
    i2c->cr1 |= STM32F4_I2C_CR1_START;
    status = wait_for_status(i2c, STM32F4_I2C_SR1_SB);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    i2c->dr = (uint32_t) ((TELEMETRY_I2C_SLAVE_ADDRESS << 1U) | I2C_READ_BIT);
    status = wait_for_status(i2c, STM32F4_I2C_SR1_ADDR);
    if (status != TELEMETRY_I2C_SUCCESS) {
        return status;
    }

    if (length == 1U) {
        i2c->cr1 &= ~STM32F4_I2C_CR1_ACK;
        clear_address_flag(i2c);
        i2c->cr1 |= STM32F4_I2C_CR1_STOP;

        status = wait_for_status(i2c, STM32F4_I2C_SR1_RxNE);
        if (status == TELEMETRY_I2C_SUCCESS) {
            data[0] = (uint8_t) i2c->dr;
        }
        restore_receive_configuration(i2c);
        return status;
    }

    if (length == 2U) {
        i2c->cr1 |= STM32F4_I2C_CR1_POS;
        i2c->cr1 &= ~STM32F4_I2C_CR1_ACK;
        clear_address_flag(i2c);

        status = wait_for_status(i2c, STM32F4_I2C_SR1_BTF);
        if (status == TELEMETRY_I2C_SUCCESS) {
            i2c->cr1 |= STM32F4_I2C_CR1_STOP;
            data[0] = (uint8_t) i2c->dr;
            data[1] = (uint8_t) i2c->dr;
        }
        restore_receive_configuration(i2c);
        return status;
    }

    clear_address_flag(i2c);
    size_t index = 0U;
    while (length - index > 3U) {
        status = wait_for_status(i2c, STM32F4_I2C_SR1_RxNE);
        if (status != TELEMETRY_I2C_SUCCESS) {
            restore_receive_configuration(i2c);
            return status;
        }
        data[index++] = (uint8_t) i2c->dr;
    }

    status = wait_for_status(i2c, STM32F4_I2C_SR1_BTF);
    if (status != TELEMETRY_I2C_SUCCESS) {
        restore_receive_configuration(i2c);
        return status;
    }

    i2c->cr1 &= ~STM32F4_I2C_CR1_ACK;
    data[index++] = (uint8_t) i2c->dr;

    status = wait_for_status(i2c, STM32F4_I2C_SR1_BTF);
    if (status != TELEMETRY_I2C_SUCCESS) {
        restore_receive_configuration(i2c);
        return status;
    }

    i2c->cr1 |= STM32F4_I2C_CR1_STOP;
    data[index++] = (uint8_t) i2c->dr;
    data[index] = (uint8_t) i2c->dr;
    restore_receive_configuration(i2c);
    return TELEMETRY_I2C_SUCCESS;
}
