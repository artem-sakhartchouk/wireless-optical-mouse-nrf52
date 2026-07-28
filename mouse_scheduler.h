#ifndef MOUSE_SCHEDULER_H
#define MOUSE_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>

//for reducing esb tx frequency in idle mode when no activity is detected
typedef enum{
   MOUSE_ACTIVE,
   MOUSE_IDLE
}mouse_power_state_t;


void mouse_scheduler_init(void);
void mouse_scheduler_mark_active(void);
bool mouse_scheduler_report_pending(void);
void mouse_scheduler_report_queued(void);

mouse_power_state_t mouse_scheduler_power_state_get(void);

#endif