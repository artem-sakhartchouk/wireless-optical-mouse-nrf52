#include "status_leds.h"
#include "nrf_gpio.h"

#include "app_timer.h"
#include "app_error.h"

#define LED_TIMER_PERIOD_MS 100


typedef struct
{
   led_mode_t mode;
   bool is_on;
   uint32_t interval_ms;
   uint32_t elapsed_ms;
}led_state_t;



//an array of gpio pin bitmasks for gpio functions

static const uint32_t m_led_pins[LED_COUNT] = 
{
    [LED_YELLOW] = NRF_GPIO_PIN_MAP(0,6);
    [LED_RED] = NRF_GPIO_PIN_MAP(0,8);
    [LED_GREEN] = NRF_GPIO_PIN_MAP(1,9);
    [LED_BLUE] = NRF_GPIO_PIN_MAP(0,12);

}


static led_state_t m_led_states[LED_COUNT]; //array of led states


APP_TIMER_DEF(m_led_timer); //declare global app timer instance


//called on app timer interrupt every timer period 
static void led_timer_handler(void *p_context)
{

    UNUSED_PARAMETER(p_context);

    for(uint32_t i = 0; i <LED_COUNT; i++)
    {
        
        led_state_t *p_state = &m_led_states[i]; //create pointer to led state struct array

        if(p_state->mode != LED_MODE_BLINK)
        {
            continue; //skip the rest if led not blinking (timer callback should ignore it
        }

        p_state->elapsed_ms += LED_TIMER_PERIOD_MS;

        if(p_state->elapsed_ms >= p_state->interval_ms)
        {
        
            p_state->elapsed_ms = 0;
            p_state->is_on = !(p_state->is_on);

            led_write((led_t)i,p_state->is_on);
        }
    
    }

}



//configures led gpio pins as outputs and turns off all leds
void leds_init(void)
{
    for(uint32_t i = 0; i < LED_COUNT; ++i)
    {

        //configures the gpio pins as outputs
        nrf_gpio_cfg_output(m_led_pins[i]);  
        
        //leds active low. this turns them off initially 
        nrf_gpio_pin_set(m_led_pins[i]); 

        m_led_states[i].mode = LED_MODE_OFF;
        m_led_states[i].is_on = false;
        m_led_states[i].interval_ms = 0;
        m_led_states[i].elapsed_ms = 0;

            
    
    }

    ret_code_t ret;

    /*initialize the timer module*/
    ret = app_timer_init();
    APP_ERROR_CHECK(ret);

    //create a timer instance
    ret = app_timer_create(&m_led_timer, APP_TIMER_MODE_REPEATED, led_timer_handler);
    APP_ERROR_CHECK(ret);

    //start the timer. handler will be called each time count reaches 100 ms
    ret = app_timer_start(m_led_timer, APP_TIMER_TICKS(LED_TIMER_PERIOD_MS),NULL);
    APP_ERROR_CHECK(ret);

}



//general low-level function for setting led pins 
static void led_write(led_t led, bool is_on)
{

    if(led >= LED_COUNT)
    {
        return;
    }

    if(is_on)
    {
        nrf_gpio_pin_clear(m_led_pin[led]);
    }
    else 
    {
        nrf_gpio_pin_set(m_led_pin[led]);
    }

}

//turns on a specific led by setting output low and connecting the circuit
void led_on(led_t led)
{
    if(led >= LED_COUNT)
    {
        return;
    }

    CRITICAL_REGION_ENTER();

    m_led_states[led].mode = LED_MODE_ON;
    m_led_states[led].is_on = true;
    m_led_states[led].elapsed_ms = 0;
    m_led_states[led].interval_ms = 0;

    CRITICAL_REGION_EXIT();



    led_write(led,true);


}



void led_off(led_t led)
{
    if(led >= LED_COUNT)
    {
        return;
    }


    CRITICAL_REGION_ENTER();

    m_led_states[led].mode = LED_MODE_OFF;
    m_led_states[led].is_on = false;
    m_led_states[led].elapsed_ms = 0;
    m_led_states[led].interval_ms = 0;

    CRITICAL_REGION_EXIT();


    led_write(led,false);

}

/*
void led_toggle(led_t led)
{
    if(led >= LED_COUNT)
    {
        return;
    }

    nrf_gpio_pin_toggle(m_led_pins[led]);

}
*/



//configures the led mode, used by usb and esb
void led_set_mode(led_t led, led_mode_t mode, uint32_t interval_ms)
{
    
    if(led >= LED_COUNT)
    {
        return;
    }

    if((mode == LED_MODE_BLINK) && (interval_ms == 0))
    {
        return;
    }

    CRITICAL_REGION_ENTER();

    led_state_t *p_state = &m_led_states[led];

    p_state->mode = mode;
    p_state->interval_ms = interval_ms;
    p_state->elapsed_ms = 0;

    switch(mode)
    {
        case LED_MODE_OFF:
            p_state->is_on = false;
            break;

        case LED_MODE_ON:
            p_state->is_on = true;
            break;

        case LED_MODE_BLINK:
            p_state->is_on = true;
            break;

        default:
            break;   
    
    }


    bool turn_on = p_state->is_on;


    CRITICAL_REGION_EXIT();


    led_write(led,turn_on);
}