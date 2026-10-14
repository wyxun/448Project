/**
 * @file rf448_test.c
 * @brief Independent RF448 sequencing and MVC integration checks.
 * @author Modus project
 * @date 2026-10-14
 */
#include "rf448_driver.h"
#include "mvc.h"
#include "mdi/instance.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t test_duty, test_relay, test_sd;
bool test_carrier, test_dac_enabled;
mdi_status_t test_io_result;
static int64_t s_now;
static int s_base_result;
static unsigned s_head_writes;
static unsigned s_registrations;
extern mvc_t tMvc;
extern mvc_cfg_t tMvcCfg;
extern void (*test_rf448_handler)(const char *);
static const rf448_driver_cfg_t s_cfg = {30U, 1U};

int64_t get_system_ms(void) { return s_now; }
bool test_Timeout(uint32_t wMs, int64_t *plDeadline, bool bReload)
{
    if (*plDeadline == 0) {
        *plDeadline = s_now + (int64_t)wMs;
        return false;
    }
    if (s_now < *plDeadline) { return false; }
    if (bReload) { *plDeadline = s_now + (int64_t)wMs; }
    return true;
}
void test_Log(const char *pchFormat, ...) { (void)pchFormat; }
void mshell_Write(const char *pchText) { (void)pchText; }
void pt_sd_io_Write(uint32_t wValue) { test_sd = wValue; }
void pt_relay_io_WriteMasked(uint32_t wMask, uint32_t wValue)
{
    assert(test_sd == 1U && !test_carrier && test_duty == 1000U);
    assert(wMask == 0x38U);
    test_relay = (test_relay & ~wMask) | (wValue & wMask);
    if (wValue != 0U) { s_head_writes++; }
}
mdi_status_t pwm1_pwm_SafeStop(void)
{
    test_carrier = false;
    return test_io_result;
}
mdi_status_t dac_pwm_pwm_SafeStop(void)
{
    test_duty = 1000U;
    test_dac_enabled = true;
    return test_io_result;
}
mdi_status_t pwm1_pwm_Enable(bool bEnable)
{
    assert(test_sd == 1U);
    if (test_io_result == MDI_OK) { test_carrier = bEnable; }
    return test_io_result;
}
bool pwm1_pwm_IsEnabled(void) { return test_carrier; }
mdi_status_t dac_pwm_pwm_Stage(const dac_pwm_pwm_frame_t *ptFrame)
{
    assert((test_sd == 1U && !test_carrier)
        || (test_sd == 0U && test_carrier));
    if (test_io_result == MDI_OK) { test_duty = ptFrame->duty; }
    return test_io_result;
}
mdi_status_t dac_pwm_pwm_Commit(void) { return test_io_result; }
int mbase_Init(modus_base_t *ptBase, modus_base_cfg_t *ptCfg)
{
    assert(ptCfg->wParent != 0U && ptCfg->FcnInterface.Run == mvc_Run);
    if (s_base_result != MODUS_SUCCESS) { return s_base_result; }
    ptBase->wId = ptCfg->wId;
    ptBase->wParent = ptCfg->wParent;
    ptBase->wRegistryCookie = 0xABCDU;
    s_registrations++;
    return s_base_result;
}

static void reset(void)
{
    test_duty = 0U;
    test_relay = 5U;
    test_sd = 0U;
    test_carrier = true;
    test_dac_enabled = false;
    test_io_result = MDI_OK;
    s_now = 0;
    s_base_result = MODUS_SUCCESS;
    s_head_writes = 0U;
}

