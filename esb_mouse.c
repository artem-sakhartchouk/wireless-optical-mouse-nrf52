#include "esb_mouse.h"

#include <stdint.h>
#include <limits.h>

#include "app_error.h"
#include "nrf_log.h"
#include "proprietary_rf/esb/nrf_esb.h"
#include "app_util_platform.h"

static nrf_esb_payload_t m_rx_payload;

static volatile int32_t m_pending_x;
static volatile int32_t m_pending_y;
static volatile uint8_t m_buttons;
static volatile bool m_buttons_changed;
static volatile bool m_remote_wakeup_pending;


/* Wireless packet format shared with the PTX. */
typedef struct __attribute__((packed))
{
    uint8_t packet_sequence;
    uint8_t buttons;
    int16_t x;
    int16_t y;
} mouse_packet_t;

_Static_assert(sizeof(mouse_packet_t) == 6,
               "Unexpected ESB mouse packet size");


static void nrf_esb_event_handler(nrf_esb_evt_t const *p_event)
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
            while (nrf_esb_read_rx_payload(&m_rx_payload) == NRF_SUCCESS)
            {
                if (m_rx_payload.length != sizeof(mouse_packet_t))
                {
                    NRF_LOG_WARNING(
                        "Invalid RX payload length: %u",
                        m_rx_payload.length);
                    continue;
                }

                mouse_packet_t const *p_packet =
                    (mouse_packet_t const *)m_rx_payload.data;

                CRITICAL_REGION_ENTER();

                if (p_packet->buttons != m_buttons)
                {
                    uint8_t pressed =
                        p_packet->buttons & (uint8_t)~m_buttons;

                    m_buttons = p_packet->buttons;
                    m_buttons_changed = true;

                    if (pressed != 0)
                    {
                        m_remote_wakeup_pending = true;
                    }
                }

                m_pending_x += p_packet->x;
                m_pending_y += p_packet->y;

                CRITICAL_REGION_EXIT();
            }

            break;
        }

        default:
            break;
    }
}


void esb_mouse_init(void)
{
    static const uint8_t base_addr_0[4] =
        {0xE7, 0xE7, 0xE7, 0xE7};

    static const uint8_t base_addr_1[4] =
        {0xC2, 0xC2, 0xC2, 0xC2};

    static const uint8_t addr_prefix[8] =
        {0xE7, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8};

    nrf_esb_config_t config = NRF_ESB_DEFAULT_CONFIG;

    config.payload_length     = sizeof(mouse_packet_t);
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


static int16_t clamp_axis_to_hid(int32_t accumulated)
{
    if (accumulated > INT16_MAX)
    {
        return INT16_MAX;
    }

    if (accumulated < INT16_MIN)
    {
        return INT16_MIN;
    }

    return (int16_t)accumulated;
}


/*
 * Snapshot pending mouse input without removing it.
 * No report is produced when neither motion nor button state changed.
 */
bool esb_mouse_rx_peek(mouse_input_report_t *report)
{
    if (report == NULL)
    {
        return false;
    }

    int32_t pending_x;
    int32_t pending_y;
    uint8_t buttons;
    bool buttons_changed;

    CRITICAL_REGION_ENTER();
    pending_x = m_pending_x;
    pending_y = m_pending_y;
    buttons = m_buttons;
    buttons_changed = m_buttons_changed;
    CRITICAL_REGION_EXIT();

    if (pending_x == 0 &&
        pending_y == 0 &&
        !buttons_changed)
    {
        return false;
    }

    report->buttons = buttons;
    report->x = clamp_axis_to_hid(pending_x);
    report->y = clamp_axis_to_hid(pending_y);

    return true;
}


/* Commit only a HID report that completed successfully on USB IN1. */
void esb_mouse_rx_commit(const mouse_input_report_t *report)
{
    if (report == NULL)
    {
        return;
    }

    CRITICAL_REGION_ENTER();

    m_pending_x -= (int32_t)report->x;
    m_pending_y -= (int32_t)report->y;

    if (m_buttons == report->buttons)
    {
        m_buttons_changed = false;
    }

    CRITICAL_REGION_EXIT();
}


/* Read and clear a button-triggered remote-wakeup request. */
bool esb_mouse_remote_wakeup_take(void)
{
    bool pending;

    CRITICAL_REGION_ENTER();

    pending = m_remote_wakeup_pending;
    m_remote_wakeup_pending = false;

    CRITICAL_REGION_EXIT();

    return pending;
}
