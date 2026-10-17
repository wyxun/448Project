/**
 * @file backend.h
 * @brief AT32F421 static MDI GPIO backend.
 */
#ifndef M448_CLIENT_AT32F421_MDI_BACKEND_H
#define M448_CLIENT_AT32F421_MDI_BACKEND_H

#include <stdint.h>

#include "at32f421.h"
#include "at32f421_adc.h"
#include "at32f421_dma.h"
#include "at32f421_usart.h"
#include "mdi/core/contract.h"
#include "mdi/core/tick.h"
#include "mdi/feature/uart_stream.h"

extern int64_t get_system_ticks(void);

#ifndef AT32F421_CORE_CLOCK_HZ
#define AT32F421_CORE_CLOCK_HZ 96000000UL
#endif

#define MDI_AT32_TICK_BIND(NAME, NOW_FN)                                        \
    MDI_INLINE mdi_tick_t MDI_OP(NAME, _tick_Now)(void)                          \
    {                                                                            \
        return (mdi_tick_t)(NOW_FN());                                           \
    }

/* AT32F421 USART primitives consumed by the generic MDI UART stream feature.
 * Queue ownership and immediate reads are provided by modus/mdi/feature. */
/**
 * @brief Acknowledge hardware RX loss before the stream reads another byte.
 * @param ptUart UART whose interrupt is being serviced.
 * @return True if overrun was observed; the ambiguous data byte is discarded.
 * @note AT32 clears RX errors with a status-read followed by a data-read.
 */
MDI_INLINE bool at32_mdi_UartRxLost(usart_type *ptUart)
{
    uint32_t wStatus = ptUart->sts;
    if ((wStatus & USART_ROERR_FLAG) == 0U) {
        return false;
    }
    (void)ptUart->dt;
    return true;
}

#define MDI_AT32_USART_RX_READY(UART)                                           \
    (((UART)->sts & USART_RDBF_FLAG) != 0U)
#define MDI_AT32_USART_RX_READ(UART) ((uint8_t)(UART)->dt)
#define MDI_AT32_USART_TX_COMPLETE(UART)                                        \
    (((UART)->ctrl1_bit.tdcien != 0U) &&                                        \
     (((UART)->sts & USART_TDC_FLAG) != 0U))
#define MDI_AT32_USART_TX_WRITE(UART, BYTE) ((UART)->dt = (BYTE))
#define MDI_AT32_USART_TX_CLEAR(UART) ((UART)->sts = ~USART_TDC_FLAG)
#define MDI_AT32_USART_TX_IRQ_ENABLE(UART) ((UART)->ctrl1_bit.tdcien = 1U)
#define MDI_AT32_USART_TX_IRQ_DISABLE(UART) ((UART)->ctrl1_bit.tdcien = 0U)

#define MDI_AT32_UART_STREAM_BIND(NAME, STATE, UART)                            \
    MDI_UART_STREAM_BIND(NAME, STATE, UART,                                     \
        MDI_AT32_USART_RX_READY, MDI_AT32_USART_RX_READ,                        \
        at32_mdi_UartRxLost, MDI_AT32_USART_TX_COMPLETE,                        \
        MDI_AT32_USART_TX_WRITE, MDI_AT32_USART_TX_CLEAR,                       \
        MDI_AT32_USART_TX_IRQ_ENABLE, MDI_AT32_USART_TX_IRQ_DISABLE)

#define MDI_AT32_ADC_DMA_TRIGGER_BIND(NAME, ADC, DMA, BUFFER, BLOCK_SIZE,     \
                                      PUBLISHED, BUSY)                         \
    MDI_INLINE mdi_status_t MDI_OP(NAME, _adc_Start)(void)                     \
    {                                                                           \
        uint32_t wSlot;                                                         \
        if ((BUSY)) { return MDI_BUSY; }                                       \
        wSlot = (uint32_t)(PUBLISHED) & 1U;                                    \
        (DMA)->maddr = (uint32_t)&(BUFFER)[wSlot * (BLOCK_SIZE)];              \
        (DMA)->dtcnt = (uint32_t)(BLOCK_SIZE);                                 \
        (DMA)->ctrl_bit.chen = 1U;                                             \
        (ADC)->ctrl2_bit.adcen = 1U;                                           \
        (BUSY) = true;                                                          \
        (ADC)->ctrl2_bit.ocswtrg = 1U;                                         \
        return MDI_OK;                                                          \
    }

