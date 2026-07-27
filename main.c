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

//global esb send flag for changed state
static volatile bool g_mouse_dirty = false;


//global button state variables that ensure proper read and write
static volatile bool button1_pressed = false;
static volatile bool button2_pressed = false;

static volatile bool report_pending;   // something new to send
volatile bool tx_busy;          // ESB is currently sending

static nrf_esb_payload_t        tx_payload = NRF_ESB_CREATE_PAYLOAD(0, 0x01, 0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00);

static nrf_esb_payload_t        rx_payload;

//count esb packets sent as well as total retries for radio link integrity testing
volatile uint32_t packets_sent = 0;
volatile uint32_t total_retries = 0;
volatile uint32_t tx_failures = 0; //count tx failures

volatile uint32_t max_attempts = 0; //to calculate maximum attemps

//mouse movement data packet definition
typedef struct 
{
  uint8_t packet_sequence;
  int8_t x;
  int8_t y;

}mouse_packet_t;

//for entering idle mode when no activity is detected
typedef enum{
   MOUSE_ACTIVE,
   MOUSE_IDLE
}mouse_power_state_t;

volatile mouse_power_state_t power_state = MOUSE_ACTIVE; //default mouse power state
volatile uint32_t inactivity_ticks = 0; //for counting inside rtc1 callback, reset in gpiote handler

static volatile mouse_packet_t current_state; //an instance for asynchronous updating

///////////////////////////////////////


static uint32_t next_cc; // for updating the rtc after every interrupt

static nrfx_rtc_t rtc1 = NRFX_RTC_INSTANCE(1); //creates a RTC1 instance

#define RTC_TICKS_PER_SEC      32768UL
#define IDLE_TIMEOUT_TICKS     (10 * RTC_TICKS_PER_SEC)


#define RTC_INTERVAL_ACTIVE   33      // ~1 ms
#define RTC_INTERVAL_IDLE     3277    // ~100 ms

volatile uint32_t current_interval; //for holding the compare interval based on power state
volatile uint32_t last_activity_tick; //for checking for idle, updated in button handler

//RTC1 interrupt callback
static void rtc_handler(nrfx_rtc_int_type_t int_type)
{


    //inactivity_ticks++;

    /*
    if(inactivity_ticks >= 10000) //after ~10 seconds of inactivity set to idle
    {
        power_state = MOUSE_IDLE;
    
    }

    */

    uint32_t now = nrfx_rtc_counter_get(&rtc1);

    if((now - last_activity_tick) >= IDLE_TIMEOUT_TICKS)
    {
    
        power_state = MOUSE_IDLE;
        current_interval = RTC_INTERVAL_IDLE;
    }


    if (int_type == NRFX_RTC_INT_COMPARE0)
    {
        report_pending = true; //compare event fired, must send esb packet
        
        uint32_t now = nrfx_rtc_counter_get(&rtc1);

        if(power_state == MOUSE_ACTIVE)
        {
        
            //uint32_t now = nrfx_rtc_counter_get(&rtc1);
            nrfx_rtc_cc_set(&rtc1, 0, now + RTC_INTERVAL_ACTIVE, true); // ~1ms
        
        }
        else
        {
        
            nrfx_rtc_cc_set(&rtc1, 0, now + RTC_INTERVAL_IDLE, true);
        
        }
       
    }
}




//set up RTC1 
void rtc1_init(void)
{
    nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;

    // prescaler 0 = full 32.768 kHz resolution
    config.prescaler = 0;

    nrfx_rtc_init(&rtc1, &config, rtc_handler);

    // enable compare interrupt
    nrfx_rtc_cc_set(&rtc1, 0, RTC_INTERVAL_ACTIVE, true); // first trigger ~1ms

    nrfx_rtc_enable(&rtc1);
}



//radio transmission callback
void nrf_esb_event_handler(nrf_esb_evt_t const * p_event)
{
    switch (p_event->evt_id)
    {
        case NRF_ESB_EVENT_TX_SUCCESS:
            NRF_LOG_DEBUG("TX SUCCESS EVENT");
            

            uint32_t attempts = nrf_esb_last_tx_attempts_get();
            if(attempts > max_attempts)
            {
            
              max_attempts = attempts;
            
            }
           
            total_retries += (attempts-1); //get number of tx attempts from the radio
            packets_sent++; //another packet was transmitted
            
            tx_busy = false; //esb finished transmitting here
            break;
        case NRF_ESB_EVENT_TX_FAILED:
            NRF_LOG_DEBUG("TX FAILED EVENT");
            (void) nrf_esb_flush_tx();
            (void) nrf_esb_start_tx();
            tx_failures++; //count tx failures
            tx_busy = false;
            break;
        case NRF_ESB_EVENT_RX_RECEIVED:
            NRF_LOG_DEBUG("RX RECEIVED EVENT");
            while (nrf_esb_read_rx_payload(&rx_payload) == NRF_SUCCESS)
            {
                if (rx_payload.length > 0)
                {
                    NRF_LOG_DEBUG("RX RECEIVED PAYLOAD");
                }
            }
            break;
    }
}


