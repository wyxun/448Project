/**
 * @file mvc.c
 * @brief DWIN and client-control Class skeleton.
 * @author Modus project
 * @date 2026-10-08
 */

#include "mvc.h"
#include "mdi/instance.h"
#include "mdebug/util_debug.h"
#include "perfc_task_pt.h"

#include <stddef.h>
#include <string.h>

#define MVC_BOARD_REPLY_TIMEOUT_MS 1000U

#if USERCONFIG_BOARD_LINK_ENABLE
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
        eStatus = MDI_STREAM_Read(board_link_stream,
            achData, wLength, &wCount);
        if ((eStatus != MDI_OK)
            && (eStatus != MDI_OVERRUN)) {
            MLOGF(W, "Board RX error=%d\r\n", (int)eStatus);
        }
        if (board_protocol_Receive(ptLink, achData, (uint16_t)wCount)
            != wCount) {
            MLOGF(E, "Board RX buffer full\r\n");
        }
    }
    if (ptLink->chTxOffset < ptLink->chTxLength) {
        wCount = 0U;
        eStatus = MDI_STREAM_Write(board_link_stream,
            &ptLink->achTx[ptLink->chTxOffset],
            ptLink->chTxLength - ptLink->chTxOffset, &wCount);
        ptLink->chTxOffset += (uint8_t)wCount;
        if (eStatus != MDI_OK) {
            MLOGF(W, "Board TX error=%d\r\n", (int)eStatus);
        }
    }
}

#endif

static modus_base_t s_tMvcBase;
static const mvc_probe_t s_atKeyProbe[INPUT_DRIVER_KEY_COUNT] = {
    MVC_PROBE_CAP, MVC_PROBE_RES, MVC_PROBE_BIO,
};

