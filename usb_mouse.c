#include "usb_mouse.h"
#include "sdk_errors.h"
#include "app_error.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_drv_usbd.h" 
#include "nrf_drv_power.h"
#include "nrf_gpio.h"
#include "status_leds.h"

#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif


#define STARTUP_DELAY 100 //usb initialization delay if power detection is not used


#define USB_REQ_SET_FEATURE        3
#define USB_REQ_CLEAR_FEATURE      1
#define USB_FEATURE_REMOTE_WAKEUP  1


/** Maximum size of the packed transfered by EP0 */
#define EP0_MAXPACKETSIZE NRF_DRV_USBD_EPSIZE



/******************************************************************************
 * String Descriptor Indices
 ******************************************************************************/
#define USBD_STRING_LANG_IX  0x00
#define USBD_STRING_MANUFACTURER_IX  0x01
#define USBD_STRING_PRODUCT_IX  0x02
#define USBD_STRING_SERIAL_IX  0x00


/******************************************************************************
 * USB Device Descriptor
 ******************************************************************************/

#define USBD_DEVICE_DESCRIPTOR \
    0x12,                        /* bLength | size of descriptor                                                  */\
    0x01,                        /* bDescriptorType | descriptor type                                             */\
    0x00, 0x02,                  /* bcdUSB | USB spec release (ver 2.0)                                           */\
    0x00,                        /* bDeviceClass ¦ class code (each interface specifies class information)        */\
    0x00,                        /* bDeviceSubClass ¦ device sub-class (must be set to 0 because class code is 0) */\
    0x00,                        /* bDeviceProtocol | device protocol (no class specific protocol)                */\
    EP0_MAXPACKETSIZE,           /* bMaxPacketSize0 | maximum packet size (64 bytes)                              */\
    0x15, 0x19,                  /* vendor ID  (0x1915 Nordic)                                                    */\
    0x0A, 0x52,                  /* product ID (0x520A nRF52 HID mouse on nrf_drv)                                */\
    0x01, 0x01,                  /* bcdDevice | final device release number in BCD Format                         */\
    USBD_STRING_MANUFACTURER_IX, /* iManufacturer | index of manufacturer string                                  */\
    USBD_STRING_PRODUCT_IX,      /* iProduct | index of product string                                            */\
    USBD_STRING_SERIAL_IX,       /* iSerialNumber | Serial Number string                                          */\
    0x01                         /* bNumConfigurations | number of configurations                                 */



static const uint8_t device_descriptor[] =
{
    USBD_DEVICE_DESCRIPTOR
};


/******************************************************************************
 * USB Configuration Descriptor
 ******************************************************************************/

#define DEVICE_SELF_POWERED 0
#define REMOTE_WU           1

//#define USBD_CONFIG_DESCRIPTOR_SIZE   9

#define USBD_CONFIG_DESCRIPTOR_FULL_SIZE   (9 + (9 + 9 + 7))

#define USBD_CONFIG_DESCRIPTOR  \
    0x09,         /* bLength | length of descriptor                                             */\
    0x02,         /* bDescriptorType | descriptor type (CONFIGURATION)                          */\
    USBD_CONFIG_DESCRIPTOR_FULL_SIZE, 0x00,    /* wTotalLength | total length of descriptor(s)  */\
    0x01,         /* bNumInterfaces                                                             */\
    0x01,         /* bConfigurationValue                                                        */\
    0x00,         /* index of string Configuration | configuration string index (not supported) */\
    0x80| (((DEVICE_SELF_POWERED) ? 1U:0U)<<6) | (((REMOTE_WU) ? 1U:0U)<<5), /* bmAttributes    */\
    49            /* maximum power in steps of 2mA (98mA)    



/******************************************************************************
 * USB Interface Descriptor
 ******************************************************************************/

#define USBD_INTERFACE0_DESCRIPTOR  \
    0x09,         /* bLength                                                                          */\
    0x04,         /* bDescriptorType | descriptor type (INTERFACE)                                    */\
    0x00,         /* bInterfaceNumber                                                                 */\
    0x00,         /* bAlternateSetting                                                                */\
    0x01,         /* bNumEndpoints | number of endpoints (1)                                          */\
    0x03,         /* bInterfaceClass | interface class (3..defined by USB spec: HID)                  */\
    0x00,         /* bInterfaceSubClass |interface sub-class (0.. no boot interface)                  */\
    0x02,         /* bInterfaceProtocol | interface protocol (1..defined by USB spec: mouse)          */\
    0x00          /* interface string index (not supported)                                           */




