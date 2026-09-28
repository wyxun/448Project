/**
 * @file backend.h
 * @brief AT32F415 static MDI GPIO backend.
 */
#ifndef M448_MASTER_AT32F415_MDI_BACKEND_H
#define M448_MASTER_AT32F415_MDI_BACKEND_H

#include <stdint.h>

#include "at32f415.h"
#include "at32f415_adc.h"
#include "at32f415_dma.h"
#include "at32f415_tmr.h"
#include "at32f415_usart.h"
#include "mdi/core/contract.h"
#include "mdi/core/tick.h"
#include "mdi/feature/uart_stream.h"

/* This board currently runs HEXT(8 MHz) / 2 * PLL36 = 144 MHz. */
#ifndef AT32F415_CORE_CLOCK_HZ
#define AT32F415_CORE_CLOCK_HZ 144000000UL
#endif

/* Raw tick provider supplied by the board's perf_counter port.  Keeping the
 * declaration at the backend boundary prevents drivers from including the
 * perf_counter header just to read a timestamp. */
extern int64_t get_system_ticks(void);

/* Base-timer capability used by the master DAC sample clock.  The timer is
 * a compile-time MDI resource; no callback table or runtime private object is
 * needed.  Frequency changes are setup/control operations, while the ISR
 * consumes the generated resource directly in the board interrupt vector. */
#define MDI_AT32_TIMER_BIND(NAME, TIMER, IRQN, CLOCK_FIELD)                      \
    MDI_INLINE mdi_status_t MDI_OP(NAME, _timer_SetFrequency)(uint32_t wHz)      \
    {                                                                            \
        crm_clocks_freq_type tClocks;                                            \
        uint32_t wTimerClock;                                                    \
        uint32_t wPeriod;                                                        \
        if ((wHz == 0U) || ((TIMER)->ctrl1_bit.tmren != 0U)) {                   \
            return (wHz == 0U) ? MDI_RANGE : MDI_BUSY;                           \
        }                                                                        \
        crm_clocks_freq_get(&tClocks);                                           \
        wTimerClock = tClocks.CLOCK_FIELD;                                       \
        if (tClocks.CLOCK_FIELD != tClocks.ahb_freq) {                           \
            wTimerClock *= 2U;                                                   \
        }                                                                        \
        wPeriod = wTimerClock / wHz;                                             \
        if ((wPeriod < 2U) || (wPeriod > UINT32_MAX)) {                          \
            return MDI_RANGE;                                                    \
        }                                                                        \
        tmr_base_init((TIMER), wPeriod - 1U, 0U);                                \
        tmr_cnt_dir_set((TIMER), TMR_COUNT_UP);                                  \
        tmr_clock_source_div_set((TIMER), TMR_CLOCK_DIV1);                       \
        tmr_flag_clear((TIMER), TMR_OVF_FLAG);                                   \
        return MDI_OK;                                                           \
    }                                                                            \
    MDI_INLINE mdi_status_t MDI_OP(NAME, _timer_Start)(void)                     \
    {                                                                            \
        tmr_flag_clear((TIMER), TMR_OVF_FLAG);                                   \
        tmr_interrupt_enable((TIMER), TMR_OVF_INT, TRUE);                        \
        nvic_irq_enable((IRQN), 1U, 0U);                                         \
        tmr_counter_enable((TIMER), TRUE);                                       \
        return MDI_OK;                                                           \
    }                                                                            \
    MDI_INLINE mdi_status_t MDI_OP(NAME, _timer_Stop)(void)                      \
    {                                                                            \
        tmr_counter_enable((TIMER), FALSE);                                      \
        tmr_interrupt_enable((TIMER), TMR_OVF_INT, FALSE);                       \
        tmr_flag_clear((TIMER), TMR_OVF_FLAG);                                   \
        return MDI_OK;                                                           \
    }                                                                            \
    MDI_INLINE bool MDI_OP(NAME, _timer_IsRunning)(void)                         \
    {                                                                            \
        return (TIMER)->ctrl1_bit.tmren != 0U;                                   \
    }

#define MDI_AT32_TICK_BIND(NAME, NOW_FN)                                        \
    MDI_INLINE mdi_tick_t MDI_OP(NAME, _tick_Now)(void)                          \
    {                                                                            \
        return (mdi_tick_t)(NOW_FN());                                           \
    }

/* AT32F415 USART primitives consumed by the generic MDI UART stream feature.
 * No queue or stream state is kept in this chip backend. */
#define MDI_AT32_USART_RX_READY(UART) \
    (usart_interrupt_flag_get((UART), USART_RDBF_FLAG) != RESET)
#define MDI_AT32_USART_RX_READ(UART) \
    usart_data_receive((UART))
#define MDI_AT32_USART_TX_COMPLETE(UART) \
    (usart_flag_get((UART), USART_TDC_FLAG) != RESET)
#define MDI_AT32_USART_TX_WRITE(UART, BYTE) \
    usart_data_transmit((UART), (BYTE))
#define MDI_AT32_USART_TX_CLEAR(UART) \
    usart_flag_clear((UART), USART_TDC_FLAG)
#define MDI_AT32_USART_TX_IRQ_ENABLE(UART) \
    usart_interrupt_enable((UART), USART_TDC_INT, TRUE)
#define MDI_AT32_USART_TX_IRQ_DISABLE(UART) \
    usart_interrupt_enable((UART), USART_TDC_INT, FALSE)

#define MDI_AT32_UART_STREAM_BIND(NAME, STATE, UART)                           \
    MDI_UART_STREAM_BIND(NAME, STATE, UART,                                   \
                         MDI_AT32_USART_RX_READY, MDI_AT32_USART_RX_READ,      \
                         MDI_AT32_USART_TX_COMPLETE,                           \
                         MDI_AT32_USART_TX_WRITE, MDI_AT32_USART_TX_CLEAR,      \
                         MDI_AT32_USART_TX_IRQ_ENABLE,                         \
                         MDI_AT32_USART_TX_IRQ_DISABLE)

/* Start one software-triggered ADC/DMA block.  BUFFER and the publication
 * counter are compile-time resources from instance.h; no runtime object or
 * provider function pointer is introduced. */
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

/* DMA ownership publication is the only MDI-visible part of the completion
 * IRQ.  The caller supplies the vendor flag names because they differ by MCU. */
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

#endif /* M448_MASTER_AT32F415_MDI_BACKEND_H */
