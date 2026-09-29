#include "sensor.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_error.h"
#include "nrf_delay.h"
#include "nrf_gpio.h"
#include "nrf_log.h"
#include "nrfx_spim.h"

#include "pmw3389_srom.h"


/* GPIO assignments. */
#define PMW3389_PIN_SCK     NRF_GPIO_PIN_MAP(0, 17)
#define PMW3389_PIN_MOSI    NRF_GPIO_PIN_MAP(0, 20)
#define PMW3389_PIN_MISO    NRF_GPIO_PIN_MAP(0, 22)
#define PMW3389_PIN_CS      NRF_GPIO_PIN_MAP(0, 23)


/* PMW3389 register addresses. */
typedef enum
{
    PMW3389_REG_PRODUCT_ID         = 0x00,
    PMW3389_REG_REVISION_ID        = 0x01,
    PMW3389_REG_MOTION             = 0x02,
    PMW3389_REG_DELTA_X_L          = 0x03,
    PMW3389_REG_DELTA_X_H          = 0x04,
    PMW3389_REG_DELTA_Y_L          = 0x05,
    PMW3389_REG_DELTA_Y_H          = 0x06,

    PMW3389_REG_RESOLUTION_L       = 0x0E,
    PMW3389_REG_RESOLUTION_H       = 0x0F,
    PMW3389_REG_CONFIG2            = 0x10,
    PMW3389_REG_SROM_ENABLE        = 0x13,

    PMW3389_REG_SROM_ID            = 0x2A,
    PMW3389_REG_POWER_UP_RESET     = 0x3A,
    PMW3389_REG_INVERSE_PRODUCT_ID = 0x3F,

    PMW3389_REG_MOTION_BURST       = 0x50,
    PMW3389_REG_SROM_LOAD_BURST    = 0x62

} pmw3389_register_t;


/* Expected sensor identification and initialization values. */
#define PMW3389_PRODUCT_ID_EXPECTED          0x47u
#define PMW3389_INVERSE_PRODUCT_ID_EXPECTED  0xB8u
#define PMW3389_POWER_UP_RESET_VALUE         0x5Au
#define PMW3389_SROM_ID_EXPECTED             0xE8u


/* Data returned by a PMW3389 Motion_Burst transaction. */
typedef struct __attribute__((packed))
{
    uint8_t motion;
    uint8_t observation;
    uint8_t dx_l;
    uint8_t dx_h;
    uint8_t dy_l;
    uint8_t dy_h;
    uint8_t squal;
    uint8_t raw_data_sum;
    uint8_t max_raw_data;
    uint8_t min_raw_data;
    uint8_t shutter_upper;
    uint8_t shutter_lower;

} pmw3389_burst_t;


_Static_assert(
    sizeof(pmw3389_burst_t) == 12,
    "Unexpected PMW3389 burst size"
);


/*
 * Motion-burst mode must be reinitialized after an ordinary
 * register access.
 */
static bool m_burst_initialized = false;

static pmw3389_burst_t m_last_burst;


/*
 * SPIM driver instance used by the PMW3389.
 */
#define PMW3389_SPIM_INSTANCE  1

static const nrfx_spim_t m_spim =
    NRFX_SPIM_INSTANCE(PMW3389_SPIM_INSTANCE);


/**
 * @brief Initialize the PMW3389 SPI transport.
 *
 * Configures SPIM1 and the manually controlled chip-select GPIO.
 * This function prepares the transport but does not communicate
 * with the sensor.
 */
