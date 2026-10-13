/**
 * @file mvc_ui.c
 * @brief MVC-owned DWIN page, VP address and event handling module.
 * @author Modus project
 * @date 2026-10-08
 */

#include "mvc_ui.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief Publish the last recognized VP receipt for UI handling.
 * @param pContext Consumer instance context.
 * @param ptFrame Borrowed frame view; valid until buffer compaction.
 * @return None.
 */
static void _mvc_ui_OnDwinReceive(
    void *pContext, const dwin_driver_frame_t *ptFrame)
{
    mvc_ui_t *ptThis = (mvc_ui_t *)pContext;
    if ((ptThis == NULL) || (ptFrame == NULL)
        || (ptThis->eState != MVC_UI_STATE_READY)) {
        return;
    }
    if ((ptFrame->chCommand == DWIN_DRIVER_CMD_READ)
        || (ptFrame->chCommand == DWIN_DRIVER_CMD_WRITE)) {
        /* VP interpretation is added when the product pages are defined. */
        ptThis->chLastCommand = ptFrame->chCommand;
        ptThis->hwLastAddress = ptFrame->hwAddress;
        ptThis->hwLastDataLength = ptFrame->hwLength;
    } else {
        /* Unknown commands leave published receipt state unchanged. */
    }
}

/**
 * @brief Bind UI receipt handling to the supplied DWIN instance.
 * @param ptThis Caller-owned module instance.
 * @param ptCfg Initialization policy in caller-facing units.
 * @return Zero on success, or -1 on invalid state or I/O failure.
 */
int mvc_ui_Init(mvc_ui_t *ptThis, const mvc_ui_cfg_t *ptCfg)
{
    dwin_driver_result_t eResult = DWIN_DRIVER_RESULT_OK;

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

/**
 * @brief Request a page and publish it only after successful submission.
 * @param ptThis Caller-owned module instance.
 * @param hwPageId DGUS page identifier.
 * @return Zero on success, or -1 on invalid state or I/O failure.
 */
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
