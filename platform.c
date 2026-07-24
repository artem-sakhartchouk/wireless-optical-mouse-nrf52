#include "platform.h"
#include "nrf_drv_power.h"
#include "app_error.h"
#include "nrf_drv_clock.h"


void platform_init(void)
{
    ret_code_t ret;
    /* Initializing power and clock */
    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);
    ret = nrf_drv_power_init(NULL);
    APP_ERROR_CHECK(ret);
    nrf_drv_clock_hfclk_request(NULL);
    nrf_drv_clock_lfclk_request(NULL);
    while (!(nrf_drv_clock_hfclk_is_running() &&
            nrf_drv_clock_lfclk_is_running()))
    {
        /* wait until both clocks are online */
    }

    nrf_power_resetreas_clear(nrf_power_resetreas_get());
}