/** Submit a BIO request using the parameters already stored in MVC. */
bool mvc_ProbeCommand(mvc_t *ptThis, mvc_probe_t eProbe,
                      probe_command_type_t eCommand)
{
    const mircoelec_param_t *ptBio = &ptThis->tProbeParam.tBio;

#if !USERCONFIG_BOARD_LINK_ENABLE
    if ((eProbe == MVC_PROBE_CAP) || (eProbe == MVC_PROBE_RES)) {
        return false;
    }
#endif

    if ((ptThis->eState != MVC_STATE_READY)
        || (eProbe < MVC_PROBE_CAP) || (eProbe > MVC_PROBE_BIO)) {
        return false;
    }
    if ((eProbe == MVC_PROBE_CAP) || (eProbe == MVC_PROBE_RES)) {
        uint16_t hwEnergy = eProbe == MVC_PROBE_CAP
            ? ptThis->tProbeParam.tCap.hwEnergyPermille
            : ptThis->tProbeParam.tRes.hwEnergyPermille;
        if (eCommand == MVC_PROBE_COMMAND_ENERGY) {
            if ((ptThis->eActiveProbe != eProbe)
                || board_protocol_IsWaiting(&ptThis->tBoardProtocol)
                || (ptThis->tProbeCommand.eCommand != MVC_PROBE_COMMAND_NONE)
                || (hwEnergy > 1000U)) {
                return false;
            }
            ptThis->tProbeCommand.eCommand = eCommand;
            return true;
        }
        if ((eCommand == MVC_PROBE_COMMAND_START) && (hwEnergy > 1000U)) {
            return false;
        }
    }
    if (eCommand == MVC_PROBE_COMMAND_STOP) {
        if ((ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_START)
            && (ptThis->tProbeParam.eProbe == eProbe)
            && (ptThis->eActiveProbe == MVC_PROBE_NONE)) {
            ptThis->tProbeCommand.eCommand = MVC_PROBE_COMMAND_NONE;
            return true;
        }
        if (ptThis->eActiveProbe != eProbe) {
            return false;
        }
        ptThis->tProbeCommand.eCommand = MVC_PROBE_COMMAND_STOP;
        return true;
    }
    if ((eCommand != MVC_PROBE_COMMAND_START)
        || (ptThis->eActiveProbe != MVC_PROBE_NONE)
        || (ptThis->tProbeCommand.eCommand != MVC_PROBE_COMMAND_NONE)
        || ((eProbe == MVC_PROBE_BIO)
            && (((unsigned)ptBio->eWaveType > DAC_DRIVER_WAVE_DIGITAL)
        || (ptBio->wAHz == 0U) || (ptBio->wAHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
        || (ptBio->wBHz == 0U) || (ptBio->wBHz > DAC_DRIVER_MAX_FREQUENCY_HZ)
        || (ptBio->chAmplitudePercent
                > DAC_DRIVER_AMPLITUDE_FULL_SCALE_PERCENT)))) {
        return false;
    }
    ptThis->tProbeParam.eProbe = eProbe;
    ptThis->tProbeCommand.eCommand = eCommand;
    return true;
}

#if USERCONFIG_BOARD_LINK_ENABLE
/** @brief Match client replies and faults to the single outstanding request. */
static void _mvc_ServiceBoard(mvc_t *ptThis)
{
    board_protocol_message_t tMessage = {0};
    _mvc_PollBoard(ptThis);
    while (board_protocol_Read(&ptThis->tBoardProtocol, &tMessage)) {
        if (tMessage.eType == BOARD_PROTOCOL_FAULT) {
            if (ptThis->eActiveProbe != MVC_PROBE_BIO) {
                ptThis->eActiveProbe = MVC_PROBE_NONE;
            }
            board_protocol_CancelRequest(&ptThis->tBoardProtocol);
            MLOGF(E, "Client fault mask=%u\r\n",
                  (unsigned)tMessage.hwFaultMask);
        } else if (board_protocol_AcceptReply(&ptThis->tBoardProtocol,
                                            &tMessage)) {
            if (((tMessage.eResult == BOARD_PROTOCOL_ACCEPTED)
                 && (tMessage.eCommand == BOARD_PROTOCOL_STOP))
                || ((tMessage.eResult != BOARD_PROTOCOL_ACCEPTED)
                    && (tMessage.eCommand == BOARD_PROTOCOL_START))) {
                ptThis->eActiveProbe = MVC_PROBE_NONE;
            }
            if (tMessage.eType == BOARD_PROTOCOL_ADC_DATA) {
                uint32_t wBus = ((uint32_t)tMessage.ahwAdc[0] * 62700U + 2047U) / 4095U;
                uint32_t wRfi = ((uint32_t)tMessage.ahwAdc[4] * 3300U + 2047U) / 4095U;
                MLOGF(I, "Client ADC IVADC=%u NTC12=%u NTC11=%u RF0V=%u RF0I=%u valid=0x%02x\r\n",
                    (unsigned)tMessage.ahwAdc[0], (unsigned)tMessage.ahwAdc[1],
                    (unsigned)tMessage.ahwAdc[2], (unsigned)tMessage.ahwAdc[3],
                    (unsigned)tMessage.ahwAdc[4], (unsigned)tMessage.chAdcValid);
                MLOGF(I, "Client bus=%lu mV RF0I=%lu mV NTC12=%d NTC11=%d (0.1C)\r\n",
                    (unsigned long)wBus, (unsigned long)wRfi,
                    (int)tMessage.anTemperature[0], (int)tMessage.anTemperature[1]);
                continue;
            }
            MLOGF(I, "Client reply cmd=%u result=%u\r\n",
                (unsigned)tMessage.eCommand, (unsigned)tMessage.eResult);
        } else {
            /* Ignore unmatched sequences and frames in the wrong direction. */
        }
    }
    if (board_protocol_IsWaiting(&ptThis->tBoardProtocol)
        && perfc_is_time_out_ms(MVC_BOARD_REPLY_TIMEOUT_MS,
                               &ptThis->lBoardReplyDeadline, false)) {
        board_protocol_CancelRequest(&ptThis->tBoardProtocol);
        /* Output is uncertain: retain the reservation until STOP is accepted. */
        MLOGF(E, "Client reply timeout; output unconfirmed\r\n");
    }
}

#endif

/** @brief Queue an RF request and reserve the selected probe until confirmed. */
static bool _mvc_DispatchRf(mvc_t *ptThis)
{
    board_protocol_message_t tMessage = {0};
    tMessage.eType = ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_STOP
        ? BOARD_PROTOCOL_STOP
        : (ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_ENERGY
            ? BOARD_PROTOCOL_ENERGY : BOARD_PROTOCOL_START);
    tMessage.eHead = (tMessage.eType == BOARD_PROTOCOL_START
        ? ptThis->tProbeParam.eProbe : ptThis->eActiveProbe) == MVC_PROBE_CAP
        ? BOARD_PROTOCOL_HEAD_CAP : BOARD_PROTOCOL_HEAD_RES;
    tMessage.hwEnergyPermille = tMessage.eHead == BOARD_PROTOCOL_HEAD_CAP
        ? ptThis->tProbeParam.tCap.hwEnergyPermille
        : ptThis->tProbeParam.tRes.hwEnergyPermille;
    if (!board_protocol_Request(&ptThis->tBoardProtocol, &tMessage)) {
        return false;
    }
    ptThis->lBoardReplyDeadline = 0;
    if (tMessage.eType == BOARD_PROTOCOL_START) {
        ptThis->eActiveProbe = ptThis->tProbeParam.eProbe;
    }
    return true;
}

static void _mvc_DispatchProbeCommand(mvc_t *ptThis)
{
    mircoelec_command_t tCommand = {0};

    if (ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_NONE) {
        return;
    }

    if ((ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_START
        ? ptThis->tProbeParam.eProbe : ptThis->eActiveProbe) != MVC_PROBE_BIO) {
        if (_mvc_DispatchRf(ptThis)) {
            ptThis->tProbeCommand.eCommand = MVC_PROBE_COMMAND_NONE;
        }
        return;
    }
    if (ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_START) {
        if ((ptThis->eActiveProbe == MVC_PROBE_NONE)
            && (ptThis->tProbeParam.eProbe == MVC_PROBE_BIO)) {
            tCommand.eCommand = MIRCROELEC_COMMAND_START;
            tCommand.ptWave = &ptThis->tProbeParam.tBio;
            (void)mbase_MessagePostToRing(MIRCROELEC, (uint8_t *)&tCommand,
                                          (uint16_t)sizeof(tCommand));
            ptThis->eActiveProbe = MVC_PROBE_BIO;
        }
    } else if (ptThis->tProbeCommand.eCommand == MVC_PROBE_COMMAND_STOP) {
        if (ptThis->eActiveProbe == MVC_PROBE_BIO) {
            tCommand.eCommand = MIRCROELEC_COMMAND_STOP;
            (void)mbase_MessagePostToRing(MIRCROELEC, (uint8_t *)&tCommand,
                                          (uint16_t)sizeof(tCommand));
            ptThis->eActiveProbe = MVC_PROBE_NONE;
        }
    } else {
        /* Unknown commands are discarded. */
    }
    ptThis->tProbeCommand.eCommand = MVC_PROBE_COMMAND_NONE;
}
static modus_base_cfg_t s_tMvcBaseCfg = {
    .wId = MVC,
    .wParent = 0U,
    .FcnInterface = {
        .Clock = mvc_Clock,
        .Run = mvc_Run,
    },
};

/**
 * @brief Initialize UI and transport dependencies before registration.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @param wObjectCfgAddr Address of the Class initialization configuration.
 * @return MODUS_SUCCESS or PT progress; MODUS_EFAIL on failure.
 */
int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    const mvc_cfg_t *ptCfg = (const mvc_cfg_t *)wObjectCfgAddr;
    mvc_ui_cfg_t tUiCfg = {0};
    int nResult = MODUS_EFAIL;

    if ((ptThis == NULL) || (ptCfg == NULL)) {
        return MODUS_EFAIL;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->ptBase = &s_tMvcBase;
    ptThis->eState = MVC_STATE_UNINITIALIZED;
#if USERCONFIG_BOARD_LINK_ENABLE
    if (!board_protocol_Init(&ptThis->tBoardProtocol)) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
#endif

    if (input_driver_Init(&ptThis->tInput) != INPUT_DRIVER_RESULT_OK) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }

    ptThis->eDwinError = dwin_driver_Init(&ptThis->tDwin, &ptCfg->tDwinCfg);
    if (ptThis->eDwinError != DWIN_DRIVER_RESULT_OK) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }

    tUiCfg.ptDwin = &ptThis->tDwin;
    if (mvc_ui_Init(&ptThis->tUi, &tUiCfg) != 0) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }

    s_tMvcBaseCfg.wParent = wObjectAddr;
    nResult = mbase_Init(ptThis->ptBase, &s_tMvcBaseCfg);
    if (nResult != MODUS_SUCCESS) {
        ptThis->eState = MVC_STATE_ERROR;
        return nResult;
    }

    ptThis->eState = MVC_STATE_READY;
    return MODUS_SUCCESS;
}

