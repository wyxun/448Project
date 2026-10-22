#include "mircoelec.h"
#include "mdi/instance.h"
#include "mdebug/mwaveform.h"
#include "mdebug/segger_rtt/SEGGER_RTT.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

extern mircoelec_t tMircoelec;
extern mircoelec_cfg_t tMircoelecCfg;
extern dac_driver_result_t mircoelec_StartWaveform(
    mircoelec_t *ptThis, dac_driver_wave_type_t eWaveType,
    uint32_t wAHz, uint32_t wBHz);
extern dac_driver_result_t mircoelec_StopWaveform(mircoelec_t *ptThis);

static unsigned s_samples;
static uint32_t s_last_index;
static int16_t s_last_values[3];
static float s_scales[3];
static bool s_rtt_full;

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)read_u16(data)
        | ((uint32_t)read_u16(&data[2]) << 16);
}

int64_t get_system_ms(void)
{
    return test_now_ms;
}

int SEGGER_RTT_ConfigUpBuffer(unsigned index, const char *name,
    void *buffer, unsigned size, unsigned flags)
{
    assert(index == 1U && buffer != NULL && size >= 1024U);
    assert(strcmp(name, "Waveform") == 0);
    assert(flags == SEGGER_RTT_MODE_NO_BLOCK_SKIP);
    return 0;
}

unsigned SEGGER_RTT_Write(unsigned index, const void *buffer, unsigned size)
{
    const uint8_t *data = buffer;
    assert(index == 1U && test_irq_depth == 0U);
    if (s_rtt_full) { return 0U; }
    if (size == 41U && data[2] == MWAVEFORM_FRAME_TYPE_DESC) {
        assert(data[3] == 3U);
        for (unsigned channel = 0U; channel < 3U; channel++) {
            memcpy(&s_scales[channel], &data[12U + 12U * channel],
                   sizeof(s_scales[channel]));
        }
        assert(s_scales[0] == 255.0f && s_scales[1] == 255.0f);
        assert(s_scales[2] == 32512.5f);
    }
    if (size > 17U && data[2] == MWAVEFORM_FRAME_TYPE_BATCH) {
        assert(data[4] == 3U && read_u32(&data[11]) == 1000000U);
        const uint16_t count = read_u16(&data[9]);
        assert(size == 17U + 7U * count);
        for (uint16_t i = 0U; i < count; i++) {
            const uint8_t *sample = &data[15U + 7U * i];
            assert(sample[0] == 7U);
            for (unsigned channel = 0U; channel < 3U; channel++) {
                s_last_values[channel] =
                    (int16_t)read_u16(&sample[1U + 2U * channel]);
            }
            s_last_index = read_u32(&data[5]) + i;
            s_samples++;
        }
    }
    return size;
}

static void flush(void)
{
    test_now_ms += 11;
    mwaveform.Poll();
}

static void step(unsigned count)
{
    for (unsigned i = 0U; i < count; i++) {
        mircoelec_IsrStep();
    }
}

static void synchronous_stream_test(void)
{
    test_reset();
    assert(mircoelec_Init((uintptr_t)&tMircoelec,
        (uintptr_t)&tMircoelecCfg) == MODUS_SUCCESS);
    step(64U);
    flush();
    assert(s_samples == 0U);
    assert(mircoelec_StartWaveform(&tMircoelec, DAC_DRIVER_WAVE_SINE,
        50U, 500U) == DAC_DRIVER_RESULT_OK);
    step(199U);
    flush();
    assert(s_samples == 0U);
    step(1U);
    flush();
    assert(s_samples == 1U && s_last_index == 0U);
    assert(s_last_values[0] > 80 && s_last_values[0] < 97);
    assert(s_last_values[1] > -10 && s_last_values[1] < 10);

    for (unsigned n = 1U; n < 400U; n++) {
        step(200U);
        flush();
        assert(s_samples == n + 1U && s_last_index == n);
        const double time = (double)(n + 1U) / 1000.0;
        const double a = 0.5 - 0.5 * sin(2.0 * 3.141592653589793 * 50.0 * time);
        const double b = sin(2.0 * 3.141592653589793 * 500.0 * time);
        assert(fabs((double)s_last_values[0] / s_scales[0] - a) < 0.02);
        assert(fabs((double)s_last_values[1] / s_scales[1] - b) < 0.04);
        assert(fabs((double)s_last_values[2] / s_scales[2] - a * b) < 0.04);
    }
    assert(mircoelec_StartWaveform(&tMircoelec, DAC_DRIVER_WAVE_SINE,
        20001U, 500U) == DAC_DRIVER_RESULT_INVALID_ARGUMENT);
    step(200U);
    flush();
    assert(s_samples == 401U && s_last_index == 400U);

    /* Leave one sample unsent, then restart: it must be discarded. */
    step(200U);
    assert(mircoelec_StartWaveform(&tMircoelec, DAC_DRIVER_WAVE_SINE,
        100U, 1000U) == DAC_DRIVER_RESULT_OK);
    flush();
    assert(s_samples == 401U);
    step(200U);
    flush();
    assert(s_samples == 402U && s_last_index == 0U);
    assert(mircoelec_StopWaveform(&tMircoelec) == DAC_DRIVER_RESULT_OK);
    step(400U);
    flush();
    assert(s_samples == 402U);

    test_start_fail = 1;
    assert(mircoelec_StartWaveform(&tMircoelec, DAC_DRIVER_WAVE_SINE,
        50U, 500U) == DAC_DRIVER_RESULT_IO);
    step(64U);
    flush();
    assert(s_samples == 402U);
    test_start_fail = 0;
}

