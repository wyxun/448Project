/**
 * @file mircoelec.c
 * @brief Micro-electronics control Class skeleton.
 */

#include "mircoelec.h"

#include <stddef.h>
#include <string.h>

#define MIRCROELEC_DAC_SAMPLE_FREQUENCY_HZ 320000U

static modus_base_t s_tMircoelecBase;
static modus_base_cfg_t s_tMircoelecBaseCfg = {
    .wId = MIRCROELEC,
    .wParent = 0U,
    .FcnInterface = {
        .Clock = mircoelec_Clock,
        .Run = mircoelec_Run,
    },
};

int mircoelec_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;
    const mircoelec_cfg_t *ptCfg =
        (const mircoelec_cfg_t *)wObjectCfgAddr;
    int nResult;

    if ((ptThis == NULL) || (ptCfg == NULL)) {
        return MODUS_EFAIL;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->ptBase = &s_tMircoelecBase;
    ptThis->eState = MIRCROELEC_STATE_UNINITIALIZED;

    if (dac_driver_Init(&ptThis->tDac, &ptCfg->tDacCfg)
        != DAC_DRIVER_RESULT_OK) {
        ptThis->eState = MIRCROELEC_STATE_ERROR;
        return MODUS_EFAIL;
    }

    s_tMircoelecBaseCfg.wParent = wObjectAddr;
    nResult = mbase_Init(ptThis->ptBase, &s_tMircoelecBaseCfg);
    if (nResult != MODUS_SUCCESS) {
        ptThis->eState = MIRCROELEC_STATE_ERROR;
        return nResult;
    }

    if (dac_driver_Start(&ptThis->tDac) != DAC_DRIVER_RESULT_OK) {
        ptThis->eState = MIRCROELEC_STATE_ERROR;
        return MODUS_EFAIL;
    }

    ptThis->eState = MIRCROELEC_STATE_READY;
    return MODUS_SUCCESS;
}

extern mircoelec_t tMircoelec;

void mircoelec_IsrStep(void)
{
    dac_driver_IsrStep(&tMircoelec.tDac);
}

int mircoelec_Run(uintptr_t wObjectAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;

    if (ptThis == NULL || ptThis->eState == MIRCROELEC_STATE_ERROR) {
        return MODUS_EFAIL;
    }
    /* MODUS 当前要求每个对象提供 Run；DAC 波形由硬件 TMR1 ISR 推进。 */
    return MODUS_SUCCESS;
}

int mircoelec_Clock(uintptr_t wObjectAddr)
{
    mircoelec_t *ptThis = (mircoelec_t *)wObjectAddr;

    if (ptThis == NULL || ptThis->eState == MIRCROELEC_STATE_ERROR) {
        return MODUS_EFAIL;
    }

    return MODUS_SUCCESS;
}

MODUS_DECLARE_OBJECT(mircoelec, Mircoelec,
    .tDacCfg = {
        .wSampleFrequencyHz = MIRCROELEC_DAC_SAMPLE_FREQUENCY_HZ,
    }
)
