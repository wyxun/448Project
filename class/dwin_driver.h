#ifndef DWIN_DRIVER_H
#define DWIN_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DWIN_DRIVER_FRAME_TIMEOUT_MS 20U
#define DWIN_DRIVER_MAX_FRAME_LEN 50U
#define DWIN_DRIVER_MAX_FRAMES_PER_RUN 8U
#define DWIN_DRIVER_RX_BUFFER_SIZE 256U
#define DWIN_DRIVER_TX_BUFFER_SIZE (DWIN_DRIVER_RX_BUFFER_SIZE + 2U)

#ifndef DWIN_DRIVER_CRC16_EN
#define DWIN_DRIVER_CRC16_EN 1
#endif

enum {
    DWIN_DRIVER_FRAME_HEAD0 = 0x5AU,
    DWIN_DRIVER_FRAME_HEAD1 = 0xA5U,
    DWIN_DRIVER_CMD_WRITE = 0x82U,
    DWIN_DRIVER_CMD_READ = 0x83U,
};

typedef enum {
    DWIN_DRIVER_RESULT_OK = 0,
    DWIN_DRIVER_RESULT_NULL,
    DWIN_DRIVER_RESULT_INVALID_ARGUMENT,
    DWIN_DRIVER_RESULT_NOT_READY,
    DWIN_DRIVER_RESULT_IO,
    DWIN_DRIVER_RESULT_FAULT,
} dwin_driver_result_t;

typedef enum {
    DWIN_DRIVER_STATE_UNINITIALIZED = 0,
    DWIN_DRIVER_STATE_READY,
    DWIN_DRIVER_STATE_ERROR,
} dwin_driver_state_t;

typedef struct {
    uint32_t wValid;
    uint32_t wBadLen;
    uint32_t wTimeout;
    uint32_t wDropped;
} dwin_driver_stats_t;

typedef void (*dwin_driver_receive_fn_t)(
    void *pContext,
    uint8_t chCmd,
    uint16_t hwAddress,
    uint8_t *pchData,
    uint16_t hwLength);

typedef struct {
    uint32_t wFrameTimeoutMs;
} dwin_driver_cfg_t;

typedef struct {
    dwin_driver_receive_fn_t fnOnReceive;
    void *pCallbackContext;
    uint8_t chRxBuf[DWIN_DRIVER_RX_BUFFER_SIZE];
    uint8_t chTxBuf[DWIN_DRIVER_TX_BUFFER_SIZE];
    uint16_t hwRxPos;
    uint64_t wFrameTimeoutTick;
    bool bFrameInProgress;
    dwin_driver_stats_t tStats;
    dwin_driver_state_t eState;
} dwin_driver_t;

dwin_driver_result_t dwin_driver_Init(
    dwin_driver_t *ptThis,
    const dwin_driver_cfg_t *ptCfg);
dwin_driver_result_t dwin_driver_Run(dwin_driver_t *ptThis);
dwin_driver_result_t dwin_driver_Clock(dwin_driver_t *ptThis);
dwin_driver_result_t dwin_driver_RegisterCallback(
    dwin_driver_t *ptThis,
    dwin_driver_receive_fn_t fnCallback,
    void *pContext);
dwin_driver_result_t dwin_driver_GetStats(
    const dwin_driver_t *ptThis,
    dwin_driver_stats_t *ptStats);

dwin_driver_result_t dwin_driver_SetPage(
    dwin_driver_t *ptThis,
    uint16_t hwPageId);
dwin_driver_result_t dwin_driver_SetN16Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint16_t *phwData,
    uint16_t hwLength);
dwin_driver_result_t dwin_driver_SetN32Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    const uint32_t *pwData,
    uint16_t hwLength);
dwin_driver_result_t dwin_driver_ReadN16Data(
    dwin_driver_t *ptThis,
    uint16_t hwAddress,
    uint8_t chLength);
dwin_driver_result_t dwin_driver_Reset(dwin_driver_t *ptThis);

#ifdef __cplusplus
}
#endif

#endif /* DWIN_DRIVER_H */

