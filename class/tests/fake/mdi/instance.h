#ifndef RF448_TEST_MDI_H
#define RF448_TEST_MDI_H
#include "mdi/core/contract.h"
#define AT32_CLIENT_DAC_PWM_PERIOD_TICKS 1000U
#define AT32_CLIENT_RF_PWM_PERIOD_TICKS 214U
#define AT32_CLIENT_RF_PWM_COMPARE_TICKS 107U
#define AT32_CLIENT_RF_PWM_DEADTIME_TICKS 11U
typedef struct { uint32_t duty; } dac_pwm_pwm_frame_t;
typedef struct { uint32_t duty; } pwm1_pwm_frame_t;
extern uint32_t test_duty, test_relay, test_sd;
extern bool test_carrier, test_dac_enabled;
extern mdi_status_t test_io_result;
void pt_sd_io_Write(uint32_t wValue);
void pt_relay_io_WriteMasked(uint32_t wMask, uint32_t wValue);
mdi_status_t pwm1_pwm_SafeStop(void);
mdi_status_t dac_pwm_pwm_SafeStop(void);
mdi_status_t pwm1_pwm_Enable(bool bEnable);
bool pwm1_pwm_IsEnabled(void);
mdi_status_t dac_pwm_pwm_Stage(const dac_pwm_pwm_frame_t *ptFrame);
mdi_status_t dac_pwm_pwm_Commit(void);
#endif
