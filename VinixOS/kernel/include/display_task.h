/* ============================================================
 * display_task.h — Display Task (HDMI output)
 * ============================================================ */

#ifndef DISPLAY_TASK_H
#define DISPLAY_TASK_H

#include "task.h"

/**
 * Get display task structure (kernel mode).
 * Runs boot screen + splash, then manages HDMI display.
 */
struct task_struct *get_display_task(void);

#endif /* DISPLAY_TASK_H */
