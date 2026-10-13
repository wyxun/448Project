/**
 * @file dwin_driver.c
 * @brief DWIN DGUS protocol driver over an MDI stream.
 * @author Modus project
 * @date 2026-10-08
 */

#include "dwin_driver.h"

#include "mdi/instance.h"
#include "perf_counter.h"

#include <stddef.h>
#include <string.h>

#define DWIN_DRIVER_RESET_ADDRESS 0x0004U
#define DWIN_DRIVER_PAGE_ADDRESS  0x0084U

#if DWIN_DRIVER_CRC16_EN
/**
 * @brief Calculate the DGUS Modbus CRC over a payload.
 * @param pchData Payload bytes; must cover the supplied length.
 * @param hwLength Word count, or byte count for CRC input.
 * @return CRC value for the supplied bytes.
 */
static uint16_t _dwin_driver_Crc16(const uint8_t *pchData, uint16_t hwLength)
{
    uint16_t hwCrc = 0xFFFFU;

    while (hwLength > 0U) {
        uint8_t chBit = 0U;
        hwCrc ^= *pchData;
        pchData++;
        for (chBit = 0U; chBit < 8U; chBit++) {
            if ((hwCrc & 0x0001U) != 0U) {
                hwCrc = (uint16_t)((hwCrc >> 1U) ^ 0xA001U);
            } else {
                hwCrc >>= 1U;
            }
        }
        hwLength--;
    }

    return hwCrc;
}

#endif

/**
 * @brief Check whether protocol operations can proceed.
 * @param ptThis Caller-owned module instance.
 * @return True when the requested check succeeds.
 */
static bool _dwin_driver_IsReady(const dwin_driver_t *ptThis)
{
    return (ptThis != NULL)
        && (ptThis->eState == DWIN_DRIVER_STATE_READY);
}

