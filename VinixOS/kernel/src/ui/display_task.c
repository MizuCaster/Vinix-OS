/* ============================================================
 * display_task.c — Display Task (Kernel Mode)
 * Runs boot screen + splash on HDMI, then manages display.
 * Independent of shell/UART — runs as scheduler task.
 * ============================================================ */

#include "types.h"
#include "display_task.h"
#include "task.h"
#include "scheduler.h"
#include "fb.h"
#include "boot_screen.h"

#define DISPLAY_STACK_SIZE 4096
static uint8_t display_stack[DISPLAY_STACK_SIZE]
    __attribute__((aligned(4096), section(".user_stack")));

static struct task_struct display_task_struct;

static void display_entry(void)
{
    /* Run boot log + splash animation */
    boot_screen_run();

    /* After splash: stay on splash screen, yield CPU.
     * Future: transition to home screen here. */
    while (1) {
        extern volatile bool need_reschedule;
        need_reschedule = true;
        scheduler_yield();
        __asm__ volatile("wfi");
    }
}

struct task_struct *get_display_task(void)
{
    display_task_struct.name = "Display";
    display_task_struct.state = TASK_STATE_READY;
    display_task_struct.id = 0;

    task_stack_init(&display_task_struct, display_entry,
                    display_stack, DISPLAY_STACK_SIZE);

    return &display_task_struct;
}
