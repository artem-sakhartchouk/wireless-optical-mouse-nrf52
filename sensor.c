#include "sensor.h"

#include "nrf_gpio.h"
#include "nrfx_spim.h"
#include "nrf_delay.h"
#include "app_error.h"
#include "nrf_log.h"

#include "pmw3389_srom.h"


/* GPIO assignments */

#define PMW3389_SPI_INSTANCE    0

#define PMW3389_PIN_SCK         NRF_GPIO_PIN_MAP(0, 17)
#define PMW3389_PIN_MOSI        NRF_GPIO_PIN_MAP(0, 20)
#define PMW3389_PIN_MISO        NRF_GPIO_PIN_MAP(0, 22)
#define PMW3389_PIN_CS          NRF_GPIO_PIN_MAP(0, 23)


/* PMW2289 register addresses */

typedef enum
{
    PMW3389_REG_PRODUCT_ID         = 0x00,
    PMW3389_REG_REVISION_ID        = 0x01,
    PMW3389_REG_MOTION             = 0x02,
    PMW3389_REG_DELTA_X_L          = 0x03,
    PMW3389_REG_DELTA_X_H          = 0x04,
    PMW3389_REG_DELTA_Y_L          = 0x05,
    PMW3389_REG_DELTA_Y_H          = 0x06,
    PMW3389_REG_SQUAL              = 0x07,
    PMW3389_REG_CONFIG2            = 0x10,
    PMW3389_REG_SROM_ENABLE        = 0x13,

    PMW3389_REG_SROM_ID            = 0x2A,
    PMW3389_REG_POWER_UP_RESET     = 0x3A,
    PMW3389_REG_INVERSE_PRODUCT_ID = 0x3F,

    PMW3389_REG_MOTION_BURST       = 0x50,
    PMW3389_REG_SROM_LOAD_BURST    = 0x62,


    PMW3389_REG_RESOLUTION_L       = 0x0Eu,
    PMW3389_REG_RESOLUTION_H       = 0x0Fu

} pmw3389_register_t;



/* define expected sensor information register values for testing connection */

#define PMW3389_PRODUCT_ID_EXPECTED          0x47u
#define PMW3389_REVISION_ID_EXPECTED         0x01u
#define PMW3389_INVERSE_PRODUCT_ID_EXPECTED  0xB8u
#define PMW3389_POWER_UP_RESET_VALUE         0x5Au
#define PMW3389_REG_SQUAL                    0x07u
#define PMW3389_SROM_ID_EXPECTED             0xE8u



/*
 * The PMW3389 sensor returns accumulated 16-bit deltas that 
 * can overflow a 16-bit register between readout. Instead, motion deltas are  
 * stored in 32-bit variables and read out incrementally by the radio scheduler
 */
typedef struct
{
    int32_t x;
    int32_t y;
} mouse_motion_accumulator_t;

static mouse_motion_accumulator_t m_accumulated_motion;



/*
 * SPIM driver instance describing the hardware peripheral used by
 * the PMW3389. The nrfx driver uses this handle to access both the
 * SPIM1 register block and its associated driver state.
 */
#define PMW3389_SPIM_INSTANCE  1

static const nrfx_spim_t m_spim =
    NRFX_SPIM_INSTANCE(PMW3389_SPIM_INSTANCE);







/**
 * @brief Brings up the PMW3389 SPI transport layer.
 *
 * Initializes the nrfx SPIM driver and configures the GPIO required for
 * chip-select. This function does not communicate with the sensor; it only
 * prepares the underlying transport used by the driver.
 */

static pmw3389_status_t pmw3389_spi_init(void)
{
    nrfx_spim_config_t config = NRFX_SPIM_DEFAULT_CONFIG;

    config.sck_pin = PMW3389_PIN_SCK;
    config.mosi_pin = PMW3389_PIN_MOSI;
    config.miso_pin = PMW3389_PIN_MISO;
    config.ss_pin = NRFX_SPIM_PIN_NOT_USED;


    config.frequency = NRF_SPIM_FREQ_1M;
    config.mode      = NRF_SPIM_MODE_3;
    config.bit_order = NRF_SPIM_BIT_ORDER_MSB_FIRST;

    nrf_gpio_cfg_output(PMW3389_PIN_CS);
    nrf_gpio_pin_set(PMW3389_PIN_CS);

    nrfx_err_t err = nrfx_spim_init(&m_spim, &config, NULL, NULL);


    if(err != NRFX_SUCCESS)
    {
        return PMW3389_ERROR_SPI_INIT;
    }

    return PMW3389_OK;


}



