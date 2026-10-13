#include "dac_driver.h"
#include "dwin_driver.h"
#include "board_link_driver.h"
#include "mircoelec.h"
#include "mvc.h"
#include "mdi/instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if defined(TEST_DWIN)
static unsigned s_callbacks;
static void receive(void *context, const dwin_driver_frame_t *frame)
{
    unsigned *count = context;
    assert(frame->chCommand == DWIN_DRIVER_CMD_READ);
    assert(frame->hwAddress == 0x1234U);
    assert(frame->hwLength == 3U);
    assert(frame->pchData[1] == 0xABU);
    (*count)++;
}
static uint32_t make_frame(uint8_t *frame)
{
    const uint8_t base[] = {0x5AU, 0xA5U, 6U, 0x83U,
                            0x12U, 0x34U, 1U, 0xABU, 0xCDU};
    memcpy(frame, base, sizeof(base));
#if DWIN_DRIVER_CRC16_EN
    uint16_t crc = 0xFFFFU;
    for (unsigned i = 3U; i < sizeof(base); i++) {
        crc ^= frame[i];
        for (unsigned bit = 0U; bit < 8U; bit++) {
            crc = (uint16_t)((crc >> 1U) ^ ((crc & 1U) ? 0xA001U : 0U));
        }
    }
    frame[2] = 8U;
    frame[9] = (uint8_t)crc;
    frame[10] = (uint8_t)(crc >> 8U);
    return 11U;
#else
    return 9U;
#endif
}
static void dwin_frames_test(void)
{
    dwin_driver_t driver = {0};
    dwin_driver_cfg_t cfg = {20U};
    uint8_t frame[11] = {0};
    uint8_t queue[110] = {0};
    uint32_t length = make_frame(frame);
    const uint8_t short_frame[] = {0x5AU, 0xA5U, 1U, 0x83U};
    assert(dwin_driver_Init(&driver, &cfg) == DWIN_DRIVER_RESULT_OK);
    assert(dwin_driver_RegisterCallback(&driver, receive, &s_callbacks)
           == DWIN_DRIVER_RESULT_OK);
    test_feed(short_frame, sizeof(short_frame));
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    assert(s_callbacks == 0U);
    for (uint32_t i = 0U; i < length; i++) {
        test_feed(&frame[i], 1U);
        assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    }
    assert(s_callbacks == 1U);
    for (uint32_t i = 0U; i < 10U; i++) {
        memcpy(&queue[i * length], frame, length);
    }
    test_feed(queue, 10U * length);
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    assert(s_callbacks == 9U);
    test_now_ms += 100;
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    assert(s_callbacks == 11U);
    assert(driver.tStats.wTimeout == 0U);
#if DWIN_DRIVER_CRC16_EN
    frame[length - 1U] ^= 1U;
    test_feed(frame, length);
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    assert(s_callbacks == 11U);
#endif
    length = make_frame(frame);
    test_feed(frame, 4U);
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    test_stream_overrun = 1;
    test_feed(frame, length);
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    assert(driver.tStats.wOverrun == 1U && driver.hwRxPos == 0U);
    assert(s_callbacks == 11U);
    test_feed(frame, length);
    assert(dwin_driver_Run(&driver) == DWIN_DRIVER_RESULT_OK);
    assert(s_callbacks == 12U);
    test_write_limit = 1U;
    assert(dwin_driver_SetPage(&driver, 1U) == DWIN_DRIVER_RESULT_IO);
    test_write_limit = UINT32_MAX;
    assert(dwin_driver_SetPage(&driver, 1U) == DWIN_DRIVER_RESULT_OK);
}
#endif
#if defined(TEST_DAC)
static void dac_test(void)
{
    dac_driver_t dac = {0};
    dac_driver_cfg_t cfg = {320000U};
    assert(dac_driver_Init(NULL, &cfg) == DAC_DRIVER_RESULT_NULL);
    assert(dac_driver_Init(&dac, NULL)
           == DAC_DRIVER_RESULT_INVALID_ARGUMENT);
    { dac_driver_cfg_t invalid = {0};
      assert(dac_driver_Init(&dac, &invalid)
             == DAC_DRIVER_RESULT_INVALID_ARGUMENT); }
    assert(dac_driver_Init(&dac, &cfg) == DAC_DRIVER_RESULT_OK);
    assert(test_timer_running == 0U);
    assert(dac_driver_SetFrequencies(&dac, 20001U, 100U)
           == DAC_DRIVER_RESULT_INVALID_ARGUMENT);
    assert(dac_driver_SetFrequencies(&dac, 20000U, 100U)
           == DAC_DRIVER_RESULT_OK);
    assert(test_irq_depth == 0U);
    test_irq_depth = 1U;
    assert(dac_driver_SetFrequencies(&dac, 20000U, 100U)
           == DAC_DRIVER_RESULT_OK);
    assert(test_irq_depth == 1U);
    test_irq_depth = 0U;
    assert(dac_driver_Start(&dac) == DAC_DRIVER_RESULT_OK);
    dac_driver_IsrStep(&dac);
    assert(dac.tCarrier.wPhase == 268435456U);
    test_stop_fail = 1;
    assert(dac_driver_Stop(&dac) == DAC_DRIVER_RESULT_IO);
    assert(test_gpio[dac_oe] == 0U);
    dac_driver_IsrStep(&dac);
    assert(dac.tCarrier.wPhase == 268435456U);
    test_stop_fail = 0;
    assert(dac_driver_Stop(&dac) == DAC_DRIVER_RESULT_OK);
    assert(dac.eState == DAC_DRIVER_STATE_ERROR);
}
#endif
#if defined(TEST_DWIN)
static void dwin_test(void)
{
    dwin_driver_t a = {0};
    dwin_driver_t b = {0};
    dwin_driver_cfg_t cfg = {20U};
    const uint8_t partial[] = {0x5AU, 0xA5U, 9U, 0x83U};
    assert(dwin_driver_Init(NULL, &cfg) == DWIN_DRIVER_RESULT_NULL);
    assert(dwin_driver_Init(&a, NULL)
           == DWIN_DRIVER_RESULT_INVALID_ARGUMENT);
    { dwin_driver_cfg_t invalid = {0};
      assert(dwin_driver_Init(&a, &invalid)
             == DWIN_DRIVER_RESULT_INVALID_ARGUMENT); }
    assert(dwin_driver_Init(&a, &cfg) == DWIN_DRIVER_RESULT_OK);
    assert(dwin_driver_Init(&b, &cfg) == DWIN_DRIVER_RESULT_OK);
    test_feed(partial, sizeof(partial));
    assert(dwin_driver_Run(&a) == DWIN_DRIVER_RESULT_OK);
    test_now_ms = 11;
    test_feed(partial, sizeof(partial));
    assert(dwin_driver_Run(&b) == DWIN_DRIVER_RESULT_OK);
    test_now_ms = 21;
    assert(dwin_driver_Run(&a) == DWIN_DRIVER_RESULT_OK);
    assert(a.tStats.wTimeout == 1U);
    assert(dwin_driver_Run(&b) == DWIN_DRIVER_RESULT_OK);
    assert(b.tStats.wTimeout == 0U);
    test_now_ms = 31;
    assert(dwin_driver_Run(&b) == DWIN_DRIVER_RESULT_OK);
    assert(b.tStats.wTimeout == 1U);
    assert(dwin_driver_Init(&a, &cfg) == DWIN_DRIVER_RESULT_OK);
    test_feed(partial, sizeof(partial));
    assert(dwin_driver_Run(&a) == DWIN_DRIVER_RESULT_OK);
    test_now_ms = 40;
    { const uint8_t byte = 0U; test_feed(&byte, 1U); }
    assert(dwin_driver_Run(&a) == DWIN_DRIVER_RESULT_OK);
    test_now_ms = 51;
    assert(dwin_driver_Run(&a) == DWIN_DRIVER_RESULT_OK);
    assert(a.tStats.wTimeout == 0U);
    test_now_ms = 60;
    assert(dwin_driver_Run(&a) == DWIN_DRIVER_RESULT_OK);
    assert(a.tStats.wTimeout == 1U);
}
#endif
#if defined(TEST_MIRCOELEC)
static void mircoelec_test(void)
{
    mircoelec_t obj = {0};
    mircoelec_cfg_t cfg = {{320000U}};
    assert(mircoelec_Run((uintptr_t)&obj) == MODUS_EFAIL);
    assert(mircoelec_Init(0U, (uintptr_t)&cfg) == MODUS_EFAIL);
    test_start_fail = 1;
    assert(mircoelec_Init((uintptr_t)&obj, (uintptr_t)&cfg) == MODUS_SUCCESS);
    assert(test_timer_running == 0U);
    assert(test_gpio[dac_oe] == 0U);
    assert(obj.eState == MIRCROELEC_STATE_READY);
    test_start_fail = 0;
    test_base_fail = 1;
    assert(mircoelec_Init((uintptr_t)&obj, (uintptr_t)&cfg) != MODUS_SUCCESS);
    assert(test_timer_running == 0U);
    assert(test_gpio[dac_oe] == 0U);
    test_base_fail = 0;
    assert(mircoelec_Init((uintptr_t)&obj, (uintptr_t)&cfg) == MODUS_SUCCESS);
    assert(obj.ptBase->wParent == (uintptr_t)&obj);
    assert(mircoelec_Run((uintptr_t)&obj) == MODUS_SUCCESS);
}
#endif
#if defined(TEST_MVC)
static void mvc_test(void)
{
    mvc_t obj = {0};
    mvc_cfg_t cfg = {{20U}};
    assert(mvc_Clock((uintptr_t)&obj) == MODUS_EFAIL);
    assert(mvc_Init(0U, (uintptr_t)&cfg) == MODUS_EFAIL);
    assert(mvc_Init((uintptr_t)&obj, (uintptr_t)&cfg) == MODUS_SUCCESS);
    assert(obj.ptBase->wParent == (uintptr_t)&obj);
    assert(mvc_Run((uintptr_t)&obj) >= 0);
    assert(mvc_Run((uintptr_t)&obj) >= 0);
    test_stream_fail = 1;
#if USERCONFIG_MSHELL_ON_SERIAL
    assert(mvc_Run((uintptr_t)&obj) >= 0);
    assert(test_read_calls == 0U);
#else
    assert(mvc_Run((uintptr_t)&obj) == MODUS_EFAIL);
    assert(obj.eState == MVC_STATE_ERROR);
#endif
}
#endif
#if defined(TEST_UI)
static void ui_test(void)
{
    dwin_driver_t dwin = {0};
    dwin_driver_cfg_t cfg = {20U};
    mvc_ui_t ui = {0};
    mvc_ui_cfg_t ui_cfg = {&dwin};
    assert(mvc_ui_Init(&ui, NULL) == -1);
    assert(dwin_driver_Init(&dwin, &cfg) == DWIN_DRIVER_RESULT_OK);
    assert(mvc_ui_Init(&ui, &ui_cfg) == 0);
    assert(mvc_ui_SetPage(&ui, 3U) == 0);
    assert(ui.hwCurrentPage == 3U);
    test_stream_fail = 1;
    assert(mvc_ui_SetPage(&ui, 4U) != 0);
    assert(ui.hwCurrentPage == 3U);
}
#endif
#if defined(TEST_LINK)
static void link_test(void)
{
    board_link_driver_t link = {0};
    const uint8_t data[] = {1U, 2U};
    assert(board_link_driver_Init(&link) == BOARD_LINK_DRIVER_RESULT_OK);
    uint32_t count = 0U;
    uint8_t received[2] = {0};
    assert(board_link_driver_Write(&link, data, sizeof(data), &count)
           == BOARD_LINK_DRIVER_RESULT_OK);
    assert(count == 2U);
    test_feed(data, sizeof(data));
    assert(board_link_driver_Read(&link, received, sizeof(received), &count)
           == BOARD_LINK_DRIVER_RESULT_OK);
    assert(count == 2U && memcmp(data, received, count) == 0);
    test_write_limit = 1U;
    assert(board_link_driver_Write(&link, data, sizeof(data), &count)
           == BOARD_LINK_DRIVER_RESULT_OK);
    assert(count == 1U && link.tStats.wTxErrors == 0U);
    test_stream_overrun = 1;
    test_feed(data, sizeof(data));
    assert(board_link_driver_Read(&link, received, sizeof(received), &count)
           == BOARD_LINK_DRIVER_RESULT_OVERRUN);
    assert(count == 2U && memcmp(data, received, count) == 0);
    assert(link.tStats.wRxErrors == 1U);
    test_stream_fail = 1;
    assert(board_link_driver_Write(&link, data, sizeof(data), &count)
           == BOARD_LINK_DRIVER_RESULT_IO);
    assert(count == 0U);
    assert(link.tStats.wTxBytes == 3U);
    assert(link.tStats.wTxErrors == 1U);
}
#endif
#if defined(TEST_ADC_SERVICE)
void mdi_Init(void);
void mdi_Service(void);
static void adc_service_test(void)
{
    const mdi_tick_t period = TEST_ADC_TICK_HZ / 100U;
    mdi_Init();
    test_raw_tick = 0U;
    mdi_Service();
    assert(test_adc_starts == 1U);
    test_raw_tick = period - 1U;
    test_adc_busy = false;
    mdi_Service();
    assert(test_adc_starts == 1U);
    test_raw_tick = period;
    test_adc_busy = true;
    mdi_Service();
    assert(test_adc_starts == 1U);
    test_adc_busy = false;
    test_raw_tick++;
    mdi_Service();
    assert(test_adc_starts == 2U);
    test_adc_busy = false;
    test_raw_tick += period - 1U;
    mdi_Service();
    assert(test_adc_starts == 2U);
    test_raw_tick++;
    mdi_Service();
    assert(test_adc_starts == 3U && test_adc_updates == 6U);
    mdi_Init();
    test_adc_busy = false;
    test_raw_tick = UINT64_MAX - period / 2U;
    mdi_Service();
    assert(test_adc_starts == 4U);
    test_adc_busy = false;
    test_raw_tick += period;
    mdi_Service();
    assert(test_adc_starts == 5U);
}
#endif
int main(void)
{
    test_reset();
#if defined(TEST_DAC)
    dac_test();
#elif defined(TEST_DWIN)
    dwin_test();
    test_reset();
    dwin_frames_test();
#elif defined(TEST_MIRCOELEC)
    mircoelec_test();
#elif defined(TEST_MVC)
    mvc_test();
#elif defined(TEST_UI)
    ui_test();
#elif defined(TEST_LINK)
    link_test();
#elif defined(TEST_ADC_SERVICE)
    adc_service_test();
#else
#error Select a module test
#endif
    puts("PASS");
    return 0;
}