static void sequence(rf448_driver_t *ptDriver, rf448_head_t eHead,
                     uint16_t hwEnergy)
{
    rf448_driver_status_t tStatus = {0};
    const unsigned before = s_head_writes;
    assert(rf448_driver_Start(ptDriver, eHead, hwEnergy) == RF448_RESULT_OK);
    assert(s_head_writes == before && !test_carrier);
    assert(rf448_driver_Run(ptDriver) == fsm_rt_on_going);
    assert(test_duty == 1000U && test_sd == 1U && !test_carrier);
    assert(test_relay == (eHead == RF448_HEAD_CAP ? 0x15U : 0x3DU));
    s_now += 29;
    assert(rf448_driver_Run(ptDriver) == fsm_rt_on_going);
    assert(test_duty == 1000U && !test_carrier);
    s_now++;
    assert(rf448_driver_Run(ptDriver) == fsm_rt_on_going);
    assert(test_duty == 1000U - (uint32_t)hwEnergy && !test_carrier);
    s_now++;
    assert(rf448_driver_Run(ptDriver) == fsm_rt_cpl);
    assert(test_carrier && test_sd == 0U);
    assert(rf448_driver_GetStatus(ptDriver, &tStatus) == RF448_RESULT_OK);
    assert(tStatus.eState == RF448_STATE_RUNNING && tStatus.bOutputEnabled);
    assert(tStatus.hwTargetEnergyPermille == hwEnergy);
    assert(rf448_driver_Run(ptDriver) == fsm_rt_cpl);
    assert(s_head_writes == before + 1U);
    const uint16_t updated_energy = hwEnergy == 1000U ? 0U : 1000U;
    const rf448_head_t other_head = eHead == RF448_HEAD_CAP
                                 ? RF448_HEAD_RES : RF448_HEAD_CAP;
    assert(rf448_driver_Start(ptDriver, other_head, updated_energy)
        == RF448_RESULT_BUSY);
    assert(rf448_driver_Start(ptDriver, eHead, 1001U) == RF448_RESULT_INVALID);
    assert(rf448_driver_GetStatus(ptDriver, &tStatus) == RF448_RESULT_OK);
    assert(tStatus.hwTargetEnergyPermille == hwEnergy);
    assert(rf448_driver_Start(ptDriver, eHead, updated_energy)
        == RF448_RESULT_OK);
    assert(test_duty == 1000U - (uint32_t)hwEnergy);
    assert(rf448_driver_Run(ptDriver) == fsm_rt_cpl);
    assert(test_duty == 1000U - (uint32_t)updated_energy);
    assert(test_carrier && test_sd == 0U && s_head_writes == before + 1U);
    assert(rf448_driver_GetStatus(ptDriver, &tStatus) == RF448_RESULT_OK);
    assert(tStatus.hwTargetEnergyPermille == updated_energy);
    assert(rf448_driver_Stop(ptDriver) == RF448_RESULT_OK);
    assert(test_duty == 1000U && !test_carrier && test_sd == 1U);
    assert(test_relay == 5U);
    assert(rf448_driver_Run(ptDriver) == fsm_rt_cpl);
}

static void continuous_lifecycle_test(void)
{
    rf448_driver_t tDriver = {0};
    reset();
    assert(rf448_driver_Init(&tDriver, &s_cfg) == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    /* Idle must suspend the ongoing PT instead of bypassing its lifecycle. */
    assert(tDriver.chRunPt != 0U);
    s_now += 1000;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(tDriver.chRunPt != 0U && s_head_writes == 0U);
    sequence(&tDriver, RF448_HEAD_CAP, 400U);
    assert(tDriver.chRunPt != 0U);
    sequence(&tDriver, RF448_HEAD_RES, 600U);
    assert(tDriver.chRunPt != 0U);
    for (unsigned phase = 0U; phase < 2U; phase++) {
        assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 500U)
            == RF448_RESULT_OK);
        assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
        s_now += 30;
        if (phase == 1U) {
            assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
            s_now++;
        }
        /* Stop must win even if the corresponding deadline just elapsed. */
        assert(rf448_driver_Stop(&tDriver) == RF448_RESULT_OK);
        assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
        assert(tDriver.eState == RF448_STATE_IDLE && !test_carrier);
        assert(test_sd == 1U && test_relay == 5U && test_duty == 1000U);
    }
}