/******************************************************************************
 * USB Endpoint Descriptor
 ******************************************************************************/

#define USBD_ENDPOINT1_DESCRIPTOR  \
    0x07,         /* bLength | length of descriptor (7 bytes)                                     */\
    0x05,         /* bDescriptorType | descriptor type (ENDPOINT)                                 */\
    0x81,         /* bEndpointAddress | endpoint address (IN endpoint, endpoint 1)                */\
    0x03,         /* bmAttributes | endpoint attributes (interrupt)                               */\
    0x08,0x00,    /* bMaxPacketSizeLowByte,bMaxPacketSizeHighByte | maximum packet size (8 bytes) */\
    0x08          /* bInterval | polling interval (10ms)                                          */








/******************************************************************************
 * USB String Descriptors
 ******************************************************************************/


#define USBD_STRING_LANG \
    0x04,         /* length of descriptor                   */\
    0x03,         /* descriptor type                        */\
    0x09,         /*                                        */\
    0x04          /* Supported LangID = 0x0409 (US-English) */





#define USBD_STRING_MANUFACTURER \
    42,           /* length of descriptor (? bytes)   */\
    0x03,         /* descriptor type                  */\
    'N', 0x00,    /* Define Unicode String "Nordic Semiconductor  */\
    'o', 0x00, \
    'r', 0x00, \
    'd', 0x00, \
    'i', 0x00, \
    'c', 0x00, \
    ' ', 0x00, \
    'S', 0x00, \
    'e', 0x00, \
    'm', 0x00, \
    'i', 0x00, \
    'c', 0x00, \
    'o', 0x00, \
    'n', 0x00, \
    'd', 0x00, \
    'u', 0x00, \
    'c', 0x00, \
    't', 0x00, \
    'o', 0x00, \
    'r', 0x00




#define USBD_STRING_PRODUCT \
    72,           /* length of descriptor (? bytes)         */\
    0x03,         /* descriptor type                        */\
    'n', 0x00,    /* generic unicode string for all devices */\
    'R', 0x00, \
    'F', 0x00, \
    '5', 0x00, \
    '2', 0x00, \
    ' ', 0x00, \
    'U', 0x00, \
    'S', 0x00, \
    'B', 0x00, \
    ' ', 0x00, \
    'H', 0x00, \
    'I', 0x00, \
    'D', 0x00, \
    ' ', 0x00, \
    'm', 0x00, \
    'o', 0x00, \
    'u', 0x00, \
    's', 0x00, \
    'e', 0x00, \
    ' ', 0x00, \
    'o', 0x00, \
    'n', 0x00, \
    ' ', 0x00, \
    'n', 0x00, \
    'r', 0x00, \
    'f', 0x00, \
    '_', 0x00, \
    'd', 0x00, \
    'r', 0x00, \
    'v', 0x00, \
    ' ', 0x00, \
    'D', 0x00, \
    'e', 0x00, \
    'm', 0x00, \
    'o', 0x00, \



static const uint8_t language_string_descriptor[] =
{
    USBD_STRING_LANG
};

static const uint8_t manufacturer_string_descriptor[] =
{
    USBD_STRING_MANUFACTURER
};

static const uint8_t product_string_descriptor[] =
{
    USBD_STRING_PRODUCT
};


/******************************************************************************
 * USB HID Descriptor
 ******************************************************************************/

#define USBD_HID0_DESCRIPTOR  \
    0x09,         /* bLength | length of descriptor (9 bytes)                    */\
    0x21,         /* bHIDDescriptor | descriptor type (HID)                      */\
    0x11, 0x01,   /* HID wBcdHID | Spec version 01.11                            */\
    0x00,         /* bCountryCode | HW Target country                            */\
    0x01,         /* bNumDescriptors | Number of HID class descriptors to follow */\
    0x22,         /* bDescriptorType | Report descriptor type is 0x22 (report)   */\
    (uint8_t)(USBD_MOUSE_REPORT_DESCRIPTOR_SIZE),      /* Total length of Report descr., low byte */ \
    (uint8_t)(USBD_MOUSE_REPORT_DESCRIPTOR_SIZE / 256) /* Total length of Report descr., high byte */





