/**
 * @file mvc.c
 * @brief RF448 class assembly and MShell control.
 * @author Modus project
 * @date 2026-10-14
 */
#include "mvc.h"
#include "mdi/instance.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"
#include <stddef.h>
#include <string.h>

#if USERCONFIG_BOARD_LINK_ENABLE && !USERCONFIG_MSHELL_ON_SERIAL
/** @brief Transfer bounded UART batches and retain short TX tails. */
static void _mvc_PollBoard(mvc_t *ptThis)
{
    board_protocol_t *ptLink = &ptThis->tBoardProtocol;
    uint8_t achData[32];
    uint32_t wCount = 0U;
    uint16_t hwFree = mringbuf_GetFree(&ptLink->tRxRing);
    uint32_t wLength = hwFree < sizeof(achData) ? hwFree : sizeof(achData);
    mdi_status_t eStatus = MDI_OK;
    if (wLength != 0U) {
        eStatus = MDI_STREAM_Read(serial_stream, achData, wLength, &wCount);
        if ((eStatus != MDI_OK) && (eStatus != MDI_OVERRUN)) {
            MLOGF(W, "Board RX error=%d\r\n", (int)eStatus);
        }
        if (board_protocol_Receive(ptLink, achData, (uint16_t)wCount)
            != wCount) {
            MLOGF(E, "Board RX buffer full\r\n");
        }
    }
    if (ptLink->chTxOffset < ptLink->chTxLength) {
        wCount = 0U;
        eStatus = MDI_STREAM_Write(serial_stream,
            &ptLink->achTx[ptLink->chTxOffset],
            ptLink->chTxLength - ptLink->chTxOffset, &wCount);
        ptLink->chTxOffset += (uint8_t)wCount;
        if (eStatus != MDI_OK) {
            MLOGF(W, "Board TX error=%d\r\n", (int)eStatus);
        }
    }
}

#endif

#define MVC_RELAY_SETTLE_MS 30U
#define MVC_CONTROL_SETTLE_MS 1U

/**
 * @brief Enable or disable both cooling fans when the state changes.
 * @param ptThis MVC instance holding the last commanded fan state.
 * @param bEnable True enables FAN2 and FAN3; false disables both.
 * @return None.
 */
static void _mvc_SetFans(mvc_t *ptThis, bool bEnable)
{
    if (ptThis->bFansEnabled != bEnable) {
        MDI_IO_Write(pt_fan, bEnable ? 0x03U : 0U);
        ptThis->bFansEnabled = bEnable;
    }
}

#if USERCONFIG_BOARD_LINK_ENABLE && !USERCONFIG_MSHELL_ON_SERIAL
/** @brief Consume one command only when its reply can be retained. */
static void _mvc_ServiceBoard(mvc_t *ptThis)
{
    board_protocol_message_t tMessage = {0};
    board_protocol_message_t tReply = {0};
    rf448_driver_status_t tStatus = {0};
    rf448_result_t eResult = RF448_RESULT_INVALID;
    board_protocol_t *ptLink = &ptThis->tBoardProtocol;
    _mvc_PollBoard(ptThis);
    if (ptLink->chTxOffset < ptLink->chTxLength) {
        return;
    }
    if (ptThis->bFaultPending) {
        tReply.eType = BOARD_PROTOCOL_FAULT;
        tReply.hwFaultMask = 2U;
        if (board_protocol_Queue(ptLink, &tReply)) {
            ptThis->bFaultPending = false;
        }
        return;
    }
    if (!board_protocol_Read(ptLink, &tMessage)) {
        return;
    }
    if (tMessage.eType == BOARD_PROTOCOL_STOP) {
        eResult = rf448_driver_Stop(&ptThis->tRf448);
        _mvc_SetFans(ptThis, false);
    } else if ((tMessage.eType == BOARD_PROTOCOL_START)
        || (tMessage.eType == BOARD_PROTOCOL_ENERGY)) {
        eResult = rf448_driver_GetStatus(&ptThis->tRf448, &tStatus);
        if ((eResult == RF448_RESULT_OK)
            && (tMessage.eType == BOARD_PROTOCOL_ENERGY)
            && ((tStatus.eState != RF448_STATE_RUNNING)
                || ((unsigned)tStatus.eTargetHead
                    != (unsigned)tMessage.eHead))) {
            eResult = RF448_RESULT_NOT_READY;
        }
        if (eResult == RF448_RESULT_OK) {
            eResult = rf448_driver_Start(&ptThis->tRf448,
                tMessage.eHead == BOARD_PROTOCOL_HEAD_CAP
                    ? RF448_HEAD_CAP : RF448_HEAD_RES,
                tMessage.hwEnergyPermille);
        }
    } else {
        return; /* Replies and faults have no meaning on the command endpoint. */
    }
    tReply.eType = BOARD_PROTOCOL_REPLY;
    tReply.chSequence = tMessage.chSequence;
    tReply.eCommand = tMessage.eType;
    /* Driver and wire result values are deliberately mapped explicitly. */
    switch (eResult) {
    case RF448_RESULT_OK: tReply.eResult = BOARD_PROTOCOL_ACCEPTED; break;
    case RF448_RESULT_INVALID: tReply.eResult = BOARD_PROTOCOL_INVALID; break;
    case RF448_RESULT_NOT_READY:
        tReply.eResult = BOARD_PROTOCOL_NOT_READY; break;
    case RF448_RESULT_BUSY: tReply.eResult = BOARD_PROTOCOL_BUSY; break;
    case RF448_RESULT_IO: tReply.eResult = BOARD_PROTOCOL_IO; break;
    default: tReply.eResult = BOARD_PROTOCOL_DRIVER_FAULT; break;
    }
    if (!board_protocol_Queue(ptLink, &tReply)) {
        MLOGF(E, "Board reply queue failed\r\n");
    }
}
#endif

