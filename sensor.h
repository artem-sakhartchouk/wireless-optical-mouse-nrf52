#ifndef PMW3389_H
#define PMW3389_H

#include <stdint.h>
#include <stdbool.h>



typedef enum
{
    PMW3389_OK = 0,
    PMW3389_ERROR_SPI_INIT,
    PMW3389_ERROR_SPI_TRANSFER,
    PMW3389_INVALID_ID
}pmw3389_status_t;


typedef struct
{
    uint8_t product_id;
    uint8_t revision_id;
    uint8_t inverse_product_id;
    uint8_t srom_id;
} pmw3389_info_t;



bool pmw3389_read_info(pmw3389_info_t *info);


pmw3389_status_t pmw3389_init(void);

bool pmw3389_is_present(void);

bool pmw3389_read_motion(uint16_t *dx, uint16_t *dy);



#endif