/**
 * @brief Encode and submit one DGUS variable write frame.
 * @param ptThis Caller-owned module instance.
 * @param hwAddress DGUS variable address.
 * @param pchData Payload bytes; must cover the supplied length.
 * @param hwDataLength Payload size in bytes.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
static dwin_driver_result_t _dwin_driver_SendWrite(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint8_t *pchData,
    uint16_t hwDataLength)
{
    uint32_t wWritten = 0U;
    mdi_status_t eStatus = MDI_OK;
    uint16_t hwIndex = 0U;
    uint16_t hwPayloadLength = 0U;
    uint16_t hwFrameLength = 0U;
#if DWIN_DRIVER_CRC16_EN
    uint16_t hwCrc = 0U;
#endif

    if ((ptThis == NULL) || (pchData == NULL && hwDataLength != 0U)) {
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    hwPayloadLength = (uint16_t)(3U + hwDataLength);
    hwFrameLength = (uint16_t)(3U + hwPayloadLength);
#if DWIN_DRIVER_CRC16_EN
    hwFrameLength = (uint16_t)(hwFrameLength + 2U);
#endif
    if (hwFrameLength > sizeof(ptThis->chTxBuf)) {
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    ptThis->chTxBuf[hwIndex] = DWIN_DRIVER_FRAME_HEAD0;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = DWIN_DRIVER_FRAME_HEAD1;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)hwPayloadLength;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = DWIN_DRIVER_CMD_WRITE;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)(hwAddress >> 8U);
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)hwAddress;
    hwIndex++;
    if (hwDataLength > 0U) {
        (void)memcpy(&ptThis->chTxBuf[hwIndex], pchData, hwDataLength);
        hwIndex = (uint16_t)(hwIndex + hwDataLength);
    }

#if DWIN_DRIVER_CRC16_EN
    hwCrc = _dwin_driver_Crc16(&ptThis->chTxBuf[3],
                               (uint16_t)(3U + hwDataLength));
    ptThis->chTxBuf[2] = (uint8_t)(hwPayloadLength + 2U);
    ptThis->chTxBuf[hwIndex] = (uint8_t)hwCrc;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)(hwCrc >> 8U);
    hwIndex++;
#endif

    eStatus = MDI_STREAM_Write(
        dwin_stream, ptThis->chTxBuf, hwIndex, &wWritten);
    return ((eStatus == MDI_OK) && (wWritten == hwIndex))
         ? DWIN_DRIVER_RESULT_OK : DWIN_DRIVER_RESULT_IO;
}

/**
 * @brief Encode and submit one DGUS variable read request.
 * @param ptThis Caller-owned module instance.
 * @param hwAddress DGUS variable address.
 * @param chLength Requested word count, nonzero.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
static dwin_driver_result_t _dwin_driver_SendRead(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    uint8_t chLength)
{
    uint32_t wWritten = 0U;
    mdi_status_t eStatus = MDI_OK;
    uint16_t hwIndex = 0U;
    uint16_t hwPayloadLength = 4U;
#if DWIN_DRIVER_CRC16_EN
    uint16_t hwCrc = 0U;
#endif

    if (ptThis == NULL) {
        return DWIN_DRIVER_RESULT_NULL;
    }

    ptThis->chTxBuf[hwIndex] = DWIN_DRIVER_FRAME_HEAD0;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = DWIN_DRIVER_FRAME_HEAD1;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)hwPayloadLength;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = DWIN_DRIVER_CMD_READ;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)(hwAddress >> 8U);
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)hwAddress;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = chLength;
    hwIndex++;

#if DWIN_DRIVER_CRC16_EN
    hwCrc = _dwin_driver_Crc16(&ptThis->chTxBuf[3], 4U);
    ptThis->chTxBuf[2] = (uint8_t)(hwPayloadLength + 2U);
    ptThis->chTxBuf[hwIndex] = (uint8_t)hwCrc;
    hwIndex++;
    ptThis->chTxBuf[hwIndex] = (uint8_t)(hwCrc >> 8U);
    hwIndex++;
#endif

    eStatus = MDI_STREAM_Write(
        dwin_stream, ptThis->chTxBuf, hwIndex, &wWritten);
    return ((eStatus == MDI_OK) && (wWritten == hwIndex))
         ? DWIN_DRIVER_RESULT_OK : DWIN_DRIVER_RESULT_IO;
}

/**
 * @brief Discard a rejected candidate byte for resynchronization.
 * @param ptThis Caller-owned module instance.
 * @return None.
 */
static void _dwin_driver_DiscardOneByte(dwin_driver_t *ptThis)
{
    if (ptThis->hwRxPos > 0U) {
        (void)memmove(ptThis->chRxBuf, &ptThis->chRxBuf[1],
                      ptThis->hwRxPos - 1U);
        ptThis->hwRxPos--;
        ptThis->tStats.wDropped++;
    }
}

/**
 * @brief Check command-specific lengths before callback delivery.
 * @param ptFrame Borrowed frame view; valid until buffer compaction.
 * @return True when the requested check succeeds.
 */
static bool _dwin_driver_ValidatePayload(dwin_driver_frame_t *ptFrame)
{
    if (ptFrame->chCommand == DWIN_DRIVER_CMD_READ) {
        if ((ptFrame->hwLength < 1U)
            || (ptFrame->hwLength
                != (uint16_t)(1U + 2U * ptFrame->pchData[0]))) {
            return false;
        }
        ptFrame->bDispatch = true;
    } else if (ptFrame->chCommand == DWIN_DRIVER_CMD_WRITE) {
        if ((ptFrame->hwLength & 1U) != 0U) {
            return false;
        }
        ptFrame->bDispatch = ptFrame->hwLength != 0U;
    } else {
        return false;
    }
    return true;
}

/**
 * @brief Validate CRC and construct a borrowed payload view.
 * @param ptThis Caller-owned module instance.
 * @param ptFrame Borrowed frame view; valid until buffer compaction.
 * @return True when the requested check succeeds.
 */
