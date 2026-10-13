#ifndef MIRCROELEC_CLASS_H
#define MIRCROELEC_CLASS_H

#include "modus.h"
#include "dac_driver.h"
#if MWAVEFORM_ENABLE
#include "mircoelec_waveform.h"
#endif

typedef enum {
    MIRCROELEC_STATE_UNINITIALIZED = 0,
    MIRCROELEC_STATE_READY,
    MIRCROELEC_STATE_ERROR,
} mircoelec_state_t;

typedef struct {
    dac_driver_cfg_t tDacCfg;
} mircoelec_cfg_t;

typedef struct {
    modus_base_t *ptBase;
    dac_driver_t tDac;
    mircoelec_state_t eState;
    dac_driver_result_t eDacError;
#if MWAVEFORM_ENABLE
    mircoelec_waveform_t tWaveform;
#endif
} mircoelec_t;

int mircoelec_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr);
int mircoelec_Run(uintptr_t wObjectAddr);
int mircoelec_Clock(uintptr_t wObjectAddr);
void mircoelec_IsrStep(void);
// 独立class 不对外公开api
// dac_driver_result_t mircoelec_StartWaveform(
//     mircoelec_t *ptThis,
//     uint32_t wAHz,
//     uint32_t wBHz);
// dac_driver_result_t mircoelec_StopWaveform(mircoelec_t *ptThis);

#endif /* MIRCROELEC_CLASS_H */
