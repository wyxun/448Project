/**
 * @file rf448_driver.h
 * @brief Fixed RF carrier, bus control and head startup sequencing.
 * @author Modus project
 * @date 2026-10-14
 */
#ifndef M448_RF448_DRIVER_H
#define M448_RF448_DRIVER_H
#include <stdbool.h>
#include <stdint.h>
#include "perf_counter.h"

#define RF448_ENERGY_FULL_SCALE 1000U
#define RF448_SETTLE_MAX_MS 500U
#define RF448_OVERTEMP_THRESHOLD_DECI_C 860
#define RF448_FEEDBACK_PERIOD_MS 50U
#define RF448_CONTACT_PERIOD_MS 50U
#define RF448_LOW_ENERGY_PERMILLE 200U
#define RF448_CONTACT_ADC_THRESHOLD 1241U
#define RF448_CONTACT_WINDOW_SAMPLES 10U
#define RF448_CONTACT_CONFIRM_SAMPLES 6U
#define RF448_BUS_CHECK_ENABLE 1U
#define RF448_BUS_TARGET_FULL_SCALE_MV 30000U
#define RF448_BUS_TOLERANCE_MV 5000U
#define RF448_BUS_SETTLE_MS 500U
#define RF448_BUS_FAULT_MS 1000U

typedef enum {
    RF448_MODE_OPEN_LOOP, RF448_MODE_CLOSED_LOOP,
} rf448_mode_t;

/** @brief Independent detector flags; unset VALID means unknown/unavailable.
 * BUS_CHECKED is separate from BUS_VALID: conversion is not a match check.
 */
#define RF448_INPUT_CONTACT_VALID    (1UL << 0)
#define RF448_INPUT_CONTACT_PRESENT  (1UL << 1)
#define RF448_INPUT_NTC12_VALID      (1UL << 2)
#define RF448_INPUT_NTC12_OVER_TEMP  (1UL << 3)
#define RF448_INPUT_NTC11_VALID      (1UL << 4)
#define RF448_INPUT_NTC11_OVER_TEMP  (1UL << 5)
#define RF448_INPUT_BUS_VALID        (1UL << 6)
#define RF448_INPUT_BUS_CHECKED      (1UL << 7)
#define RF448_INPUT_BUS_MISMATCH     (1UL << 8)
#define RF448_INPUT_FAULT_MASK (RF448_INPUT_NTC12_OVER_TEMP \
    | RF448_INPUT_NTC11_OVER_TEMP | RF448_INPUT_BUS_MISMATCH)

typedef enum {
    RF448_HEAD_CAP = 0, RF448_HEAD_RES,
} rf448_head_t;
typedef enum {
    RF448_STATE_UNINITIALIZED = 0, RF448_STATE_IDLE,
    RF448_STATE_RELAY_WAIT, RF448_STATE_ENERGY_WAIT,
    RF448_STATE_RUNNING, RF448_STATE_ERROR,
} rf448_state_t;
typedef enum {
    RF448_REQUEST_NONE = 0, RF448_REQUEST_START, RF448_REQUEST_STOP,
    RF448_REQUEST_ENERGY,
} rf448_request_t;
typedef enum {
    RF448_RESULT_OK = 0, RF448_RESULT_INVALID, RF448_RESULT_NOT_READY,
    RF448_RESULT_BUSY, RF448_RESULT_IO, RF448_RESULT_FAULT,
} rf448_result_t;

/** @brief Startup delays in milliseconds, each in 1..500. */
typedef struct {
    uint32_t wRelaySettleMs;
    uint32_t wControlSettleMs;
} rf448_driver_cfg_t;

/** @brief Latest bus ADC sample and converted voltage. */
typedef struct {
    uint16_t hwAdcCode;
    uint32_t wMilliVolt;
    bool bValid;
} rf448_driver_bus_voltage_t;