static uint8_t pmw3389_read_reg(pmw3389_register_t reg)
{
    uint8_t address = (uint8_t)reg & 0x7Fu;
    uint8_t dummy   = 0x00u;
    uint8_t value   = 0x00u;

    nrf_gpio_pin_clear(PMW3389_PIN_CS); //cs low for communication with sensor

    nrfx_spim_xfer_desc_t address_xfer =
        NRFX_SPIM_XFER_TX(&address, 1); //pointer to tx buffer and its length

    APP_ERROR_CHECK(
        nrfx_spim_xfer(&m_spim, &address_xfer, 0)
    );

    nrf_delay_us(160);

    nrfx_spim_xfer_desc_t data_xfer =
        NRFX_SPIM_XFER_TRX(&dummy, 1, &value, 1);

    APP_ERROR_CHECK(
        nrfx_spim_xfer(&m_spim, &data_xfer, 0)
    );

    nrf_gpio_pin_set(PMW3389_PIN_CS); 

    nrf_delay_us(20);

    return value;
}



static pmw3389_status_t pmw3389_write_reg(pmw3389_register_t reg, uint8_t value)
{
    
    
    uint8_t tx_buf[2] =
    {
        ((uint8_t)reg) | 0x80u,
        value
    };

    
        
    nrfx_spim_xfer_desc_t xfer =
    NRFX_SPIM_XFER_TX(tx_buf, sizeof(tx_buf));

    nrf_gpio_pin_clear(PMW3389_PIN_CS);

    nrfx_err_t err =
        nrfx_spim_xfer(&m_spim, &xfer, 0);

    /*
     * Keep CS asserted long enough for the write cycle
     * to complete before ending the transaction.
     */
    nrf_delay_us(35);

    nrf_gpio_pin_set(PMW3389_PIN_CS);

    /*
     * Wait before issuing another SPI command.
     */
    nrf_delay_us(145);

    if (err != NRFX_SUCCESS)
    {
        return PMW3389_ERROR_SPI_TRANSFER;
    }

    return PMW3389_OK;

}



pmw3389_status_t pmw3389_set_cpi(uint16_t cpi)
{


    if ((cpi < 50u) || (cpi > 26000u) || ((cpi % 50u) != 0u))
    {
        return PMW3389_INVALID_PARAMETER;
    }

    uint16_t reg = (cpi / 50u) -1u;

    pmw3389_status_t status;

    status = pmw3389_write_reg(PMW3389_REG_RESOLUTION_H,
                      (uint8_t)(reg>>8));

    if(status != PMW3389_OK)
    {
        return status;
    }

    status = pmw3389_write_reg(PMW3389_REG_RESOLUTION_L,
                      (uint8_t)reg);

    return status;   

}

/* writes to motion register to capture latest deltas */

static pmw3389_status_t pmw3389_latch_motion(void)
{
    return pmw3389_write_reg(PMW3389_REG_MOTION, 0x00u);
}



bool pmw3389_read_motion(pmw3389_motion_t *motion)
{
    if (motion == NULL)
    {
        return false;
    }


    if (pmw3389_latch_motion() != PMW3389_OK)
    {
        return false;
    }

    uint8_t motion_reg = pmw3389_read_reg(PMW3389_REG_MOTION);

    uint8_t dx_l = pmw3389_read_reg(PMW3389_REG_DELTA_X_L);
    uint8_t dx_h = pmw3389_read_reg(PMW3389_REG_DELTA_X_H);
    uint8_t dy_l = pmw3389_read_reg(PMW3389_REG_DELTA_Y_L);
    uint8_t dy_h = pmw3389_read_reg(PMW3389_REG_DELTA_Y_H);

    //uint8_t squal = pmw3389_read_reg(PMW3389_REG_SQUAL);


    NRF_LOG_INFO("RAW: M=%02X XL=%02X XH=%02X YL=%02X YH=%02X",
             motion_reg, dx_l, dx_h, dy_l, dy_h);


    motion->motion = motion_reg;
    motion->dx = (int16_t)(((uint16_t)dx_h << 8) | dx_l);
    motion->dy = (int16_t)(((uint16_t)dy_h << 8) | dy_l);

    return true;
}




/* read sensor information registers into a struct instance */

