/**
 * @file mvc.h
 * @brief MODUS client application owning the RF448 driver.
 * @author Modus project
 * @date 2026-10-14
 */
#ifndef M448_CLIENT_MVC_H
#define M448_CLIENT_MVC_H
#include "modus.h"
#include "rf448_driver.h"
typedef enum {
    MVC_STATE_UNINITIALIZED = 0, MVC_STATE_IDLE,
    MVC_STATE_RUNNING, MVC_STATE_ERROR,
} mvc_state_t;
typedef struct {
    uint32_t wObjectId;
    rf448_driver_cfg_t tRf448Cfg;
} mvc_cfg_t;
typedef struct {
    modus_base_t tBase;
    modus_base_cfg_t tBaseCfg;
    rf448_driver_t tRf448;
    mvc_state_t eState;
} mvc_t;
/**
 * @brief Initialize the owned driver and register its parent MODUS object.
 * @param wObjectAddr Address of caller-owned MVC storage.
 * @param wObjectCfgAddr Address of MVC configuration.
 * @return MODUS_SUCCESS, otherwise MODUS_EFAIL with safe outputs.
 * @note Storage is zero-initialized by MODUS_DECLARE_OBJECT. Recovery keeps
 * the registered base intact and requires the same object ID.
 */
int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr);
/**
 * @brief Provide the mandatory MODUS clock callback without ISR work.
 * @param wObjectAddr Address of the MVC object.
 * @return MODUS_SUCCESS for a nonzero address, otherwise MODUS_EFAIL.
 * @note Called from SysTick; the RF448 sequence runs only in mvc_Run.
 */
int mvc_Clock(uintptr_t wObjectAddr);
/**
 * @brief Service the driver's internal sequence and collect its status.
 * @param wObjectAddr Address of the MVC object.
 * @return MODUS_SUCCESS, otherwise MODUS_EFAIL for initialization/fault errors.
 */
int mvc_Run(uintptr_t wObjectAddr);
#endif
