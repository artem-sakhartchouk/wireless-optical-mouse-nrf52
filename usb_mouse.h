#ifndef USB_MOUSE_H
#define USB_MOUSE_H


/* Initialize the USB HID mouse interface. */
void usb_mouse_init(void);

/* Process button-triggered remote-wakeup requests from the main loop. */
void usb_mouse_remote_wakeup_process(void);


#endif
