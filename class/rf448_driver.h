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
    uint32_t wRelaySettleMs;
    uint32_t wControlSettleMs;
    uint8_t chRunPt;
    int64_t lWaitDeadline;      /**< perf_counter tick deadline; 0 disarmed. */
    rf448_driver_bus_voltage_t tBusVoltage;
} rf448_driver_t;

/** @brief Target settings and current startup/output state. */
typedef struct {
    rf448_state_t eState;
    rf448_result_t eLastError;
    rf448_head_t eTargetHead;
    uint16_t hwTargetEnergyPermille;
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
 * @return OK when accepted, otherwise INVALID, NOT_READY, BUSY or FAULT.
 * @note Run consumes the request. A running head cannot be changed.
 */
rf448_result_t rf448_driver_Start(rf448_driver_t *ptThis,
    rf448_head_t eHead, uint16_t hwEnergyPermille);
/**
 * @brief Advance startup and refresh NTC and bus feedback while running.
 * @param ptThis Initialized driver serviced from the foreground.
 * @return on_going while settling or waiting between 20 Hz checks, cpl when
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
 * @brief Copy the latest bus voltage sample, refreshed while RF is running.
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
