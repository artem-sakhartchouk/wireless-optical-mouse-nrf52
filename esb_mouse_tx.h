#ifndef ESB_MOUSE_TX_H
#define ESB_MOUSE_TX_H

#include <stdint.h>
#include <stdbool.h>

#include "mouse_motion.h"



uint32_t esb_mouse_tx_init(void); //everything needed to initialize esb

bool esb_mouse_tx_send(const mouse_motion_t *p_motion, uint8_t buttons); //keep mouse packet type internal to esb with sequence number

bool esb_mouse_tx_busy(void);

#endif