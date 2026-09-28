/**
 * @file mvc_ui.c
 * @brief MVC-owned DWIN page, VP address and event handling module.
 */

#include "mvc_ui.h"

#include <stddef.h>
#include <string.h>

static void _mvc_ui_OnDwinReceive(void *pContext,
                                  uint8_t chCommand,
                                  uint16_t hwAddress,
                                  uint8_t *pchData,
                                  uint16_t hwLength);

static void _mvc_ui_HandleReadResponse(mvc_ui_t *ptThis,
                                       uint16_t hwAddress,
                                       uint8_t *pchData,
                                       uint16_t hwLength)
{
    if ((ptThis == NULL) || (pchData == NULL) || (hwLength == 0U)) {
        return;
    }

    /*
     * DGUS 0x83 data starts with the returned word count. Concrete VP
     * addresses and application value conversion belong in this module.
     * Keep the transport-independent receipt state here for now.
     */
    ptThis->hwLastAddress = hwAddress;
    ptThis->hwLastDataLength = hwLength;
}

static void _mvc_ui_HandleWriteEvent(mvc_ui_t *ptThis,
                                     uint16_t hwAddress,
                                     uint8_t *pchData,
                                     uint16_t hwLength)
{
    (void)pchData;
    if (ptThis == NULL) {
        return;
    }

    /* Concrete touch/return-key VP handlers are added by address here. */
    ptThis->hwLastAddress = hwAddress;
    ptThis->hwLastDataLength = hwLength;
}

static void _mvc_ui_OnDwinReceive(void *pContext,
                                  uint8_t chCommand,
                                  uint16_t hwAddress,
                                  uint8_t *pchData,
                                  uint16_t hwLength)
{
    mvc_ui_t *ptThis = (mvc_ui_t *)pContext;

    if ((ptThis == NULL) || (ptThis->eState == MVC_UI_STATE_ERROR)) {
        return;
    }

    ptThis->chLastCommand = chCommand;
    if (chCommand == DWIN_DRIVER_CMD_READ) {
        _mvc_ui_HandleReadResponse(ptThis, hwAddress, pchData, hwLength);
    } else if (chCommand == DWIN_DRIVER_CMD_WRITE) {
        _mvc_ui_HandleWriteEvent(ptThis, hwAddress, pchData, hwLength);
    }
}

int mvc_ui_Init(mvc_ui_t *ptThis, const mvc_ui_cfg_t *ptCfg)
{
    dwin_driver_result_t eResult;

    if (ptThis == NULL) {
        return -1;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = MVC_UI_STATE_UNINITIALIZED;
    ptThis->hwCurrentPage = MVC_UI_INVALID_PAGE;
    ptThis->hwLastAddress = MVC_UI_INVALID_ADDRESS;
    if ((ptCfg == NULL) || (ptCfg->ptDwin == NULL)) {
        ptThis->eState = MVC_UI_STATE_ERROR;
        return -1;
    }

    ptThis->ptDwin = ptCfg->ptDwin;
    eResult = dwin_driver_RegisterCallback(ptThis->ptDwin,
                                           _mvc_ui_OnDwinReceive,
                                           ptThis);
    if (eResult != DWIN_DRIVER_RESULT_OK) {
        ptThis->eState = MVC_UI_STATE_ERROR;
        return -1;
    }

    ptThis->eState = MVC_UI_STATE_READY;
    return 0;
}

int mvc_ui_Run(mvc_ui_t *ptThis)
{
    if ((ptThis == NULL) || (ptThis->eState == MVC_UI_STATE_ERROR)) {
        return -1;
    }
    return 0;
}

int mvc_ui_SetPage(mvc_ui_t *ptThis, uint16_t hwPageId)
{
    if ((ptThis == NULL) || (ptThis->eState != MVC_UI_STATE_READY)) {
        return -1;
    }
    if (dwin_driver_SetPage(ptThis->ptDwin, hwPageId)
        != DWIN_DRIVER_RESULT_OK) {
        return -1;
    }
    ptThis->hwCurrentPage = hwPageId;
    return 0;
}

int mvc_ui_SetN16Data(mvc_ui_t *ptThis,
                      uint16_t hwAddress,
                      const uint16_t *phwData,
                      uint16_t hwLength)
{
    if ((ptThis == NULL) || (ptThis->eState != MVC_UI_STATE_READY)) {
        return -1;
    }
    return (dwin_driver_SetN16Data(ptThis->ptDwin, hwAddress, phwData,
                                   hwLength) == DWIN_DRIVER_RESULT_OK)
         ? 0 : -1;
}

int mvc_ui_SetN32Data(mvc_ui_t *ptThis,
                      uint16_t hwAddress,
                      const uint32_t *pwData,
                      uint16_t hwLength)
{
    if ((ptThis == NULL) || (ptThis->eState != MVC_UI_STATE_READY)) {
        return -1;
    }
    return (dwin_driver_SetN32Data(ptThis->ptDwin, hwAddress, pwData,
                                   hwLength) == DWIN_DRIVER_RESULT_OK)
         ? 0 : -1;
}

int mvc_ui_ReadN16Data(mvc_ui_t *ptThis,
                       uint16_t hwAddress,
                       uint8_t chLength)
{
    if ((ptThis == NULL) || (ptThis->eState != MVC_UI_STATE_READY)) {
        return -1;
    }
    return (dwin_driver_ReadN16Data(ptThis->ptDwin, hwAddress, chLength)
            == DWIN_DRIVER_RESULT_OK) ? 0 : -1;
}

int mvc_ui_Reset(mvc_ui_t *ptThis)
{
    if ((ptThis == NULL) || (ptThis->eState != MVC_UI_STATE_READY)) {
        return -1;
    }
    return (dwin_driver_Reset(ptThis->ptDwin) == DWIN_DRIVER_RESULT_OK)
         ? 0 : -1;
}