#define MDI_AT32_ADC_DMA_COMPLETE_IRQ(NAME, ADC, DMA, ERROR_FLAG, DONE_FLAG,  \
                                       BUSY)                                    \
    do {                                                                        \
        if (dma_interrupt_flag_get(ERROR_FLAG) != RESET) {                     \
            dma_flag_clear(ERROR_FLAG);                                        \
            (DMA)->ctrl_bit.chen = 0U;                                         \
            (ADC)->ctrl2_bit.adcen = 0U;                                       \
            (BUSY) = false;                                                     \
            return;                                                             \
        }                                                                       \
        if (dma_interrupt_flag_get(DONE_FLAG) == RESET) { return; }             \
        dma_flag_clear(DONE_FLAG);                                              \
        (DMA)->ctrl_bit.chen = 0U;                                              \
        (ADC)->ctrl2_bit.adcen = 0U;                                            \
        (BUSY) = false;                                                         \
        MDI_ADC_DMA_Publish(NAME);                                              \
    } while (0)

#define AT32_MDI_PIN_MASK(V, L, P, I) \
    | (UINT32_C(1) << (P))
#define AT32_MDI_PIN_LOGICAL(V, L, P, I) \
    | (UINT32_C(1) << (L))
#define AT32_MDI_PIN_PHYSICAL_SUM(V, L, P, I) \
    + (UINT64_C(1) << (P))
#define AT32_MDI_PIN_LOGICAL_SUM(V, L, P, I) \
    + (UINT64_C(1) << (L))
#define AT32_MDI_PIN_ENCODE(V, L, P, I) \
    | (((((uint32_t)(V) >> (L)) ^ (uint32_t)(I)) & 1U) << (P))
#define AT32_MDI_PIN_MASKED_MASK(C, L, P, I) \
    | ((((C)->wMask >> (L)) & 1U) << (P))
#define AT32_MDI_PIN_MASKED_VALUE(C, L, P, I) \
    | (((((C)->wValue >> (L)) ^ (uint32_t)(I)) & 1U) << (P))
#define AT32_MDI_PIN_DECODE(V, L, P, I) \
    | (((((uint32_t)(V) >> (P)) ^ (uint32_t)(I)) & 1U) << (L))
#define AT32_MDI_PIN_CHECK(W, L, P, I) \
    _Static_assert((L) >= 0 && (L) < (W), "logical pin out of range"); \
    _Static_assert((P) >= 0 && (P) < 16, "AT32 pin out of range"); \
    _Static_assert((I) == 0 || (I) == 1, "invalid pin polarity");

#define AT32_MDI_PORT_CHECK(W, PORT, PINS) \
    PINS(AT32_MDI_PIN_CHECK, W) \
    _Static_assert((0ULL PINS(AT32_MDI_PIN_PHYSICAL_SUM, 0)) == \
                   (0ULL PINS(AT32_MDI_PIN_MASK, 0)), \
                   "duplicate physical pin");

#define AT32_MDI_PORT_LOGICAL_SUM(V, PORT, PINS) \
    + (0ULL PINS(AT32_MDI_PIN_LOGICAL_SUM, 0))
#define AT32_MDI_PORT_LOGICAL_MASK(V, PORT, PINS) \
    | (0UL PINS(AT32_MDI_PIN_LOGICAL, 0))
#define AT32_MDI_PORT_WRITE(V, PORT, PINS) \
    { \
        const uint32_t wMask = 0U PINS(AT32_MDI_PIN_MASK, 0); \
        const uint32_t wBits = 0U PINS(AT32_MDI_PIN_ENCODE, V); \
        uint32_t wOutput = (PORT)->odt; \
        wOutput = (wOutput & ~wMask) | wBits; \
        (PORT)->odt = wOutput; \
    }
#define AT32_MDI_PORT_WRITE_ATOMIC(V, PORT, PINS) \
    { \
        const uint32_t wMask = 0U PINS(AT32_MDI_PIN_MASK, 0); \
        const uint32_t wBits = 0U PINS(AT32_MDI_PIN_ENCODE, V); \
        (PORT)->clr = wMask & ~wBits; \
        (PORT)->scr = wBits; \
    }
#define AT32_MDI_PORT_WRITE_ATOMIC_MASKED(C, PORT, PINS) \
    { \
        const uint32_t wMask = 0U PINS(AT32_MDI_PIN_MASKED_MASK, C); \
        const uint32_t wBits = 0U PINS(AT32_MDI_PIN_MASKED_VALUE, C); \
        (PORT)->clr = wMask & ~wBits; \
        (PORT)->scr = wBits; \
    }
