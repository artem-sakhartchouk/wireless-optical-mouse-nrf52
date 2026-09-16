#include <stdbool.h>

#include "nrf.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

#include "status_leds.h"
#include "usb_mouse.h"
#include "esb_mouse.h"
#include "platform.h"


int main(void)
{
    platform_init();
    leds_init();

    esb_mouse_init();
    usb_mouse_init();

    while (true)
    {
        /* Process remote-wakeup requests triggered by button presses. */
        usb_mouse_remote_wakeup_process();

        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());

        /*
         * Sleep until an interrupt/event occurs.
         * Clear any stale SEV state before sleeping again.
         */
        __WFE();
        __SEV();
        __WFE();
    }
}
