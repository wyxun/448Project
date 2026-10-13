#ifndef CLASS_TEST_PERF_COUNTER_H
#define CLASS_TEST_PERF_COUNTER_H
#define __PERFORMANCE_COUNTER_H__
#include <stdbool.h>
#include <stdint.h>
typedef enum { fsm_rt_err = -1, fsm_rt_cpl = 0,
               fsm_rt_on_going = 1 } fsm_rt_t;
extern int64_t test_now_ms;
extern unsigned test_irq_depth;
bool test_timeout(uint32_t ms, int64_t *deadline, bool reload);
#define perfc_is_time_out_ms(M, P, R) test_timeout((M), (P), (R))
#define safe_atom_code() \
    for (bool once = (++test_irq_depth != 0U); once; \
         once = false, --test_irq_depth)
#endif
