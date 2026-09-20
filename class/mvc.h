#ifndef MVC_CLASS_H
#define MVC_CLASS_H

#include "modus.h"
#include "board_link_driver.h"
#include "dwin_driver.h"
#include "mvc_ui.h"

typedef enum {
    MVC_STATE_UNINITIALIZED = 0,
    MVC_STATE_READY,
    MVC_STATE_ERROR,
} mvc_state_t;

typedef struct {
    board_link_driver_cfg_t tBoardLinkCfg;
    dwin_driver_cfg_t tDwinCfg;
} mvc_cfg_t;

typedef struct {
    modus_base_t *ptBase;
    board_link_driver_t tBoardLink;
    dwin_driver_t tDwin;
    mvc_ui_t tUi;
    mvc_state_t eState;
} mvc_t;

int mvc_Init(uintptr_t wObjectAddr, uintptr_t wObjectCfgAddr);
int mvc_Run(uintptr_t wObjectAddr);
int mvc_Clock(uintptr_t wObjectAddr);

#endif /* MVC_CLASS_H */
