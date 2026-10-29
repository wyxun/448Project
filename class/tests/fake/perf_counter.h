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
typedef unsigned perfc_global_interrupt_status_t;
static inline perfc_global_interrupt_status_t perfc_port_disable_global_interrupt(void)
{
    unsigned previous = test_irq_depth;
    ++test_irq_depth;
    return previous;
}
static inline void perfc_port_resume_global_interrupt(perfc_global_interrupt_status_t previous)
{
    test_irq_depth = previous;
}
#endif
