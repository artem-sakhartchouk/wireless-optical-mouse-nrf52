#include "sensor.h"

#include "nrf_gpio.h"
#include "nrfx_spim.h"
#include "nrf_delay.h"
#include "app_error.h"
#include "nrf_log.h"



/* GPIO assignments */

#define PMW3389_SPI_INSTANCE    0

#define PMW3389_PIN_SCK         NRF_GPIO_PIN_MAP(0, 17)
#define PMW3389_PIN_MOSI        NRF_GPIO_PIN_MAP(0, 20)
#define PMW3389_PIN_MISO        NRF_GPIO_PIN_MAP(0, 22)
#define PMW3389_PIN_CS          NRF_GPIO_PIN_MAP(0, 23)


/* PMW2289 register addresses */

typedef enum
{
     PMW3389_REG_PRODUCT_ID   =          0x00u,
     PMW3389_REG_REVISION_ID  =          0x01u,

     PMW3389_REG_MOTION       =          0x02u,
     PMW3389_REG_DELTA_X_L    =          0x03u,
     PMW3389_REG_DELTA_X_H    =          0x04u,
     PMW3389_REG_DELTA_Y_L    =          0x05u,
     PMW3389_REG_DELTA_Y_H    =          0x06u,

     PMW3389_REG_SROM_ID       =         0x2Au,
     PMW3389_REG_POWER_UP_RESET  =        0x3Au,
     PMW3389_REG_INVERSE_PRODUCT_ID   =   0x3Fu,

     PMW3389_REG_MOTION_BURST    =   0x50u

}pmw3389_register_t;


#define PMW3389_PRODUCT_ID_EXPECTED          0x47u
#define PMW3389_REVISION_ID_EXPECTED         0x01u
#define PMW3389_INVERSE_PRODUCT_ID_EXPECTED  0xB8u


#define PMW3389_SPIM_INSTANCE  1

static const nrfx_spim_t m_spim =
    NRFX_SPIM_INSTANCE(PMW3389_SPIM_INSTANCE);



/* configure and initialize the spi peripheral */

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

    ret_code_t ret = nrfx_spim_init(&m_spim, &config, NULL, NULL);


    //APP_ERROR_CHECK(ret);

    if(ret != NRFX_SUCCESS)
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


/* read sensor information registers into a struct instance */

bool pmw3389_read_info(pmw3389_info_t *info)
{
    if(info == NULL)
    {
        return false;
    }

    info->product_id = pmw3389_read_reg(PMW3389_REG_PRODUCT_ID);
    
    info->revision_id = pmw3389_read_reg(PMW3389_REG_REVISION_ID);

    info->inverse_product_id = pmw3389_read_reg(PMW3389_REG_INVERSE_PRODUCT_ID);

    info->srom_id = pmw3389_read_reg(PMW3389_REG_SROM_ID);


    return true;

}


#if 0

pmw3389_status_t pmw3389_init(void)
{
    
    pmw3389_status_t status;

    status = pmw3389_spi_init();

    if(status != PMW3389_OK)
    {
        return status;
    }


    nrf_delay_ms(50);




}

#endif




pmw3389_status_t pmw3389_init(void)
{
    pmw3389_status_t status;

    status = pmw3389_spi_init();

    if (status != PMW3389_OK)
    {
        return status;
    }

    /*
     * CS should already be driven high by pmw3389_spi_init().
     * Give the sensor time to settle after power-up.
     */
    nrf_delay_ms(50);

    uint8_t product_id =
        pmw3389_read_reg(PMW3389_REG_PRODUCT_ID);

    uint8_t revision_id =
        pmw3389_read_reg(PMW3389_REG_REVISION_ID);

    uint8_t inverse_product_id =
        pmw3389_read_reg(PMW3389_REG_INVERSE_PRODUCT_ID);

    uint8_t srom_id =
        pmw3389_read_reg(PMW3389_REG_SROM_ID);

    NRF_LOG_INFO(
        "PMW3389: PID=0x%02X REV=0x%02X INV=0x%02X SROM=0x%02X",
        product_id,
        revision_id,
        inverse_product_id,
        srom_id
    );

    if ((product_id != PMW3389_PRODUCT_ID_EXPECTED) ||
        (inverse_product_id != PMW3389_INVERSE_PRODUCT_ID_EXPECTED))
    {
        return PMW3389_INVALID_ID;
    }

    return PMW3389_OK;
}