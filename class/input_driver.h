/**
 * @file input_driver.h
 * @brief Master temperature and key input driver.
 */
#ifndef M448_INPUT_DRIVER_H
#define M448_INPUT_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INPUT_DRIVER_NTC_COUNT 4U
#define INPUT_DRIVER_KEY_COUNT 3U
#define INPUT_DRIVER_TEMPERATURE_PERIOD_MS 50U
#define INPUT_DRIVER_KEY_DEBOUNCE_MS 10U
#define INPUT_DRIVER_NTC_MIN_DECI_C (-100)
#define INPUT_DRIVER_NTC_MAX_DECI_C 800

typedef enum {
    INPUT_DRIVER_RESULT_OK = 0,
    INPUT_DRIVER_RESULT_NULL,
    INPUT_DRIVER_RESULT_INVALID_ARGUMENT,
    INPUT_DRIVER_RESULT_NOT_READY,
    INPUT_DRIVER_RESULT_NO_DATA,
    INPUT_DRIVER_RESULT_ADC_ERROR,
    INPUT_DRIVER_RESULT_NTC_INVALID,
} input_driver_result_t;

typedef enum {
    INPUT_DRIVER_NTC_CAP = 0,   /**< NTC1: CAP probe. */
    INPUT_DRIVER_NTC_RES,       /**< NTC2: RES probe. */
    INPUT_DRIVER_NTC_BIO,       /**< NTC3: BIO probe. */
    INPUT_DRIVER_NTC_COMMON,    /**< NTC4: common electrode. */
} input_driver_ntc_t;

/** A key event is a bit; multiple different keys can be reported together. */
typedef enum {
    INPUT_DRIVER_KEY_NONE = 0,
    INPUT_DRIVER_KEY_CAP = 1U << 0,
    INPUT_DRIVER_KEY_RES = 1U << 1,
    INPUT_DRIVER_KEY_BIO = 1U << 2,
} input_driver_key_t;

typedef struct {
    int16_t nDeciC;              /**< Temperature in 0.1 degrees Celsius. */
    input_driver_result_t eResult; /**< Validity of this channel's value. */
} input_driver_temperature_t;

/** Runtime data owned by the parent Class. */
typedef struct {
    input_driver_temperature_t tTemperature[INPUT_DRIVER_NTC_COUNT];
    int64_t lTemperatureDeadline; /**< 20 Hz foreground update deadline. */
    uint8_t achKeyDebounce[INPUT_DRIVER_KEY_COUNT];
    uint8_t chStableKeys;          /**< Debounced pressed-key bitmask. */
    volatile uint8_t chPendingKeys; /**< Click events published by Clock. */
    bool bInitialized;
} input_driver_t;

/** Initialize one caller-owned input driver. */
input_driver_result_t input_driver_Init(input_driver_t *ptThis);

/**
 * Foreground service: refresh NTC snapshots at 20 Hz using the board ADC.
 * Call repeatedly from the parent Class Run; the driver manages the interval.
 */
void input_driver_Run(input_driver_t *ptThis);

/**
 * Sample the three active-low keys once per 1 ms Class Clock invocation.
 * This short ISR-side entry does not perform ADC conversion or logging.
 */
void input_driver_Clock(input_driver_t *ptThis);

/** Copy the latest channel snapshot; return NO_DATA before its first update. */
input_driver_result_t input_driver_GetTemperature(
    const input_driver_t *ptThis, input_driver_ntc_t eNtc,
    input_driver_temperature_t *ptTemperature);

/**
 * Atomically take pending click bits and clear them; NONE means no click.
 * Repeated clicks on the same key before a read are coalesced.
 */
input_driver_result_t input_driver_GetKeyValue(
    input_driver_t *ptThis, uint8_t *pchKeys);

#ifdef __cplusplus
}
#endif
#endif /* M448_INPUT_DRIVER_H */
