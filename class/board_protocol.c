/**
 * @file board_protocol.c
 * @brief Board-link framing, CRC and explicit payload encoding.
 * @author Modus project
 * @date 2026-10-29
 */
#include "board_protocol.h"

#include <string.h>

/** @brief CRC-16/MODBUS over version through payload. */
static uint16_t _board_protocol_Crc(const uint8_t *pchData, uint8_t chLength)
{
    uint16_t hwCrc = 0xFFFFU;
    for (uint8_t i = 0U; i < chLength; ++i) {
        hwCrc ^= pchData[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            hwCrc = (uint16_t)((hwCrc >> 1)
                ^ ((hwCrc & 1U) != 0U ? 0xA001U : 0U));
        }
    }
    return hwCrc;
}

/** @brief Validate fields and report the payload length, or -1. */
static int _board_protocol_Length(const board_protocol_message_t *ptMessage)
{
    switch (ptMessage->eType) {
    case BOARD_PROTOCOL_START:
    case BOARD_PROTOCOL_ENERGY:
        return (((unsigned)ptMessage->eHead <= BOARD_PROTOCOL_HEAD_RES)
            && (ptMessage->hwEnergyPermille <= 1000U)) ? 3 : -1;
    case BOARD_PROTOCOL_STOP:
    case BOARD_PROTOCOL_READ_ADC:
        return 0;
    case BOARD_PROTOCOL_ADC_DATA:
        return 15;
    case BOARD_PROTOCOL_REPLY:
        return ((ptMessage->eCommand >= BOARD_PROTOCOL_START)
            && (ptMessage->eCommand <= BOARD_PROTOCOL_READ_ADC)
            && ((unsigned)ptMessage->eResult <= BOARD_PROTOCOL_DRIVER_FAULT))
            ? 2 : -1;
    case BOARD_PROTOCOL_FAULT:
        return 2;
    default:
        return -1;
    }
}

/** @brief Read a little-endian word without alignment assumptions. */
static uint16_t _board_protocol_Read16(const uint8_t *pchData)
{
    return (uint16_t)((uint16_t)pchData[0] | ((uint16_t)pchData[1] << 8));
}

/** @brief Write a little-endian word without struct packing. */
static void _board_protocol_Write16(uint8_t *pchData, uint16_t hwValue)
{
    pchData[0] = (uint8_t)hwValue;
    pchData[1] = (uint8_t)(hwValue >> 8);
}

/** @copydoc board_protocol_Init */
bool board_protocol_Init(board_protocol_t *ptThis)
{
    (void)memset(ptThis, 0, sizeof(*ptThis));
    return mringbuf_Init(&ptThis->tRxRing, ptThis->achRxStorage,
                         BOARD_PROTOCOL_RX_CAPACITY) == 0;
}

/** @copydoc board_protocol_Queue */
bool board_protocol_Queue(board_protocol_t *ptThis,
                          const board_protocol_message_t *ptMessage)
{
    int nLength = _board_protocol_Length(ptMessage);
    uint8_t *pchFrame = ptThis->achTx;
    uint16_t hwCrc = 0U;

    if ((ptThis->chTxOffset < ptThis->chTxLength) || (nLength < 0)) {
        return false;
    }
    pchFrame[0] = 0xA5U;
    pchFrame[1] = 0x5AU;
    pchFrame[2] = 1U;
    pchFrame[3] = (uint8_t)ptMessage->eType;
    pchFrame[4] = ptMessage->chSequence;
    pchFrame[5] = (uint8_t)nLength;
    switch (ptMessage->eType) {
    case BOARD_PROTOCOL_START:
    case BOARD_PROTOCOL_ENERGY:
        pchFrame[6] = (uint8_t)ptMessage->eHead;
        _board_protocol_Write16(&pchFrame[7],
                                ptMessage->hwEnergyPermille);
        break;
    case BOARD_PROTOCOL_REPLY:
        pchFrame[6] = (uint8_t)ptMessage->eCommand;
        pchFrame[7] = (uint8_t)ptMessage->eResult;
        break;
    case BOARD_PROTOCOL_ADC_DATA:
        pchFrame[6] = ptMessage->chAdcValid;
        for (uint8_t i = 0U; i < 5U; ++i) {
            _board_protocol_Write16(&pchFrame[7U + i * 2U], ptMessage->ahwAdc[i]);
        }
        for (uint8_t i = 0U; i < 2U; ++i) {
            _board_protocol_Write16(&pchFrame[17U + i * 2U],
                                    (uint16_t)ptMessage->anTemperature[i]);
        }
        break;
    case BOARD_PROTOCOL_FAULT:
        _board_protocol_Write16(&pchFrame[6], ptMessage->hwFaultMask);
        break;
    default:
        break;
    }
    hwCrc = _board_protocol_Crc(&pchFrame[2], (uint8_t)(nLength + 4));
    _board_protocol_Write16(&pchFrame[6 + nLength], hwCrc);
    ptThis->chTxOffset = 0U;
    ptThis->chTxLength = (uint8_t)(nLength + 8);
    return true;
}

/** @brief Advance the ring tail without moving buffered bytes. */
static bool _board_protocol_Drop(board_protocol_t *ptThis, uint8_t chCount)
{
    return mringbuf_ReadBulk(&ptThis->tRxRing, NULL, chCount) == chCount;
}