/******************************************************************************
 * USB Report Descriptor
 ******************************************************************************/

#define USBD_MOUSE_REPORT_DESCRIPTOR_SIZE  46
#define USBD_MOUSE_REPORT_DESCRIPTOR \
    0x05, 0x01,     /* usage page (generic desktop). Global item, applies to all subsequent items   */\
    0x09, 0x02,     /* usage (mouse). Local item                                                    */\
    0xA1, 0x01,     /* collection (application)                                                     */\
    0x09, 0x01,     /* usage (pointer)                                                              */\
    0xA1, 0x00,     /* collection (physical)                                                        */\
    0x05, 0x09,     /*   usage page (buttons). Global item, applies to all subsequent items         */\
    0x19, 0x01,     /*   usage minimum (1)                                                          */\
    0x29, 0x08,     /*   usage maximum (8)                                                          */\
    0x15, 0x00,     /*   logical minimum (0)                                                        */\
    0x25, 0x01,     /*   logical maximum (1)                                                        */\
    0x95, 0x08,     /*   report count (8)                                                           */\
    0x75, 0x01,     /*   report size (1)                                                            */\
    0x81, 0x02,     /*   input (data, var, abs)                                                     */\
    0x05, 0x01,     /*   usage page (generic desktop). Global item, applies to all subsequent items */\
    0x15, 0x81,     /*   logical minimum (-127)                                                     */\
    0x25, 0x7F,     /*   logical maximum (127)                                                      */\
    0x75, 0x08,     /*   report size (8)                                                            */\
    0x09, 0x30,     /*   usage (X)                                                                  */\
    0x09, 0x31,     /*   usage (Y)                                                                  */\
    0x09, 0x38,     /*   usage wheel                                                                */\
    0x95, 0x03,     /*   report count (3)                                                           */\
    0x81, 0x06,     /*   input (3 position bytes X, Y & roller)                                     */\
    0xC0,           /* end collection                                                               */\
    0xC0            /* End Collection                                                               */



static const uint8_t hid_report_descriptor[] =
{
    USBD_MOUSE_REPORT_DESCRIPTOR
}; 

static const uint8_t configuration_descriptor[] =
{
    USBD_CONFIG_DESCRIPTOR,
    USBD_INTERFACE0_DESCRIPTOR,
    USBD_HID0_DESCRIPTOR,
    USBD_ENDPOINT1_DESCRIPTOR
};

/******************************************************************************
 * USB Control Response Data
 ******************************************************************************/


static const uint8_t configured_response[] = {1};
static const uint8_t unconfigured_response[] = {0};


static const uint8_t device_status[] =
{
    DEVICE_SELF_POWERED ? 0x01 : 0x00,
    0x00
};

static const uint8_t device_status_remote_wakeup[] =
{
    (DEVICE_SELF_POWERED ? 0x01 : 0x00) | 0x02,
    0x00
};




static const uint8_t interface_status[] = {0,0};

static const uint8_t endpoint_status_halted[] = {1,0};

static const uint8_t endpoint_status_active[] = {0,0};



/* Sizes of sub-descriptors within configuration descriptor */

#define CONFIG_DESCRIPTOR_SIZE      9
#define INTERFACE_DESCRIPTOR_SIZE   9
#define HID_DESCRIPTOR_SIZE         9
#define ENDPOINT_DESCRIPTOR_SIZE    7

_Static_assert(
    sizeof(configuration_descriptor) == USBD_CONFIG_DESCRIPTOR_FULL_SIZE,
    "Descriptor size mismatch!"
);

static const uint8_t * const interface_descriptor =
    &configuration_descriptor[9];

static const uint8_t * const hid_descriptor =
    &configuration_descriptor[9 + INTERFACE_DESCRIPTOR_SIZE];

static const uint8_t * const endpoint_descriptor =
    &configuration_descriptor[
        9 +
        INTERFACE_DESCRIPTOR_SIZE +
        HID_DESCRIPTOR_SIZE];



/******************************************************************************
 * USB Module State
 ******************************************************************************/

static volatile bool m_usbd_configured = false; //set if device is configured and ready to transmit data
static bool m_usbd_suspended = false; //set if device suspended by host
static volatile bool m_usbd_rwu_enabled = false; //host can enable remote wakeup
static volatile bool m_send_mouse_position = false; //set when usb is busy sending data to host
static volatile bool m_usbd_suspend_state_req = false; //set if host wants to suspend the device

