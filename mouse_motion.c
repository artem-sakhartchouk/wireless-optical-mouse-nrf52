#include "mouse_motion.h"
#include "limits.h"

#include "stddef.h"



/* globals for accumulating 16-bit signed motion deltas to prevent overflow */
static int32_t m_pending_x;
static int32_t m_pending_y;



/* add current sensor delta readings to the running total */ 
void mouse_motion_accumulate(int16_t dx, int16_t dy)
{
    m_pending_x += dx;
    m_pending_y += dy;

}


/* limits maximum transmitted motion deltas to 8 bit values for original setup */

static int16_t clamp_axis_to_report(int32_t value)
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



/* extract motion deltas clamped to int8_t from accumulators,
 * make sure youre not sending zero deltas. 
 */
bool mouse_motion_peek_report(mouse_motion_t *report)
{
    if(report == NULL)
    {
        return  false;
    }

    report->x = clamp_axis_to_report(m_pending_x);
    report->y = clamp_axis_to_report(m_pending_y);


    return (m_pending_x != 0) || (m_pending_y != 0);

   
}


/* only remove extracted int8_t deltas from total if radio sends report */

void mouse_motion_commit_report(const mouse_motion_t *report)
{
    if (report == NULL)
    {
        return;
    }

    m_pending_x -= (int32_t)report->x;
    m_pending_y -= (int32_t)report->y;
}

