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


#include "sensor.h"


pmw3389_status_t sensor_status;

static volatile mouse_motion_t mouse_current_state; //an instance for asynchronous updating

static volatile bool m_lfclk_started;

static volatile bool m_sensor_poll_due = false;


/* Start the external high-frequency clock required by ESB radio.
 * The function blocks until the the crystal oscillator reports it is running
 */
static void hfclk_start( void )
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);
}



/* Update the temporary button-based motion source.
 *
 * Either button edge counts as activity and returns the scheduler to its
 * active report rate. Holding one button produces a fixed horizontal delta;
 * pressing neither or both produces zero motion.
 */
static void button_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{
    

    (void) pin;
    (void) action;

    mouse_scheduler_mark_active(); //inform tx scheduler of motion activity


 
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

    /* buttons currently simulate horizontal motion only */
    mouse_current_state.y = 0; 

}


/* Configure GPIOTE for DK button pins.
 *
 * Either button edge triggers the temporary button-based motion callback
 */
static void buttons_init(void)
{

  nrfx_gpiote_init();


  /* generate an interrupt on both press and release
   * high-accuracy mode uses a dedicated GPIOTE channel
   */
  nrfx_gpiote_in_config_t config =
        NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true); 

  config.pull = NRF_GPIO_PIN_PULLUP; 

  APP_ERROR_CHECK(
      nrfx_gpiote_in_init(BUTTON_1, &config, button_handler)); 

  APP_ERROR_CHECK(
      nrfx_gpiote_in_init(BUTTON_2, &config, button_handler));


  nrfx_gpiote_in_event_enable(BUTTON_1, true); 
  nrfx_gpiote_in_event_enable(BUTTON_2, true);


}



static void leds_init( void )
{
    
    bsp_board_init(BSP_INIT_LEDS);
    
}



static void lfclk_handler(nrfx_clock_evt_type_t event)
{
    (void)event;
}




int main(void)
{

    ret_code_t err_code;

    /* initialize board LEDs and temporary button-based motion input */
    leds_init();
    buttons_init(); 


    /* initialize logging for modules that emit diagnostic messages */
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    /* start clocks required by ESB and the RTC scheduler */
    hfclk_start(); 

    err_code = nrfx_clock_init(lfclk_handler);
    APP_ERROR_CHECK(err_code);

    nrfx_clock_lfclk_start();

    while(!nrfx_clock_lfclk_is_running())
    {
    }


    /* initialize app modules after clocks are running */
    mouse_scheduler_init();  
    err_code = esb_mouse_tx_init();  
    APP_ERROR_CHECK(err_code);

   
    sensor_status = pmw3389_init();

    if (sensor_status == PMW3389_OK)
    {
        NRF_LOG_INFO("PMW3389 detected");
    }
    else
    {
        NRF_LOG_ERROR(
            "PMW3389 initialization failed: %u",
            sensor_status
        );
    }

    pmw3389_info_t info;

    bool read_ok;
    read_ok = pmw3389_read_info(&info);

        NRF_LOG_INFO(
        "PMW3389: PID=0x%02X REV=0x%02X INV=0x%02X SROM=0x%02X",
        info.product_id,
        info.revision_id,
        info.inverse_product_id,
        info.srom_id
    );

    while (true)
    {

            static uint8_t idle_divider = 0;


            #if 0
            if(m_sensor_poll_due)
            {
                m_sensor_poll_due = false;
            
                pmw3389_motion_t motion;

                if (pmw3389_read_motion(&motion))
                {
                    if ((motion.dx != 0) || (motion.dy != 0))
                    {
                        NRF_LOG_INFO("DX=%d DY=%d",
                             motion.dx,
                             motion.dy);
                    }
            
                }

            }

            
            #endif


            if(mouse_scheduler_report_pending() && !esb_mouse_tx_busy())
            {
                
                
                pmw3389_motion_t sensor_motion;

                pmw3389_read_motion(&sensor_motion);
                #if 0
                {
                    if ((sensor_motion.dx != 0) || (sensor_motion.dy != 0))
                    {
                        NRF_LOG_INFO("DX=%d DY=%d",
                             sensor_motion.dx,
                             sensor_motion.dy);
                    }
            
                }
                #endif


                mouse_motion_t motion;

                CRITICAL_REGION_ENTER();
                motion = mouse_current_state;
                CRITICAL_REGION_EXIT();
            

                if(esb_mouse_tx_send(&motion))
                {
                    mouse_scheduler_report_queued(); //clear pending flag
                
                    if(mouse_scheduler_power_state_get() == MOUSE_IDLE) //report pending every 100 ms in idle
                    {

                        if(++idle_divider >= 10) //10 * 100 ms idle period, led toggles once per second 
                        {
                    
                            bsp_board_led_invert(BSP_BOARD_LED_0);
                            idle_divider = 0;
                        }

                
                    }
                    else
                    {
                        idle_divider = 0; //start from zero for next idle 
                    }

                }


            }
             
            UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());

            __WFE(); //idles cpu while waiting for esb, gpiote, or rtc1 to fire interrupts
      

    }

}
