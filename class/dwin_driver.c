/**
 * @file dwin_driver.c
 * @brief DWIN DGUS protocol driver over an MDI stream.
 */

#include "dwin_driver.h"

#include "mdi/instance.h"

#include <stddef.h>
#include <string.h>

#define DWIN_DRIVER_RESET_ADDRESS 0x0004U
#define DWIN_DRIVER_PAGE_ADDRESS  0x0084U

/* The public configuration remains in milliseconds.  Conversion is done
 * once at initialization with the board's compile-time raw-tick frequency;
 * the receive path only performs unsigned tick subtraction. */
#define DWIN_DRIVER_TICKS_PER_MS (AT32F415_CORE_CLOCK_HZ / 1000UL)

static uint16_t _dwin_driver_Crc16(const uint8_t *pchData, uint16_t hwLength)
{
    uint16_t hwCrc = 0xFFFFU;

    while (hwLength > 0U) {
        uint8_t chBit;
        hwCrc ^= *pchData++;
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

static bool _dwin_driver_IsReady(const dwin_driver_t *ptThis)
{
    return (ptThis != NULL)
        && (ptThis->eState == DWIN_DRIVER_STATE_READY);
}

static dwin_driver_result_t _dwin_driver_SendWrite(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint8_t *pchData,
    uint16_t hwDataLength)
{
    uint16_t hwIndex = 0U;
    uint16_t hwPayloadLength;
    uint16_t hwFrameLength;
    uint16_t hwCrc;

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

    ptThis->chTxBuf[hwIndex++] = DWIN_DRIVER_FRAME_HEAD0;
    ptThis->chTxBuf[hwIndex++] = DWIN_DRIVER_FRAME_HEAD1;
    ptThis->chTxBuf[hwIndex++] = (uint8_t)hwPayloadLength;
    ptThis->chTxBuf[hwIndex++] = DWIN_DRIVER_CMD_WRITE;
    ptThis->chTxBuf[hwIndex++] = (uint8_t)(hwAddress >> 8U);
    ptThis->chTxBuf[hwIndex++] = (uint8_t)hwAddress;
    if (hwDataLength > 0U) {
        (void)memcpy(&ptThis->chTxBuf[hwIndex], pchData, hwDataLength);
        hwIndex = (uint16_t)(hwIndex + hwDataLength);
    }

#if DWIN_DRIVER_CRC16_EN
    hwCrc = _dwin_driver_Crc16(&ptThis->chTxBuf[3],
                               (uint16_t)(3U + hwDataLength));
    ptThis->chTxBuf[2] = (uint8_t)(hwPayloadLength + 2U);
    ptThis->chTxBuf[hwIndex++] = (uint8_t)hwCrc;
    ptThis->chTxBuf[hwIndex++] = (uint8_t)(hwCrc >> 8U);
#endif

    return (MDI_STREAM_Write(dwin_stream, ptThis->chTxBuf, hwIndex)
            == (int32_t)hwIndex)
         ? DWIN_DRIVER_RESULT_OK : DWIN_DRIVER_RESULT_IO;
}

static dwin_driver_result_t _dwin_driver_SendRead(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    uint8_t chLength)
{
    uint16_t hwIndex = 0U;
    uint16_t hwPayloadLength = 4U;
    uint16_t hwCrc;

    if (ptThis == NULL) {
        return DWIN_DRIVER_RESULT_NULL;
    }

    ptThis->chTxBuf[hwIndex++] = DWIN_DRIVER_FRAME_HEAD0;
    ptThis->chTxBuf[hwIndex++] = DWIN_DRIVER_FRAME_HEAD1;
    ptThis->chTxBuf[hwIndex++] = (uint8_t)hwPayloadLength;
    ptThis->chTxBuf[hwIndex++] = DWIN_DRIVER_CMD_READ;
    ptThis->chTxBuf[hwIndex++] = (uint8_t)(hwAddress >> 8U);
    ptThis->chTxBuf[hwIndex++] = (uint8_t)hwAddress;
    ptThis->chTxBuf[hwIndex++] = chLength;

#if DWIN_DRIVER_CRC16_EN
    hwCrc = _dwin_driver_Crc16(&ptThis->chTxBuf[3], 4U);
    ptThis->chTxBuf[2] = (uint8_t)(hwPayloadLength + 2U);
    ptThis->chTxBuf[hwIndex++] = (uint8_t)hwCrc;
    ptThis->chTxBuf[hwIndex++] = (uint8_t)(hwCrc >> 8U);
#endif

    return (MDI_STREAM_Write(dwin_stream, ptThis->chTxBuf, hwIndex)
            == (int32_t)hwIndex)
         ? DWIN_DRIVER_RESULT_OK : DWIN_DRIVER_RESULT_IO;
}

static void _dwin_driver_DiscardOneByte(dwin_driver_t *ptThis)
{
    if (ptThis->hwRxPos > 0U) {
        (void)memmove(ptThis->chRxBuf, &ptThis->chRxBuf[1],
                      ptThis->hwRxPos - 1U);
        ptThis->hwRxPos--;
        ptThis->tStats.wDropped++;
    }
}

static bool _dwin_driver_ValidateFrame(
    dwin_driver_t *ptThis,
    uint8_t *pchCmd,
    uint16_t *phwAddress,
    uint8_t **ppchData,
    uint16_t *phwDataLength,
    bool *pbDispatch)
{
    uint8_t chPayloadLength;
    uint16_t hwFullFrameLength;
    uint16_t hwDataLength = 0U;
    uint16_t hwAddress;
    uint8_t chCmd;

    chPayloadLength = ptThis->chRxBuf[2];
    if ((chPayloadLength == 0U)
        || (chPayloadLength > DWIN_DRIVER_MAX_FRAME_LEN)) {
        return false;
    }

    hwFullFrameLength = (uint16_t)(3U + chPayloadLength);
    if (ptThis->hwRxPos < hwFullFrameLength) {
        ptThis->bFrameInProgress = true;
        return false;
    }

    chCmd = ptThis->chRxBuf[3];
    hwAddress = (uint16_t)(((uint16_t)ptThis->chRxBuf[4] << 8U)
                           | ptThis->chRxBuf[5]);
    *pbDispatch = false;

#if DWIN_DRIVER_CRC16_EN
    if (chPayloadLength < 5U) {
        return false;
    }
    {
        uint16_t hwCrcRx = (uint16_t)ptThis->chRxBuf[hwFullFrameLength - 2U]
                         | ((uint16_t)ptThis->chRxBuf[hwFullFrameLength - 1U]
                            << 8U);
        uint16_t hwCrc = _dwin_driver_Crc16(&ptThis->chRxBuf[3],
                                            (uint16_t)(chPayloadLength - 2U));
        if (hwCrc != hwCrcRx) {
            return false;
        }
    }
    hwDataLength = (uint16_t)(chPayloadLength - 5U);
    if (chCmd == DWIN_DRIVER_CMD_READ) {
        uint16_t hwCount;
        if (hwDataLength < 1U) {
            return false;
        }
        hwCount = ptThis->chRxBuf[6];
        if (hwDataLength != (uint16_t)(1U + 2U * hwCount)) {
            return false;
        }
        *pbDispatch = true;
    } else if (chCmd == DWIN_DRIVER_CMD_WRITE) {
        if (hwDataLength == 0U) {
            *pbDispatch = false;
        } else if ((hwDataLength & 1U) == 0U) {
            *pbDispatch = true;
        } else {
            return false;
        }
    } else {
        return false;
    }
#else
    if (chCmd == DWIN_DRIVER_CMD_READ) {
        uint16_t hwCount;
        if (chPayloadLength < 4U) {
            return false;
        }
        hwCount = ptThis->chRxBuf[6];
        if (chPayloadLength != (uint16_t)(4U + 2U * hwCount)) {
            return false;
        }
        hwDataLength = (uint16_t)(chPayloadLength - 3U);
        *pbDispatch = true;
    } else if (chCmd == DWIN_DRIVER_CMD_WRITE) {
        if (chPayloadLength == 3U) {
            *pbDispatch = false;
            hwDataLength = 0U;
        } else if ((chPayloadLength >= 5U)
                   && (((chPayloadLength - 3U) & 1U) == 0U)) {
            hwDataLength = (uint16_t)(chPayloadLength - 3U);
            *pbDispatch = true;
        } else {
            return false;
        }
    } else {
        return false;
    }
#endif

    *pchCmd = chCmd;
    *phwAddress = hwAddress;
    *ppchData = &ptThis->chRxBuf[6];
    *phwDataLength = hwDataLength;
    ptThis->bFrameInProgress = false;
    return true;
}

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
    ptThis->wFrameTimeoutTick =
        (uint64_t)ptCfg->wFrameTimeoutMs * DWIN_DRIVER_TICKS_PER_MS;
    ptThis->eState = DWIN_DRIVER_STATE_READY;
    return DWIN_DRIVER_RESULT_OK;
}

dwin_driver_result_t dwin_driver_Run(dwin_driver_t *ptThis)
{
    int32_t nRead;
    uint16_t hwFramesDone = 0U;

    if (ptThis == NULL) {
        return DWIN_DRIVER_RESULT_NULL;
    }
    if (!_dwin_driver_IsReady(ptThis)) {
        return (ptThis->eState == DWIN_DRIVER_STATE_UNINITIALIZED)
             ? DWIN_DRIVER_RESULT_NOT_READY : DWIN_DRIVER_RESULT_FAULT;
    }

    nRead = MDI_STREAM_Read(dwin_stream,
                            &ptThis->chRxBuf[ptThis->hwRxPos],
                            sizeof(ptThis->chRxBuf) - ptThis->hwRxPos);
    if (nRead < 0) {
        ptThis->eState = DWIN_DRIVER_STATE_ERROR;
        return DWIN_DRIVER_RESULT_IO;
    }
    if (nRead > 0) {
        ptThis->hwRxPos = (uint16_t)(ptThis->hwRxPos + (uint16_t)nRead);
    }

    /* One MDI_TICK_Elapsed expansion owns the baseline.  A received byte
     * reloads that baseline with a zero limit; only an idle partial frame is
     * allowed to expire against the configured timeout. */
    if (ptThis->bFrameInProgress) {
        uint64_t wTimeout = (nRead > 0)
                          ? 0U : ptThis->wFrameTimeoutTick;
        bool bTickElapsed = MDI_TICK_Elapsed(raw_tick, wTimeout);
        if ((nRead == 0) && bTickElapsed) {
            ptThis->tStats.wTimeout++;
            ptThis->tStats.wDropped += ptThis->hwRxPos;
            ptThis->hwRxPos = 0U;
            ptThis->bFrameInProgress = false;
        }
    }

    while ((ptThis->hwRxPos >= 3U)
           && (hwFramesDone < DWIN_DRIVER_MAX_FRAMES_PER_RUN)) {
        uint16_t hwScanIndex;
        bool bFoundHeader = false;
        uint8_t chCmd;
        uint16_t hwAddress;
        uint8_t *pchData;
        uint16_t hwDataLength;
        bool bDispatch;
        uint16_t hwFullFrameLength;

        for (hwScanIndex = 0U; hwScanIndex + 1U < ptThis->hwRxPos;
             hwScanIndex++) {
            if ((ptThis->chRxBuf[hwScanIndex]
                 == DWIN_DRIVER_FRAME_HEAD0)
                && (ptThis->chRxBuf[hwScanIndex + 1U]
                    == DWIN_DRIVER_FRAME_HEAD1)) {
                bFoundHeader = true;
                break;
            }
        }
        if (!bFoundHeader) {
            ptThis->chRxBuf[0] = ptThis->chRxBuf[ptThis->hwRxPos - 1U];
            ptThis->hwRxPos = 1U;
            ptThis->tStats.wDropped++;
            break;
        }
        if (hwScanIndex > 0U) {
            ptThis->tStats.wDropped += hwScanIndex;
            (void)memmove(ptThis->chRxBuf,
                          &ptThis->chRxBuf[hwScanIndex],
                          ptThis->hwRxPos - hwScanIndex);
            ptThis->hwRxPos = (uint16_t)(ptThis->hwRxPos - hwScanIndex);
        }

        if ((ptThis->chRxBuf[2] == 0U)
            || (ptThis->chRxBuf[2] > DWIN_DRIVER_MAX_FRAME_LEN)) {
            ptThis->tStats.wBadLen++;
            _dwin_driver_DiscardOneByte(ptThis);
            hwFramesDone++;
            continue;
        }
        hwFullFrameLength = (uint16_t)(3U + ptThis->chRxBuf[2]);
        if (ptThis->hwRxPos < hwFullFrameLength) {
            ptThis->bFrameInProgress = true;
            break;
        }
        if (!_dwin_driver_ValidateFrame(ptThis, &chCmd, &hwAddress,
                                         &pchData, &hwDataLength,
                                         &bDispatch)) {
            ptThis->tStats.wBadLen++;
            _dwin_driver_DiscardOneByte(ptThis);
            hwFramesDone++;
            continue;
        }

        ptThis->tStats.wValid++;
        if (bDispatch && ptThis->fnOnReceive != NULL) {
            ptThis->fnOnReceive(ptThis->pCallbackContext, chCmd, hwAddress,
                                pchData, hwDataLength);
        }
        (void)memmove(ptThis->chRxBuf,
                      &ptThis->chRxBuf[hwFullFrameLength],
                      ptThis->hwRxPos - hwFullFrameLength);
        ptThis->hwRxPos = (uint16_t)(ptThis->hwRxPos - hwFullFrameLength);
        hwFramesDone++;
    }

    return DWIN_DRIVER_RESULT_OK;
}

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

dwin_driver_result_t dwin_driver_SetN16Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint16_t *phwData,
    uint16_t hwLength)
{
    uint16_t hwIndex;
    uint8_t chData[DWIN_DRIVER_MAX_FRAME_LEN - 3U];
    uint16_t hwDataBytes;

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

dwin_driver_result_t dwin_driver_SetN32Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint32_t *pwData,
    uint16_t hwLength)
{
    uint16_t hwIndex;
    uint8_t chData[DWIN_DRIVER_MAX_FRAME_LEN - 3U];
    uint16_t hwDataBytes;

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

