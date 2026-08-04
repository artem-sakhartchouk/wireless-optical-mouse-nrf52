#ifndef PMW3389_H
#define PMW3389_H

#include <stdint.h>
#include <stdbool.h>


typedef struct
{
    uint8_t motion;
    int16_t dx;
    int16_t dy;
} pmw3389_motion_t;




typedef enum
{
    PMW3389_OK = 0,
    PMW3389_ERROR_SPI_INIT,
    PMW3389_ERROR_SPI_TRANSFER,
    PMW3389_INVALID_ID,
    PMW3389_ERROR_SROM,
    PMW3389_INVALID_PARAMETER
}pmw3389_status_t;


typedef struct
{
    uint8_t product_id;
    uint8_t revision_id;
    uint8_t inverse_product_id;
    uint8_t srom_id;
} pmw3389_info_t;



pmw3389_status_t pmw3389_read_info(pmw3389_info_t *info);


pmw3389_status_t pmw3389_init(void);

bool pmw3389_is_present(void);

bool pmw3389_read_motion(pmw3389_motion_t *motion);



#endif