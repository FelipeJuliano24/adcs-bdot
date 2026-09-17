#include "obdh_uart.h"

#include <bsp/io.h>
#include <bsp/irq.h>
#include <bsp/rcc.h>
#include <bsp/stm32_usart.h>

#include "../telemetry/telemetry.h"

namespace {

constexpr uintptr_t STM32F4_USART3_BASE_ADDRESS = 0x40004800U;
constexpr rtems_interval OBDH_UART_TX_TIMEOUT_TICKS = 100U;
constexpr uint16_t RX_RING_CAPACITY = 2048U;
constexpr uint16_t RX_RING_MASK = RX_RING_CAPACITY - 1U;
constexpr uint8_t HDLC_FLAG = 0x7eU;
constexpr uint8_t HDLC_ESCAPE = 0x7dU;
constexpr uint8_t HDLC_ESCAPE_XOR = 0x20U;

static_assert((RX_RING_CAPACITY & RX_RING_MASK) == 0U,
              "RX ring capacity must be a power of two");

volatile stm32f4_usart *const usart3 =
    reinterpret_cast<volatile stm32f4_usart *>(STM32F4_USART3_BASE_ADDRESS);

volatile uint8_t rx_ring[RX_RING_CAPACITY] = {};
volatile uint16_t rx_head = 0U;
volatile uint16_t rx_tail = 0U;
volatile uint32_t rx_error_flags = 0U;
volatile uint32_t rx_dropped_bytes = 0U;
rtems_id receiver_task = RTEMS_ID_NONE;
bool uart_initialised = false;

const stm32f4_gpio_config usart3_pins[] = {
    { { STM32F4_GPIO_PIN(OBDH_UART_TX_GPIO_PORT, OBDH_UART_TX_GPIO_PIN),
        STM32F4_GPIO_PIN(OBDH_UART_TX_GPIO_PORT, OBDH_UART_TX_GPIO_PIN),
        STM32F4_GPIO_MODE_AF, STM32F4_GPIO_OTYPE_PUSH_PULL,
        STM32F4_GPIO_OSPEED_25_MHZ, STM32F4_GPIO_PULL_UP, 0U,
        STM32F4_GPIO_AF_USART3, 0U } },
    { { STM32F4_GPIO_PIN(OBDH_UART_RX_GPIO_PORT, OBDH_UART_RX_GPIO_PIN),
        STM32F4_GPIO_PIN(OBDH_UART_RX_GPIO_PORT, OBDH_UART_RX_GPIO_PIN),
        STM32F4_GPIO_MODE_AF, STM32F4_GPIO_OTYPE_PUSH_PULL,
        STM32F4_GPIO_OSPEED_25_MHZ, STM32F4_GPIO_PULL_UP, 0U,
        STM32F4_GPIO_AF_USART3, 0U } },
    STM32F4_GPIO_CONFIG_TERMINAL
};

void data_memory_barrier()
{
    __asm__ volatile("dmb" ::: "memory");
}

bool interval_elapsed(rtems_interval start, rtems_interval timeout)
{
    return (rtems_clock_get_ticks_since_boot() - start) >= timeout;
}

bool wait_for_transmit_empty(rtems_interval start)
{
    while ((usart3->sr & STM32F4_USART_SR_TXE) == 0U) {
        if (interval_elapsed(start, OBDH_UART_TX_TIMEOUT_TICKS)) {
            return false;
        }
    }
    return true;
}

bool wait_for_transmission_complete(rtems_interval start)
{
    while ((usart3->sr & STM32F4_USART_SR_TC) == 0U) {
        if (interval_elapsed(start, OBDH_UART_TX_TIMEOUT_TICKS)) {
            return false;
        }
    }
    return true;
}

void record_hardware_errors(uint32_t status)
{
    if ((status & STM32F4_USART_SR_ORE) != 0U) {
        rx_error_flags |= OBDH_UART_RX_ERROR_OVERRUN;
    }
    if ((status & STM32F4_USART_SR_NF) != 0U) {
        rx_error_flags |= OBDH_UART_RX_ERROR_NOISE;
    }
    if ((status & STM32F4_USART_SR_FE) != 0U) {
        rx_error_flags |= OBDH_UART_RX_ERROR_FRAMING;
    }
    if ((status & STM32F4_USART_SR_PE) != 0U) {
        rx_error_flags |= OBDH_UART_RX_ERROR_PARITY;
    }
}

bool push_received_byte(uint8_t byte)
{
    const uint16_t next_head = (uint16_t) ((rx_head + 1U) & RX_RING_MASK);
    if (next_head == rx_tail) {
        rx_error_flags |= OBDH_UART_RX_ERROR_RING_OVERFLOW;
        ++rx_dropped_bytes;
        return false;
    }

    rx_ring[rx_head] = byte;
    data_memory_barrier();
    rx_head = next_head;
    return true;
}

void usart3_interrupt_handler(void *)
{
    bool received_byte = false;

    while (true) {
        const uint32_t status = usart3->sr;
        const uint32_t relevant_flags =
            STM32F4_USART_SR_RXNE |
            STM32F4_USART_SR_ORE |
            STM32F4_USART_SR_NF |
            STM32F4_USART_SR_FE |
            STM32F4_USART_SR_PE;

        if ((status & relevant_flags) == 0U) {
            break;
        }

        /* STM32F4 clears RXNE and line-error flags by reading SR then DR. */
        const uint8_t byte = (uint8_t) STM32F4_USART_DR_GET(usart3->dr);
        record_hardware_errors(status);

        if ((status & STM32F4_USART_SR_RXNE) != 0U) {
            received_byte = push_received_byte(byte) || received_byte;
        }
    }

    if (received_byte && receiver_task != RTEMS_ID_NONE) {
        (void) rtems_event_send(receiver_task, OBDH_UART_RX_EVENT);
    }
}

obdh_uart_status_t send_encoded_byte(uint8_t byte, rtems_interval start)
{
    if (!wait_for_transmit_empty(start)) {
        return OBDH_UART_TIMEOUT;
    }
    usart3->dr = STM32F4_USART_DR(byte);
    return OBDH_UART_SUCCESS;
}

} // namespace

