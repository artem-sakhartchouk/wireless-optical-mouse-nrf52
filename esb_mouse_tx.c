#include "esb_mouse_tx.h"

#include <stdint.h>
#include <stdbool.h>

#include "nrf_esb.h"
#include "nrf_error.h"
#include "nrf_log.h"
#include "nrf_error.h"

//mouse movement data packet definition
typedef struct __attribute__((packed))
{
  uint8_t packet_sequence;
  uint8_t buttons;
  int16_t x;
  int16_t y;

}mouse_packet_t;

_Static_assert(sizeof(mouse_packet_t) == 6,
               "Unexpected ESB mouse packet size");

// esb payloads
static nrf_esb_payload_t        tx_payload = 
{
    .pipe = 0,
    .length = sizeof(mouse_packet_t),
    .noack = false
};

static nrf_esb_payload_t        rx_payload; //to recieve ack payloads from the dongle esb receiver

static volatile bool m_tx_busy = false; //set on esb send and cleared in esb event handler
static uint8_t m_tx_count = 0; //for packet sequencing
static volatile uint32_t max_attempts = 0; //to calculate maximum attemps

//count esb packets sent as well as total retries for radio link integrity testing
volatile uint32_t packets_sent = 0;
volatile uint32_t total_retries = 0;
volatile uint32_t tx_failures = 0; //count tx failures




//radio transmission callback
void nrf_esb_event_handler(nrf_esb_evt_t const * p_event)
{
    switch (p_event->evt_id)
    {
        case NRF_ESB_EVENT_TX_SUCCESS:
            NRF_LOG_DEBUG("TX SUCCESS EVENT");
            

            uint32_t attempts = nrf_esb_last_tx_attempts_get();
            if(attempts > max_attempts)
            {
            
              max_attempts = attempts;
            
            }
           
            total_retries += (attempts-1); //get number of tx attempts from the radio
            packets_sent++; //another packet was transmitted
            
            m_tx_busy = false; //esb finished transmitting here
            break;


        case NRF_ESB_EVENT_TX_FAILED:
            NRF_LOG_DEBUG("TX FAILED EVENT");
            (void) nrf_esb_flush_tx();
            (void) nrf_esb_start_tx();
            tx_failures++; //count tx failures
            m_tx_busy = false;
            break;
        case NRF_ESB_EVENT_RX_RECEIVED:
            NRF_LOG_DEBUG("RX RECEIVED EVENT");
            while (nrf_esb_read_rx_payload(&rx_payload) == NRF_SUCCESS)
            {
                if (rx_payload.length > 0)
                {
                    NRF_LOG_INFO("RECEIVED Prefix: 0x%02X", rx_payload.data[0]);
                }
            }
            break;

        default:
            break;
    }
}



uint32_t esb_mouse_tx_init( void )
{
    uint32_t err_code;
    uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[8] = {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8 };

    nrf_esb_config_t nrf_esb_config         = NRF_ESB_DEFAULT_CONFIG;
    nrf_esb_config.protocol                 = NRF_ESB_PROTOCOL_ESB_DPL;
    nrf_esb_config.retransmit_delay         = 600;
    nrf_esb_config.bitrate                  = NRF_ESB_BITRATE_2MBPS;
    nrf_esb_config.event_handler            = nrf_esb_event_handler;
    nrf_esb_config.mode                     = NRF_ESB_MODE_PTX;
    nrf_esb_config.selective_auto_ack       = false;

    nrf_esb_config.tx_output_power = NRF_ESB_TX_POWER_0DBM;

    err_code = nrf_esb_init(&nrf_esb_config);

    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_0(base_addr_0);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_base_address_1(base_addr_1);
    VERIFY_SUCCESS(err_code);

    err_code = nrf_esb_set_prefixes(addr_prefix, NRF_ESB_PIPE_COUNT);
    VERIFY_SUCCESS(err_code);

    return err_code;
}



bool esb_mouse_tx_send(const mouse_motion_t *p_motion, uint8_t buttons)
{
    
    if(p_motion == NULL)
    {
        return false;
    }


    if(m_tx_busy)
    {
        return false;
    }

    mouse_packet_t packet = 
    {
        .packet_sequence = m_tx_count,
        .buttons = buttons,
        .x = p_motion->x,
        .y = p_motion->y
    };

 
    //copy current mouse state to esb tx payload
    memcpy(tx_payload.data, &packet, sizeof(packet));



    ret_code_t ret = nrf_esb_write_payload(&tx_payload);

    //send payload to radio buffer and initiate tx
    if (ret != NRF_SUCCESS) 
    {

        NRF_LOG_WARNING("Sending packet failed: %lu", ret);
        return false;

    }
  

    m_tx_count++;
    m_tx_busy = true; //radio transmitting, cleared on TX event in esb event handler
    //report_pending = false; //report is being sent to RX dongle. clear for next RTC1 compare interrupt

    return true;
}





bool esb_mouse_tx_busy(void)
{
    return m_tx_busy;
}