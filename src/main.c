/**
 * @file main.c
 * @brief Application startup and MODUS foreground service.
 * @author Modus project
 * @date 2026-10-08
 */

/*============================ INCLUDES ======================================*/
#include "global_define.h"
#include <stdio.h>
#include "peripheral.h"
#include "mdi/instance.h"
#include "perf_counter.h"
#include "util_debug.h"
#include "debug_transport.h"

#if MODUS_ENABLE
#include "modus.h"
#endif

#if MODUS_ENABLE
#include <string.h>
#include "mdebug/mshell.h"
#endif

#if MODUS_ENABLE
static modus_t s_tModus = { .ptAppFlash = NULL };
#if USERCONFIG_MSHELL_ON_SERIAL
/* Best-effort trace records drops without recursively logging them. */
static volatile uint32_t s_wSerialDroppedBytes = 0U;
#endif

#if USERCONFIG_MSHELL_ON_SERIAL
#if MSHELL_ENABLE
static volatile uint32_t s_wSerialRxOverruns = 0U;
/**
 * @brief Read available shell bytes without blocking.
 * @param pchBuf Caller-provided byte buffer.
 * @param hwSize Buffer size in bytes.
 * @return Number of bytes read; zero when unavailable or failed.
 */
static unsigned s_uart_read(char *pchBuf, unsigned hwSize)
{
    uint32_t wRead = 0U;
    mdi_status_t eStatus = MDI_STREAM_Read(
        serial_stream, (uint8_t *)pchBuf, (uint32_t)hwSize, &wRead);
    if (eStatus == MDI_OVERRUN) {
        s_wSerialRxOverruns++;
    }
    return ((eStatus == MDI_OK) || (eStatus == MDI_OVERRUN))
         ? (unsigned)wRead : 0U;
}
#endif

/**
 * @brief Submit best-effort serial trace and account for dropped bytes.
 * @param pchBuf Caller-provided byte buffer.
 * @param hwSize Buffer size in bytes.
 * @return None.
 */
static void s_uart_write(const char *pchBuf, unsigned hwSize)
{
    uint32_t wWritten = 0U;
    mdi_status_t eStatus = MDI_STREAM_Write(
        serial_stream, (const uint8_t *)pchBuf, (uint32_t)hwSize, &wWritten);
    if (eStatus != MDI_OK) {
        s_wSerialDroppedBytes += hwSize;
    } else if (wWritten < hwSize) {
        s_wSerialDroppedBytes += hwSize - wWritten;
    } else {
        /* The stream accepted the entire fragment. */
    }
}

#if MSHELL_ENABLE
static const mshell_io_t s_tUartIO = {
    .pfcnRead  = s_uart_read,
    .pfcnWrite = s_uart_write,
};
#endif
#endif

/**
 * @brief Route trace text through the configured debug transport.
 * @param str NUL-terminated trace text, or NULL.
 * @return None.
 */
void user_trace_output(const char *str)
{
#if USERCONFIG_MSHELL_ON_SERIAL
    if (str) {
        s_uart_write(str, (unsigned)strlen(str));
    }
#else
    debug_transport_write_string(str);
#endif
}
#endif

/**
 * @brief Initialize hardware and continuously service MODUS.
 * @return Zero if the application exits.
 */
int main(void)
{
    bool bCounterReady = false;
    peripheral_Init();
    bCounterReady = perfc_init(true);
    if (!bCounterReady) {
        /* Do not start time-dependent drivers with an invalid clock. */
        return 1;
    }

#if MODUS_ENABLE
    #if MSHELL_ENABLE || !defined(__NO_USE_LOG__)
        debug_transport_init();
        MLOGF(I, "Booting MODUS M448_Client...\r\n");
    #endif
    modus_Init(&s_tModus);
    #if USERCONFIG_MSHELL_ON_SERIAL && MSHELL_ENABLE
    mshell_SetIO(&s_tUartIO);
    #endif
#endif

#if MODUS_ENABLE
    while (1) {
        modus_Run();
    }
#endif

    return 0;
}
