/**
 * @file pwm_control.h
 * @brief Static lifecycle operations for client RF and inverted bus PWM.
 * @author Modus project
 * @date 2026-10-14
 */
#ifndef M448_CLIENT_PWM_CONTROL_H
#define M448_CLIENT_PWM_CONTROL_H
#include "at32f421_tmr.h"
#include "mdi/core/contract.h"

#define AT32_CLIENT_RF_PWM_PERIOD_TICKS 214U
#define AT32_CLIENT_RF_PWM_COMPARE_TICKS 107U
#define AT32_CLIENT_RF_PWM_DEADTIME_TICKS 24U
#define AT32_CLIENT_DAC_PWM_PERIOD_TICKS 1000U
#define AT32_CLIENT_DAC_PWM_PRESCALER 19U

/**
 * @brief Inhibit both carrier outputs at their configured low idle level.
 * @return MDI_OK; operation is synchronous and has no external wait.
 */
MDI_INLINE mdi_status_t pwm1_pwm_SafeStop(void)
{
    tmr_output_enable(TMR1, FALSE);
    tmr_counter_enable(TMR1, FALSE);
    tmr_counter_value_set(TMR1, 0U);
    return MDI_OK;
}

/**
 * @brief Enable the fixed carrier only when its board timing is intact.
 * @param bEnable True to enable; false to establish low idle outputs.
 * @return MDI_OK or MDI_RANGE for an unexpected timer configuration.
 * @note Caller keeps SD asserted until this operation succeeds.
 */
MDI_INLINE mdi_status_t pwm1_pwm_Enable(bool bEnable)
{
    if (!bEnable) { return pwm1_pwm_SafeStop(); }
    if ((TMR1->pr != AT32_CLIENT_RF_PWM_PERIOD_TICKS - 1U)
        || (TMR1->div != 0U)
        || (TMR1->c1dt != AT32_CLIENT_RF_PWM_COMPARE_TICKS)
        || (TMR1->brk_bit.dtc != AT32_CLIENT_RF_PWM_DEADTIME_TICKS)) {
        return MDI_RANGE;
    }
    tmr_counter_value_set(TMR1, 0U);
    tmr_event_sw_trigger(TMR1, TMR_OVERFLOW_SWTRIG);
    tmr_counter_enable(TMR1, TRUE);
    tmr_output_enable(TMR1, TRUE);
    return MDI_OK;
}

/**
 * @brief Inspect timer running and carrier main-output enable together.
 * @return True when both hardware enables are active.
 */
MDI_INLINE bool pwm1_pwm_IsEnabled(void)
{
    return (TMR1->ctrl1_bit.tmren != 0U) && (TMR1->brk_bit.oen != 0U);
}

/**
 * @brief Hold DACPWM high so the external NPN forces zero analog command.
 * @return MDI_OK; the forced high mode does not depend on timer phase.
 * @note TMR3 is a general timer and has no advanced main-output enable.
 */
MDI_INLINE mdi_status_t dac_pwm_pwm_SafeStop(void)
{
    tmr_output_channel_mode_select(TMR3, TMR_SELECT_CHANNEL_1,
                                   TMR_OUTPUT_CONTROL_FORCE_HIGH);
    tmr_channel_value_set(TMR3, TMR_SELECT_CHANNEL_1,
                          AT32_CLIENT_DAC_PWM_PERIOD_TICKS);
    tmr_event_sw_trigger(TMR3, TMR_OVERFLOW_SWTRIG);
    tmr_counter_enable(TMR3, TRUE);
    return MDI_OK;
}

/**
 * @brief Commit the staged DAC compare and restore PWM operation.
 * @return MDI_OK or MDI_RANGE for invalid board timing or compare value.
 * @note Startup transfers the initial compare with RF inhibited. Running PWM
 * uses compare preload at the next period boundary without resetting phase.
 */
MDI_INLINE mdi_status_t dac_pwm_pwm_Commit(void)
{
    if ((TMR3->pr != AT32_CLIENT_DAC_PWM_PERIOD_TICKS - 1U)
        || (TMR3->div != AT32_CLIENT_DAC_PWM_PRESCALER)
        || (TMR3->c1dt > AT32_CLIENT_DAC_PWM_PERIOD_TICKS)) {
        return MDI_RANGE;
    }
    if (TMR3->cm1_output_bit.c1octrl != TMR_OUTPUT_CONTROL_PWM_MODE_A) {
        tmr_counter_value_set(TMR3, 0U);
        tmr_event_sw_trigger(TMR3, TMR_OVERFLOW_SWTRIG);
        tmr_output_channel_mode_select(TMR3, TMR_SELECT_CHANNEL_1,
                                       TMR_OUTPUT_CONTROL_PWM_MODE_A);
        tmr_counter_enable(TMR3, TRUE);
    }
    return MDI_OK;
}
#endif
