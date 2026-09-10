/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nrf_log_ctrl.h" //for logging
#include "nrf.h" //need
#include "nrf_gpio.h"
#include "nrf_drv_usbd.h" //need obviously
#include "nrf_drv_clock.h" //need this for timer/jitter
#include "proprietary_rf/esb/nrf_esb.h"


#include "nrf_drv_power.h"
#include "nrf_log.h"
#include "nrf_delay.h"
 
#include "app_error.h" //need this to detect usb and esb errors 


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
       
        /*process remote wakeup requests triggered by button presses*/
        usb_mouse_remote_wakeup_process();


        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());

        /* Even if we miss an event enabling USB,
         * USB event would wake us up. */
        __WFE();
        /* Clear SEV flag if CPU was woken up by event */
        __SEV();
        __WFE();
        
    }
}
