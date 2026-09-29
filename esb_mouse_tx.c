#include "esb_mouse_tx.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "nrf_esb.h"
#include "nrf_error.h"
#include "nrf_log.h"

#include "app_util_platform.h"

#include "mouse_motion.h"


/* Wireless mouse application payload. */
typedef struct __attribute__((packed))
{
    uint8_t packet_sequence;
    uint8_t buttons;
    int16_t x;
    int16_t y;

} mouse_packet_t;

_Static_assert(
    sizeof(mouse_packet_t) == 6,
    "Unexpected ESB mouse packet size"
);


/* ESB transmit payload. */
static nrf_esb_payload_t tx_payload =
{
    .pipe = 0,
    .length = sizeof(mouse_packet_t),
    .noack = false
};


/* One application-level packet may be in flight at a time. */
static volatile bool m_tx_busy = false;


/* Application sequence number used for packet-loss diagnostics. */
static uint8_t m_tx_count = 0;


/* Result consumed by the main application after ESB completion. */
static volatile esb_mouse_tx_result_t m_tx_result =
    ESB_MOUSE_TX_NONE;


/* Handle ESB radio events. */
void nrf_esb_event_handler(nrf_esb_evt_t const *p_event)
{
    switch (p_event->evt_id)
    {
        case NRF_ESB_EVENT_TX_SUCCESS:
        {
            m_tx_result = ESB_MOUSE_TX_SUCCESS;
            break;
        }

        case NRF_ESB_EVENT_TX_FAILED:
        {
            m_tx_result = ESB_MOUSE_TX_FAILED;

            (void)nrf_esb_flush_tx();
            (void)nrf_esb_start_tx();

            break;
        }

        default:
        {
            break;
        }
    }
}


/* Initialize ESB for point-to-point mouse transmission. */
uint32_t esb_mouse_tx_init(void)
{
    uint32_t err_code;

    uint8_t base_addr_0[4] =
    {
        0xE7, 0xE7, 0xE7, 0xE7
    };

    uint8_t base_addr_1[4] =
    {
        0xC2, 0xC2, 0xC2, 0xC2
    };

    uint8_t addr_prefix[8] =
    {
        0xE7, 0xC2, 0xC3, 0xC4,
        0xC5, 0xC6, 0xC7, 0xC8
    };


    nrf_esb_config_t nrf_esb_config =
        NRF_ESB_DEFAULT_CONFIG;

    nrf_esb_config.protocol =
        NRF_ESB_PROTOCOL_ESB_DPL;

    nrf_esb_config.retransmit_delay =
        250;

    nrf_esb_config.retransmit_count =
        0;

    nrf_esb_config.bitrate =
        NRF_ESB_BITRATE_2MBPS;

    nrf_esb_config.event_handler =
        nrf_esb_event_handler;

    nrf_esb_config.mode =
        NRF_ESB_MODE_PTX;

    nrf_esb_config.selective_auto_ack =
        false;

    nrf_esb_config.tx_output_power =
        NRF_ESB_TX_POWER_0DBM;


    err_code =
        nrf_esb_init(&nrf_esb_config);

    VERIFY_SUCCESS(err_code);


    err_code =
        nrf_esb_set_base_address_0(base_addr_0);

    VERIFY_SUCCESS(err_code);


    err_code =
        nrf_esb_set_base_address_1(base_addr_1);

    VERIFY_SUCCESS(err_code);


    err_code =
        nrf_esb_set_prefixes(
            addr_prefix,
            NRF_ESB_PIPE_COUNT
        );

    VERIFY_SUCCESS(err_code);


    return err_code;
}


/*
 * Submit the current mouse-state snapshot for transmission.
 *
 * The caller retains ownership of the motion snapshot until the
 * corresponding ESB result has been processed.
 */
bool esb_mouse_tx_send(
    const mouse_motion_t *p_motion,
    uint8_t buttons)
{
    if (p_motion == NULL)
    {
        return false;
    }


    if (m_tx_busy)
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


    memcpy(
        tx_payload.data,
        &packet,
        sizeof(packet)
    );


    m_tx_busy = true;


    ret_code_t ret =
        nrf_esb_write_payload(&tx_payload);


    if (ret != NRF_SUCCESS)
    {
        m_tx_busy = false;

        NRF_LOG_WARNING(
            "Sending packet failed: %lu",
            ret
        );

        return false;
    }


    m_tx_count++;

    return true;
}


/* Return whether an application-level ESB packet is currently in flight. */
bool esb_mouse_tx_busy(void)
{
    return m_tx_busy;
}


/*
 * Consume the most recent ESB transmission result.
 *
 * Clearing the busy state here ensures main processes the completed
 * transaction before another application-level packet is submitted.
 */
esb_mouse_tx_result_t esb_mouse_tx_result_take(void)
{
    esb_mouse_tx_result_t result;


    CRITICAL_REGION_ENTER();

    result = m_tx_result;
    m_tx_result = ESB_MOUSE_TX_NONE;

    if (result != ESB_MOUSE_TX_NONE)
    {
        m_tx_busy = false;
    }

    CRITICAL_REGION_EXIT();


    return result;
}