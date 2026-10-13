/**
 * @file board_link_driver.c
 * @brief Protocol-neutral board-to-board stream driver.
 * @author Modus project
 * @date 2026-10-08
 */

#include "board_link_driver.h"

#include "mdi/instance.h"

#include <stddef.h>
#include <string.h>

/**
 * @brief Check whether board-link transport is initialized.
 * @param ptThis Caller-owned module instance.
 * @return True when the requested check succeeds.
 */
static int _board_link_driver_IsReady(const board_link_driver_t *ptThis)
{
    return (ptThis != NULL)
        && (ptThis->eState == BOARD_LINK_DRIVER_STATE_READY);
}

/**
 * @brief Reset the protocol-neutral board-link transport state.
 * @param ptThis Caller-owned module instance.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
board_link_driver_result_t board_link_driver_Init(
    board_link_driver_t *ptThis)
{
    if (ptThis == NULL) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }

    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = BOARD_LINK_DRIVER_STATE_UNINITIALIZED;
    ptThis->eState = BOARD_LINK_DRIVER_STATE_READY;
    return BOARD_LINK_DRIVER_RESULT_OK;
}

/**
 * @brief Submit bytes and account for partial or failed writes.
 * @param ptThis Caller-owned module instance.
 * @param pchData Payload bytes; must cover the supplied length.
 * @param wLength Buffer size in bytes.
 * @param pwCount Actual transferred bytes, including a short transfer.
 * @return Status; RX OVERRUN may accompany valid received bytes.
 */
board_link_driver_result_t board_link_driver_Write(
    board_link_driver_t *ptThis,
    const uint8_t *pchData,
    uint32_t wLength,
    uint32_t *pwCount)
{
    mdi_status_t eStatus = MDI_OK;

    if (pwCount == NULL) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }
    *pwCount = 0U;
    if ((ptThis == NULL) || (pchData == NULL && wLength != 0U)) {
        return BOARD_LINK_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    if (!_board_link_driver_IsReady(ptThis)) {
        return BOARD_LINK_DRIVER_RESULT_NOT_READY;
    }

    eStatus = MDI_STREAM_Write(board_link_stream, pchData, wLength, pwCount);
    ptThis->tStats.wTxBytes += *pwCount;
    if (eStatus != MDI_OK) {
        ptThis->tStats.wTxErrors++;
        return BOARD_LINK_DRIVER_RESULT_IO;
    }
    /* A short enqueue is normal backpressure; caller retries only the tail. */
    return BOARD_LINK_DRIVER_RESULT_OK;
}

/**
 * @brief Receive available bytes and account for transport errors.
 * @param ptThis Caller-owned module instance.
 * @param pchData Payload bytes; must cover the supplied length.
 * @param wLength Buffer size in bytes.
 * @param pwCount Actual transferred bytes, including a short transfer.
 * @return Status; RX OVERRUN may accompany valid received bytes.
 */
board_link_driver_result_t board_link_driver_Read(
    board_link_driver_t *ptThis,
    uint8_t *pchData,
    uint32_t wLength,
    uint32_t *pwCount)
{
    mdi_status_t eStatus = MDI_OK;

    if (pwCount == NULL) {
        return BOARD_LINK_DRIVER_RESULT_NULL;
    }
    *pwCount = 0U;
    if ((ptThis == NULL) || (pchData == NULL && wLength != 0U)) {
        return BOARD_LINK_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    if (!_board_link_driver_IsReady(ptThis)) {
        return BOARD_LINK_DRIVER_RESULT_NOT_READY;
    }

    eStatus = MDI_STREAM_Read(board_link_stream, pchData, wLength, pwCount);
    ptThis->tStats.wRxBytes += *pwCount;
    if (eStatus != MDI_OK) {
        ptThis->tStats.wRxErrors++;
        return eStatus == MDI_OVERRUN ? BOARD_LINK_DRIVER_RESULT_OVERRUN
                                     : BOARD_LINK_DRIVER_RESULT_IO;
    }
    return BOARD_LINK_DRIVER_RESULT_OK;
}

/**
 * @brief Copy board-link transport counters to the caller.
 * @param ptThis Caller-owned module instance.
 * @param ptStats Destination counter snapshot.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
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
