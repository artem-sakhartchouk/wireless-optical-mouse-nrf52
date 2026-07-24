#ifndef STATUS_LEDS_H
#define STATUS_LEDS_H

#include <stdint.h>

//board led pin number definitions
typedef enum{
    LED_YELLOW,
    LED_RED,
    LED_GREEN,
    LED_BLUE,

    LED_COUNT
}led_t;


//possible led modes
typedef enum{
    LED_MODE_OFF,
    LED_MODE_ON,
    LED_MODE_BLINK
}led_mode_t;


void leds_init(void); //cofigures the gpio pins

void led_set_mode(led_t led, led_mode_t mode, uint32_t interval_ms); //configures led behavior (on, off, or blinking)

void led_on(led_t led);
void led_off(led_t led);

void led_write(led_t led, bool is_on);

#endif