static void driver_test(void)
{
    rf448_driver_t tDriver = {0};
    rf448_driver_t tOther = {0};
    rf448_driver_cfg_t tBad = {0U, 1U};
    rf448_driver_status_t tStatus = {0};
    reset();
    assert(rf448_driver_Init(NULL, &s_cfg) == RF448_RESULT_INVALID);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_err);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 1U)
        == RF448_RESULT_NOT_READY);
    assert(rf448_driver_Init(&tDriver, &tBad) == RF448_RESULT_INVALID);
    assert(test_sd == 1U && test_duty == 1000U && !test_carrier);
    assert(rf448_driver_Init(&tDriver, &s_cfg) == RF448_RESULT_OK);
    assert(test_dac_enabled && s_head_writes == 0U);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 1001U)
        == RF448_RESULT_INVALID);
    assert(rf448_driver_Start(&tDriver, (rf448_head_t)99, 0U)
        == RF448_RESULT_INVALID);
    sequence(&tDriver, RF448_HEAD_CAP, 0U);
    sequence(&tDriver, RF448_HEAD_RES, 1000U);
    sequence(&tDriver, RF448_HEAD_CAP, 500U);
    /* A pending request can be cancelled without moving any relay. */
    const unsigned before_cancel = s_head_writes;
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 99U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Stop(&tDriver) == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(s_head_writes == before_cancel && !test_carrier);
    /* Energy may be nonzero while RF is still inhibited; Stop must clear it. */
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 777U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now += 30;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    assert(test_duty == 223U && !test_carrier);
    assert(rf448_driver_Stop(&tDriver) == RF448_RESULT_OK);
    assert(test_duty == 1000U && !test_carrier);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_RES, 100U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    assert(rf448_driver_Stop(&tDriver) == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    s_now += 100;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl && !test_carrier);
    assert(rf448_driver_Init(&tOther, &s_cfg) == RF448_RESULT_OK);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 100U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    const int64_t first_deadline = tDriver.lWaitDeadline;
    s_now += 10;
    assert(rf448_driver_Start(&tOther, RF448_HEAD_RES, 100U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tOther) == fsm_rt_on_going);
    assert(tDriver.lWaitDeadline == first_deadline);
    assert(tOther.lWaitDeadline == s_now + (int64_t)s_cfg.wRelaySettleMs);
    assert(rf448_driver_Stop(&tOther) == RF448_RESULT_OK);
    assert(rf448_driver_Stop(&tDriver) == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 123U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now += 30;
    test_io_result = MDI_IO_ERROR;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_err);
    assert(rf448_driver_GetStatus(&tDriver, &tStatus) == RF448_RESULT_OK);
    assert(tStatus.eState == RF448_STATE_ERROR && !tStatus.bOutputEnabled);
    assert(test_duty == 1000U && test_sd == 1U);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 0U)
        == RF448_RESULT_FAULT);
    assert(rf448_driver_Init(&tDriver, &s_cfg) == RF448_RESULT_IO);
    assert(test_sd == 1U && !test_carrier && test_duty == 1000U);
    test_io_result = MDI_OK;
    assert(rf448_driver_Init(&tDriver, &s_cfg) == RF448_RESULT_OK);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_RES, 700U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now += 30;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now++;
    test_io_result = MDI_IO_ERROR;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_err);
    assert(test_sd == 1U && !test_carrier && test_duty == 1000U);
}

static void live_energy_failure_test(void)
{
    rf448_driver_t tDriver = {0};
    rf448_driver_status_t tStatus = {0};
    reset();
    assert(rf448_driver_Init(&tDriver, &s_cfg) == RF448_RESULT_OK);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 500U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now += 30;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now++;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 800U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Stop(&tDriver) == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(test_duty == 1000U && test_relay == 5U && !test_carrier);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 500U)
        == RF448_RESULT_OK);
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now += 30;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_on_going);
    s_now++;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_cpl);
    assert(rf448_driver_Start(&tDriver, RF448_HEAD_CAP, 800U)
        == RF448_RESULT_OK);
    test_io_result = MDI_IO_ERROR;
    assert(rf448_driver_Run(&tDriver) == fsm_rt_err);
    assert(test_duty == 1000U && test_relay == 5U && test_sd == 1U);
    assert(rf448_driver_GetStatus(&tDriver, &tStatus) == RF448_RESULT_OK);
    assert(tStatus.eState == RF448_STATE_ERROR && !tStatus.bOutputEnabled);
}

