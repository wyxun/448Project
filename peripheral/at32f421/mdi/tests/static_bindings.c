#include "mdi/instance.h"

void at32_client_mdi_static_binding_probe(void)
{
    uint32_t wCount = 0U;
    (void)MDI_STREAM_Write(serial_stream, (const uint8_t *)0, 0U, &wCount);
    (void)MDI_STREAM_Read(serial_stream, (uint8_t *)0, 0U, &wCount);
    (void)MDI_STREAM_Available(serial_stream);
    (void)MDI_STREAM_IsBusy(serial_stream);
}
