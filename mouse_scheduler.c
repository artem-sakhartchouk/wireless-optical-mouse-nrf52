#include "nrfx_rtc.h"
#include "app_error.h"
#include "nrfx_timer.h"

#include "mouse_scheduler.h"

#include "nrf_log.h"


#define RTC_COUNTER_MASK      0x00FFFFFFUL
#define RTC_TICKS_PER_SEC     32768UL
#define IDLE_TIMEOUT_TICKS    (10 * RTC_TICKS_PER_SEC)

#define RTC_INTERVAL_IDLE     3277    /* ~100 ms */


static nrfx_rtc_t m_counter = NRFX_RTC_INSTANCE(1);

static const nrfx_timer_t m_active_timer =
    NRFX_TIMER_INSTANCE(1);


/* Last activity time in RTC ticks. */
static volatile uint32_t m_last_activity_tick;


/* Determines active 2 kHz versus idle ~10 Hz sensor scheduling. */
static volatile mouse_power_state_t m_power_state =
    MOUSE_ACTIVE;


/* Set by TIMER1 or RTC1 when a sensor-service interval has elapsed. */
static volatile bool m_report_pending = false;


/* Next RTC compare value used during idle polling. */
static uint32_t m_next_cc;


/* RTC1 compare interrupt handler for idle polling. */
static void rtc_handler(nrfx_rtc_int_type_t int_type)
{
    if (int_type != NRFX_RTC_INT_COMPARE0)
    {
        return;
    }

    m_report_pending = true;

    m_next_cc += RTC_INTERVAL_IDLE;

    nrfx_rtc_cc_set(
        &m_counter,
        0,
        m_next_cc,
        true
    );
}


/* TIMER1 interrupt handler for active 2 kHz polling. */
static void active_timer_handler(
    nrf_timer_event_t event_type,
    void *p_context)
{
    (void)p_context;

    if (event_type == NRF_TIMER_EVENT_COMPARE0)
    {
        m_report_pending = true;
    }
}


/* Initialize active and idle scheduling sources. */
void mouse_scheduler_init(void)
{
    nrfx_timer_config_t timer_config =
        NRFX_TIMER_DEFAULT_CONFIG;

    timer_config.frequency = NRF_TIMER_FREQ_1MHz;
    timer_config.bit_width = NRF_TIMER_BIT_WIDTH_32;

    ret_code_t err = nrfx_timer_init(
        &m_active_timer,
        &timer_config,
        active_timer_handler
    );

    APP_ERROR_CHECK(err);


    uint32_t ticks =
        nrfx_timer_us_to_ticks(&m_active_timer, 500);

    nrfx_timer_extended_compare(
        &m_active_timer,
        NRF_TIMER_CC_CHANNEL0,
        ticks,
        NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK,
        true
    );


    nrfx_rtc_config_t config =
        NRFX_RTC_DEFAULT_CONFIG;

    config.prescaler = 0;

    err = nrfx_rtc_init(
        &m_counter,
        &config,
        rtc_handler
    );

    APP_ERROR_CHECK(err);

    nrfx_rtc_counter_clear(&m_counter);


    m_last_activity_tick =
        nrfx_rtc_counter_get(&m_counter);

    m_next_cc = 0;


    nrfx_rtc_enable(&m_counter);


    m_power_state = MOUSE_ACTIVE;

    nrfx_timer_clear(&m_active_timer);
    nrfx_timer_enable(&m_active_timer);
}


/* Record activity and return to the active polling rate if needed. */
void mouse_scheduler_mark_active(void)
{
    uint32_t now =
        nrfx_rtc_counter_get(&m_counter);

    m_last_activity_tick = now;


    if (m_power_state == MOUSE_IDLE)
    {
        /* Stop idle RTC compare interrupts. */
        nrfx_rtc_int_disable(
            &m_counter,
            NRFX_RTC_INT_COMPARE0
        );


        nrfx_timer_clear(&m_active_timer);
        nrfx_timer_enable(&m_active_timer);

        m_power_state = MOUSE_ACTIVE;

        NRF_LOG_INFO("Entering Active");
    }
}


/* Return and clear the pending scheduled sensor-service flag. */
bool mouse_scheduler_report_take(void)
{
    bool pending;

    CRITICAL_REGION_ENTER();

    pending = m_report_pending;
    m_report_pending = false;

    CRITICAL_REGION_EXIT();

    return pending;
}


mouse_power_state_t mouse_scheduler_power_state_get(void)
{
    return m_power_state;
}


/* Enter idle polling after the configured inactivity interval. */
void mouse_scheduler_try_enter_idle(void)
{
    bool entered_idle = false;

    CRITICAL_REGION_ENTER();


    if (m_power_state == MOUSE_ACTIVE)
    {
        uint32_t now =
            nrfx_rtc_counter_get(&m_counter);

        uint32_t elapsed =
            (now - m_last_activity_tick) &
            RTC_COUNTER_MASK;


        if (elapsed >= IDLE_TIMEOUT_TICKS)
        {
            /* Stop active 2 kHz polling. */
            nrfx_timer_disable(&m_active_timer);


            /* Schedule the first ~100 ms idle poll. */
            m_next_cc =
                now + RTC_INTERVAL_IDLE;

            nrfx_rtc_cc_set(
                &m_counter,
                0,
                m_next_cc,
                true
            );


            m_power_state = MOUSE_IDLE;
            entered_idle = true;
        }
    }


    CRITICAL_REGION_EXIT();


    if (entered_idle)
    {
        NRF_LOG_INFO("Entering Idle");
    }
}