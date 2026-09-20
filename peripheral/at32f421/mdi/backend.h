/**
 * @file backend.h
 * @brief AT32F415 static MDI GPIO backend.
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
#include "mringbuf.h"

extern int64_t get_system_ticks(void);

#ifndef AT32F421_CORE_CLOCK_HZ
#define AT32F421_CORE_CLOCK_HZ 48000000UL
#endif

#define MDI_AT32_TICK_BIND(NAME, NOW_FN)                                        \
    MDI_INLINE mdi_tick_t MDI_OP(NAME, _tick_Now)(void)                          \
    {                                                                            \
        return (mdi_tick_t)(NOW_FN());                                           \
    }

typedef struct {
    usart_type *ptUsart;
    mringbuf_t tTxQueue;
    mringbuf_t tRxQueue;
    volatile uint8_t chTxBusy;
    volatile uint8_t chRxState;
    volatile uint8_t chRxGuardMs;
} mdi_at32_stream_state_t;

#define MDI_AT32_STREAM_BIND(NAME, STATE)                                      \
    MDI_INLINE int32_t MDI_OP(NAME, _stream_Write)(                            \
        const uint8_t *pchData, uint32_t wLength)                              \
    {                                                                           \
        mdi_at32_stream_state_t *pt = &(STATE);                                \
        uint32_t wIndex = 0U;                                                   \
        while (wIndex < wLength) {                                              \
            if (mringbuf_Write(&pt->tTxQueue, pchData[wIndex]) == 0U) {         \
                break;                                                          \
            }                                                                   \
            ++wIndex;                                                           \
        }                                                                       \
        if ((pt->chTxBusy == 0U) &&                                            \
            (mringbuf_GetUsed(&pt->tTxQueue) > 0U)) {                           \
            uint8_t ch = 0U;                                                    \
            if (mringbuf_Read(&pt->tTxQueue, &ch) == 1U) {                       \
                usart_data_transmit(pt->ptUsart, ch);                           \
                pt->chTxBusy = 1U;                                              \
            }                                                                   \
        }                                                                       \
        return (int32_t)wIndex;                                                  \
    }                                                                           \
    MDI_INLINE int32_t MDI_OP(NAME, _stream_Read)(                             \
        uint8_t *pchData, uint32_t wLength)                                     \
    {                                                                           \
        mdi_at32_stream_state_t *pt = &(STATE);                                \
        uint32_t wIndex = 0U;                                                   \
        if ((pt->chRxState != 2U) || (wLength == 0U)) { return 0; }              \
        while (wIndex < wLength) {                                              \
            if (mringbuf_Read(&pt->tRxQueue, &pchData[wIndex]) == 0U) {          \
                break;                                                          \
            }                                                                   \
            ++wIndex;                                                           \
        }                                                                       \
        pt->chRxState = 0U;                                                     \
        return (int32_t)wIndex;                                                  \
    }                                                                           \
    MDI_INLINE uint32_t MDI_OP(NAME, _stream_Available)(void)                   \
    {                                                                           \
        return (uint32_t)mringbuf_GetUsed(&(STATE).tRxQueue);                   \
    }                                                                           \
    MDI_INLINE bool MDI_OP(NAME, _stream_IsBusy)(void)                          \
    {                                                                           \
        return (STATE).chTxBusy != 0U;                                          \
    }

#define MDI_AT32_STREAM_INIT(STATE, USART, TXBUF, TXSIZE, RXBUF, RXSIZE)        \
    do {                                                                        \
        (STATE).ptUsart = (USART);                                              \
        mringbuf_Init(&(STATE).tTxQueue, (TXBUF), (uint16_t)(TXSIZE));           \
        mringbuf_Init(&(STATE).tRxQueue, (RXBUF), (uint16_t)(RXSIZE));           \
        (STATE).chTxBusy = 0U;                                                  \
        (STATE).chRxState = 0U;                                                  \
        (STATE).chRxGuardMs = 0U;                                               \
    } while (0)

#define MDI_AT32_STREAM_TICK_1MS(STATE)                                         \
    do {                                                                        \
        if ((STATE).chRxState == 1U) {                                         \
            if ((STATE).chRxGuardMs > 0U) { --(STATE).chRxGuardMs; }            \
            else { (STATE).chRxState = 2U; }                                    \
        }                                                                       \
    } while (0)

#define MDI_AT32_STREAM_IRQ(STATE)                                              \
    do {                                                                        \
        uint8_t chRx = 0U;                                                      \
        uint8_t chTx = 0U;                                                      \
        if (usart_interrupt_flag_get((STATE).ptUsart, USART_RDBF_FLAG) != RESET) \
        {                                                                       \
            chRx = usart_data_receive((STATE).ptUsart);                        \
            (void)mringbuf_Write(&(STATE).tRxQueue, chRx);                      \
            (STATE).chRxGuardMs = 5U;                                           \
            (STATE).chRxState = 1U;                                             \
        }                                                                       \
        if (usart_flag_get((STATE).ptUsart, USART_TDC_FLAG) != RESET) {         \
            usart_flag_clear((STATE).ptUsart, USART_TDC_FLAG);                  \
            if (mringbuf_Read(&(STATE).tTxQueue, &chTx) == 1U) {                \
                usart_data_transmit((STATE).ptUsart, chTx);                     \
                (STATE).chTxBusy = 1U;                                          \
            } else {                                                            \
                (STATE).chTxBusy = 0U;                                          \
            }                                                                   \
        }                                                                       \
    } while (0)

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