pmw3389_status_t pmw3389_read_info(pmw3389_info_t *info)
{
    if(info == NULL)
    {
        return PMW3389_INVALID_PARAMETER;
    }

    info->product_id = pmw3389_read_reg(PMW3389_REG_PRODUCT_ID);
    
    info->revision_id = pmw3389_read_reg(PMW3389_REG_REVISION_ID);

    info->inverse_product_id = pmw3389_read_reg(PMW3389_REG_INVERSE_PRODUCT_ID);

    info->srom_id = pmw3389_read_reg(PMW3389_REG_SROM_ID);


    return PMW3389_OK;

}



static void pmw3389_clear_motion_data(void)
{
    (void)pmw3389_read_reg(PMW3389_REG_MOTION);
    (void)pmw3389_read_reg(PMW3389_REG_DELTA_X_L);
    (void)pmw3389_read_reg(PMW3389_REG_DELTA_X_H);
    (void)pmw3389_read_reg(PMW3389_REG_DELTA_Y_L);
    (void)pmw3389_read_reg(PMW3389_REG_DELTA_Y_H);
}



static pmw3389_status_t pmw3389_power_up_reset(void)
{
    pmw3389_status_t status = pmw3389_write_reg(
        PMW3389_REG_POWER_UP_RESET,
        PMW3389_POWER_UP_RESET_VALUE);

    if (status != PMW3389_OK)
    {
        return status;
    }

    nrf_delay_ms(50);

    pmw3389_clear_motion_data();

    return PMW3389_OK;
}




static pmw3389_status_t pmw3389_upload_srom(void)
{
    pmw3389_status_t status;

    status = pmw3389_write_reg(PMW3389_REG_CONFIG2, 0x00u);
    if (status != PMW3389_OK)
    {
        return status;
    }

    status = pmw3389_write_reg(PMW3389_REG_SROM_ENABLE, 0x1Du);
    if (status != PMW3389_OK)
    {
        return status;
    }

    nrf_delay_ms(10);

    status = pmw3389_write_reg(PMW3389_REG_SROM_ENABLE, 0x18u);
    if (status != PMW3389_OK)
    {
        return status;
    }

    uint8_t burst_command =
        ((uint8_t)PMW3389_REG_SROM_LOAD_BURST) | 0x80u;

    nrf_gpio_pin_clear(PMW3389_PIN_CS);

    nrfx_spim_xfer_desc_t command_xfer =
        NRFX_SPIM_XFER_TX(&burst_command, 1);

    if (nrfx_spim_xfer(&m_spim, &command_xfer, 0) != NRFX_SUCCESS)
    {
        nrf_gpio_pin_set(PMW3389_PIN_CS);
        return PMW3389_ERROR_SPI_TRANSFER;
    }

    nrf_delay_us(15);

    for (size_t i = 0; i < pmw3389_srom_size; ++i)
    {
        uint8_t byte = pmw3389_srom_data[i];

        nrfx_spim_xfer_desc_t byte_xfer =
            NRFX_SPIM_XFER_TX(&byte, 1);

        if (nrfx_spim_xfer(&m_spim, &byte_xfer, 0) != NRFX_SUCCESS)
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
 * @brief Initializes the PMW3389 sensor interface and verifies communication.
 *
 * Initializes the SPIM transport, waits for the sensor to become ready,
 * reads the device identification registers, and verifies that the expected
 * PMW3389 device is connected.
 *
 * @return PMW3389_OK if initialization and identification succeed.
 * @return PMW3389_ERROR_SPI_INIT if the SPIM peripheral cannot be initialized.
 * @return PMW3389_INVALID_ID if the returned device identifiers are invalid.
 */

pmw3389_status_t pmw3389_init(void)
{
    pmw3389_status_t status;

    status = pmw3389_spi_init();

    if (status != PMW3389_OK)
    {
        return status;
    }

    
    /* allow the sensor supply and internal circuitry to settle */
    nrf_delay_ms(50);


    pmw3389_info_t info;


    status = pmw3389_read_info(&info);

    if(status != PMW3389_OK)
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



    status = pmw3389_power_up_reset();

    if(status != PMW3389_OK)
    {
        return status;
    }


    status = pmw3389_upload_srom();
     
    if(status != PMW3389_OK)
    {
        return status;
    }


    uint8_t srom_id =
        pmw3389_read_reg(PMW3389_REG_SROM_ID);

    NRF_LOG_INFO("SROM ID after upload: 0x%02X", srom_id);


    if(srom_id != PMW3389_SROM_ID_EXPECTED)
    {
        return PMW3389_ERROR_SROM;
    }


    return pmw3389_set_cpi(800u);

    
}