/**
 * @brief Service the DWIN foreground flow once per PT iteration.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @return MODUS_SUCCESS or PT progress; MODUS_EFAIL on failure.
 */
int mvc_Run(uintptr_t wObjectAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    uint8_t chKeys = 0U;

    if ((ptThis == NULL) || (ptThis->eState != MVC_STATE_READY)) {
        return MODUS_EFAIL;
    }

    PERFC_PT_BEGIN(ptThis->chRunPt)
    while (1) {
        input_driver_Run(&ptThis->tInput);
        if (input_driver_GetKeyValue(&ptThis->tInput, &chKeys)
            == INPUT_DRIVER_RESULT_OK) {
            for (uint8_t i = 0U; i < INPUT_DRIVER_KEY_COUNT; ++i) {
                if ((chKeys & (1U << i)) != 0U) {
                    mvc_probe_t eProbe = s_atKeyProbe[i];
                    probe_command_type_t eCommand =
                        ((ptThis->eActiveProbe == eProbe)
                         || ((ptThis->tProbeCommand.eCommand
                              == MVC_PROBE_COMMAND_START)
                             && (ptThis->tProbeParam.eProbe == eProbe)))
                        ? MVC_PROBE_COMMAND_STOP : MVC_PROBE_COMMAND_START;
                    (void)mvc_ProbeCommand(ptThis, eProbe, eCommand);
                    break;
                }
            }
        }
#if USERCONFIG_BOARD_LINK_ENABLE
        _mvc_ServiceBoard(ptThis);
#endif
        _mvc_DispatchProbeCommand(ptThis);
#if !(USERCONFIG_MSHELL_ON_SERIAL && MSHELL_ENABLE)
        /* USART1 has one foreground consumer: DWIN or the debug shell. */
        ptThis->eDwinError = dwin_driver_Run(&ptThis->tDwin);
        if (ptThis->eDwinError != DWIN_DRIVER_RESULT_OK) {
            ptThis->eState = MVC_STATE_ERROR;
            return MODUS_EFAIL;
        }
#endif
        PERFC_PT_YIELD(fsm_rt_cpl)
    }
    PERFC_PT_END()
    return MODUS_SUCCESS;
}

/**
 * @brief Validate the Class required fixed-rate callback.
 * @param wObjectAddr Address of the MODUS Class instance.
 * @return MODUS_SUCCESS or PT progress; MODUS_EFAIL on failure.
 */
int mvc_Clock(uintptr_t wObjectAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;

    if ((ptThis == NULL) || (ptThis->eState != MVC_STATE_READY)) {
        return MODUS_EFAIL;
    }

    input_driver_Clock(&ptThis->tInput);

    return MODUS_SUCCESS;
}

MODUS_DECLARE_OBJECT(mvc, Mvc,
    .tDwinCfg = {
        .wFrameTimeoutMs = DWIN_DRIVER_FRAME_TIMEOUT_MS,
    }
)

/* MVC owns the shared input snapshots; both objects have static lifetime. */
MODUS_DECLARE_OBJECT(mircoelec, Mircoelec,
    .tDacCfg = {.wSampleFrequencyHz = 200000U},
    .ptTemperature = &tMvc.tInput.tTemperature[INPUT_DRIVER_NTC_BIO]
)
