/**
 * PTX application for the wireless optical mouse.
 *
 * Sensor acquisition is scheduled independently of ESB transmission.
 * Motion is accumulated until a bounded snapshot is successfully transmitted.
 */


#include <stdbool.h>
#include <stdint.h>

#include "sdk_common.h"
#include "nrf.h"

#include "nrf_error.h"

#include "nrf_gpio.h"
#include "boards.h"
#include "app_util.h"
#include "nrfx_gpiote.h"

#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"

#include "esb_mouse_tx.h"
#include "mouse_scheduler.h"

#include "sensor.h"
#include "mouse_motion.h"


/* Button bit positions aligned with the USB HID button field. */
#define MOUSE_BUTTON_LEFT   (1u << 0)
#define MOUSE_BUTTON_RIGHT  (1u << 1)

/* Current button state, updated by the GPIOTE button handler. */
static volatile uint8_t m_buttons = 0;

/*
 * Set when the button state changes.
 * Cleared after a successful ESB transmission if the transmitted
 * snapshot still matches the current button state.
 */
static volatile bool m_buttons_changed = false;


/* Button-state snapshot sent with the current ESB transmission. */
static uint8_t m_buttons_sent = 0;


/*
 * Motion snapshot sent with the current ESB transmission.
 * Committed to the motion accumulator only after TX success.
 */
static mouse_motion_t m_motion_sent;


/*
 * Start the external high-frequency clock required by ESB radio.
 * The function blocks until the the crystal oscillator reports it is running.
 */
static void hfclk_start(void)
{
    NRF_CLOCK->EVENTS_HFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_HFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_HFCLKSTARTED == 0);
}


/*
 * Start the external 32.768 kHz crystal used by RTC1.
 * Blocks until the LFCLK reports that it is running.
 */
static void lfclk_start(void)
{
    NRF_CLOCK->LFCLKSRC = CLOCK_LFCLKSRC_SRC_Xtal;

    NRF_CLOCK->EVENTS_LFCLKSTARTED = 0;
    NRF_CLOCK->TASKS_LFCLKSTART = 1;

    while (NRF_CLOCK->EVENTS_LFCLKSTARTED == 0)
    {
    }
}


/* Start the clocks required by the radio and scheduler. */
static void clocks_init(void)
{
    hfclk_start();
    lfclk_start();
}


/*
 * Update the current mouse-button state.
 *
 * Either button edge counts as activity and returns the scheduler
 * to its active report rate.
 */
static void button_handler(nrfx_gpiote_pin_t pin,
                           nrf_gpiote_polarity_t action)
{
    (void)pin;
    (void)action;

    uint8_t new_buttons = 0;

    if (nrf_gpio_pin_read(BUTTON_1) == 0)
    {
        new_buttons |= MOUSE_BUTTON_LEFT;
    }

    if (nrf_gpio_pin_read(BUTTON_2) == 0)
    {
        new_buttons |= MOUSE_BUTTON_RIGHT;
    }

    if (new_buttons != m_buttons)
    {
        m_buttons = new_buttons;
        m_buttons_changed = true;

        mouse_scheduler_mark_active();
    }
}


/*
 * Configure GPIOTE for the DK mouse-button inputs.
 *
 * Either button edge triggers the button-state callback.
 */
static void buttons_init(void)
{
    nrfx_gpiote_init();

    /*
     * Generate an interrupt on both press and release.
     * High-accuracy mode uses a dedicated GPIOTE channel.
     */
    nrfx_gpiote_in_config_t config =
        NRFX_GPIOTE_CONFIG_IN_SENSE_TOGGLE(true);

    config.pull = NRF_GPIO_PIN_PULLUP;

    APP_ERROR_CHECK(
        nrfx_gpiote_in_init(BUTTON_1, &config, button_handler)
    );

    APP_ERROR_CHECK(
        nrfx_gpiote_in_init(BUTTON_2, &config, button_handler)
    );

    nrfx_gpiote_in_event_enable(BUTTON_1, true);
    nrfx_gpiote_in_event_enable(BUTTON_2, true);
}


static void leds_init(void)
{
    bsp_board_init(BSP_INIT_LEDS);
}


