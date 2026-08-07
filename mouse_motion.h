#ifndef MOUSE_MOTION_H
#define MOUSE_MOTION_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
   int16_t x;
   int16_t y;

}mouse_motion_t;


void mouse_motion_accumulate(int16_t dx, int16_t dy);
bool mouse_motion_peek_report(mouse_motion_t *report);
void mouse_motion_commit_report(const mouse_motion_t *report);
bool mouse_motion_pending(void);

#endif