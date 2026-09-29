#include "mouse_motion.h"

#include <limits.h>
#include <stddef.h>


/* Pending signed motion accumulated between successful reports. */
static int32_t m_pending_x = 0;
static int32_t m_pending_y = 0;


/* Add the latest sensor deltas to the pending motion total. */
void mouse_motion_accumulate(int16_t dx, int16_t dy)
{
    m_pending_x += dx;
    m_pending_y += dy;
}


/*
 * Clamp an accumulated axis value to the range representable
 * by one 16-bit motion report.
 */
static int16_t clamp_to_int16(int32_t value)
{
    if (value > INT16_MAX)
    {
        return INT16_MAX;
    }

    if (value < INT16_MIN)
    {
        return INT16_MIN;
    }

    return (int16_t)value;
}


/*
 * Snapshot the currently pending motion without consuming it.
 *
 * Each axis is limited to the range representable by one report.
 * Any remaining motion stays pending for a later report.
 */
bool mouse_motion_peek_report(mouse_motion_t *report)
{
    if (report == NULL)
    {
        return false;
    }

    report->x = clamp_to_int16(m_pending_x);
    report->y = clamp_to_int16(m_pending_y);

    return (m_pending_x != 0) || (m_pending_y != 0);
}


/*
 * Remove the motion contained in a successfully delivered report.
 *
 * Motion accumulated after the snapshot remains pending.
 */
void mouse_motion_commit_report(const mouse_motion_t *report)
{
    if (report == NULL)
    {
        return;
    }

    m_pending_x -= (int32_t)report->x;
    m_pending_y -= (int32_t)report->y;
}