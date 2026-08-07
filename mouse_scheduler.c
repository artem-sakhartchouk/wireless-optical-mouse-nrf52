
#include "nrfx_clock.h"
#include "nrfx_rtc.h"
#include "app_error.h"

#include "mouse_scheduler.h"



#define RTC_COUNTER_MASK 0x00FFFFFFUL //ignore the upper 8 bits, counter is 24 bits
#define RTC_TICKS_PER_SEC      32768UL
#define IDLE_TIMEOUT_TICKS     (10 * RTC_TICKS_PER_SEC) //set power state to idle after 10 seconds of inactivity


#define RTC_INTERVAL_ACTIVE   33      // ~1 ms
#define RTC_INTERVAL_IDLE     3277    // ~100 ms



static nrfx_rtc_t m_counter = NRFX_RTC_INSTANCE(1); //creates a RTC1 instance



//volatile uint32_t current_interval; //for holding the compare interval based on power state
static volatile uint32_t m_last_activity_tick; //timer count at button press/motion, updated in button handler

static volatile mouse_power_state_t m_power_state = MOUSE_ACTIVE; //determines ESB transmit frequency

static volatile bool m_report_pending = false;

static uint32_t m_next_cc; // for incrementing rtc after every interrupt


//RTC1 interrupt callback
static void rtc_handler(nrfx_rtc_int_type_t int_type)
{


    //this is the only type of interrupt that should trigger based off config    
    if (int_type != NRFX_RTC_INT_COMPARE0) 
    {
        return;
    }

    
    m_report_pending = true; //compare event fired, must send esb packet

    uint32_t now = nrfx_rtc_counter_get(&m_counter); //get current count for idle check

    uint32_t elapsed = ((now - m_last_activity_tick) & RTC_COUNTER_MASK);

    if(elapsed >= IDLE_TIMEOUT_TICKS)
    {   
        m_power_state = MOUSE_IDLE;     
    }

    /* check power state in case button press/motion 
     * changed idle/active transmission scheduling */
    uint32_t interval = 
        (m_power_state == MOUSE_ACTIVE)
            ? RTC_INTERVAL_ACTIVE
            : RTC_INTERVAL_IDLE;

    m_next_cc += interval; //already on counter schedule, no need to read count
    
    nrfx_rtc_cc_set(&m_counter, 0, m_next_cc, true); 
    
}


//set up RTC1 and start counting
void mouse_scheduler_init(void)
{
    nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG; 
    config.prescaler = 0; // prescaler 0 = full 32.768 kHz resolution

    //explicitly clear the count after initialize
    nrfx_rtc_init(&m_counter, &config, rtc_handler);
    nrfx_rtc_counter_clear(&m_counter);

    m_next_cc = RTC_INTERVAL_ACTIVE;
   

    //set count threshold and enable compare interrupt
    nrfx_rtc_cc_set(&m_counter, 0, m_next_cc, true); // first trigger ~1ms

    nrfx_rtc_enable(&m_counter); //start the counter
}




void mouse_scheduler_mark_active(void)
{
    uint32_t now = nrfx_rtc_counter_get(&m_counter);

    m_last_activity_tick = now;

    if (m_power_state == MOUSE_IDLE)
    {
        m_power_state = MOUSE_ACTIVE;

        m_next_cc = now + RTC_INTERVAL_ACTIVE;

        nrfx_rtc_cc_set(&m_counter, 0, m_next_cc, true);
    }
}



bool mouse_scheduler_report_pending(void)
{

    return m_report_pending;
}


void mouse_scheduler_report_queued(void)
{
    m_report_pending = false;

}


mouse_power_state_t mouse_scheduler_power_state_get(void)
{

    return m_power_state;

}