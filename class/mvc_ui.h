#ifndef MVC_UI_H
#define MVC_UI_H

#include <stdint.h>

#include "dwin_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MVC_UI_INVALID_PAGE    0xFFFFU
#define MVC_UI_INVALID_ADDRESS 0xFFFFU

typedef enum {
    MVC_UI_STATE_UNINITIALIZED = 0,
    MVC_UI_STATE_READY,
    MVC_UI_STATE_ERROR,
} mvc_ui_state_t;

typedef struct {
    dwin_driver_t *ptDwin;
} mvc_ui_cfg_t;

typedef struct {
    dwin_driver_t *ptDwin;
    mvc_ui_state_t eState;
    uint16_t hwCurrentPage;
    uint16_t hwLastAddress;
    uint8_t chLastCommand;
    uint16_t hwLastDataLength;
} mvc_ui_t;

int mvc_ui_Init(mvc_ui_t *ptThis, const mvc_ui_cfg_t *ptCfg);
int mvc_ui_Run(mvc_ui_t *ptThis);

int mvc_ui_SetPage(mvc_ui_t *ptThis, uint16_t hwPageId);
int mvc_ui_SetN16Data(mvc_ui_t *ptThis,
                      uint16_t hwAddress,
                      const uint16_t *phwData,
                      uint16_t hwLength);
int mvc_ui_SetN32Data(mvc_ui_t *ptThis,
                      uint16_t hwAddress,
                      const uint32_t *pwData,
                      uint16_t hwLength);
int mvc_ui_ReadN16Data(mvc_ui_t *ptThis,
                       uint16_t hwAddress,
                       uint8_t chLength);
int mvc_ui_Reset(mvc_ui_t *ptThis);

#ifdef __cplusplus
}
#endif

#endif /* MVC_UI_H */