#define AT32_MDI_PORT_WRITE_MASKED(C, PORT, PINS) \
    { \
        const uint32_t wMask = 0U PINS(AT32_MDI_PIN_MASKED_MASK, C); \
        const uint32_t wBits = 0U PINS(AT32_MDI_PIN_MASKED_VALUE, C); \
        uint32_t wOutput = (PORT)->odt; \
        wOutput = (wOutput & ~wMask) | wBits; \
        (PORT)->odt = wOutput; \
    }
#define AT32_MDI_PORT_READ(V, PORT, PINS) \
    { \
        const uint32_t wPort = (PORT)->idt; \
        (V) |= 0U PINS(AT32_MDI_PIN_DECODE, wPort); \
    }

#define MDI_AT32_IO_BIND_VALIDATE(NAME, WIDTH, PORTS, CAPS) \
    _Static_assert((WIDTH) > 0 && (WIDTH) <= 32, "invalid IO width"); \
    PORTS(AT32_MDI_PORT_CHECK, WIDTH) \
    _Static_assert((0ULL PORTS(AT32_MDI_PORT_LOGICAL_SUM, 0)) == \
                   ((UINT64_C(1) << (WIDTH)) - 1U), \
                   "duplicate or missing logical pin"); \
    enum { MDI_OP(NAME, _io_width) = (WIDTH) }; \
    enum { MDI_OP(NAME, _io_caps) = (CAPS) };

#define MDI_AT32_IO_BIND_CAPS(NAME, WIDTH, PORTS, CAPS) \
    _Static_assert(((CAPS) & (MDI_IO_CAP_INPUT | MDI_IO_CAP_OUTPUT)) == \
                   (MDI_IO_CAP_INPUT | MDI_IO_CAP_OUTPUT), \
                   "use input/output-specific binding for one-way IO"); \
    MDI_AT32_IO_BIND_VALIDATE(NAME, WIDTH, PORTS, CAPS) \
    MDI_INLINE void MDI_OP(NAME, _io_Write)(uint32_t wValue) \
    { PORTS(AT32_MDI_PORT_WRITE, wValue) } \
    MDI_INLINE void MDI_OP(NAME, _io_WriteMasked)( \
        uint32_t wMask, uint32_t wValue) \
    { \
        struct { uint32_t wMask; uint32_t wValue; } tWrite = {wMask, wValue}; \
        PORTS(AT32_MDI_PORT_WRITE_MASKED, &tWrite) \
    } \
    MDI_INLINE uint32_t MDI_OP(NAME, _io_Read)(void) \
    { uint32_t wValue = 0U; PORTS(AT32_MDI_PORT_READ, wValue) return wValue; }

#define MDI_AT32_IO_BIND_INPUT_CAPS(NAME, WIDTH, PORTS, CAPS) \
    _Static_assert(((CAPS) & MDI_IO_CAP_INPUT) != 0U && \
                   ((CAPS) & MDI_IO_CAP_OUTPUT) == 0U, \
                   "input binding capabilities must be input-only"); \
    MDI_AT32_IO_BIND_VALIDATE(NAME, WIDTH, PORTS, CAPS) \
    MDI_INLINE uint32_t MDI_OP(NAME, _io_Read)(void) \
    { uint32_t wValue = 0U; PORTS(AT32_MDI_PORT_READ, wValue) return wValue; }

#define MDI_AT32_IO_BIND_OUTPUT_CAPS(NAME, WIDTH, PORTS, CAPS) \
    _Static_assert(((CAPS) & MDI_IO_CAP_OUTPUT) != 0U && \
                   ((CAPS) & MDI_IO_CAP_INPUT) == 0U, \
                   "output binding capabilities must be output-only"); \
    MDI_AT32_IO_BIND_VALIDATE(NAME, WIDTH, PORTS, CAPS) \
    MDI_INLINE void MDI_OP(NAME, _io_Write)(uint32_t wValue) \
    { PORTS(AT32_MDI_PORT_WRITE_ATOMIC, wValue) } \
    MDI_INLINE void MDI_OP(NAME, _io_WriteMasked)(uint32_t wMask, uint32_t wValue) \
    { struct { uint32_t wMask; uint32_t wValue; } tWrite = {wMask, wValue}; \
      PORTS(AT32_MDI_PORT_WRITE_ATOMIC_MASKED, &tWrite) }

#define MDI_AT32_IO_BIND(NAME, WIDTH, PORTS) \
    MDI_AT32_IO_BIND_CAPS(NAME, WIDTH, PORTS, \
                          MDI_IO_CAP_INPUT | MDI_IO_CAP_OUTPUT)

#endif /* M448_CLIENT_AT32F421_MDI_BACKEND_H */

