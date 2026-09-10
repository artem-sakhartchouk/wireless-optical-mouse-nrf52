#ifndef USB_MOUSE_H
#define USB_MOUSE_H

#include <stdbool.h>
#include <stdint.h>




/**
 * Initialize the USB HID mouse.
 */
void usb_mouse_init(void);

/**
 * Perform USB background processing.
 *
 * Call repeatedly from the main loop.
 */
void usb_mouse_remote_wakeup_process(void);

/**
 * Returns true when the USB host has configured the HID interface.
 */
bool usb_mouse_ready(void);



bool usb_mouse_send_report(
    uint8_t buttons,
    int16_t x,
    int16_t y,
    int8_t wheel);



/**
 * Returns true when the USB host has configured the HID interface.
 */
void usb_mouse_request_remote_wakeup(void);

#endif