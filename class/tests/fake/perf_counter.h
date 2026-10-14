#ifndef RF448_TEST_PERF_COUNTER_H
#define RF448_TEST_PERF_COUNTER_H
#define __PERFORMANCE_COUNTER_H__
#include <stdint.h>
#include <stdbool.h>
#define UNUSED_PARAM(V) ((void)(V))
#define PERFC_SAFE_NAME(N) N
typedef enum {
    fsm_rt_err = -1, fsm_rt_cpl = 0, fsm_rt_on_going = 1
} fsm_rt_t;
int64_t get_system_ms(void);
bool test_Timeout(uint32_t wMs, int64_t *plDeadline, bool bReload);
#define perfc_is_time_out_ms(M, P, R) test_Timeout((M), (P), (R))
#endif
