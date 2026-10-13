#ifndef CLASS_TEST_MDI_H
#define CLASS_TEST_MDI_H
#include <stdbool.h>
#include <stdint.h>
#include "perf_counter.h"
#include "mdi/core/contract.h"
#define MDI_IO MDI_IO_ERROR
extern int test_stop_fail;
extern int test_start_fail;
extern int test_stream_fail;
extern int test_stream_overrun;
extern uint32_t test_write_limit;
extern unsigned test_read_calls;
extern int test_base_fail;
extern unsigned test_registrations;
extern unsigned test_timer_running;
extern unsigned test_gpio[5];
enum { dac_oe, dac_ab, dac_parallel, dac_wr, bio_power };
enum { dwin_stream, board_link_stream };
mdi_status_t test_read(unsigned resource, uint8_t *data,
                       uint32_t length, uint32_t *count);
mdi_status_t test_write(unsigned resource, const uint8_t *data,
                        uint32_t length, uint32_t *count);
void test_feed(const uint8_t *data, uint32_t length);
void test_reset(void);
mdi_status_t test_start(void);
mdi_status_t test_stop(void);
#define MDI_STREAM_Read(R, P, L, N) test_read((R), (P), (L), (N))
#define MDI_STREAM_Write(R, P, L, N) test_write((R), (P), (L), (N))
#define TEST_GPIO(R) \
    static inline void R##_io_Write(uint32_t value) { test_gpio[R] = value; }
TEST_GPIO(dac_oe)
TEST_GPIO(dac_ab)
TEST_GPIO(dac_parallel)
TEST_GPIO(dac_wr)
TEST_GPIO(bio_power)
#define MDI_TIMER_SetFrequency(R, H) ((H) != 0U ? MDI_OK : MDI_IO)
#define MDI_TIMER_Start(R) test_start()
#define MDI_TIMER_Stop(R) test_stop()
typedef struct { uint32_t wCode; } mdi_adc_value_t;
enum { adc_ntc1, adc_ntc2, adc_ntc3, adc_ntc4, adc_bioadc };
static inline mdi_status_t test_adc(unsigned channel, mdi_adc_value_t *value)
{
    value->wCode = channel;
    return MDI_OK;
}
#define MDI_ADC_Read(R, P) test_adc((R), (P))
#if defined(TEST_ADC_SERVICE)
#include "mdi/core/tick.h"
#define AT32F415_CORE_CLOCK_HZ 144000000UL
#define AT32F421_CORE_CLOCK_HZ 48000000UL
extern mdi_tick_t test_raw_tick;
extern unsigned test_adc_starts;
extern unsigned test_adc_updates;
extern bool test_adc_busy;
mdi_status_t test_adc_start(void);
mdi_status_t test_adc_update(void);
static inline mdi_tick_t raw_tick_tick_Now(void) { return test_raw_tick; }
#define MDI_ADC_Start(R) test_adc_start()
#define MDI_ADC_MeanUpdate(R) test_adc_update()
#endif
#endif
