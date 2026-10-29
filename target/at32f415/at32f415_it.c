/**
 * @file at32f415_it.c
 * @brief AT32F415 interrupt adapters.
 * @author Codex
 * @date 2026-09-18
 */

#include "at32f415.h"
#include "perf_counter.h"
#include "mdebug_cm.h"
#include "mdi/instance.h"
#include "dac_driver.h"
#include "mircoelec.h"

extern void modus_Clock(void);
extern mircoelec_t tMircoelec;

void NMI_Handler(void) { while (1) { } }
#ifndef MDEBUG_CM_FAULT_HANDLERS_ACTIVE
void HardFault_Handler(void) { while (1) { } }
void MemManage_Handler(void) { while (1) { } }
void BusFault_Handler(void) { while (1) { } }
void UsageFault_Handler(void) { while (1) { } }
#endif
void SVC_Handler(void) { }
void DebugMon_Handler(void) { }
void PendSV_Handler(void) { }

void SysTick_Handler(void)
{
    perfc_port_insert_to_system_timer_insert_ovf_handler();
    modus_Clock();
}

void DMA1_Channel1_IRQHandler(void)
{
    MDI_AT32_ADC_DMA_COMPLETE_IRQ(
        adc_dma, ADC1, DMA1_CHANNEL1, DMA1_DTERR1_FLAG, DMA1_FDT1_FLAG,
        g_at32_master_adc_dma_busy);
}
void USART1_IRQHandler(void) { MDI_UART_STREAM_IRQ(dwin_stream); }
void USART3_IRQHandler(void) { MDI_UART_STREAM_IRQ(board_link_stream); }
void UART4_IRQHandler(void) { }

void WWDT_IRQHandler(void) { }
void PVM_IRQHandler(void) { }
void TMR1_OVF_TMR10_IRQHandler(void)
{
    if (tmr_interrupt_flag_get(TMR1, TMR_OVF_FLAG) != RESET) {
        tmr_flag_clear(TMR1, TMR_OVF_FLAG);
        dac_driver_IsrStep(&tMircoelec.tDac);
    }
}
void TMR1_CH_IRQHandler(void) { }
void TMR3_GLOBAL_IRQHandler(void) { }
void ADC1_IRQHandler(void) { }
