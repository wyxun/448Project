/**
 * @file heater_control.h
 * @brief Master RTIN1/RTIN2 heater control driver.
 */
#ifndef M448_HEATER_CONTROL_H
#define M448_HEATER_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "input_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEATER_CONTROL_CHANNEL_COUNT 2U
#ifndef HEATER_CONTROL_PWM_PERIOD_MS
#define HEATER_CONTROL_PWM_PERIOD_MS 3000U
#endif
#ifndef HEATER_CONTROL_UPDATE_PERIOD_MS
#define HEATER_CONTROL_UPDATE_PERIOD_MS 50U
#endif
#define HEATER_CONTROL_DUTY_FULL_PERMILLE 1000U
#ifndef HEATER_CONTROL_BIO_TARGET_DECI_C
#define HEATER_CONTROL_BIO_TARGET_DECI_C 420
#endif
#ifndef HEATER_CONTROL_COMMON_TARGET_DECI_C
#define HEATER_CONTROL_COMMON_TARGET_DECI_C 420
#endif
#ifndef HEATER_CONTROL_PROPORTIONAL_BAND_DECI_C
#define HEATER_CONTROL_PROPORTIONAL_BAND_DECI_C 20
#endif
#ifndef HEATER_CONTROL_OVERTEMP_DECI_C
#define HEATER_CONTROL_OVERTEMP_DECI_C 480
#endif

typedef enum {
    HEATER_CONTROL_CHANNEL_BIO = 0,    /**< RTIN1, paired with NTC3. */
    HEATER_CONTROL_CHANNEL_COMMON,     /**< RTIN2, paired with NTC4. */
} heater_control_channel_t;

#define HEATER_CONTROL_OVERTEMP_BIO_MASK    (1U << HEATER_CONTROL_CHANNEL_BIO)
#define HEATER_CONTROL_OVERTEMP_COMMON_MASK \
    (1U << HEATER_CONTROL_CHANNEL_COMMON)

typedef enum {
    HEATER_CONTROL_RESULT_OK = 0,
    HEATER_CONTROL_RESULT_NULL,
} heater_control_result_t;

/** Runtime state owned by the parent Class. */
typedef struct {
    const input_driver_temperature_t *ptTemperature;
    uint16_t ahwDutyPermille[HEATER_CONTROL_CHANNEL_COUNT];
    uint16_t hwPwmElapsedMs;
    int64_t lControlDeadline;
    uint8_t chOverTempMask;
    bool bEnabled;
} heater_control_t;

/** Initialize with consecutive NTC3/NTC4 snapshots; leave heaters off. */
heater_control_result_t heater_control_Init(
    heater_control_t *ptThis,
    const input_driver_temperature_t *ptTemperature);

/** Enable temperature control; PWM starts after the next temperature update. */
void heater_control_Start(heater_control_t *ptThis);

/** Disable temperature control and immediately turn both outputs off. */
void heater_control_Stop(heater_control_t *ptThis);

/**
 * Update heater duty targets and over-temperature state from snapshots.
 * Temperature monitoring continues while control is disabled.
 */
void heater_control_Run(heater_control_t *ptThis);

/** Advance the 1 ms, 3-second-window heater PWM service. */
void heater_control_Clock(heater_control_t *ptThis);

#ifdef __cplusplus
}
#endif

#endif /* M448_HEATER_CONTROL_H */