static void mvc_test(void)
{
    mvc_t tObject = {0};
    const mvc_cfg_t tCfg = {123U, {30U, 1U}};
    rf448_driver_status_t tStatus = {0};
    reset();
    assert(mvc_Init(0U, (uintptr_t)&tCfg) == MODUS_EFAIL);
    assert(mvc_Init((uintptr_t)&tObject, (uintptr_t)&tCfg) == MODUS_SUCCESS);
    assert(tObject.tBase.wParent == (uintptr_t)&tObject);
    assert(tObject.tBase.wId == 123U);
    assert(mvc_Run((uintptr_t)&tObject) == MODUS_SUCCESS);
    assert(tObject.eState == MVC_STATE_IDLE);
    assert(rf448_driver_Start(&tObject.tRf448, RF448_HEAD_CAP, 250U)
        == RF448_RESULT_OK);
    assert(mvc_Run((uintptr_t)&tObject) == MODUS_SUCCESS);
    assert(tObject.eState == MVC_STATE_RUNNING);
    s_now += 30;
    assert(mvc_Run((uintptr_t)&tObject) == MODUS_SUCCESS);
    s_now++;
    assert(mvc_Run((uintptr_t)&tObject) == MODUS_SUCCESS);
    assert(tObject.eState == MVC_STATE_RUNNING);
    const unsigned registered = s_registrations;
    assert(mvc_Init((uintptr_t)&tObject, (uintptr_t)&tCfg) == MODUS_SUCCESS);
    assert(s_registrations == registered);
    assert(tObject.tBase.wRegistryCookie == 0xABCDU);
    const mvc_cfg_t tBadCfg = {0U, {30U, 1U}};
    assert(mvc_Init((uintptr_t)&tObject, (uintptr_t)&tBadCfg) == MODUS_EFAIL);
    assert(tObject.tBase.wRegistryCookie == 0xABCDU);
    assert(mvc_Init((uintptr_t)&tObject, (uintptr_t)&tCfg) == MODUS_SUCCESS);
    assert(s_registrations == registered);
    mvc_t tRejected = {0};
    s_base_result = MODUS_EFAIL;
    assert(mvc_Init((uintptr_t)&tRejected, (uintptr_t)&tCfg) == MODUS_EFAIL);
    assert(test_sd == 1U && !test_carrier && test_duty == 1000U);
    s_base_result = MODUS_SUCCESS;
    assert(mvc_Init((uintptr_t)&tMvc, (uintptr_t)&tMvcCfg) == MODUS_SUCCESS);
    test_rf448_handler("start cap 500 extra");
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_SUCCESS && s_head_writes == 1U);
    test_rf448_handler("start res 1000");
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_SUCCESS);
    assert(test_relay == 0x3DU);
    s_now += 30;
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_SUCCESS);
    s_now++;
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_SUCCESS);
    assert(test_carrier && test_duty == 0U);
    test_rf448_handler("stop");
    assert(!test_carrier && test_duty == 1000U);
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_SUCCESS);
    assert(rf448_driver_GetStatus(&tMvc.tRf448, &tStatus) == RF448_RESULT_OK);
    assert(tStatus.eState == RF448_STATE_IDLE);
    test_rf448_handler("start res -1");
    test_rf448_handler("start res 1001");
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_SUCCESS && !test_carrier);
    test_rf448_handler("start cap 300");
    test_io_result = MDI_IO_ERROR;
    assert(mvc_Run((uintptr_t)&tMvc) == MODUS_EFAIL);
    assert(tMvc.eState == MVC_STATE_ERROR);
}

int main(int argc, char **argv)
{
    const char *module = argc > 1 ? argv[1] : "all";
    if (strcmp(module, "mvc") != 0) {
        continuous_lifecycle_test();
        driver_test();
        live_energy_failure_test();
    }
    if (strcmp(module, "driver") != 0) { mvc_test(); }
    puts("RF448 PASS: sequencing, endpoints, stop, errors, MVC, shell");
    return 0;
}
