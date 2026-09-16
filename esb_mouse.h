#ifndef ESB_MOUSE_H
#define ESB_MOUSE_H

#include <stdbool.h>
#include <stdint.h>


typedef struct
{
    uint8_t buttons;
    int16_t x;
    int16_t y;
} mouse_input_report_t;


/* Initialize ESB as the wireless-mouse PRX. */
void esb_mouse_init(void);

/* Snapshot and commit pending receiver-side mouse input. */
bool esb_mouse_rx_peek(mouse_input_report_t *report);
void esb_mouse_rx_commit(const mouse_input_report_t *report);

/* Read and clear a pending button-triggered remote-wakeup request. */
bool esb_mouse_remote_wakeup_take(void);


#endif
