#ifndef RF448_TEST_MODUS_H
#define RF448_TEST_MODUS_H
#include <stdint.h>
#include "perf_counter.h"
#define MODUS_SUCCESS 0
#define MODUS_EFAIL (-20)
#define MVC 25601U
typedef struct {
    uint32_t wId;
    uintptr_t wParent;
    uint32_t wRegistryCookie;
} modus_base_t;
typedef struct {
    uint32_t wId;
    uintptr_t wParent;
    struct {
        int (*Run)(uintptr_t);
        int (*Clock)(uintptr_t);
    } FcnInterface;
} modus_base_cfg_t;
int mbase_Init(modus_base_t *ptBase, modus_base_cfg_t *ptCfg);
#define MODUS_DECLARE_OBJECT(T, N, ...)                                         \
    T##_cfg_t t##N##Cfg = {__VA_ARGS__}; T##_t t##N;
#endif