static bool _dwin_driver_ValidateFrame(
    const dwin_driver_t *ptThis, dwin_driver_frame_t *ptFrame)
{
    uint16_t hwPayload = ptThis->chRxBuf[2];
#if DWIN_DRIVER_CRC16_EN
    uint16_t hwFullLength = (uint16_t)(3U + hwPayload);
    uint16_t hwCrcRx = 0U;
    uint16_t hwCrc = 0U;
    if (hwPayload < 5U) {
        return false;
    }
    hwCrcRx = (uint16_t)((uint16_t)ptThis->chRxBuf[hwFullLength - 2U]
             | ((uint16_t)ptThis->chRxBuf[hwFullLength - 1U] << 8U));
    hwCrc = _dwin_driver_Crc16(&ptThis->chRxBuf[3],
                              (uint16_t)(hwPayload - 2U));
    if (hwCrc != hwCrcRx) {
        return false;
    }
    hwPayload = (uint16_t)(hwPayload - 2U);
#else
    if (hwPayload < 3U) {
        return false;
    }
#endif
    ptFrame->chCommand = ptThis->chRxBuf[3];
    ptFrame->hwAddress = (uint16_t)(
        ((uint16_t)ptThis->chRxBuf[4] << 8U) | ptThis->chRxBuf[5]);
    ptFrame->pchData = &ptThis->chRxBuf[6];
    ptFrame->hwLength = (uint16_t)(hwPayload - 3U);
    return _dwin_driver_ValidatePayload(ptFrame);
}

/* Retain a trailing 0x5A so a header may straddle stream reads. */
/**
 * @brief Discard noise while preserving a split frame header.
 * @param ptThis Caller-owned module instance.
 * @return True when the requested check succeeds.
 */
static bool _dwin_driver_AlignHeader(dwin_driver_t *ptThis)
{
    uint16_t hwIndex = 0U;
    for (hwIndex = 0U; hwIndex + 1U < ptThis->hwRxPos; hwIndex++) {
        if ((ptThis->chRxBuf[hwIndex] == DWIN_DRIVER_FRAME_HEAD0)
            && (ptThis->chRxBuf[hwIndex + 1U] == DWIN_DRIVER_FRAME_HEAD1)) {
            if (hwIndex != 0U) {
                (void)memmove(ptThis->chRxBuf,
                              &ptThis->chRxBuf[hwIndex],
                              ptThis->hwRxPos - hwIndex);
                ptThis->hwRxPos = (uint16_t)(ptThis->hwRxPos - hwIndex);
                ptThis->tStats.wDropped += hwIndex;
            }
            return true;
        }
    }
    hwIndex = ptThis->chRxBuf[ptThis->hwRxPos - 1U]
            == DWIN_DRIVER_FRAME_HEAD0 ? 1U : 0U;
    if (hwIndex != 0U) {
        ptThis->chRxBuf[0] = DWIN_DRIVER_FRAME_HEAD0;
    }
    ptThis->tStats.wDropped += (uint32_t)(ptThis->hwRxPos - hwIndex);
    ptThis->hwRxPos = hwIndex;
    return false;
}

/**
 * @brief Validate and dispatch at most one frame candidate.
 * @param ptThis Caller-owned module instance.
 * @return True when the requested check succeeds.
 */