static pmw3389_status_t pmw3389_spi_init(void)
{
    nrfx_spim_config_t config =
        NRFX_SPIM_DEFAULT_CONFIG;

    config.sck_pin   = PMW3389_PIN_SCK;
    config.mosi_pin  = PMW3389_PIN_MOSI;
    config.miso_pin  = PMW3389_PIN_MISO;
    config.ss_pin    = NRFX_SPIM_PIN_NOT_USED;

    config.frequency = NRF_SPIM_FREQ_1M;
    config.mode      = NRF_SPIM_MODE_3;
    config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;


    nrf_gpio_cfg_output(PMW3389_PIN_CS);
    nrf_gpio_pin_set(PMW3389_PIN_CS);


    nrfx_err_t err =
        nrfx_spim_init(
            &m_spim,
            &config,
            NULL,
            NULL
        );


    if (err != NRFX_SUCCESS)
    {
        return PMW3389_ERROR_SPI_INIT;
    }


    return PMW3389_OK;
}


/* Read one PMW3389 register. */
static uint8_t pmw3389_read_reg(
    pmw3389_register_t reg)
{
    /*
     * Any ordinary register access invalidates the current
     * motion-burst state.
     */
    m_burst_initialized = false;


    uint8_t address =
        (uint8_t)reg & 0x7Fu;

    uint8_t dummy = 0x00u;
    uint8_t value = 0x00u;


    nrf_gpio_pin_clear(PMW3389_PIN_CS);


    nrfx_spim_xfer_desc_t address_xfer =
        NRFX_SPIM_XFER_TX(
            &address,
            1
        );

    APP_ERROR_CHECK(
        nrfx_spim_xfer(
            &m_spim,
            &address_xfer,
            0
        )
    );


    nrf_delay_us(160);


    nrfx_spim_xfer_desc_t data_xfer =
        NRFX_SPIM_XFER_TRX(
            &dummy,
            1,
            &value,
            1
        );

    APP_ERROR_CHECK(
        nrfx_spim_xfer(
            &m_spim,
            &data_xfer,
            0
        )
    );


    nrf_gpio_pin_set(PMW3389_PIN_CS);

    nrf_delay_us(20);


    return value;
}


/* Write one PMW3389 register. */
static pmw3389_status_t pmw3389_write_reg(
    pmw3389_register_t reg,
    uint8_t value)
{
    if (reg != PMW3389_REG_MOTION_BURST)
    {
        m_burst_initialized = false;
    }


    uint8_t tx_buf[2] =
    {
        ((uint8_t)reg) | 0x80u,
        value
    };


    nrfx_spim_xfer_desc_t xfer =
        NRFX_SPIM_XFER_TX(
            tx_buf,
            sizeof(tx_buf)
        );


    nrf_gpio_pin_clear(PMW3389_PIN_CS);


    nrfx_err_t err =
        nrfx_spim_xfer(
            &m_spim,
            &xfer,
            0
        );


    /*
     * Keep CS asserted long enough for the write cycle
     * to complete before ending the transaction.
     */
    nrf_delay_us(35);

    nrf_gpio_pin_set(PMW3389_PIN_CS);


    /* Wait before issuing another SPI command. */
    nrf_delay_us(145);


    if (err != NRFX_SUCCESS)
    {
        return PMW3389_ERROR_SPI_TRANSFER;
    }


    return PMW3389_OK;
}


/* Configure sensor resolution in counts per inch. */
static pmw3389_status_t pmw3389_set_cpi(
    uint16_t cpi)
{
    if ((cpi < 50u) ||
        (cpi > 26000u) ||
        ((cpi % 50u) != 0u))
    {
        return PMW3389_INVALID_PARAMETER;
    }


    uint16_t reg =
        (cpi / 50u) - 1u;


    pmw3389_status_t status;


    status =
        pmw3389_write_reg(
            PMW3389_REG_RESOLUTION_H,
            (uint8_t)(reg >> 8)
        );

    if (status != PMW3389_OK)
    {
        return status;
    }


    status =
        pmw3389_write_reg(
            PMW3389_REG_RESOLUTION_L,
            (uint8_t)reg
        );


    return status;
}


/*
 * Read the current accumulated motion using the PMW3389
 * Motion_Burst interface.
 */
