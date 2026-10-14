/**
 * @file pwm_control_test.c
 * @brief Check the actual board PWM lifecycle against fake timer registers.
 * @author Modus project
 * @date 2026-10-14
 */
#include "../../peripheral/at32f421/mdi/pwm_control.h"
#include <assert.h>
#include <stdio.h>
tmr_type test_tmr1, test_tmr3;
void tmr_output_enable(tmr_type *ptTimer, confirm_state eEnable)
{
    assert(ptTimer == TMR1);
    ptTimer->brk_bit.oen = (uint32_t)eEnable;
}
void tmr_counter_enable(tmr_type *ptTimer, confirm_state eEnable)
{ ptTimer->ctrl1_bit.tmren = (uint32_t)eEnable; }
void tmr_counter_value_set(tmr_type *ptTimer, uint32_t wValue)
{ ptTimer->counter = wValue; }
void tmr_event_sw_trigger(tmr_type *ptTimer, tmr_event_trigger_type eEvent)
{ assert(eEvent == TMR_OVERFLOW_SWTRIG); ptTimer->updates++; }
void tmr_channel_value_set(tmr_type *ptTimer,
                           tmr_channel_select_type eChannel, uint32_t wValue)
{ assert(eChannel == TMR_SELECT_CHANNEL_1); ptTimer->c1dt = wValue; }
void tmr_output_channel_mode_select(tmr_type *ptTimer,
    tmr_channel_select_type eChannel, tmr_output_control_mode_type eMode)
{
    assert(eChannel == TMR_SELECT_CHANNEL_1);
    ptTimer->mode = eMode;
    ptTimer->cm1_output_bit.c1octrl = (uint32_t)eMode;
}
int main(void)
{
    test_tmr1.pr = AT32_CLIENT_RF_PWM_PERIOD_TICKS - 1U;
    test_tmr1.c1dt = AT32_CLIENT_RF_PWM_COMPARE_TICKS;
    test_tmr1.brk_bit.dtc = AT32_CLIENT_RF_PWM_DEADTIME_TICKS;
    assert(MDI_PWM_Enable(pwm1, true) == MDI_OK);
    assert(MDI_PWM_IsEnabled(pwm1) && test_tmr1.counter == 0U);
    assert(MDI_PWM_SafeStop(pwm1) == MDI_OK);
    assert(!MDI_PWM_IsEnabled(pwm1) && test_tmr1.ctrl1_bit.tmren == 0U);
    test_tmr1.pr++;
    assert(MDI_PWM_Enable(pwm1, true) == MDI_RANGE);
    test_tmr1.pr--;
    test_tmr1.c1dt++;
    assert(MDI_PWM_Enable(pwm1, true) == MDI_RANGE);
    test_tmr1.c1dt--;
    test_tmr1.brk_bit.dtc++;
    assert(MDI_PWM_Enable(pwm1, true) == MDI_RANGE);
    test_tmr1.brk_bit.dtc--;
    test_tmr1.div = 1U;
    assert(MDI_PWM_Enable(pwm1, true) == MDI_RANGE);
    test_tmr3.pr = AT32_CLIENT_DAC_PWM_PERIOD_TICKS - 1U;
    test_tmr3.div = AT32_CLIENT_DAC_PWM_PRESCALER;
    assert(MDI_PWM_SafeStop(dac_pwm) == MDI_OK);
    assert(test_tmr3.mode == TMR_OUTPUT_CONTROL_FORCE_HIGH);
    assert(test_tmr3.c1dt == 1000U && test_tmr3.ctrl1_bit.tmren == 1U);
    assert(MDI_PWM_Commit(dac_pwm) == MDI_OK);
    assert(test_tmr3.mode == TMR_OUTPUT_CONTROL_PWM_MODE_A);
    const uint32_t before = test_tmr3.updates;
    test_tmr3.counter = 321U;
    test_tmr3.c1dt = 0U;
    assert(MDI_PWM_Commit(dac_pwm) == MDI_OK);
    assert(test_tmr3.updates == before && test_tmr3.counter == 321U);
    test_tmr3.c1dt = 1001U;
    assert(MDI_PWM_Commit(dac_pwm) == MDI_RANGE);
    assert(MDI_PWM_SafeStop(dac_pwm) == MDI_OK);
    assert(test_tmr3.mode == TMR_OUTPUT_CONTROL_FORCE_HIGH);
    puts("RF448 PWM PASS: fixed timing, enable, safe levels, endpoints");
    return 0;
}