static bool _dwin_driver_ProcessFrame(dwin_driver_t *ptThis)
{
    uint16_t hwFullLength = 0U;
    dwin_driver_frame_t tFrame = {0};
    if (!_dwin_driver_AlignHeader(ptThis)) {
        return false;
    }
    if (ptThis->hwRxPos < 3U) {
        return false;
    }
    if ((ptThis->chRxBuf[2] == 0U)
        || (ptThis->chRxBuf[2] > DWIN_DRIVER_MAX_FRAME_LEN)) {
        ptThis->tStats.wBadLen++;
        _dwin_driver_DiscardOneByte(ptThis);
        return true;
    }
    hwFullLength = (uint16_t)(3U + ptThis->chRxBuf[2]);
    if (ptThis->hwRxPos < hwFullLength) {
        return false;
    }
    if (!_dwin_driver_ValidateFrame(ptThis, &tFrame)) {
        ptThis->tStats.wBadLen++;
        _dwin_driver_DiscardOneByte(ptThis);
        return true;
    }
    ptThis->tStats.wValid++;
    if (tFrame.bDispatch && (ptThis->fnOnReceive != NULL)) {
        /* View expires when the receive buffer is compacted below. */
        ptThis->fnOnReceive(ptThis->pCallbackContext, &tFrame);
    }
    (void)memmove(ptThis->chRxBuf, &ptThis->chRxBuf[hwFullLength],
                  ptThis->hwRxPos - hwFullLength);
    ptThis->hwRxPos = (uint16_t)(ptThis->hwRxPos - hwFullLength);
    ptThis->lRxDeadline = 0;
    return true;
}

/**
 * @brief Validate timeout policy and reset per-instance parser state.
 * @param ptThis Caller-owned module instance.
 * @param ptCfg Initialization policy in caller-facing units.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_Init(
    dwin_driver_t *ptThis,
    const dwin_driver_cfg_t *ptCfg)
{
    if (ptThis == NULL) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    (void)memset(ptThis, 0, sizeof(*ptThis));
    ptThis->eState = DWIN_DRIVER_STATE_UNINITIALIZED;
    if (ptCfg == NULL) {
        ptThis->eState = DWIN_DRIVER_STATE_ERROR;
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }

    if (ptCfg->wFrameTimeoutMs == 0U) {
        ptThis->eState = DWIN_DRIVER_STATE_ERROR;
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    ptThis->wFrameTimeoutMs = ptCfg->wFrameTimeoutMs;
    ptThis->eState = DWIN_DRIVER_STATE_READY;
    return DWIN_DRIVER_RESULT_OK;
}

/**
 * @brief Receive bytes and process a bounded number of candidates.
 * @param ptThis Caller-owned module instance.
 * @return Module result code; OK on success, otherwise the failure cause.
 * @note Foreground-only; callbacks must not reenter this parser.
 */