static uint8_t m_previous_buttons; //global for storing incoming button states for click detect for usb rwu

/******************************************************************************
 * USB Helper Functions
 ******************************************************************************/

static ret_code_t ep_configuration(uint8_t index)
{
    if ( index == 1 )
    {
        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPIN1);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN1);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPIN1);
        m_usbd_configured = true;
        led_on(LED_GREEN);
        //led_set_mode(LED_GREEN,LED_MODE_BLINK,500);
        //nrf_gpio_pin_clear(12); // force OFF when configured
        nrf_drv_usbd_setup_clear();
    }
    else if ( index == 0 )
    {
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPIN1);
        m_usbd_configured = false;
        nrf_drv_usbd_setup_clear();
    }
    else
    {
        return NRF_ERROR_INVALID_PARAM;
    }
    return NRF_SUCCESS;
}


static void respond_setup_data(
    nrf_drv_usbd_setup_t const * const p_setup,
    void const * p_data, size_t size)
{
    /* Check the size against required response size */
    if (size > p_setup->wLength)
    {
        size = p_setup->wLength;
    }
    ret_code_t ret;
    nrf_drv_usbd_transfer_t transfer =
    {
        .p_data = {.tx = p_data},
        .size = size
    };
    ret = nrf_drv_usbd_ep_transfer(NRF_DRV_USBD_EPIN0, &transfer);
    if (ret != NRF_SUCCESS)
    {
        NRF_LOG_ERROR("Transfer starting failed: %d", (uint32_t)ret);
    }
    ASSERT(ret == NRF_SUCCESS);
    UNUSED_VARIABLE(ret);
}


/******************************************************************************
 * USB Setup Request Handlers
 ******************************************************************************/

static void usbd_setup_GetStatus(nrf_drv_usbd_setup_t const * const p_setup)
{
    switch (p_setup->bmRequestType)
    {
    case 0x80: // Device
        if (((p_setup->wIndex) & 0xff) == 0)
        {
            respond_setup_data(
                p_setup,
                m_usbd_rwu_enabled ? device_status_remote_wakeup : device_status,
                sizeof(device_status));
            return;
        }
        break;
    case 0x81: // Interface
        if (m_usbd_configured) // Respond only if configured
        {
            if (((p_setup->wIndex) & 0xff) == 0) // Only interface 0 supported
            {
                respond_setup_data(
                    p_setup,
                    interface_status,
                    sizeof(interface_status));
                return;
            }
        }
        break;
    case 0x82: // Endpoint
        if (((p_setup->wIndex) & 0xff) == 0) // Endpoint 0
        {
            respond_setup_data(
                p_setup,
                endpoint_status_active,
                sizeof(endpoint_status_active));
            return;
        }
        if (m_usbd_configured) // Other endpoints responds if configured
        {
            if (((p_setup->wIndex) & 0xff) == NRF_DRV_USBD_EPIN1)
            {
                if (nrf_drv_usbd_ep_stall_check(NRF_DRV_USBD_EPIN1))
                {
                    respond_setup_data(
                        p_setup,
                        endpoint_status_halted,
                        sizeof(endpoint_status_halted));
                    return;
                }
                else
                {
                    respond_setup_data(
                        p_setup,
                        endpoint_status_active,
                        sizeof(endpoint_status_active));
                    return;
                }
            }
        }
        break;
    default:
        break; // Just go to stall
    }
    NRF_LOG_ERROR("Unknown status: 0x%2x", p_setup->bmRequestType);
    nrf_drv_usbd_setup_stall();
}