static void endpoints_and_backpressure_test(void)
{
    mircoelec_waveform_t wave = {0};
    dac_driver_t dac = { .eState = DAC_DRIVER_STATE_RUNNING };
    mircoelec_waveform_Init(&wave, 200000U);
    mircoelec_waveform_Start(&wave, 0U, 0U);
    for (unsigned a = 0U; a <= 255U; a += 255U) {
        for (unsigned b = 0U; b <= 255U; b += 255U) {
            dac.tEnvelope.chLastValue = (uint8_t)a;
            dac.tCarrier.chLastValue = (uint8_t)b;
            for (unsigned i = 0U; i < 200U; i++) {
                mircoelec_waveform_IsrStep(&wave, &dac);
            }
            flush();
            assert(s_last_values[0] == (a == 0U ? 0 : 255));
            assert(s_last_values[1] == (b == 0U ? -255 : 255));
            assert(s_last_values[2]
                == (a == 0U ? 0 : (b == 0U ? -32512 : 32512)));
            const double ideal = ((double)a / 255.0)
                * (2.0 * (double)b / 255.0 - 1.0);
            assert(fabs((double)s_last_values[2] / s_scales[2] - ideal)
                <= 1.0 / 65025.0 + 1e-12);
        }
    }
    /* Negative odd products must truncate toward zero, not round down. */
    dac.tEnvelope.chLastValue = 1U;
    dac.tCarrier.chLastValue = 127U;
    for (unsigned i = 0U; i < 200U; i++) {
        mircoelec_waveform_IsrStep(&wave, &dac);
    }
    flush();
    assert(s_last_values[0] == 1 && s_last_values[1] == -1);
    assert(s_last_values[2] == 0);
    s_rtt_full = true;
    for (unsigned i = 0U;
         i < 200U * (MWAVEFORM_BATCH_DEPTH + 5U); i++) {
        mircoelec_waveform_IsrStep(&wave, &dac);
    }
    flush();
    assert(mwaveform.GetDropCount() == 5U);
    assert(mwaveform.GetRTTFullCount() > 0U);
    s_rtt_full = false;
    flush();
    mircoelec_waveform_Stop(&wave);
    mircoelec_waveform_Init(&wave, 0U);
    mircoelec_waveform_Start(&wave, 50U, 500U);
    const unsigned before = s_samples;
    for (unsigned i = 0U; i < 640U; i++) {
        mircoelec_waveform_IsrStep(&wave, &dac);
    }
    flush();
    assert(s_samples == before && test_irq_depth == 0U);
}

static void digital_square_waveform_test(void)
{
    test_reset();
    s_samples = 0U;
    assert(mircoelec_Init((uintptr_t)&tMircoelec,
        (uintptr_t)&tMircoelecCfg) == MODUS_SUCCESS);
    assert(mircoelec_StartWaveform(&tMircoelec, DAC_DRIVER_WAVE_DIGITAL,
        50U, 500U) == DAC_DRIVER_RESULT_OK);
    step(200U);
    flush();
    assert(s_samples == 1U);

    unsigned high_count = 0U;
    unsigned low_count = 0U;

    for (unsigned n = 1U; n < 200U; n++) {
        step(200U);
        flush();
        assert(s_samples == n + 1U);
        /* In digital mode, EnvA (channel 0) must be strictly 0 or 255 */
        assert((s_last_values[0] == 0) || (s_last_values[0] == 255));
        if (s_last_values[0] == 255) {
            high_count++;
        } else {
            low_count++;
        }
    }

    /* 50 Hz wave over 199 ms should have roughly equal 50% duty split */
    assert(high_count > 90U && high_count < 110U);
    assert(low_count > 90U && low_count < 110U);

    assert(mircoelec_StopWaveform(&tMircoelec) == DAC_DRIVER_RESULT_OK);
}

int main(void)
{
    synchronous_stream_test();
    digital_square_waveform_test();
    endpoints_and_backpressure_test();
    puts("DAC waveform: synchronization, AM, dig square, stop passed");
    return 0;
}