extern "C" rtems_status_code obdh_uart_init(rtems_id target_task)
{
    if (target_task == RTEMS_ID_NONE) {
        return RTEMS_INVALID_ID;
    }
    if (uart_initialised) {
        return RTEMS_INCORRECT_STATE;
    }

    stm32f4_gpio_set_config_array(usart3_pins);
    stm32f4_rcc_set_clock(STM32F4_RCC_USART3, true);

    usart3->cr1 = 0U;
    usart3->cr2 = 0U;
    usart3->cr3 = 0U;
    usart3->bbr = (STM32F4_PCLK1 + OBDH_UART_BAUD_RATE / 2U) /
        OBDH_UART_BAUD_RATE;

    /* Clear any stale receive/error condition before enabling its interrupt. */
    (void) usart3->sr;
    (void) usart3->dr;

    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    rx_head = 0U;
    rx_tail = 0U;
    rx_error_flags = 0U;
    rx_dropped_bytes = 0U;
    receiver_task = target_task;
    rtems_interrupt_enable(level);

    usart3->cr1 = STM32F4_USART_CR1_UE |
        STM32F4_USART_CR1_TE |
        STM32F4_USART_CR1_RE;

    const rtems_status_code status = rtems_interrupt_handler_install(
        STM32F4_IRQ_USART3,
        "OBDH USART3 RX",
        RTEMS_INTERRUPT_UNIQUE,
        usart3_interrupt_handler,
        nullptr);
    if (status != RTEMS_SUCCESSFUL) {
        receiver_task = RTEMS_ID_NONE;
        usart3->cr1 = 0U;
        return status;
    }

    usart3->cr3 = STM32F4_USART_CR3_EIE;
    usart3->cr1 |= STM32F4_USART_CR1_RXNEIE | STM32F4_USART_CR1_PEIE;
    uart_initialised = true;
    return RTEMS_SUCCESSFUL;
}

extern "C" bool obdh_uart_read_byte(uint8_t *byte)
{
    if (!byte || !uart_initialised || rx_tail == rx_head) {
        return false;
    }

    *byte = rx_ring[rx_tail];
    data_memory_barrier();
    rx_tail = (uint16_t) ((rx_tail + 1U) & RX_RING_MASK);
    return true;
}

extern "C" void obdh_uart_take_rx_diagnostics(
    obdh_uart_rx_diagnostics_t *diagnostics)
{
    if (!diagnostics) {
        return;
    }

    rtems_interrupt_level level;
    rtems_interrupt_disable(level);
    diagnostics->flags = rx_error_flags;
    diagnostics->dropped_bytes = rx_dropped_bytes;
    rx_error_flags = 0U;
    rx_dropped_bytes = 0U;
    rtems_interrupt_enable(level);
}

extern "C" obdh_uart_status_t obdh_uart_send_pus_frame(
    const uint8_t *frame,
    size_t length)
{
    if (!uart_initialised) {
        return OBDH_UART_NOT_INITIALISED;
    }
    if (!frame || length == 0U || length > PUS_FRAME_MAX_SIZE) {
        return OBDH_UART_INVALID_ARGUMENT;
    }

    const rtems_interval start = rtems_clock_get_ticks_since_boot();
    obdh_uart_status_t status = send_encoded_byte(HDLC_FLAG, start);
    if (status != OBDH_UART_SUCCESS) {
        return status;
    }

    for (size_t index = 0U; index < length; ++index) {
        const uint8_t byte = frame[index];
        if (byte == HDLC_FLAG || byte == HDLC_ESCAPE) {
            status = send_encoded_byte(HDLC_ESCAPE, start);
            if (status != OBDH_UART_SUCCESS) {
                return status;
            }
            status = send_encoded_byte((uint8_t) (byte ^ HDLC_ESCAPE_XOR), start);
        } else {
            status = send_encoded_byte(byte, start);
        }

        if (status != OBDH_UART_SUCCESS) {
            return status;
        }
    }

    status = send_encoded_byte(HDLC_FLAG, start);
    if (status != OBDH_UART_SUCCESS || !wait_for_transmission_complete(start)) {
        return OBDH_UART_TIMEOUT;
    }

    return OBDH_UART_SUCCESS;
}
