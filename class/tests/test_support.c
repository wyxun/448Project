#include "mdi/instance.h"
#include "modus.h"
#include <string.h>
int64_t test_now_ms;
unsigned test_irq_depth;
int test_stop_fail;
int test_start_fail;
int test_stream_fail;
int test_stream_overrun;
uint32_t test_write_limit;
unsigned test_read_calls;
int test_base_fail;
unsigned test_registrations;
unsigned test_timer_running;
unsigned test_gpio[6];
static uint8_t s_rx[256];
static uint32_t s_length;
void test_reset(void)
{
    test_now_ms = 1;
    test_stream_overrun = 0;
    test_read_calls = 0U;
    test_write_limit = UINT32_MAX;
    test_irq_depth = 0U;
    test_stop_fail = test_start_fail = test_stream_fail = test_base_fail = 0;
    test_registrations = test_timer_running = s_length = 0U;
    memset(test_gpio, 0, sizeof(test_gpio));
}
void test_feed(const uint8_t *data, uint32_t length)
{
    memcpy(s_rx, data, length);
    s_length = length;
}
mdi_status_t test_read(unsigned resource, uint8_t *data,
                       uint32_t length, uint32_t *count)
{
    (void)resource;
    test_read_calls++;
    *count = 0U;
    if (test_stream_fail) { return MDI_IO_ERROR; }
    *count = length < s_length ? length : s_length;
    memcpy(data, s_rx, *count);
    memmove(s_rx, s_rx + *count, s_length - *count);
    s_length -= *count;
    if (test_stream_overrun) {
        test_stream_overrun = 0;
        return MDI_OVERRUN;
    }
    return MDI_OK;
}
mdi_status_t test_write(unsigned resource, const uint8_t *data,
                        uint32_t length, uint32_t *count)
{
    (void)resource;
    (void)data;
    *count = 0U;
    if (test_stream_fail) { return MDI_IO_ERROR; }
    *count = length < test_write_limit ? length : test_write_limit;
    return MDI_OK;
}
void test_log(const char *format, ...)
{
    (void)format;
}
mdi_status_t test_start(void)
{
    if (test_start_fail) { return MDI_IO; }
    test_timer_running = 1U;
    return MDI_OK;
}
mdi_status_t test_stop(void)
{
    if (test_stop_fail) { return MDI_IO; }
    test_timer_running = 0U;
    return MDI_OK;
}
int mbase_Init(modus_base_t *base, modus_base_cfg_t *cfg)
{
    test_registrations++;
    base->wParent = cfg->wParent;
    return test_base_fail ? MODUS_EFAIL : MODUS_SUCCESS;
}
bool test_timeout(uint32_t ms, int64_t *deadline, bool reload)
{
    if (*deadline == 0) { *deadline = test_now_ms + ms; return false; }
    if (test_now_ms < *deadline) { return false; }
    if (reload) { *deadline = test_now_ms + ms; }
    return true;
}

#if defined(TEST_ADC_SERVICE)
mdi_tick_t test_raw_tick;
unsigned test_adc_starts;
unsigned test_adc_updates;
bool test_adc_busy;
mdi_status_t test_adc_start(void)
{
    if (test_adc_busy) { return MDI_BUSY; }
    test_adc_starts++;
    test_adc_busy = true;
    return MDI_OK;
}
mdi_status_t test_adc_update(void)
{
    test_adc_updates++;
    return MDI_BUSY;
}
#endif
