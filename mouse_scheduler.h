#ifndef MOUSE_SCHEDULER_H
#define MOUSE_SCHEDULER_H

#include <stdbool.h>

typedef enum
{
    MOUSE_ACTIVE,
    MOUSE_IDLE
} mouse_power_state_t;

void mouse_scheduler_init(void);

void mouse_scheduler_mark_active(void);

bool mouse_scheduler_report_take(void);

void mouse_scheduler_try_enter_idle(void);

mouse_power_state_t mouse_scheduler_power_state_get(void);

#endif