//creates esb payload and sends//
static void try_send_report(void)
{
    
    static uint8_t tx_counter = 0;

    mouse_packet_t snapshot = current_state; //copy global mouse state into temp location, because it could change from interrupts

    snapshot.packet_sequence = tx_counter++; //put sequence number in payload incremented w/r to transmissions

    //copy current mouse state to esb tx payload
    memcpy(tx_payload.data, &snapshot, sizeof(snapshot));
    tx_payload.length = sizeof(snapshot);
    tx_payload.noack = false; 

    //send payload to radio buffer and initiate tx
    if (nrf_esb_write_payload(&tx_payload) == NRF_SUCCESS) //if writing to payload fails for some reason
    {
      tx_busy = true; //radio busy. no tx attempts allowed until cleared

    }
    else
    {
      NRF_LOG_WARNING("Sending packet failed");
    }
       
    report_pending = false; //report is being sent to RX dongle. clear for next RTC1 compare interrupt

}


void clocks_start( void )
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);
}



//gpiote button interrupt handler // GPIOTE interrupt updates button state
// Main loop uses button state to generate mouse movement packets.
static void button_handler(nrfx_gpiote_pin_t pin, nrf_gpiote_polarity_t action)
{

    //inactivity_ticks = 0; //reset inactivity counter on button change
    power_state = MOUSE_ACTIVE; //revert to active mouse state on button change
    
    bsp_board_led_off(BSP_BOARD_LED_0);

    last_activity_tick = nrfx_rtc_counter_get(&rtc1); //samples count count on button activity 
    
    if(current_interval != RTC_INTERVAL_ACTIVE)
    {
    
        current_interval = RTC_INTERVAL_ACTIVE;

        next_cc = nrfx_rtc_counter_get(&rtc1) + current_interval;

        nrfx_rtc_cc_set(&rtc1,0,next_cc,true);
    
    
    }

    //g_mouse_dirty = true; //mouse state changed due to button press/release

  
    if(nrf_gpio_pin_read(BUTTON_1) == 0)
    {
      current_state.x = -20;
      bsp_board_led_invert(BSP_BOARD_LED_2);
    }
    else if(nrf_gpio_pin_read(BUTTON_2) == 0)
    {
      current_state.x = 20;    
       bsp_board_led_invert(BSP_BOARD_LED_3);
    }
    else // if none or both pressed
    {
      current_state.x = 0;
    }

    current_state.y = 0; //vertical velocity ignored for now

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


uint32_t esb_init( void )
{
    uint32_t err_code;
    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8 };

    nrf_esb_config_t nrf_esb_config         = NRF_ESB_DEFAULT_CONFIG;
    nrf_esb_config.protocol                 = NRF_ESB_PROTOCOL_ESB_DPL;
    nrf_esb_config.retransmit_delay         = 600;
    nrf_esb_config.bitrate                  = NRF_ESB_BITRATE_2MBPS;
    nrf_esb_config.event_handler            = nrf_esb_event_handler;
    nrf_esb_config.mode                     = NRF_ESB_MODE_PTX;
    nrf_esb_config.selective_auto_ack       = false;

    nrf_esb_config.tx_output_power = NRF_ESB_TX_POWER_0DBM;

    err_code = nrf_esb_init(&nrf_esb_config);

    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_0(base_addr_0);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_1(base_addr_1);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_prefixes(addr_prefix, NRF_ESB_PIPE_COUNT);
    VERIFY_SUCCESS(err_code);

    return err_code;
}


static void lfclk_handler(nrfx_clock_evt_type_t event)
{
  
  if(event == NRFX_CLOCK_EVT_LFCLK_STARTED)
  {
    bsp_board_led_invert(BSP_BOARD_LED_3);
  }
}

int main(void)
{

    NVIC_EnableIRQ(RTC1_IRQn);
  
    ret_code_t err_code;

    //clock_init();
    
    volatile int a = 1;


    gpio_init();

    volatile int b = 2;

    buttons_init(); //for gpiote




    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEFAULT_BACKENDS_INIT();

    clocks_start(); //start hf clock

    err_code = nrfx_clock_init(lfclk_handler);

    nrfx_clock_lfclk_start();

    rtc1_init(); //initialize RTC1
   

    err_code = esb_init();


    APP_ERROR_CHECK(err_code);

    NRF_LOG_DEBUG("Enhanced ShockBurst Transmitter Example started.");

 
    /*
    ret_code_t err = nrfx_gpiote_init();
    //APP_ERROR_CHECK(err);

    if(err == NRFX_ERROR_INVALID_STATE)
    {
       bsp_board_led_on(BSP_BOARD_LED_2);
    }

    */


    SEGGER_RTT_printf(0, "Entering main loop\r\n");


    static uint16_t counter = 0;
    

    while (true)
    {

            static uint8_t idle_divider = 0;

            if(report_pending && !tx_busy)
            {
                
                try_send_report();
                                
               
                if(power_state == MOUSE_IDLE)
                {

                    if(idle_divider >= 10)
                    {
                    
                        bsp_board_led_invert(BSP_BOARD_LED_0);
                        idle_divider = 0;
                    }

                    idle_divider++; //increment if idle mode
                }

           }  
            __WFE(); //idles cpu while waiting for esb, gpiote, or rtc1 to fire interrupts
      

    }



}