/** @brief Decode a CRC-validated frame; leave output unchanged on failure. */
static bool _board_protocol_Decode(const uint8_t *pchFrame,
    uint8_t chLength, board_protocol_message_t *ptMessage)
{
    board_protocol_message_t tMessage = {0};
    tMessage.eType = (board_protocol_type_t)pchFrame[3];
    tMessage.chSequence = pchFrame[4];
    if ((tMessage.eType == BOARD_PROTOCOL_START)
        || (tMessage.eType == BOARD_PROTOCOL_ENERGY)) {
        if (chLength == 3U) {
            tMessage.eHead = (board_protocol_head_t)pchFrame[6];
            tMessage.hwEnergyPermille =
                _board_protocol_Read16(&pchFrame[7]);
        }
    } else if (tMessage.eType == BOARD_PROTOCOL_REPLY) {
        if (chLength == 2U) {
            tMessage.eCommand = (board_protocol_type_t)pchFrame[6];
            tMessage.eResult = (board_protocol_result_t)pchFrame[7];
        }
    } else if (tMessage.eType == BOARD_PROTOCOL_ADC_DATA) {
        if (chLength == 15U) {
            tMessage.chAdcValid = pchFrame[6];
            for (uint8_t i = 0U; i < 5U; ++i) {
                tMessage.ahwAdc[i] = _board_protocol_Read16(&pchFrame[7U + i * 2U]);
            }
            for (uint8_t i = 0U; i < 2U; ++i) {
                tMessage.anTemperature[i] = (int16_t)_board_protocol_Read16(
                    &pchFrame[17U + i * 2U]);
            }
        }
    } else if (tMessage.eType == BOARD_PROTOCOL_FAULT) {
        if (chLength == 2U) {
            tMessage.hwFaultMask = _board_protocol_Read16(&pchFrame[6]);
        }
    } else {
        /* STOP has no payload; unknown types fail Length below. */
    }
    if (_board_protocol_Length(&tMessage) != (int)chLength) {
        return false;
    }
    *ptMessage = tMessage;
    return true;
}

/** @copydoc board_protocol_Receive */
uint16_t board_protocol_Receive(board_protocol_t *ptThis,
    const uint8_t *pchData, uint16_t hwLength)
{
    return mringbuf_WriteBulk(&ptThis->tRxRing, pchData, hwLength);
}

/** @copydoc board_protocol_Read */
bool board_protocol_Read(board_protocol_t *ptThis,
    board_protocol_message_t *ptMessage)
{
    uint8_t achFrame[BOARD_PROTOCOL_MAX_FRAME] = {0};
    uint8_t chLength = 0U;
    uint8_t chFrameLength = 0U;
    uint16_t hwAvailable = 0U;
    bool bDecoded = false;
    while (true) {
        hwAvailable = mringbuf_PeekBulk(&ptThis->tRxRing, achFrame,
                                        sizeof(achFrame));
        if (hwAvailable < 2U) {
            return false;
        }
        if ((achFrame[0] != 0xA5U) || (achFrame[1] != 0x5AU)) {
            if (!_board_protocol_Drop(ptThis, 1U)) { return false; }
            continue;
        }
        if (hwAvailable < 6U) {
            return false;
        }
        chLength = achFrame[5];
        if ((achFrame[2] != 1U)
            || (chLength > BOARD_PROTOCOL_MAX_PAYLOAD)) {
            ptThis->wRxRejected++;
            if (!_board_protocol_Drop(ptThis, 1U)) { return false; }
            continue;
        }
        chFrameLength = (uint8_t)(chLength + 8U);
        if (hwAvailable < chFrameLength) {
            return false;
        }
        if (_board_protocol_Crc(&achFrame[2], (uint8_t)(chLength + 4U))
            != _board_protocol_Read16(&achFrame[6U + chLength])) {
            ptThis->wRxRejected++;
            if (!_board_protocol_Drop(ptThis, 1U)) { return false; }
            continue;
        }
        bDecoded = _board_protocol_Decode(achFrame, chLength, ptMessage);
        if (!_board_protocol_Drop(ptThis, chFrameLength)) { return false; }
        if (!bDecoded) {
            ptThis->wRxRejected++;
            continue;
        }
        ptThis->wRxFrames++;
        return true;
    }
}

/** @copydoc board_protocol_Request */
bool board_protocol_Request(board_protocol_t *ptThis,
    const board_protocol_message_t *ptMessage)
{
    board_protocol_message_t tMessage = *ptMessage;
    if (ptThis->tRequest.bWaiting
        || (tMessage.eType < BOARD_PROTOCOL_START)
        || (tMessage.eType > BOARD_PROTOCOL_READ_ADC)) {
        return false;
    }
    tMessage.chSequence = (uint8_t)(ptThis->tRequest.chSequence + 1U);
    if (!board_protocol_Queue(ptThis, &tMessage)) {
        return false;
    }
    ptThis->tRequest.chSequence = tMessage.chSequence;
    ptThis->tRequest.eCommand = tMessage.eType;
    ptThis->tRequest.bWaiting = true;
    return true;
}

/** @copydoc board_protocol_IsWaiting */
bool board_protocol_IsWaiting(const board_protocol_t *ptThis)
{
    return ptThis->tRequest.bWaiting;
}

/** @copydoc board_protocol_AcceptReply */
bool board_protocol_AcceptReply(board_protocol_t *ptThis,
    const board_protocol_message_t *ptMessage)
{
    if (!ptThis->tRequest.bWaiting
        || (ptMessage->chSequence != ptThis->tRequest.chSequence)
        || !(((ptMessage->eType == BOARD_PROTOCOL_REPLY)
              && (ptMessage->eCommand == ptThis->tRequest.eCommand))
             || ((ptMessage->eType == BOARD_PROTOCOL_ADC_DATA)
                 && (ptThis->tRequest.eCommand == BOARD_PROTOCOL_READ_ADC)))) {
        return false;
    }
    ptThis->tRequest.bWaiting = false;
    return true;
}

/** @copydoc board_protocol_CancelRequest */
void board_protocol_CancelRequest(board_protocol_t *ptThis)
{
    ptThis->tRequest.bWaiting = false;
}
