/**
 * @file mvc.c
 * @brief RF448 class assembly and MShell control.
 * @author Modus project
 * @date 2026-10-14
 */
#include "mvc.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"
#include <stddef.h>
#include <string.h>

#define MVC_RELAY_SETTLE_MS 30U
#define MVC_CONTROL_SETTLE_MS 1U

/** @copydoc mvc_Init */
int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    const mvc_cfg_t *ptCfg = (const mvc_cfg_t *)wObjectCfgAddr;
    rf448_result_t eResult = RF448_RESULT_OK;
    int nResult = MODUS_EFAIL;
    bool bRegistered = false;
    if (ptThis == NULL) { return MODUS_EFAIL; }
    bRegistered = ptThis->tBase.wParent == wObjectAddr;
    /* A registered base contains live list links and must survive recovery. */
    if (!bRegistered) { (void)memset(ptThis, 0, sizeof(*ptThis)); }
    ptThis->eState = MVC_STATE_UNINITIALIZED;
    if ((ptCfg == NULL) || (ptCfg->wObjectId == 0U)
        || (bRegistered && (ptCfg->wObjectId != ptThis->tBase.wId))) {
        eResult = rf448_driver_Stop(&ptThis->tRf448);
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC invalid config; stop=%u\r\n", (unsigned)eResult);
        return MODUS_EFAIL;
    }
    eResult = rf448_driver_Init(&ptThis->tRf448, &ptCfg->tRf448Cfg);
    if (eResult != RF448_RESULT_OK) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
    if (!bRegistered) {
        ptThis->tBaseCfg.wId = ptCfg->wObjectId;
        ptThis->tBaseCfg.wParent = wObjectAddr;
        ptThis->tBaseCfg.FcnInterface.Clock = mvc_Clock;
        ptThis->tBaseCfg.FcnInterface.Run = mvc_Run;
        nResult = mbase_Init(&ptThis->tBase, &ptThis->tBaseCfg);
        if (nResult != MODUS_SUCCESS) {
            eResult = rf448_driver_Stop(&ptThis->tRf448);
            ptThis->eState = MVC_STATE_ERROR;
            MLOGF(E, "MVC registration failed=%d; stop=%u\r\n",
                  nResult, (unsigned)eResult);
            return MODUS_EFAIL;
        }
    }
    ptThis->eState = MVC_STATE_IDLE;
    return MODUS_SUCCESS;
}

/** @copydoc mvc_Clock */
int mvc_Clock(uintptr_t wObjectAddr)
{
    /* MODUS calls every registered Clock; RF448 has no periodic ISR work. */
    return wObjectAddr != 0U ? MODUS_SUCCESS : MODUS_EFAIL;
}

/** @copydoc mvc_Run */
int mvc_Run(uintptr_t wObjectAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    rf448_driver_status_t tStatus = {0};
    rf448_result_t eResult = RF448_RESULT_OK;
    fsm_rt_t eRun = fsm_rt_err;
    if ((ptThis == NULL) || (ptThis->eState == MVC_STATE_UNINITIALIZED)
        || (ptThis->eState == MVC_STATE_ERROR)) {
        return MODUS_EFAIL;
    }
    eRun = rf448_driver_Run(&ptThis->tRf448);
    eResult = rf448_driver_GetStatus(&ptThis->tRf448, &tStatus);
    if ((eRun == fsm_rt_err) || (eResult != RF448_RESULT_OK)) {
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC RF448 failed=%u\r\n", (unsigned)tStatus.eLastError);
        return MODUS_EFAIL;
    }
    ptThis->eState = tStatus.eState == RF448_STATE_IDLE
                  ? MVC_STATE_IDLE : MVC_STATE_RUNNING;
    return MODUS_SUCCESS;
}

MODUS_DECLARE_OBJECT(mvc, Mvc,
    .wObjectId = MVC,
    .tRf448Cfg = {
        .wRelaySettleMs = MVC_RELAY_SETTLE_MS,
        .wControlSettleMs = MVC_CONTROL_SETTLE_MS,
    }
)

#if MSHELL_ENABLE
/**
 * @brief Print the diagnostic snapshot through the selected Shell transport.
 * @return None; snapshot failures are reported through the debug logger.
 */
static void _mvc_PrintStatus(void)
{
    rf448_driver_status_t tStatus = {0};
    rf448_result_t eResult = rf448_driver_GetStatus(&tMvc.tRf448, &tStatus);
    if (eResult != RF448_RESULT_OK) {
        MLOGF(E, "RF448 status failed=%u\r\n", (unsigned)eResult);
        return;
    }
    MLOGF(I, "RF448 state=%u head=%s target=%u/1000 output=%u error=%u\r\n",
        (unsigned)tStatus.eState,
        tStatus.eTargetHead == RF448_HEAD_CAP ? "cap" : "res",
        (unsigned)tStatus.hwTargetEnergyPermille,
        (unsigned)tStatus.bOutputEnabled, (unsigned)tStatus.eLastError);
}

/**
 * @brief Parse the requested head and normalized level as one transaction.
 * @param pchArgs Arguments after the start token.
 * @return None; invalid input leaves the driver unchanged.
 */
static void _mvc_ShellStart(const char *pchArgs)
{
    rf448_head_t eHead = RF448_HEAD_CAP;
    uint16_t hwEnergy = 0U;
    rf448_result_t eResult = RF448_RESULT_INVALID;
    if (mshell_ConsumeWord(&pchArgs, "cap")) {
        eHead = RF448_HEAD_CAP;
    } else if (mshell_ConsumeWord(&pchArgs, "res")) {
        eHead = RF448_HEAD_RES;
    } else {
        mshell_Write("Usage: rf448 start cap|res <0..1000>\r\n");
        return;
    }
    if ((mshell_Parse(&pchArgs, &hwEnergy) != MSHELL_PARSE_OK)
        || !mshell_AtEnd(pchArgs) || (hwEnergy > RF448_ENERGY_FULL_SCALE)) {
        mshell_Write("Usage: rf448 start cap|res <0..1000>\r\n");
        return;
    }
    eResult = rf448_driver_Start(&tMvc.tRf448, eHead, hwEnergy);
    MLOGF(I, "RF448 start request result=%u\r\n", (unsigned)eResult);
}

/**
 * @brief Submit typed RF control operations without editing driver state.
 * @param pchArgs Complete text following the rf448 command.
 * @return None; operation failures are reported in the Shell log.
 */
static void _mvc_ShellRf448(const char *pchArgs)
{
    rf448_result_t eResult = RF448_RESULT_OK;
    if (mshell_ConsumeWord(&pchArgs, "start")) {
        _mvc_ShellStart(pchArgs);
    } else if (mshell_ConsumeWord(&pchArgs, "stop") && mshell_AtEnd(pchArgs)) {
        eResult = rf448_driver_Stop(&tMvc.tRf448);
        MLOGF(I, "RF448 stop result=%u\r\n", (unsigned)eResult);
    } else if (mshell_AtEnd(pchArgs)
        || (mshell_ConsumeWord(&pchArgs, "status") && mshell_AtEnd(pchArgs))) {
        _mvc_PrintStatus();
    } else {
        mshell_Write("Usage: rf448 start cap|res <0..1000> | stop | status\r\n");
    }
}
MODUS_SHELL_CMD(rf448, _mvc_ShellRf448,
    "RF448: start cap|res <0..1000> | stop | status");
#endif