dwin_driver_result_t dwin_driver_Run(dwin_driver_t *ptThis)
{
    uint32_t wRead = 0U;
    mdi_status_t eStatus = MDI_OK;
    uint16_t hwFramesDone = 0U;
    bool bExpired = false;
    if (ptThis == NULL) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    if (!_dwin_driver_IsReady(ptThis)) {
        return (ptThis->eState == DWIN_DRIVER_STATE_UNINITIALIZED)
             ? DWIN_DRIVER_RESULT_NOT_READY : DWIN_DRIVER_RESULT_FAULT;
    }
    eStatus = MDI_STREAM_Read(dwin_stream,
                            &ptThis->chRxBuf[ptThis->hwRxPos],
                            sizeof(ptThis->chRxBuf) - ptThis->hwRxPos,
                            &wRead);
    if (eStatus == MDI_OVERRUN) {
        /* The loss position is unknown: never join bytes across the gap. */
        ptThis->tStats.wOverrun++;
        ptThis->tStats.wDropped += ptThis->hwRxPos + wRead;
        ptThis->hwRxPos = 0U;
        ptThis->lRxDeadline = 0;
        ptThis->bFrameInProgress = false;
        return DWIN_DRIVER_RESULT_OK;
    }
    if ((eStatus != MDI_OK)
        || (wRead > sizeof(ptThis->chRxBuf) - ptThis->hwRxPos)) {
        ptThis->eState = DWIN_DRIVER_STATE_ERROR;
        return DWIN_DRIVER_RESULT_IO;
    }
    if (wRead > 0U) {
        ptThis->hwRxPos = (uint16_t)(ptThis->hwRxPos + (uint16_t)wRead);
        ptThis->lRxDeadline = 0;
    } else if (ptThis->bFrameInProgress) {
        bExpired = perfc_is_time_out_ms(ptThis->wFrameTimeoutMs,
                                       &ptThis->lRxDeadline, false);
        if (bExpired) {
            ptThis->tStats.wTimeout++;
            ptThis->tStats.wDropped += ptThis->hwRxPos;
            ptThis->hwRxPos = 0U;
            ptThis->lRxDeadline = 0;
        }
    } else {
        /* No partial frame owns a deadline. */
    }
    while ((ptThis->hwRxPos >= 2U)
           && (hwFramesDone < DWIN_DRIVER_MAX_FRAMES_PER_RUN)) {
        if (!_dwin_driver_ProcessFrame(ptThis)) {
            break;
        }
        hwFramesDone++;
    }
    /* The run budget must not cause complete queued frames to time out. */
    ptThis->bFrameInProgress = (ptThis->hwRxPos > 0U)
                           && (hwFramesDone < DWIN_DRIVER_MAX_FRAMES_PER_RUN);
    if (ptThis->bFrameInProgress) {
        bExpired = perfc_is_time_out_ms(ptThis->wFrameTimeoutMs,
                                       &ptThis->lRxDeadline, false);
        if (bExpired) {
            ptThis->lRxDeadline = 0;
        }
    } else {
        ptThis->lRxDeadline = 0;
    }
    return DWIN_DRIVER_RESULT_OK;
}