/** @brief Foreground-only state owned by the parent MVC instance. */
typedef struct {
    rf448_state_t eState;
    rf448_result_t eLastError;
    rf448_request_t eRequest;
    rf448_head_t eTargetHead;
    uint16_t hwTargetEnergyPermille;
    uint16_t hwAppliedEnergyPermille;
    rf448_mode_t eMode;
    uint32_t wInputFlags;
    uint32_t wRelaySettleMs;
    uint32_t wControlSettleMs;
    uint8_t chRunPt;
    uint16_t hwRf0iAdc;
    bool bRf0iValid;
    uint8_t chContactSamples;
    uint8_t chContactHits;
    int64_t lBusSettleDeadline;
    int64_t lBusFaultDeadline;
    int64_t lContactDeadline;
    int64_t lFeedbackDeadline;
    int16_t nNtc12DeciC;
    int16_t nNtc11DeciC;
    int64_t lWaitDeadline;      /**< perf_counter tick deadline; 0 disarmed. */
    rf448_driver_bus_voltage_t tBusVoltage;
} rf448_driver_t;

/** @brief Target settings and current startup/output state. */
typedef struct {
    rf448_state_t eState;
    rf448_result_t eLastError;
    rf448_head_t eTargetHead;
    uint16_t hwTargetEnergyPermille;
    uint16_t hwAppliedEnergyPermille;
    rf448_mode_t eMode;
    uint32_t wInputFlags;
    bool bOutputEnabled;
} rf448_driver_status_t;

/**
 * @brief Initialize the driver with RF inhibited and zero energy output.
 * @param ptThis Caller-owned driver; exclusive owner of board RF resources.
 * @param ptCfg Valid startup delays; copied during initialization.
 * @return OK, INVALID or IO; errors leave RF inhibited.
 * @note Foreground only, with no concurrent driver access.
 */
rf448_result_t rf448_driver_Init(rf448_driver_t *ptThis,
                                const rf448_driver_cfg_t *ptCfg);
/**
 * @brief Submit startup, or an energy update on the already running head.
 * @param ptThis Initialized driver in IDLE or RUNNING.
 * @param eHead CAP or RES head.
 * @param hwEnergyPermille Requested bus control level, 0..1000.
 * @param eMode Open loop follows target; closed loop follows contact.
 * @note Running updates require the same head and mode.
 * Unknown contact keeps low energy; bus checks use the applied level.
 * @return OK when accepted, otherwise INVALID, NOT_READY, BUSY or FAULT.
 * @note Run consumes the request. A running head cannot be changed.
 */
rf448_result_t rf448_driver_Start(rf448_driver_t *ptThis,
    rf448_head_t eHead, uint16_t hwEnergyPermille, rf448_mode_t eMode);
/**
 * @brief Advance startup and consume independently refreshed input flags.
 * @param ptThis Initialized driver serviced from the foreground.
 * @return on_going while settling or waiting for startup steps, cpl when
 *         an interval completes or the driver is idle, err on failure.
 */
fsm_rt_t rf448_driver_Run(rf448_driver_t *ptThis);
/**
 * @brief Inhibit RF, zero energy and reset head relays before cancelling Run.
 * @param ptThis Initialized driver, including a faulted driver.
 * @return OK when hardware stopped; IO or INVALID otherwise.
 * @note A latched error survives Stop; Init is the recovery boundary.
 */
rf448_result_t rf448_driver_Stop(rf448_driver_t *ptThis);
/**
 * @brief Copy target settings and output state without changing the driver.
 * @param ptThis Driver to inspect.
 * @param ptStatus Destination status snapshot.
 * @return OK or INVALID for a null pointer.
 */
rf448_result_t rf448_driver_GetStatus(const rf448_driver_t *ptThis,
                                     rf448_driver_status_t *ptStatus);

/**
 * @brief Copy bus feedback, refreshed during RF startup and running.
 * @param ptThis Driver holding the latest feedback sample.
 * @param ptVoltage Destination snapshot, including validity.
 * @return OK or INVALID for a null pointer.
 */
rf448_result_t rf448_driver_GetBusVoltage(
    const rf448_driver_t *ptThis,
    rf448_driver_bus_voltage_t *ptVoltage);

/**
 * @brief Convert the 12-bit ADC code from either 10 kOhm B3950 NTC to deci-degrees Celsius.
 * @param wAdcCode ADC code using a 0..4095 full scale.
 * @param pnTemperatureDeciC Destination for temperature in 0.1 degrees Celsius.
 * @return true for a valid non-rail ADC code and non-null output; false otherwise.
 * @note Uses a precomputed table; codes 0 and 4095 are treated as short/open circuit.
 */
bool rf448_driver_ConvertNtcAdcToDeciCelsius(uint16_t wAdcCode,
                                           int16_t *pnTemperatureDeciC);
#endif
