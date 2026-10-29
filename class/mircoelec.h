#ifndef MIRCROELEC_CLASS_H
#define MIRCROELEC_CLASS_H

#include "modus.h"
#include "dac_driver.h"
#include "heater_control.h"

typedef enum {
    MIRCROELEC_STATE_UNINITIALIZED = 0,
    MIRCROELEC_STATE_READY,
    MIRCROELEC_STATE_ERROR,
} mircoelec_state_t;

typedef struct {
    dac_driver_wave_type_t eWaveType;
    uint32_t wAHz;
    uint32_t wBHz;
    uint8_t chAmplitudePercent;
} mircoelec_param_t;

typedef enum {
    MIRCROELEC_COMMAND_START = 1,
    MIRCROELEC_COMMAND_STOP,
} mircoelec_command_type_t;

typedef struct {
    mircoelec_command_type_t eCommand;
    /* Points into MVC's static parameter bank; the ring copies this pointer. */
    const mircoelec_param_t *ptWave;
} mircoelec_command_t;

typedef enum {
    MIRCROELEC_BIO_IDLE = 0,
    MIRCROELEC_BIO_WAITING,
    MIRCROELEC_BIO_RUNNING,
    MIRCROELEC_BIO_ERROR,
} mircoelec_bio_state_t;

typedef struct {
    dac_driver_cfg_t tDacCfg;
    const input_driver_temperature_t *ptTemperature;
} mircoelec_cfg_t;

typedef struct {
    modus_base_t *ptBase;
    dac_driver_t tDac;
    heater_control_t tHeater;
    mircoelec_bio_state_t eBioState;
    mircoelec_command_t tBioCommand;
    const mircoelec_param_t *ptBioWave;
    int64_t lBioDeadline;
    uint8_t chBioPt;
    mircoelec_state_t eState;
    dac_driver_result_t eDacError;
} mircoelec_t;

int mircoelec_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr);
int mircoelec_Run(uintptr_t wObjectAddr);
int mircoelec_Clock(uintptr_t wObjectAddr);
dac_driver_result_t mircoelec_StartConfiguredWaveform(
    mircoelec_t *ptThis, dac_driver_wave_type_t eWaveType,
    uint32_t wAHz, uint32_t wBHz, uint8_t chAmplitudePercent);
dac_driver_result_t mircoelec_StopWaveform(mircoelec_t *ptThis);
// 独立class 不对外公开api
// dac_driver_result_t mircoelec_StartWaveform(
//     mircoelec_t *ptThis,
//     uint32_t wAHz,
//     uint32_t wBHz);
// dac_driver_result_t mircoelec_StopWaveform(mircoelec_t *ptThis);

#endif /* MIRCROELEC_CLASS_H */