pmw3389_status_t pmw3389_read_motion_burst(
    pmw3389_motion_t *motion)
{
    if (motion == NULL)
    {
        return PMW3389_INVALID_PARAMETER;
    }


    /*
     * Motion-burst mode must be initialized again after
     * any ordinary register access.
     */
    if (!m_burst_initialized)
    {
        pmw3389_status_t status =
            pmw3389_write_reg(
                PMW3389_REG_MOTION_BURST,
                0x00u
            );

        if (status != PMW3389_OK)
        {
            return status;
        }

        m_burst_initialized = true;
    }


    uint8_t address =
        PMW3389_REG_MOTION_BURST & 0x7Fu;


    nrf_gpio_pin_clear(PMW3389_PIN_CS);


    /* Send the Motion_Burst read address. */
    nrfx_spim_xfer_desc_t addr_xfer =
        NRFX_SPIM_XFER_TX(
            &address,
            1
        );


    nrfx_err_t err =
        nrfx_spim_xfer(
            &m_spim,
            &addr_xfer,
            0
        );


    if (err != NRFX_SUCCESS)
    {
        nrf_gpio_pin_set(PMW3389_PIN_CS);
        nrf_delay_us(20);

        return PMW3389_ERROR_SPI_TRANSFER;
    }


    /* PMW3389 burst-read address-to-data delay. */
    nrf_delay_us(35);


    /* Clock out the complete 12-byte motion-burst response. */
    nrfx_spim_xfer_desc_t burst_xfer =
        NRFX_SPIM_XFER_RX(
            (uint8_t *)&m_last_burst,
            sizeof(m_last_burst)
        );


    err =
        nrfx_spim_xfer(
            &m_spim,
            &burst_xfer,
            0
        );


    nrf_gpio_pin_set(PMW3389_PIN_CS);

    nrf_delay_us(20);


    if (err != NRFX_SUCCESS)
    {
        return PMW3389_ERROR_SPI_TRANSFER;
    }


    motion->motion =
        m_last_burst.motion;

    motion->dx =
        (int16_t)(
            ((uint16_t)m_last_burst.dx_h << 8) |
            m_last_burst.dx_l
        );

    motion->dy =
        (int16_t)(
            ((uint16_t)m_last_burst.dy_h << 8) |
            m_last_burst.dy_l
        );


    return PMW3389_OK;
}


/* Read PMW3389 identification registers. */
pmw3389_status_t pmw3389_read_info(
    pmw3389_info_t *info)
{
    if (info == NULL)
    {
        return PMW3389_INVALID_PARAMETER;
    }


    info->product_id =
        pmw3389_read_reg(
            PMW3389_REG_PRODUCT_ID
        );

    info->revision_id =
        pmw3389_read_reg(
            PMW3389_REG_REVISION_ID
        );

    info->inverse_product_id =
        pmw3389_read_reg(
            PMW3389_REG_INVERSE_PRODUCT_ID
        );

    info->srom_id =
        pmw3389_read_reg(
            PMW3389_REG_SROM_ID
        );


    return PMW3389_OK;
}


/* Clear residual motion data following reset. */
static void pmw3389_clear_motion_data(void)
{
    (void)pmw3389_read_reg(
        PMW3389_REG_MOTION
    );

    (void)pmw3389_read_reg(
        PMW3389_REG_DELTA_X_L
    );

    (void)pmw3389_read_reg(
        PMW3389_REG_DELTA_X_H
    );

    (void)pmw3389_read_reg(
        PMW3389_REG_DELTA_Y_L
    );

    (void)pmw3389_read_reg(
        PMW3389_REG_DELTA_Y_H
    );
}


/* Perform the PMW3389 power-up reset sequence. */
static pmw3389_status_t pmw3389_power_up_reset(void)
{
    pmw3389_status_t status =
        pmw3389_write_reg(
            PMW3389_REG_POWER_UP_RESET,
            PMW3389_POWER_UP_RESET_VALUE
        );


    if (status != PMW3389_OK)
    {
        return status;
    }


    nrf_delay_ms(50);


    pmw3389_clear_motion_data();


    return PMW3389_OK;
}


