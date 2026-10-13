#ifndef BOARD_LINK_DRIVER_H
#define BOARD_LINK_DRIVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BOARD_LINK_DRIVER_STATE_UNINITIALIZED = 0,
    BOARD_LINK_DRIVER_STATE_READY,
    BOARD_LINK_DRIVER_STATE_ERROR,
} board_link_driver_state_t;

typedef enum {
    BOARD_LINK_DRIVER_RESULT_OK = 0,
    BOARD_LINK_DRIVER_RESULT_NULL,
    BOARD_LINK_DRIVER_RESULT_INVALID_ARGUMENT,
    BOARD_LINK_DRIVER_RESULT_NOT_READY,
    BOARD_LINK_DRIVER_RESULT_IO,
    BOARD_LINK_DRIVER_RESULT_FAULT,
    BOARD_LINK_DRIVER_RESULT_OVERRUN,
} board_link_driver_result_t;

typedef struct {
    uint32_t wRxBytes;
    uint32_t wTxBytes;
    uint32_t wRxErrors;
    uint32_t wTxErrors;
} board_link_driver_stats_t;

typedef struct {
    board_link_driver_state_t eState;
    board_link_driver_stats_t tStats;
} board_link_driver_t;

board_link_driver_result_t board_link_driver_Init(
    board_link_driver_t *ptThis);
board_link_driver_result_t board_link_driver_Write(
    board_link_driver_t *ptThis,
    const uint8_t *pchData,
    uint32_t wLength,
    uint32_t *pwCount);
board_link_driver_result_t board_link_driver_Read(
    board_link_driver_t *ptThis,
    uint8_t *pchData,
    uint32_t wLength,
    uint32_t *pwCount);
board_link_driver_result_t board_link_driver_GetStats(
    const board_link_driver_t *ptThis,
    board_link_driver_stats_t *ptStats);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_LINK_DRIVER_H */
