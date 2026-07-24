#include "esb_mouse.h"

#include <stdint.h>
#include "app_error.h"
#include "nrf_log.h"

#include "nrf_drv_clock.h" 
#include "proprietary_rf/esb/nrf_esb.h"

#include "usb_mouse.h"


static nrf_esb_payload_t m_rx_payload; //creates a private esb payload object



//mouse packet to store equivalent mouse packet from the ptx
typedef struct{
    
   uint8_t packet_sequence;
   int8_t x;
   int8_t y;
}mouse_packet_t;

/*esb event handler*/
static void nrf_esb_event_handler(nrf_esb_evt_t const * p_event)
{
    switch (p_event->evt_id)
    {
        case NRF_ESB_EVENT_TX_SUCCESS:
            NRF_LOG_DEBUG("TX SUCCESS");
            break;
        case NRF_ESB_EVENT_TX_FAILED:
            NRF_LOG_DEBUG("TX FAILED");
            break;
        case NRF_ESB_EVENT_RX_RECEIVED:
        {    

            ret_code_t ret = nrf_esb_read_rx_payload(&m_rx_payload);

            if (ret != NRF_SUCCESS)
            {

                NRF_LOG_WARNING("Failed reading ESB rx payload: %u", ret);
                break;
            }
                
            //if payload contains wrong packet length
            if(m_rx_payload.length < sizeof(mouse_packet_t))
            {
                NRF_LOG_WARNING(
                    "RX payload too short: %u bytes",
                    m_rx_payload.length
                );

                break;

            }
                   
                    
            mouse_packet_t const *p_packet = (mouse_packet_t*)m_rx_payload.data; //interp payload data location as start of typedef struct

                
            bool accepted = usb_mouse_send_report(
                        0, //buttons
                        p_packet ->x , //x delta
                        p_packet ->y, //y delta
                        0); //wheel

            if(!accepted)
            {
                NRF_LOG_DEBUG("USB mouse report not accepted");

            }

            NRF_LOG_DEBUG(
                "Mouse packet: sequence=%u, x=%d, y=%d",
                p_packet->packet_sequence,
                p_packet->x,
                p_packet->y
            );

            break;
        }
             
        default:
            break;
               
    }
           
    
}



void esb_mouse_init(void)
{
    

    static const uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};

    static const uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};

    static const uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8 };

    nrf_esb_config_t config = NRF_ESB_DEFAULT_CONFIG;


    config.payload_length     = 8;
    config.protocol           = NRF_ESB_PROTOCOL_ESB_DPL;
    config.bitrate            = NRF_ESB_BITRATE_2MBPS;
    config.mode               = NRF_ESB_MODE_PRX;
    config.event_handler      = nrf_esb_event_handler;
    config.selective_auto_ack = false;


    ret_code_t ret;

    ret = nrf_esb_init(&config);
    APP_ERROR_CHECK(ret);

    ret = nrf_esb_set_base_address_0(base_addr_0);
    APP_ERROR_CHECK(ret);

    
    ret = nrf_esb_set_base_address_1(base_addr_1);
    APP_ERROR_CHECK(ret);

    ret = nrf_esb_set_prefixes(addr_prefix, 8);
    APP_ERROR_CHECK(ret);

    ret = nrf_esb_start_rx();
    APP_ERROR_CHECK(ret);

}