static void usbd_setup_ClearFeature(nrf_drv_usbd_setup_t const * const p_setup)
{
    if ((p_setup->bmRequestType) == 0x02) // standard request, recipient=endpoint
    {
        if ((p_setup->wValue) == 0)
        {
            if ((p_setup->wIndex) == NRF_DRV_USBD_EPIN1)
            {
                nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN1);
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    else if ((p_setup->bmRequestType) ==  0x0) // standard request, recipient=device
    {
        if (REMOTE_WU)
        {
            if ((p_setup->wValue) == 1) // Feature Wakeup
            {
                m_usbd_rwu_enabled = false;
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    NRF_LOG_ERROR("Unknown feature to clear");
    nrf_drv_usbd_setup_stall();
}



static void usbd_setup_SetFeature(nrf_drv_usbd_setup_t const * const p_setup)
{
    if ((p_setup->bmRequestType) == 0x02) // standard request, recipient=endpoint
    {
        if ((p_setup->wValue) == 0) // Feature HALT
        {
            if ((p_setup->wIndex) == NRF_DRV_USBD_EPIN1)
            {
                nrf_drv_usbd_ep_stall(NRF_DRV_USBD_EPIN1);
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    else if ((p_setup->bmRequestType) ==  0x0) // standard request, recipient=device
    {

        if (p_setup->bRequest == USB_REQ_SET_FEATURE)
        {
            if (p_setup->wValue == USB_FEATURE_REMOTE_WAKEUP)
            {
                m_usbd_rwu_enabled = true;
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
        else if (p_setup->bRequest == USB_REQ_CLEAR_FEATURE)
        {
            if (p_setup->wValue == USB_FEATURE_REMOTE_WAKEUP)
            {
                m_usbd_rwu_enabled = false;
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
      
    }

    NRF_LOG_ERROR("Unknown feature to set");
    nrf_drv_usbd_setup_stall();
}


static void usbd_setup_GetDescriptor(nrf_drv_usbd_setup_t const * const p_setup)
{
    //determine which descriptor has been asked for
    switch ((p_setup->wValue) >> 8)
    {
    case 1: // Device
        if ((p_setup->bmRequestType) == 0x80)
        {
            respond_setup_data(
                p_setup,
                device_descriptor,
                sizeof(device_descriptor));
            return;
        }
        break;
    case 2: // Configuration
        if ((p_setup->bmRequestType) == 0x80)
        {
            respond_setup_data(
                p_setup,
                configuration_descriptor,
                USBD_CONFIG_DESCRIPTOR_FULL_SIZE);
            return;
        }
        break;
    case 3: // String
        if ((p_setup->bmRequestType) == 0x80)
        {
            // Select the string
            switch ((p_setup->wValue) & 0xFF)
            {
            case USBD_STRING_LANG_IX:
                respond_setup_data(
                    p_setup,
                    language_string_descriptor,
                    sizeof(language_string_descriptor));
                return;
            case USBD_STRING_MANUFACTURER_IX:
                respond_setup_data(
                    p_setup,
                    manufacturer_string_descriptor,
                    sizeof(manufacturer_string_descriptor));
                return;
            case USBD_STRING_PRODUCT_IX:
                respond_setup_data(p_setup,
                    product_string_descriptor,
                    sizeof(product_string_descriptor));
                return;
            default:
                break;
            }
        }
        break;
    case 4: // Interface
        if ((p_setup->bmRequestType) == 0x80)
        {
            // Which interface?
            if ((((p_setup->wValue) & 0xFF) == 0))
            {
                respond_setup_data(
                    p_setup,
                    interface_descriptor,
                    INTERFACE_DESCRIPTOR_SIZE);
                return;
            }
        }
        break;
    case 5: // Endpoint
        if ((p_setup->bmRequestType) == 0x80)
        {
            // Which endpoint?
            if (((p_setup->wValue) & 0xFF) == 1)
            {
                respond_setup_data(
                    p_setup,
                    endpoint_descriptor,
                    ENDPOINT_DESCRIPTOR_SIZE);
                return;
            }
        }
        break;
    case 0x21: // HID
        if ((p_setup->bmRequestType) == 0x81)
        {
            // Which interface
            if (((p_setup->wValue) & 0xFF) == 0)
            {
                respond_setup_data(
                    p_setup,
                    hid_descriptor,
                    HID_DESCRIPTOR_SIZE);
                return;
            }
        }
        break;
    case 0x22: // HID report
        if ((p_setup->bmRequestType) == 0x81)
        {
            // Which interface?
            if (((p_setup->wValue) & 0xFF) == 0)
            {
                respond_setup_data(
                    p_setup,
                    hid_report_descriptor,
                    sizeof(hid_report_descriptor));
                return;
            }
        }
        break;
    default:
        break; // Not supported - go to stall
    }

    NRF_LOG_ERROR("Unknown descriptor requested: 0x%2x, type: 0x%2x or value: 0x%2x",
        p_setup->wValue >> 8,
        p_setup->bmRequestType,
        p_setup->wValue & 0xFF);
    nrf_drv_usbd_setup_stall();
}


static void usbd_setup_GetConfig(nrf_drv_usbd_setup_t const * const p_setup)
{
    if (m_usbd_configured)
    {
        respond_setup_data(
            p_setup,
            configured_response,
            sizeof(configured_response));
    }
    else
    {
        respond_setup_data(
            p_setup,
            unconfigured_response,
            sizeof(unconfigured_response));
    }
}


static void usbd_setup_SetConfig(nrf_drv_usbd_setup_t const * const p_setup)
{
    if ((p_setup->bmRequestType) == 0x00)
    {
        // accept only 0 and 1
        if (((p_setup->wIndex) == 0) && ((p_setup->wLength) == 0) &&
            ((p_setup->wValue) <= UINT8_MAX))
        {
            if (NRF_SUCCESS == ep_configuration((uint8_t)(p_setup->wValue)))
            {
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    NRF_LOG_ERROR("Wrong configuration: Index: 0x%2x, Value: 0x%2x.",
        p_setup->wIndex,
        p_setup->wValue);
    nrf_drv_usbd_setup_stall();
}


static void usbd_setup_SetIdle(nrf_drv_usbd_setup_t const * const p_setup)
{
    if (p_setup->bmRequestType == 0x21)
    {
        //accept any value
        nrf_drv_usbd_setup_clear();
        return;
    }
    NRF_LOG_ERROR("Set Idle wrong type: 0x%2x.", p_setup->bmRequestType);
    nrf_drv_usbd_setup_stall();
}


static void usbd_setup_SetInterface(nrf_drv_usbd_setup_t const * const p_setup)
{
    //no alternate setting is supported - STALL always
    NRF_LOG_ERROR("No alternate interfaces supported.");
    nrf_drv_usbd_setup_stall();
}


static void usbd_setup_SetProtocol(nrf_drv_usbd_setup_t const * const p_setup)
{
    if (p_setup->bmRequestType == 0x21)
    {
        //accept any value
        nrf_drv_usbd_setup_clear();
        return;
    }
    NRF_LOG_ERROR("Set Protocol wrong type: 0x%2x.", p_setup->bmRequestType);
    nrf_drv_usbd_setup_stall();
}


/******************************************************************************
 * USB Power Handler
 ******************************************************************************/

static void power_usb_event_handler(nrf_drv_power_usb_evt_t event)
{
    switch (event)
    {
    case NRF_DRV_POWER_USB_EVT_DETECTED:
        NRF_LOG_INFO("USB power detected");
        if (!nrf_drv_usbd_is_enabled())
        {
            nrf_drv_usbd_enable();
        }
        break;
    case NRF_DRV_POWER_USB_EVT_REMOVED:
        NRF_LOG_INFO("USB power removed");
        m_usbd_configured = false;
        m_send_mouse_position = false;
        if (nrf_drv_usbd_is_started())
        {
            nrf_drv_usbd_stop();
        }
        if (nrf_drv_usbd_is_enabled())
        {
            nrf_drv_usbd_disable();
        }
        /* Turn OFF LEDs */
        break;
    case NRF_DRV_POWER_USB_EVT_READY:
        NRF_LOG_INFO("USB ready");
        
        if (!nrf_drv_usbd_is_started())
        {
            nrf_drv_usbd_start(true);
        }
        break;
    default:
        ASSERT(false);
    }
}

/******************************************************************************
 * USB Mouse Globals
 ******************************************************************************/



//defines values for usb state to avoid data collision
typedef enum{
   USB_TX_IDLE,
   USB_TX_BUSY
}usb_tx_state_t;


typedef struct{ 
  usb_tx_state_t state; //IN ep status
  uint8_t buffer[8]; //global IN buffer
  uint8_t length; //length of data packet
  bool pending; //flag for new data
}usb_tx_ctx_t;


//global endpoint transfer struct instance
static usb_tx_ctx_t m_tx = 
{
    .state = USB_TX_IDLE,
    .length = 0,
    .pending = false
}; 


//hid mouse report definition
typedef struct
{
    uint8_t buttons;
    int8_t x;
    int8_t y;
    int8_t wheel;
    //uint8_t reserved[4];
} hid_mouse_report_t;


/******************************************************************************
 * Custom Helper Function
 ******************************************************************************/


//for converting usbd errors numbers to text identifiers
const char* usb_err_to_str(ret_code_t err)
{
    switch (err)
    {
        case NRF_SUCCESS: return "NRF_SUCCESS";
        case NRF_ERROR_BUSY: return "NRF_ERROR_BUSY";
        case NRF_ERROR_INVALID_STATE: return "NRF_ERROR_INVALID_STATE";
        case NRF_ERROR_NO_MEM: return "NRF_ERROR_NO_MEM";
        case NRF_ERROR_INVALID_PARAM: return "NRF_ERROR_INVALID_PARAM";
        default: return "UNKNOWN_ERROR";
    }
}


/******************************************************************************
 * USB Event Handlers
 ******************************************************************************/

//modular function that is called on SOF event interrupts, for consistent report timing
void usb_on_sof(void)
{
    if (!m_usbd_configured|| m_usbd_suspended) //abort if usbd not configured
        return;

    if (m_tx.pending && m_tx.state == USB_TX_IDLE)
    {
        
        nrfx_usbd_transfer_t transfer = {
            .p_data.tx =  m_tx.buffer,
            .size = sizeof(hid_mouse_report_t),
            .flags = 0
        };


        ret_code_t err = nrfx_usbd_ep_transfer(
            NRF_DRV_USBD_EPIN1,
            &transfer
        );

        if (err == NRF_SUCCESS)
        {
            m_tx.state = USB_TX_BUSY;
            m_tx.pending = false;
        }
        else if (err == NRF_ERROR_INVALID_STATE)
        {
            m_tx.state = USB_TX_IDLE; // recover
        }
        else
        {
            printf("USB TX error: %s (%d)\n", usb_err_to_str(err), err);
            m_tx.state = USB_TX_IDLE;
        }
    }
}


 //usbd event handler
static void usbd_event_handler(nrf_drv_usbd_evt_t const * const p_event)
{
    switch (p_event->type)
    {


    case NRFX_USBD_EVT_SUSPEND:
    {
    
         m_usbd_suspended = true;
         break;
    }

    case NRFX_USBD_EVT_RESUME:
    {
    
        m_usbd_suspended = false;
        break;
    }


    case NRF_DRV_USBD_EVT_RESET:
        {
            ret_code_t ret = ep_configuration(0);
            ASSERT(ret == NRF_SUCCESS);
            UNUSED_VARIABLE(ret);
            m_usbd_suspend_state_req = false;
            break;
        }
    case NRF_DRV_USBD_EVT_SOF:
        {

            usb_on_sof();  
            break;
        }
    case NRF_DRV_USBD_EVT_EPTRANSFER:
        if (NRF_DRV_USBD_EPIN1 == p_event->data.eptransfer.ep)
        {

            m_tx.state = USB_TX_IDLE; //endpoint IN1 has successfully sent data to host and is available
            //m_send_mouse_position = false;
        }
        else
        if (NRF_DRV_USBD_EPIN0 == p_event->data.eptransfer.ep)
        {
            if (NRF_USBD_EP_OK == p_event->data.eptransfer.status)
            {
                /* Transfer ok - allow status stage */
                nrf_drv_usbd_setup_clear();
            }
            else if (NRF_USBD_EP_ABORTED == p_event->data.eptransfer.status)
            {
                /* Just ignore */
                NRF_LOG_INFO("Transfer aborted event on EPIN0");
            }
            else
            {
                NRF_LOG_ERROR("Transfer failed on EPIN0: %d", p_event->data.eptransfer.status);
                nrf_drv_usbd_setup_stall();
            }
        }

        break;
    case NRF_DRV_USBD_EVT_SETUP:
        {
            nrf_drv_usbd_setup_t setup;
            nrf_drv_usbd_setup_get(&setup);
            switch (setup.bRequest)
            {
            case 0x00: // GetStatus
                usbd_setup_GetStatus(&setup);
                break;
            case 0x01: // CleartFeature
                usbd_setup_ClearFeature(&setup);
                break;
            case 0x03: // SetFeature
                usbd_setup_SetFeature(&setup);
                break;
            case 0x05: // SetAddress
                //nothing to do, handled by hardware; but don't STALL
                break;
            case 0x06: // GetDescriptor
                usbd_setup_GetDescriptor(&setup);
                break;
            case 0x08: // GetConfig
                usbd_setup_GetConfig(&setup);
                break;
            case 0x09: // SetConfig
                usbd_setup_SetConfig(&setup);
                break;
            //HID class
            case 0x0A: // SetIdle
                usbd_setup_SetIdle(&setup);
                break;
            case 0x0B: // SetProtocol or SetInterface
                if (setup.bmRequestType == 0x01) // standard request, recipient=interface
                {
                    usbd_setup_SetInterface(&setup);
                }
                else if (setup.bmRequestType == 0x21) // class request, recipient=interface
                {
                    usbd_setup_SetProtocol(&setup);
                }
                else
                {
                    NRF_LOG_ERROR("Command 0xB. Unknown request: 0x%2x", setup.bmRequestType);
                    nrf_drv_usbd_setup_stall();
                }
                break;
            default:
                NRF_LOG_ERROR("Unknown request: 0x%2x", setup.bRequest);
                nrf_drv_usbd_setup_stall();
                return;
            }
            break;
        }
    default:
        break;
    }
}


//initialize the usb device
void usb_mouse_init(void)
{
    ret_code_t ret;

    ret = nrf_drv_usbd_init(usbd_event_handler);

    APP_ERROR_CHECK(ret);

    nrf_drv_usbd_ep_max_packet_size_set(
        NRF_DRV_USBD_EPOUT0,
        EP0_MAXPACKETSIZE);

    nrf_drv_usbd_ep_max_packet_size_set(
        NRF_DRV_USBD_EPIN0,
        EP0_MAXPACKETSIZE);


    if (USBD_POWER_DETECTION)
    {
        static const nrf_drv_power_usbevt_config_t config =
        {
            .handler = power_usb_event_handler
        };

        ret = nrf_drv_power_usbevt_init(&config);
        APP_ERROR_CHECK(ret);
    }

    else
    {
        NRF_LOG_INFO("No USB power detection enabled\r\nStarting USB now");
        nrf_delay_us(STARTUP_DELAY);
        if (!nrf_drv_usbd_is_enabled())
        {
            nrf_drv_usbd_enable();
            ret = ep_configuration(0);
            APP_ERROR_CHECK(ret);
        }
        /* Wait for regulator power up */
        while (NRF_DRV_POWER_USB_STATE_CONNECTED
              ==
              nrf_drv_power_usbstatus_get())
        {
            /* Just waiting */
        }

        if (NRF_DRV_POWER_USB_STATE_READY == nrf_drv_power_usbstatus_get())
        {
            if (!nrf_drv_usbd_is_started())
            {
                nrf_drv_usbd_start(true);
            }
        }
        else
        {
            nrf_drv_usbd_disable();
        }
    }

}




//function called by the esb module when rx packet with motion payload arrives updates global mouse state struct 
static bool usb_mouse_send(const uint8_t *data, uint8_t length)
{
    if(length > sizeof(m_tx.buffer)) //might only be possible due to programming error. consider changing
    {
        return false;
    }

    //store data in global usbd buffer for IN ep transmission
    memcpy(m_tx.buffer, data, length);
    m_tx.length = length;
    m_tx.pending = true;

    return true;
}

//function called by the esb module when rx packet with motion payload arrives updates global mouse state struct
bool usb_mouse_send_report(
    uint8_t buttons,
    int8_t x,
    int8_t y,
    int8_t wheel)
{


    uint8_t new_click = buttons & (uint8_t)~m_previous_buttons; //check for new presses

    m_previous_buttons = buttons; //capture current button state 



    //build hid report struct for usb transmission
    hid_mouse_report_t report =
    {
        .buttons = buttons,
        .x = x,
        .y = y,
        .wheel = wheel
    };
        
    //store new report to global usbd buffer
    bool accepted =  usb_mouse_send(
        (const uint8_t *)&report,
        sizeof(report));

    //if host suspended the device, request remote wakeup on button click+
    if(m_usbd_suspended)
    {
        if(m_usbd_rwu_enabled && new_click != 0)
        {
            (void)nrf_drv_usbd_wakeup_req();
        }
    
        /* next report hid report is queued but wont 
           transmit until resume and sof event */
    }

    return accepted; //return true if report was successfully stored inside of the global buffer

}