#ifndef ESB_MOUSE_TX_H
#define ESB_MOUSE_TX_H

#include <stdbool.h>
#include <stdint.h>

#include "mouse_motion.h"


typedef enum
{
    ESB_MOUSE_TX_NONE,
    ESB_MOUSE_TX_SUCCESS,
    ESB_MOUSE_TX_FAILED
} esb_mouse_tx_result_t;


uint32_t esb_mouse_tx_init(void);

bool esb_mouse_tx_send(
    const mouse_motion_t *p_motion,
    uint8_t buttons
);

bool esb_mouse_tx_busy(void);

esb_mouse_tx_result_t esb_mouse_tx_result_take(void);


#endif