/**
 * @brief Bind the consumer and its explicit instance context.
 * @param ptThis Caller-owned module instance.
 * @param fnCallback Consumer callback, or NULL to disable dispatch.
 * @param pContext Consumer instance context.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_RegisterCallback(
    dwin_driver_t *ptThis,
    dwin_driver_receive_fn_t fnCallback,
    void *pContext)
{
    if (ptThis == NULL) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    ptThis->fnOnReceive = fnCallback;
    ptThis->pCallbackContext = pContext;
    return DWIN_DRIVER_RESULT_OK;
}

/**
 * @brief Copy protocol counters into a read-only snapshot.
 * @param ptThis Caller-owned module instance.
 * @param ptStats Destination counter snapshot.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_GetStats(
    const dwin_driver_t *ptThis,
    dwin_driver_stats_t *ptStats)
{
    if ((ptThis == NULL) || (ptStats == NULL)) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    *ptStats = ptThis->tStats;
    return DWIN_DRIVER_RESULT_OK;
}

/**
 * @brief Submit the DGUS page-switch control value.
 * @param ptThis Caller-owned module instance.
 * @param hwPageId DGUS page identifier.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_SetPage(
    dwin_driver_t *ptThis,
    uint16_t hwPageId)
{
    const uint8_t chData[4] = {
        0x5AU, 0x01U, (uint8_t)(hwPageId >> 8U), (uint8_t)hwPageId};

    if (!_dwin_driver_IsReady(ptThis)) {
        return (ptThis == NULL) ? DWIN_DRIVER_RESULT_NULL
             : DWIN_DRIVER_RESULT_NOT_READY;
    }
    return _dwin_driver_SendWrite(ptThis, DWIN_DRIVER_PAGE_ADDRESS,
                                  chData, sizeof(chData));
}

/**
 * @brief Encode host words as big-endian DGUS variable data.
 * @param ptThis Caller-owned module instance.
 * @param hwAddress DGUS variable address.
 * @param phwData Host-order 16-bit words.
 * @param hwLength Word count, or byte count for CRC input.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_SetN16Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint16_t *phwData,
    uint16_t hwLength)
{
    uint16_t hwIndex = 0U;
    uint8_t chData[DWIN_DRIVER_MAX_FRAME_LEN - 3U] = {0};
    uint16_t hwDataBytes = 0U;

    if ((ptThis == NULL) || (phwData == NULL)) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    if (!_dwin_driver_IsReady(ptThis)) {
        return DWIN_DRIVER_RESULT_NOT_READY;
    }
    if ((hwLength == 0U)
        || (hwLength > (uint16_t)((DWIN_DRIVER_MAX_FRAME_LEN - 3U) / 2U))) {
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    hwDataBytes = (uint16_t)(hwLength * 2U);

    for (hwIndex = 0U; hwIndex < hwLength; hwIndex++) {
        chData[hwIndex * 2U] = (uint8_t)(phwData[hwIndex] >> 8U);
        chData[hwIndex * 2U + 1U] = (uint8_t)phwData[hwIndex];
    }
    return _dwin_driver_SendWrite(ptThis, hwAddress, chData, hwDataBytes);
}

/**
 * @brief Encode host double words as big-endian DGUS data.
 * @param ptThis Caller-owned module instance.
 * @param hwAddress DGUS variable address.
 * @param pwData Host-order 32-bit words.
 * @param hwLength Word count, or byte count for CRC input.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_SetN32Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint32_t *pwData,
    uint16_t hwLength)
{
    uint16_t hwIndex = 0U;
    uint8_t chData[DWIN_DRIVER_MAX_FRAME_LEN - 3U] = {0};
    uint16_t hwDataBytes = 0U;

    if ((ptThis == NULL) || (pwData == NULL)) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    if (!_dwin_driver_IsReady(ptThis)) {
        return DWIN_DRIVER_RESULT_NOT_READY;
    }
    if ((hwLength == 0U)
        || (hwLength > (uint16_t)((DWIN_DRIVER_MAX_FRAME_LEN - 3U) / 4U))) {
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    hwDataBytes = (uint16_t)(hwLength * 4U);

    for (hwIndex = 0U; hwIndex < hwLength; hwIndex++) {
        uint32_t wValue = pwData[hwIndex];
        chData[hwIndex * 4U] = (uint8_t)(wValue >> 24U);
        chData[hwIndex * 4U + 1U] = (uint8_t)(wValue >> 16U);
        chData[hwIndex * 4U + 2U] = (uint8_t)(wValue >> 8U);
        chData[hwIndex * 4U + 3U] = (uint8_t)wValue;
    }
    return _dwin_driver_SendWrite(ptThis, hwAddress, chData, hwDataBytes);
}

/**
 * @brief Request a nonzero count of DGUS variable words.
 * @param ptThis Caller-owned module instance.
 * @param hwAddress DGUS variable address.
 * @param chLength Requested word count, nonzero.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_ReadN16Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    uint8_t chLength)
{
    if (!_dwin_driver_IsReady(ptThis)) {
        return (ptThis == NULL) ? DWIN_DRIVER_RESULT_NULL
             : DWIN_DRIVER_RESULT_NOT_READY;
    }
    if (chLength == 0U) {
        return DWIN_DRIVER_RESULT_INVALID_ARGUMENT;
    }
    return _dwin_driver_SendRead(ptThis, hwAddress, chLength);
}

/**
 * @brief Submit the DGUS reset control sequence.
 * @param ptThis Caller-owned module instance.
 * @return Module result code; OK on success, otherwise the failure cause.
 */
dwin_driver_result_t dwin_driver_Reset(dwin_driver_t *ptThis)
{
    const uint8_t chData[4] = {0x55U, 0xAAU, 0x5AU, 0xA5U};

    if (!_dwin_driver_IsReady(ptThis)) {
        return (ptThis == NULL) ? DWIN_DRIVER_RESULT_NULL
             : DWIN_DRIVER_RESULT_NOT_READY;
    }
    return _dwin_driver_SendWrite(ptThis, DWIN_DRIVER_RESET_ADDRESS,
                                  chData, sizeof(chData));
}

