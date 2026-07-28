/**
 * Copyright (c) 2014 - 2021, Nordic Semiconductor ASA
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


/**
 * rtc compare interrupt determines transmission rate. callback sets flag. 
 * main loop checks it. sends report if pending and clears it 
 *
 */



#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "sdk_common.h"
#include "nrf.h"
#include "nrf_esb.h"
#include "nrf_error.h"
#include "nrf_esb_error_codes.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "boards.h"
#include "nrf_delay.h"
#include "app_util.h"
#include "nrfx_gpiote.h"


#include "nrfx_clock.h"
#include "nrfx_rtc.h"


#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "SEGGER_RTT.h"


#include "esb_mouse_tx.h"
#include "mouse_scheduler.h"


static volatile mouse_motion_t mouse_current_state; //an instance for asynchronous updating

static volatile bool m_lfclk_started;




void hfclk_start( void )
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);
}



//gpiote button interrupt handler // GPIOTE interrupt updates button state
// Main loop uses button state to generate mouse movement packets.
static void button_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    

    mouse_scheduler_mark_active(); //inform tx scheduler of motion activity


  
    bsp_board_led_off(BSP_BOARD_LED_0); //debug code






    bool left = (nrf_gpio_pin_read(BUTTON_1) == 0);
    bool right = (nrf_gpio_pin_read(BUTTON_2) == 0);
  
    if(left && !right)
    {
        mouse_current_state.x = -20;
        bsp_board_led_invert(BSP_BOARD_LED_2);
    }
    else if(right && !left)
    {
        mouse_current_state.x = 20;    
        bsp_board_led_invert(BSP_BOARD_LED_3);
    }
    else // if none or both pressed
    {
      mouse_current_state.x = 0;
    }

    mouse_current_state.y = 0; //vertical velocity ignored for now

}


//initializes buttons for gpiote
static void buttons_init(void)
{

  nrfx_gpiote_init();



  nrfx_gpiote_in_config_t config =
        NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true); //macro configures the pin to high-accuracy

  config.pull = NRF_GPIO_PIN_PULLUP; //specifies the pullup setting for the config used for the buttons


  nrfx_gpiote_in_init(BUTTON_1, &config, button_handler); //configures gpiote for each button, and connects the callback function
  nrfx_gpiote_in_init(BUTTON_2, &config, button_handler);



  nrfx_gpiote_in_event_enable(BUTTON_1, true); //enables the in interrupt event for each button
  nrfx_gpiote_in_event_enable(BUTTON_2, true);


}



void gpio_init( void )
{
    
    bsp_board_init(BSP_INIT_LEDS);
    //nrf_gpio_cfg_input(BUTTON_1, NRF_GPIO_PIN_PULLUP);
    
}



static void lfclk_handler(nrfx_clock_evt_type_t event)
{
    //only handle lfclk event, ignore others
    if(event != NRFX_CLOCK_EVT_LFCLK_STARTED)
    {
        return;
    }

    m_lfclk_started = true; //set the lfclk started flag for rtc1 startup timing
}




int main(void)
{


    ret_code_t err_code;

    
    gpio_init();

  
    buttons_init(); //for gpiote




    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();

    hfclk_start(); //start hf clock

    err_code = nrfx_clock_init(lfclk_handler);
    APP_ERROR_CHECK(err_code);

    nrfx_clock_lfclk_start();

    while(!nrfx_clock_lfclk_is_running())
    {
       
    }


    mouse_scheduler_init();
    //rtc1_init(); //initialize RTC1 after lfclk started
   
    err_code = esb_mouse_tx_init();
    //err_code = esb_init();
    APP_ERROR_CHECK(err_code);

   
     
    while (true)
    {

            static uint8_t idle_divider = 0;

            if(mouse_scheduler_report_pending() && !esb_mouse_tx_busy())
            {
                
                mouse_motion_t motion;

                CRITICAL_REGION_ENTER();
                motion = mouse_current_state;
                CRITICAL_REGION_EXIT();
            



                if(esb_mouse_tx_send(&motion))
                {
                    mouse_scheduler_report_queued(); //clear pending flag
                }
                //esb_queue_report();
                                
            } 
            
            if(mouse_scheduler_power_state_get() == MOUSE_IDLE) //report pending every 100 ms in idle
            {

                if(idle_divider >= 10) //10 * 100 ms idle period, led toggles once per second 
                {
                    
                    bsp_board_led_invert(BSP_BOARD_LED_0);
                    idle_divider = 0;
                }

                idle_divider++; //increment if idle mode
            }

             
            __WFE(); //idles cpu while waiting for esb, gpiote, or rtc1 to fire interrupts
      

    }

}
