#ifndef MIRCOELEC_WAVEFORM_H
#define MIRCOELEC_WAVEFORM_H

#include <stdbool.h>
#include <stdint.h>
#include "modus.h"
#include "dac_driver.h"

#if MWAVEFORM_ENABLE
/** @brief Diagnostic state owned by the mircoelec Class. */
typedef struct {
    uint32_t wSampleDivider;
    volatile uint32_t wSampleCount;
    volatile bool bRunning;
    uint8_t achChannels[3];
    int16_t ahwSamples[3];
} mircoelec_waveform_t;

/**
 * @brief Configure optional ideal-AM telemetry without enabling it.
 * @param ptThis Class-owned diagnostic state.
 * @param wSampleFrequencyHz DAC update rate, an integer multiple of 10 kHz.
 * @return None; configuration failures disable telemetry and emit a warning.
 * @note Foreground only, before starting the DAC; owns the mwaveform stream.
 */
void mircoelec_waveform_Init(mircoelec_waveform_t *ptThis,
                            uint32_t wSampleFrequencyHz);

/**
 * @brief Reset the diagnostic stream and enable synchronous capture.
 * @param ptThis Initialized Class-owned diagnostic state.
 * @param wAHz Envelope frequency in Hz, used for the aliasing warning.
 * @param wBHz Carrier frequency in Hz, used for the aliasing warning.
 * @return None; telemetry failure does not affect DAC hardware control.
 * @note Foreground only, after a successful DAC start.
 */
void mircoelec_waveform_Start(mircoelec_waveform_t *ptThis,
                             uint32_t wAHz, uint32_t wBHz);

/**
 * @brief Stop producing samples while preserving already captured data.
 * @param ptThis Class-owned diagnostic state.
 * @return None.
 * @note Foreground only; interrupts are masked only to disable capture.
 */
void mircoelec_waveform_Stop(mircoelec_waveform_t *ptThis);

/**
 * @brief Capture DAC values and calculate ideal AM at a divided ISR rate.
 * @param ptThis Class-owned diagnostic state.
 * @param ptDac Read-only DAC diagnostics, after both channel updates.
 * @return None.
 * @note ISR only; non-blocking. Internal DAC state is read for telemetry only.
 */
void mircoelec_waveform_IsrStep(mircoelec_waveform_t *ptThis,
                               const dac_driver_t *ptDac);
#endif

#endif /* MIRCOELEC_WAVEFORM_H */
