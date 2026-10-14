#ifndef RF448_TEST_TMR_H
#define RF448_TEST_TMR_H
#include <stdint.h>
typedef enum { FALSE = 0, TRUE = 1 } confirm_state;
typedef enum { TMR_SELECT_CHANNEL_1 = 0 } tmr_channel_select_type;
typedef enum {
    TMR_OUTPUT_CONTROL_FORCE_HIGH = 5, TMR_OUTPUT_CONTROL_PWM_MODE_A = 6
} tmr_output_control_mode_type;
typedef enum { TMR_OVERFLOW_SWTRIG = 1 } tmr_event_trigger_type;
typedef struct {
    uint32_t pr, div, c1dt, counter, updates;
    struct { uint32_t tmren; } ctrl1_bit;
    struct { uint32_t oen, dtc; } brk_bit;
    struct { uint32_t c1octrl; } cm1_output_bit;
    tmr_output_control_mode_type mode;
} tmr_type;
extern tmr_type test_tmr1, test_tmr3;
#define TMR1 (&test_tmr1)
#define TMR3 (&test_tmr3)
void tmr_output_enable(tmr_type *ptTimer, confirm_state eEnable);
void tmr_counter_enable(tmr_type *ptTimer, confirm_state eEnable);
void tmr_counter_value_set(tmr_type *ptTimer, uint32_t wValue);
void tmr_event_sw_trigger(tmr_type *ptTimer, tmr_event_trigger_type eEvent);
void tmr_channel_value_set(tmr_type *ptTimer,
                           tmr_channel_select_type eChannel, uint32_t wValue);
void tmr_output_channel_mode_select(tmr_type *ptTimer,
    tmr_channel_select_type eChannel, tmr_output_control_mode_type eMode);
#endif
