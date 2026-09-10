#ifndef ESB_MOUSE_H
#define ESB_MOUSE_H

#include <stdint.h>
#include <stdbool.h>


/**
* initialize esb to receive packets from wireless mouse
*/


typedef struct
{
    uint8_t buttons;
    int16_t x;
    int16_t y;
} mouse_input_report_t;




void esb_mouse_init(void);


bool esb_mouse_rx_peek(mouse_input_report_t *report);
void esb_mouse_rx_commit(const mouse_input_report_t *report);

bool esb_mouse_queue_prefix_ack(uint8_t prefix);

bool esb_mouse_remote_wakeup_take(void);

#endif 