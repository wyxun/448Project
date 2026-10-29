/**
 * @file board_protocol.h
 * @brief Board-link messages and incremental frame codec.
 * @author Modus project
 * @date 2026-10-29
 */
#ifndef M448_BOARD_PROTOCOL_H
#define M448_BOARD_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include "utilities/mringbuf.h"

#define BOARD_PROTOCOL_RX_CAPACITY 128U
#define BOARD_PROTOCOL_MAX_PAYLOAD 16U
#define BOARD_PROTOCOL_MAX_FRAME (BOARD_PROTOCOL_MAX_PAYLOAD + 8U)

typedef enum {
    BOARD_PROTOCOL_START = 0x01,
    BOARD_PROTOCOL_STOP = 0x02,
    BOARD_PROTOCOL_ENERGY = 0x03,
    BOARD_PROTOCOL_REPLY = 0x81,
    BOARD_PROTOCOL_FAULT = 0x82,
} board_protocol_type_t;

typedef enum {
    BOARD_PROTOCOL_HEAD_CAP = 0,
    BOARD_PROTOCOL_HEAD_RES = 1,
} board_protocol_head_t;

typedef enum {
    BOARD_PROTOCOL_ACCEPTED = 0,
    BOARD_PROTOCOL_INVALID = 1,
    BOARD_PROTOCOL_NOT_READY = 2,
    BOARD_PROTOCOL_BUSY = 3,
    BOARD_PROTOCOL_IO = 4,
    BOARD_PROTOCOL_DRIVER_FAULT = 5,
} board_protocol_result_t;

/** Wire values are encoded explicitly; never transmit this struct directly. */
typedef struct {
    board_protocol_type_t eType;
    uint8_t chSequence;
    board_protocol_head_t eHead;
    uint16_t hwEnergyPermille;
    board_protocol_type_t eCommand;
    board_protocol_result_t eResult;
    uint16_t hwFaultMask;
} board_protocol_message_t;

/** One outstanding command; sequence survives cancellation and completion. */
typedef struct {
    uint8_t chSequence;
    bool bWaiting;
    board_protocol_type_t eCommand;
} board_protocol_request_t;

/** Owned by MVC; foreground-only, one pending TX frame at a time. */
typedef struct {
    board_protocol_request_t tRequest;
    mringbuf_t tRxRing;
    uint8_t achRxStorage[BOARD_PROTOCOL_RX_CAPACITY];
    uint8_t achTx[BOARD_PROTOCOL_MAX_FRAME];
    uint8_t chTxLength;
    uint8_t chTxOffset;
    uint32_t wRxFrames;
    uint32_t wRxRejected;
} board_protocol_t;

/** @brief Reset codec storage. @param ptThis Valid object. */
bool board_protocol_Init(board_protocol_t *ptThis);

/** @brief Buffer bytes; returns accepted count. Caller retains any tail. */
uint16_t board_protocol_Receive(board_protocol_t *ptThis,
    const uint8_t *pchData, uint16_t hwLength);

/** @brief Pop one validated message; incomplete frames survive the call. */
bool board_protocol_Read(board_protocol_t *ptThis,
    board_protocol_message_t *ptMessage);

/**
 * @brief Encode one message if the previous TX frame has been consumed.
 * @param ptThis Initialized codec.
 * @param ptMessage Message including explicit command/reply sequence.
 * @return true when queued; false on busy or invalid fields.
 * @note Transport writes achTx from chTxOffset and advances that offset by
 * its actual write count. Reaching chTxLength makes Queue available again.
 */
bool board_protocol_Queue(board_protocol_t *ptThis,
                          const board_protocol_message_t *ptMessage);

/** @brief Queue a command with an automatic sequence; reject while waiting.
 * @param ptThis Initialized protocol, foreground only.
 * @param ptMessage START/STOP/ENERGY fields; caller sequence is ignored.
 * @return true when queued, false if invalid, waiting or TX busy.
 */
bool board_protocol_Request(board_protocol_t *ptThis,
    const board_protocol_message_t *ptMessage);

/** @brief Report whether a command is awaiting a matching reply. */
bool board_protocol_IsWaiting(const board_protocol_t *ptThis);

/** @brief Complete a request only for its matching sequence and command.
 * @return true for a matched reply, including a rejection result.
 */
bool board_protocol_AcceptReply(board_protocol_t *ptThis,
    const board_protocol_message_t *ptMessage);

/** @brief End the wait after timeout or fault; queued TX bytes are retained. */
void board_protocol_CancelRequest(board_protocol_t *ptThis);

#endif
