#ifndef MVC_CLASS_H
#define MVC_CLASS_H

#include "modus.h"
#include "board_protocol.h"
#include "dwin_driver.h"
#include "mvc_ui.h"
#include "input_driver.h"
#include "mircoelec.h"

typedef enum {
    MVC_STATE_UNINITIALIZED = 0,
    MVC_STATE_READY,
    MVC_STATE_ERROR,
} mvc_state_t;

typedef enum {
    MVC_PROBE_NONE = 0,
    MVC_PROBE_CAP,
    MVC_PROBE_RES,
    MVC_PROBE_BIO,
} mvc_probe_t;

typedef enum {
    MVC_PROBE_COMMAND_NONE = 0,
    MVC_PROBE_COMMAND_START,
    MVC_PROBE_COMMAND_STOP,
    MVC_PROBE_COMMAND_ENERGY,
} probe_command_type_t;

typedef struct {
    probe_command_type_t eCommand;
} probe_command_t;

typedef struct {
    uint16_t hwEnergyPermille;
} probe_448_param_t;

/** Single RAM parameter bank; UI writes it and control paths read it. */
typedef struct {
    mvc_probe_t eProbe;
    mircoelec_param_t tBio;
    probe_448_param_t tCap;
    probe_448_param_t tRes;
} probe_param_t;

typedef struct {
    dwin_driver_cfg_t tDwinCfg;
} mvc_cfg_t;

typedef struct {
    modus_base_t *ptBase;
    board_protocol_t tBoardProtocol;
    dwin_driver_t tDwin;
    mvc_ui_t tUi;
    input_driver_t tInput;
    probe_param_t tProbeParam;
    probe_command_t tProbeCommand;
    mvc_probe_t eActiveProbe;
    mvc_state_t eState;
    dwin_driver_result_t eDwinError;
    uint8_t chRunPt;
    int64_t lBoardReplyDeadline;
} mvc_t;

int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr);
int mvc_Run(uintptr_t wObjectAddr);
int mvc_Clock(uintptr_t wObjectAddr);
bool mvc_ProbeCommand(mvc_t *ptThis, mvc_probe_t eProbe,
                      probe_command_type_t eCommand);

#endif /* MVC_CLASS_H */