int main(void)
{
    ret_code_t err_code;

    /* Initialize board LEDs and button input. */
    leds_init();
    buttons_init();


    /* Initialize logging. */
    err_code = NRF_LOG_INIT(NULL);
    APP_ERROR_CHECK(err_code);
    NRF_LOG_DEFAULT_BACKENDS_INIT();


    clocks_init();


    /* Initialize application modules after clocks are running. */
    mouse_scheduler_init();

    err_code = esb_mouse_tx_init();
    APP_ERROR_CHECK(err_code);


    /* Initialize PMW3389. */
    pmw3389_status_t sensor_status = pmw3389_init();

    if (sensor_status == PMW3389_OK)
    {
        NRF_LOG_INFO("PMW3389 detected");
    }
    else
    {
        NRF_LOG_ERROR(
            "PMW3389 initialization failed: %u",
            sensor_status
        );
    }


    /* Read and report PMW3389 identification information. */
    pmw3389_info_t info;

    if (pmw3389_read_info(&info) == PMW3389_OK)
    {
        NRF_LOG_INFO(
            "PMW3389: PID=0x%02X REV=0x%02X INV=0x%02X SROM=0x%02X",
            info.product_id,
            info.revision_id,
            info.inverse_product_id,
            info.srom_id
        );
    }
    else
    {
        NRF_LOG_WARNING(
            "Failed to read PMW3389 device information"
        );
    }


    while (true)
    {
        static uint32_t idle_divider = 0;
        static uint32_t active_divider = 0;


        /*
         * Process the previous ESB transaction result.
         *
         * A successful packet commits only the motion snapshot that
         * was associated with that transmission. Any motion acquired
         * while the packet was in flight remains in the accumulator.
         */
        esb_mouse_tx_result_t result =
            esb_mouse_tx_result_take();


        if (result == ESB_MOUSE_TX_SUCCESS)
        {
            /*
             * Remove the successfully transmitted snapshot from the
             * software motion accumulator.
             */
            mouse_motion_commit_report(&m_motion_sent);


            /*
             * Clear the pending button-change flag only if the current
             * button state still matches the state that was transmitted.
             */
            CRITICAL_REGION_ENTER();

            if (m_buttons == m_buttons_sent)
            {
                m_buttons_changed = false;
            }

            CRITICAL_REGION_EXIT();
        }


        /*
         * Service each scheduled PMW3389 read independently of ESB state.
         *
         * Sensor acquisition continues at the scheduled 2 kHz rate while
         * a radio packet is in flight. Newly acquired motion is accumulated
         * independently of radio transaction timing.
         */
        if (mouse_scheduler_report_take())
        {
            pmw3389_motion_t sensor_motion;

            if (pmw3389_read_motion_burst(&sensor_motion) == PMW3389_OK)
            {
                if (sensor_motion.dx != 0 ||
                    sensor_motion.dy != 0)
                {
                    /*
                     * Add newly acquired motion to the accumulator.
                     * Acquisition may continue while an ESB packet is in flight.
                     */
                    mouse_motion_accumulate(
                        sensor_motion.dx,
                        sensor_motion.dy
                    );


                    /*
                     * Any detected motion keeps or returns the scheduler
                     * to active 500 us operation.
                     */
                    mouse_scheduler_mark_active();
                }
            }


            /*
             * Enter idle mode after the configured inactivity interval.
             */
            mouse_scheduler_try_enter_idle();


        }


        /*
         * Transmission remains gated by ESB availability.
         *
         * Only one application-level mouse packet is allowed to be
         * in flight at a time.
         */
        if (!esb_mouse_tx_busy())
        {
            mouse_motion_t report = {0};


            /*
             * Obtain the current pending motion snapshot without
             * removing it from the accumulator.
             */
            bool motion_pending =
                mouse_motion_peek_report(&report);


            if (motion_pending || m_buttons_changed)
            {
                uint8_t buttons;

                buttons = m_buttons;


                if (esb_mouse_tx_send(&report, buttons))
                {
                    /*
                     * Associate this snapshot with the ESB transaction
                     * currently in flight.
                     */
                    m_motion_sent = report;
                    m_buttons_sent = buttons;
                }
            }
        }


        UNUSED_RETURN_VALUE(
            NRF_LOG_PROCESS()
        );


        /*
         * Sleep until TIMER1, RTC1, GPIOTE, ESB, or another
         * enabled interrupt generates an event.
         */
        __WFE();
    }
}