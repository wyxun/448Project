/**
 * @file board_protocol_test.c
 * @brief Protocol wire format, backpressure and resynchronization checks.
 * @author Modus project
 * @date 2026-10-29
 */
#include "board_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    board_protocol_t tx;
    board_protocol_t rx;
    board_protocol_message_t message = {0};
    board_protocol_message_t received = {0};
    const uint8_t stop[] = {0xA5,0x5A,1,2,7,0,0xA2,0x28};
    const board_protocol_type_t types[] = {
        BOARD_PROTOCOL_START, BOARD_PROTOCOL_STOP, BOARD_PROTOCOL_ENERGY,
        BOARD_PROTOCOL_REPLY, BOARD_PROTOCOL_FAULT,
    };
    assert(board_protocol_Init(&tx));
    assert(board_protocol_Init(&rx));
    message.eType = BOARD_PROTOCOL_STOP;
    message.chSequence = 7U;
    assert(board_protocol_Queue(&tx, &message));
    assert(tx.chTxLength == sizeof(stop));
    assert(memcmp(tx.achTx, stop, sizeof(stop)) == 0);
    assert(!board_protocol_Queue(&tx, &message));
    for (unsigned t = 0U; t < sizeof(types)/sizeof(types[0]); ++t) {
        tx.chTxOffset = tx.chTxLength;
        message.eType = types[t];
        message.eHead = BOARD_PROTOCOL_HEAD_RES;
        message.hwEnergyPermille = 1000U;
        message.eCommand = BOARD_PROTOCOL_START;
        message.eResult = BOARD_PROTOCOL_BUSY;
        message.hwFaultMask = 3U;
        assert(board_protocol_Queue(&tx, &message));
        /* Noise and overlapping header prefixes must not lose the frame. */
        const uint8_t noise[] = {0U, 0xA5U};
        assert(board_protocol_Receive(&rx, noise, sizeof(noise))
            == sizeof(noise));
        assert(!board_protocol_Read(&rx, &received));
        for (unsigned i = 0U; i < tx.chTxLength; ++i) {
            assert(board_protocol_Receive(&rx, &tx.achTx[i], 1U) == 1U);
            bool ready = board_protocol_Read(&rx, &received);
            assert(ready == (i == (unsigned)tx.chTxLength - 1U));
        }
        assert(received.eType == message.eType);
        assert(received.chSequence == 7U);
        if (t == 0U || t == 2U) {
            assert(received.eHead == BOARD_PROTOCOL_HEAD_RES);
            assert(received.hwEnergyPermille == 1000U);
        }
    }
    /* Corrupt CRC, then valid frame: only the valid frame is delivered. */
    for (unsigned i = 0U; i < sizeof(stop); ++i) {
        uint8_t byte = stop[i];
        if (i == 7U) { byte ^= 1U; }
        assert(board_protocol_Receive(&rx, &byte, 1U) == 1U);
        assert(!board_protocol_Read(&rx, &received));
    }
    for (unsigned i = 0U; i < sizeof(stop); ++i) {
        assert(board_protocol_Receive(&rx, &stop[i], 1U) == 1U);
        bool ready = board_protocol_Read(&rx, &received);
        assert(ready == (i == sizeof(stop) - 1U));
    }
    assert(rx.wRxRejected != 0U);
    tx.chTxOffset = tx.chTxLength;
    message.eType = BOARD_PROTOCOL_START;
    message.hwEnergyPermille = 1001U;
    assert(!board_protocol_Queue(&tx, &message));
    /* Batched sticky frames, partial frames and repeated physical wrap. */
    assert(board_protocol_Init(&rx));
    for (unsigned round = 0U; round < 40U; ++round) {
        for (unsigned frame = 0U; frame < 5U; ++frame) {
            assert(board_protocol_Receive(&rx, stop, sizeof(stop))
                == sizeof(stop));
        }
        for (unsigned frame = 0U; frame < 5U; ++frame) {
            assert(board_protocol_Read(&rx, &message));
            assert(message.eType == BOARD_PROTOCOL_STOP);
            assert(message.chSequence == 7U);
        }
        assert(!board_protocol_Read(&rx, &message));
    }
    assert(board_protocol_Receive(&rx, stop, 3U) == 3U);
    assert(!board_protocol_Read(&rx, &message));
    assert(mringbuf_GetUsed(&rx.tRxRing) == 3U);
    assert(board_protocol_Receive(&rx, &stop[3], 5U) == 5U);
    assert(board_protocol_Read(&rx, &message));
    /* Full ring rejects new bytes without overwriting unread data. */
    {
        uint8_t noise[128] = {0};
        assert(board_protocol_Receive(&rx, noise, sizeof(noise)) == 127U);
        assert(board_protocol_Receive(&rx, stop, sizeof(stop)) == 0U);
        assert(!board_protocol_Read(&rx, &message));
        assert(board_protocol_Receive(&rx, stop, sizeof(stop)) == sizeof(stop));
        assert(board_protocol_Read(&rx, &message));
    }
    /* Transaction matching, duplicate replies, busy and sequence wrap. */
    assert(board_protocol_Init(&tx));
    for (unsigned i = 0U; i < 257U; ++i) {
        board_protocol_message_t reply = {0};
        message.eType = BOARD_PROTOCOL_STOP;
        assert(board_protocol_Request(&tx, &message));
        assert(board_protocol_IsWaiting(&tx));
        assert(!board_protocol_Request(&tx, &message));
        assert(board_protocol_Receive(&rx, tx.achTx, tx.chTxLength)
            == tx.chTxLength);
        assert(board_protocol_Read(&rx, &received));
        assert(received.chSequence == (uint8_t)(i + 1U));
        reply.eType = BOARD_PROTOCOL_REPLY;
        reply.eCommand = BOARD_PROTOCOL_START;
        reply.chSequence = received.chSequence;
        assert(!board_protocol_AcceptReply(&tx, &reply));
        reply.eCommand = BOARD_PROTOCOL_STOP;
        reply.chSequence++;
        assert(!board_protocol_AcceptReply(&tx, &reply));
        assert(board_protocol_IsWaiting(&tx));
        reply.chSequence = received.chSequence;
        assert(board_protocol_AcceptReply(&tx, &reply));
        assert(!board_protocol_AcceptReply(&tx, &reply));
        assert(!board_protocol_IsWaiting(&tx));
        /* TX busy rejects a new request without entering the wait state. */
        assert(!board_protocol_Request(&tx, &message));
        assert(!board_protocol_IsWaiting(&tx));
        tx.chTxOffset = tx.chTxLength;
    }
    assert(board_protocol_Request(&tx, &message));
    board_protocol_CancelRequest(&tx);
    assert(!board_protocol_IsWaiting(&tx));
    puts("board protocol: PASS");
    return 0;
}

