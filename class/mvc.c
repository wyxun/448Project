/**
 * @file mvc.c
 * @brief DWIN and client-control Class skeleton.
 */

#include "mvc.h"

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

int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;
    const mvc_cfg_t *ptCfg = (const mvc_cfg_t *)wObjectCfgAddr;
    dwin_driver_cfg_t tDwinCfg;
    mvc_ui_cfg_t tUiCfg;
    int nResult;
    dwin_driver_result_t eDwinResult;

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

    tDwinCfg = ptCfg->tDwinCfg;
    eDwinResult = dwin_driver_Init(&ptThis->tDwin, &tDwinCfg);
    if (eDwinResult != DWIN_DRIVER_RESULT_OK) {
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

int mvc_Run(uintptr_t wObjectAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;

    if (ptThis == NULL || ptThis->eState == MVC_STATE_ERROR) {
        return MODUS_EFAIL;
    }

    if (dwin_driver_Run(&ptThis->tDwin) != DWIN_DRIVER_RESULT_OK) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
    if (mvc_ui_Run(&ptThis->tUi) != 0) {
        ptThis->eState = MVC_STATE_ERROR;
        return MODUS_EFAIL;
    }
    return MODUS_SUCCESS;
}

int mvc_Clock(uintptr_t wObjectAddr)
{
    mvc_t *ptThis = (mvc_t *)wObjectAddr;

    if (ptThis == NULL || ptThis->eState == MVC_STATE_ERROR) {
        return MODUS_EFAIL;
    }

    return MODUS_SUCCESS;
}

MODUS_DECLARE_OBJECT(mvc, Mvc,
    .tDwinCfg = {
        .wFrameTimeoutMs = DWIN_DRIVER_FRAME_TIMEOUT_MS,
    }
)
