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
void usb_mouse_process(void);

/**
 * Returns true when the USB host has configured the HID interface.
 */
bool usb_mouse_ready(void);

/**
 * Send a mouse movement report.
 *
 * Returns false if the endpoint is busy or USB is not configured.
 */
static bool usb_mouse_send(const uint8_t *data, uint8_t length);

bool usb_mouse_send_report(
    uint8_t buttons,
    int8_t x,
    int8_t y,
    int8_t wheel);

#endif