/**
 * @file mvc.c
 * @brief DWIN and client-control Class skeleton.
 * @author Modus project
 * @date 2026-10-08
 */

#include "mvc.h"
#include "mdi/instance.h"
#include "mdebug/mshell.h"
#include "mdebug/util_debug.h"
#include "perfc_task_pt.h"

#include <stddef.h>
#include <string.h>

static modus_base_t s_tMvcBase;
static modus_base_cfg_t s_tMvcBaseCfg = {
    .wId = MVC,
    .wParent = 0U,
    .FcnInterface = {
        .Clock = mvc_Clock,
        .Run = mvc_Run,
    },
};

#if MSHELL_ENABLE
/**
 * @brief Echo one shell command argument to the configured debug output.
 * @param pchArgs Text received after the echo command.
 * @return None.
 */
static void _mvc_Echo(const char *pchArgs)
{
    MLOGF(I, "echo: %s\r\n", pchArgs);
}

MODUS_SHELL_CMD(echo, _mvc_Echo, "Echo text to verify serial RX/TX");

/**
 * @brief Enable or disable the bio supply relay output.
 * @param pchArgs Text received after the bio_power command.
 * @return None.
 */
static void _mvc_BioPowerCommand(const char *pchArgs)
{
    const char *pchText = pchArgs;
    bool bEnable = false;
    uint32_t wPinLevel = 0U;

    if ((mshell_Parse(&pchText, &bEnable) != MSHELL_PARSE_OK)
        || !mshell_AtEnd(pchText)) {
        MLOGF(W, "Usage: bio_power <on|off>\r\n");
        return;
    }

    wPinLevel = bEnable ? 1U : 0U;
    MDI_IO_Write(bio_power, wPinLevel);
    MLOGF(I, "BIO power %s (PA11=%lu)\r\n",
          bEnable ? "enabled" : "disabled",
          (unsigned long)wPinLevel);
}

MODUS_SHELL_CMD(bio_power, _mvc_BioPowerCommand,
                "Control bio power: on | off");
#endif

/**
 * @brief Read and log the five averaged ADC channels.
 * @return None.
 */
static void _mvc_LogAdcValues(void)
{
    mdi_adc_value_t tValues[5] = {{0U}};
    mdi_status_t tStatus[5] = {
        MDI_BUSY, MDI_BUSY, MDI_BUSY, MDI_BUSY, MDI_BUSY};

    tStatus[0] = MDI_ADC_Read(adc_ntc1, &tValues[0]);
    tStatus[1] = MDI_ADC_Read(adc_ntc2, &tValues[1]);
    tStatus[2] = MDI_ADC_Read(adc_ntc3, &tValues[2]);
    tStatus[3] = MDI_ADC_Read(adc_ntc4, &tValues[3]);
    tStatus[4] = MDI_ADC_Read(adc_bioadc, &tValues[4]);

    MLOGF(T,
          "ADC NT1=%lu(%d) NT2=%lu(%d) NT3=%lu(%d) NT4=%lu(%d) BIO=%lu(%d)\r\n",
          (unsigned long)tValues[0].wCode, (int)tStatus[0],
          (unsigned long)tValues[1].wCode, (int)tStatus[1],
          (unsigned long)tValues[2].wCode, (int)tStatus[2],
          (unsigned long)tValues[3].wCode, (int)tStatus[3],
          (unsigned long)tValues[4].wCode, (int)tStatus[4]);
}

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

    if (board_link_driver_Init(&ptThis->tBoardLink)
        != BOARD_LINK_DRIVER_RESULT_OK) {
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

    if ((ptThis == NULL) || (ptThis->eState != MVC_STATE_READY)) {
        return MODUS_EFAIL;
    }

    PERFC_PT_BEGIN(ptThis->chRunPt)
    while (1) {
#if !(USERCONFIG_MSHELL_ON_SERIAL && MSHELL_ENABLE)
        /* USART1 has one foreground consumer: DWIN or the debug shell. */
        ptThis->eDwinError = dwin_driver_Run(&ptThis->tDwin);
        if (ptThis->eDwinError != DWIN_DRIVER_RESULT_OK) {
            ptThis->eState = MVC_STATE_ERROR;
            return MODUS_EFAIL;
        }
#endif
        if (perfc_is_time_out_ms(1000U, &ptThis->lAdcLogDeadline,
                                 false)) {
            ptThis->lAdcLogDeadline = 0;
            _mvc_LogAdcValues();
        }
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

    return MODUS_SUCCESS;
}

MODUS_DECLARE_OBJECT(mvc, Mvc,
    .tDwinCfg = {
        .wFrameTimeoutMs = DWIN_DRIVER_FRAME_TIMEOUT_MS,
    }
)
