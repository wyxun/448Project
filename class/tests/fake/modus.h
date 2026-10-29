#ifndef CLASS_TEST_MODUS_H
#define CLASS_TEST_MODUS_H
#include <stdint.h>
#include "perf_counter.h"
#include "global_define.h"
#define MIRCROELEC 2U
#define MVC 3U
typedef struct {
    uintptr_t wParent;
    uint8_t *pchRingBuffer;
    uint16_t hwRingSize, hwRead, hwWrite;
} modus_base_t;
typedef struct {
    uint32_t wId;
    uintptr_t wParent;
    uint8_t *pchRingBuffer;
    uint16_t hwRingSize;
    struct { int (*Clock)(uintptr_t); int (*Run)(uintptr_t); } FcnInterface;
} modus_base_cfg_t;
int mbase_Init(modus_base_t *base, modus_base_cfg_t *cfg);
int mbase_MessagePostToRing(uint32_t id, uint8_t *data, uint16_t len);
int mbase_MessagePendFromRing(modus_base_t *base, uint8_t *data, uint16_t len);
#define MODUS_DECLARE_OBJECT(T, N, ...) \
    T##_cfg_t t##N##Cfg = { __VA_ARGS__ }; T##_t t##N;
#endif
