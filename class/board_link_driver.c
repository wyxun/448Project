/**
 * @file board_link_driver.c
 * @brief Protocol-neutral board-to-board stream driver.
 */

#include "board_link_driver.h"

#include "mdi/instance.h"

#include <stddef.h>
#include <string.h>

static int _board_link_driver_IsReady(const board_link_driver_t *ptThis)
{
    return (ptThis != NULL)
        && (ptThis->eState == BOARD_LINK_DRIVER_STATE_READY);
}

board_link_driver_result_t board_link_driver_Init(
    board_link_driver_t *ptThis,
    const board_link_driver_cfg_t *ptCfg)
{
    if (ptThis == NULL) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = BOARD_LINK_DRIVER_STATE_UNINITIALIZED;
    if (ptCfg == NULL) {
        ptThis->eState = BOARD_LINK_DRIVER_STATE_ERROR;
        return BOARD_LINK_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    ptThis->eState = BOARD_LINK_DRIVER_STATE_READY;
    return BOARD_LINK_DRIVER_RESULT_OK;
}

board_link_driver_result_t board_link_driver_Run(
    board_link_driver_t *ptThis)
{
    if (ptThis == NULL) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }
    if (!_board_link_driver_IsReady(ptThis)) {
        return (ptThis->eState == BOARD_LINK_DRIVER_STATE_UNINITIALIZED)
             ? BOARD_LINK_DRIVER_RESULT_NOT_READY
             : BOARD_LINK_DRIVER_RESULT_FAULT;
    }

    /* Protocol parsing is intentionally left to the future board-link layer. */
    return BOARD_LINK_DRIVER_RESULT_OK;
}

board_link_driver_result_t board_link_driver_Clock(
    board_link_driver_t *ptThis)
{
    if (ptThis == NULL) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }
    if (!_board_link_driver_IsReady(ptThis)) {
        return (ptThis->eState == BOARD_LINK_DRIVER_STATE_UNINITIALIZED)
             ? BOARD_LINK_DRIVER_RESULT_NOT_READY
             : BOARD_LINK_DRIVER_RESULT_FAULT;
    }
    return BOARD_LINK_DRIVER_RESULT_OK;
}

board_link_driver_result_t board_link_driver_Write(
    board_link_driver_t *ptThis,
    const uint8_t *pchData,
    uint32_t wLength)
{
    int32_t nWritten;

    if ((ptThis == NULL) || (pchData == NULL && wLength != 0U)) {
        return BOARD_LINK_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    if (!_board_link_driver_IsReady(ptThis)) {
        return BOARD_LINK_DRIVER_RESULT_NOT_READY;
    }

    nWritten = MDI_STREAM_Write(board_link_stream, pchData, wLength);
    if (nWritten < 0) {
        ptThis->tStats.wTxErrors++;
        return BOARD_LINK_DRIVER_RESULT_IO;
    }
    ptThis->tStats.wTxBytes += (uint32_t)nWritten;
    if ((uint32_t)nWritten != wLength) {
        ptThis->tStats.wTxErrors++;
        return BOARD_LINK_DRIVER_RESULT_IO;
    }
    return BOARD_LINK_DRIVER_RESULT_OK;
}

int32_t board_link_driver_Read(
    board_link_driver_t *ptThis,
    uint8_t *pchData,
    uint32_t wLength)
{
    int32_t nRead;

    if ((ptThis == NULL) || (pchData == NULL && wLength != 0U)) {
        return -1;
    }
    if (!_board_link_driver_IsReady(ptThis)) {
        return -1;
    }

    nRead = MDI_STREAM_Read(board_link_stream, pchData, wLength);
    if (nRead < 0) {
        ptThis->tStats.wRxErrors++;
        return -1;
    }
    ptThis->tStats.wRxBytes += (uint32_t)nRead;
    return nRead;
}

board_link_driver_result_t board_link_driver_GetStats(
    const board_link_driver_t *ptThis,
    board_link_driver_stats_t *ptStats)
{
    if ((ptThis == NULL) || (ptStats == NULL)) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }
    *ptStats = ptThis->tStats;
    return BOARD_LINK_DRIVER_RESULT_OK;
}