/** @copydoc mvc_Init */
int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    const mvc_cfg_t *ptCfg = (const mvc_cfg_t *)wObjectCfgAddr;
    rf448_result_t eResult = RF448_RESULT_OK;
    int nResult = MODUS_EFAIL;
    if (ptThis == NULL) { return MODUS_EFAIL; }
    MDI_IO_Write(pt_fan, 0U);
    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = MVC_STATE_UNINITIALIZED;
#if USERCONFIG_BOARD_LINK_ENABLE && !USERCONFIG_MSHELL_ON_SERIAL
    if (!board_protocol_Init(&ptThis->tBoardProtocol)) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
#endif
    if ((ptCfg == NULL) || (ptCfg->wObjectId == 0U)) {
        eResult = rf448_driver_Stop(&ptThis->tRf448);
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC invalid config; stop=%u\r\n", (unsigned)eResult);
        return MODUS_EFAIL;
    }
    eResult = rf448_driver_Init(&ptThis->tRf448, &ptCfg->tRf448Cfg);
    if (eResult != RF448_RESULT_OK) {
        (void)rf448_driver_Stop(&ptThis->tRf448);
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
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
    if (ptThis == NULL) { return MODUS_EFAIL; }
#if USERCONFIG_BOARD_LINK_ENABLE && !USERCONFIG_MSHELL_ON_SERIAL
    if (ptThis->eState == MVC_STATE_ERROR) {
        _mvc_ServiceBoard(ptThis);
    }
#endif
    if ((ptThis->eState == MVC_STATE_UNINITIALIZED)
        || (ptThis->eState == MVC_STATE_ERROR)) {
        return MODUS_EFAIL;
    }
#if USERCONFIG_BOARD_LINK_ENABLE && !USERCONFIG_MSHELL_ON_SERIAL
    _mvc_ServiceBoard(ptThis);
#endif
    eRun = rf448_driver_Run(&ptThis->tRf448);
    eResult = rf448_driver_GetStatus(&ptThis->tRf448, &tStatus);
    if ((eRun == fsm_rt_err) || (eResult != RF448_RESULT_OK)) {
        _mvc_SetFans(ptThis, false);
        ptThis->bFaultPending = true;
        ptThis->eState = MVC_STATE_ERROR;
        MLOGF(E, "MVC RF448 failed=%u\r\n", (unsigned)tStatus.eLastError);
        return MODUS_EFAIL;
    }
    _mvc_SetFans(ptThis, tStatus.bOutputEnabled);
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
        _mvc_SetFans(&tMvc, false);
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
