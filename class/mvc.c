/**
 * @file mvc.c
 * @brief RF448 class assembly and board control.
 * @author Modus project
 * @date 2026-10-14
 */
#include "mvc.h"
#include "mdi/instance.h"
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

/** @brief Stop RF and immediately turn off the cooling fans. */
rf448_result_t mvc_StopRf448(mvc_t *ptThis)
{
    rf448_result_t eResult = rf448_driver_Stop(&ptThis->tRf448);
    _mvc_SetFans(ptThis, false);
    return eResult;
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
    if (tMessage.eType == BOARD_PROTOCOL_READ_ADC) {
        tReply.chSequence = tMessage.chSequence;
        if ((ptThis->tRf448.eState != RF448_STATE_RUNNING)
            || (ptThis->tRf448.eRequest == RF448_REQUEST_STOP)) {
            tReply.eType = BOARD_PROTOCOL_REPLY;
            tReply.eCommand = BOARD_PROTOCOL_READ_ADC;
            tReply.eResult = BOARD_PROTOCOL_NOT_READY;
        } else {
            mdi_adc_value_t tValue = {0};
            tReply.eType = BOARD_PROTOCOL_ADC_DATA;
            if (MDI_ADC_Read(client_ivadc, &tValue) == MDI_OK) {
                tReply.ahwAdc[0] = (uint16_t)tValue.wCode;
                tReply.chAdcValid |= (1U << 0);
            }
            if (MDI_ADC_Read(client_ntc12, &tValue) == MDI_OK) {
                tReply.ahwAdc[1] = (uint16_t)tValue.wCode;
                tReply.chAdcValid |= (1U << 1);
            }
            if (MDI_ADC_Read(client_ntc11, &tValue) == MDI_OK) {
                tReply.ahwAdc[2] = (uint16_t)tValue.wCode;
                tReply.chAdcValid |= (1U << 2);
            }
            if (MDI_ADC_Read(client_rfv, &tValue) == MDI_OK) {
                tReply.ahwAdc[3] = (uint16_t)tValue.wCode;
                tReply.chAdcValid |= (1U << 3);
            }
            if (MDI_ADC_Read(client_rfi, &tValue) == MDI_OK) {
                tReply.ahwAdc[4] = (uint16_t)tValue.wCode;
                tReply.chAdcValid |= (1U << 4);
            }
            if (rf448_driver_ConvertNtcAdcToDeciCelsius(
                    tReply.ahwAdc[1], &tReply.anTemperature[0])
                && ((tReply.chAdcValid & (1U << 1)) != 0U)) {
                tReply.chAdcValid |= (1U << 5);
            }
            if (rf448_driver_ConvertNtcAdcToDeciCelsius(
                    tReply.ahwAdc[2], &tReply.anTemperature[1])
                && ((tReply.chAdcValid & (1U << 2)) != 0U)) {
                tReply.chAdcValid |= (1U << 6);
            }
        }
        if (!board_protocol_Queue(ptLink, &tReply)) {
            MLOGF(E, "Board ADC reply queue failed\r\n");
        }
        return;
    }
    if (tMessage.eType == BOARD_PROTOCOL_STOP) {
        eResult = mvc_StopRf448(ptThis);
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
                tMessage.hwEnergyPermille, RF448_MODE_OPEN_LOOP);
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