/* Upload the PMW3389 SROM image using burst-write mode. */
static pmw3389_status_t pmw3389_upload_srom(void)
{
    pmw3389_status_t status;


    status =
        pmw3389_write_reg(
            PMW3389_REG_CONFIG2,
            0x00u
        );

    if (status != PMW3389_OK)
    {
        return status;
    }


    status =
        pmw3389_write_reg(
            PMW3389_REG_SROM_ENABLE,
            0x1Du
        );

    if (status != PMW3389_OK)
    {
        return status;
    }


    nrf_delay_ms(10);


    status =
        pmw3389_write_reg(
            PMW3389_REG_SROM_ENABLE,
            0x18u
        );

    if (status != PMW3389_OK)
    {
        return status;
    }


    uint8_t burst_command =
        ((uint8_t)PMW3389_REG_SROM_LOAD_BURST) |
        0x80u;


    nrf_gpio_pin_clear(PMW3389_PIN_CS);


    nrfx_spim_xfer_desc_t command_xfer =
        NRFX_SPIM_XFER_TX(
            &burst_command,
            1
        );


    if (nrfx_spim_xfer(
            &m_spim,
            &command_xfer,
            0) != NRFX_SUCCESS)
    {
        nrf_gpio_pin_set(PMW3389_PIN_CS);

        return PMW3389_ERROR_SPI_TRANSFER;
    }


    nrf_delay_us(15);


    for (size_t i = 0;
         i < pmw3389_srom_size;
         ++i)
    {
        uint8_t byte =
            pmw3389_srom_data[i];


        nrfx_spim_xfer_desc_t byte_xfer =
            NRFX_SPIM_XFER_TX(
                &byte,
                1
            );


        if (nrfx_spim_xfer(
                &m_spim,
                &byte_xfer,
                0) != NRFX_SUCCESS)
        {
            nrf_gpio_pin_set(PMW3389_PIN_CS);

            return PMW3389_ERROR_SPI_TRANSFER;
        }


        nrf_delay_us(15);
    }


    nrf_gpio_pin_set(PMW3389_PIN_CS);

    nrf_delay_us(200);


    return PMW3389_OK;
}


/**
 * @brief Initialize and verify the PMW3389 sensor.
 *
 * Initializes the SPI transport, verifies sensor identification,
 * performs the power-up reset, uploads the SROM image, verifies
 * the loaded SROM ID, and configures the sensor for 800 CPI.
 */
pmw3389_status_t pmw3389_init(void)
{
    pmw3389_status_t status;


    status =
        pmw3389_spi_init();

    if (status != PMW3389_OK)
    {
        return status;
    }


    /* Allow sensor supply and internal circuitry to settle. */
    nrf_delay_ms(50);


    pmw3389_info_t info;


    status =
        pmw3389_read_info(&info);

    if (status != PMW3389_OK)
    {
        return status;
    }


    NRF_LOG_INFO(
        "PMW3389: PID=0x%02X REV=0x%02X INV=0x%02X SROM=0x%02X",
        info.product_id,
        info.revision_id,
        info.inverse_product_id,
        info.srom_id
    );


    if ((info.product_id != PMW3389_PRODUCT_ID_EXPECTED) ||
        (info.inverse_product_id != PMW3389_INVERSE_PRODUCT_ID_EXPECTED))
    {
        return PMW3389_INVALID_ID;
    }


    status =
        pmw3389_power_up_reset();

    if (status != PMW3389_OK)
    {
        return status;
    }


    status =
        pmw3389_upload_srom();

    if (status != PMW3389_OK)
    {
        return status;
    }


    uint8_t srom_id =
        pmw3389_read_reg(
            PMW3389_REG_SROM_ID
        );


    NRF_LOG_INFO(
        "SROM ID after upload: 0x%02X",
        srom_id
    );


    if (srom_id != PMW3389_SROM_ID_EXPECTED)
    {
        return PMW3389_ERROR_SROM;
    }


    return pmw3389_set_cpi(800u);
}