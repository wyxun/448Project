#include "input_driver.h"
#include "mdi/instance.h"
#include <assert.h>
#include <stdio.h>

int64_t test_now_ms = 1;
unsigned test_irq_depth;
uint32_t test_input_codes[4] = {2048U, 0U, 4095U, 2048U};
mdi_status_t test_input_status[4] = {MDI_OK, MDI_OK, MDI_OK, MDI_BUSY};
uint32_t test_input_keys = 7U;

bool test_timeout(uint32_t ms, int64_t *deadline, bool reload)
{
    if (*deadline == 0) { *deadline = test_now_ms + ms; return false; }
    if (test_now_ms < *deadline) { return false; }
    if (reload) { *deadline = test_now_ms + ms; }
    return true;
}

int main(void)
{
    input_driver_t tInput;
    input_driver_temperature_t tTemp;
    uint8_t chKeys;
    unsigned i;
    assert(input_driver_Init(&tInput) == INPUT_DRIVER_RESULT_OK);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_CAP, &tTemp)
           == INPUT_DRIVER_RESULT_NO_DATA);
    input_driver_Run(&tInput);
    test_now_ms += 49;
    input_driver_Run(&tInput);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_CAP, &tTemp)
           == INPUT_DRIVER_RESULT_NO_DATA);
    test_now_ms++;
    input_driver_Run(&tInput);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_CAP, &tTemp)
           == INPUT_DRIVER_RESULT_OK && tTemp.nDeciC == 250);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_RES, &tTemp)
           == INPUT_DRIVER_RESULT_NTC_INVALID);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_BIO, &tTemp)
           == INPUT_DRIVER_RESULT_NTC_INVALID);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_COMMON, &tTemp)
           == INPUT_DRIVER_RESULT_NO_DATA);
    test_input_codes[0] = 4094U;
    test_input_status[3] = MDI_IO_ERROR;
    input_driver_Run(&tInput);
    test_now_ms += 50;
    input_driver_Run(&tInput);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_CAP, &tTemp)
           == INPUT_DRIVER_RESULT_NTC_INVALID);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_COMMON, &tTemp)
           == INPUT_DRIVER_RESULT_ADC_ERROR);
    test_input_codes[0] = 3494U;
    test_input_codes[1] = 3497U;
    test_input_codes[2] = 461U;
    test_input_codes[3] = 460U;
    test_input_status[3] = MDI_OK;
    input_driver_Run(&tInput);
    test_now_ms += 50;
    input_driver_Run(&tInput);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_CAP, &tTemp)
           == INPUT_DRIVER_RESULT_OK && tTemp.nDeciC == -100);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_RES, &tTemp)
           == INPUT_DRIVER_RESULT_NTC_INVALID);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_BIO, &tTemp)
           == INPUT_DRIVER_RESULT_OK && tTemp.nDeciC == 800);
    assert(input_driver_GetTemperature(&tInput, INPUT_DRIVER_NTC_COMMON, &tTemp)
           == INPUT_DRIVER_RESULT_NTC_INVALID);
    test_input_keys = 6U;
    for (i = 0; i < 5; ++i) { input_driver_Clock(&tInput); }
    test_input_keys = 7U;
    input_driver_Clock(&tInput);
    assert(input_driver_GetKeyValue(&tInput, &chKeys) == INPUT_DRIVER_RESULT_OK && chKeys == 0);
    test_input_keys = 6U;
    for (i = 0; i < 9; ++i) { input_driver_Clock(&tInput); }
    assert(input_driver_GetKeyValue(&tInput, &chKeys) == INPUT_DRIVER_RESULT_OK && chKeys == 0);
    input_driver_Clock(&tInput);
    assert(input_driver_GetKeyValue(&tInput, &chKeys) == INPUT_DRIVER_RESULT_OK && chKeys == INPUT_DRIVER_KEY_CAP);
    for (i = 0; i < 20; ++i) { input_driver_Clock(&tInput); }
    assert(input_driver_GetKeyValue(&tInput, &chKeys) == INPUT_DRIVER_RESULT_OK && chKeys == 0);
    test_input_keys = 7U;
    for (i = 0; i < 10; ++i) { input_driver_Clock(&tInput); }
    test_input_keys = 4U;
    for (i = 0; i < 10; ++i) { input_driver_Clock(&tInput); }
    assert(input_driver_GetKeyValue(&tInput, &chKeys) == INPUT_DRIVER_RESULT_OK
           && chKeys == (INPUT_DRIVER_KEY_CAP | INPUT_DRIVER_KEY_RES));
    puts("input driver: PASS");
    return 0;
}
