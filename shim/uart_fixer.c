#include "uart_fixer.h"
#include "../common.h"
#include "../config/runtime_config.h" //hw_config
#include "../internal/call_protected.h" //early_serial_setup(), update_console_cmdline()
#include "../internal/uart/uart_defs.h" //struct uart_port, COM ports definition
#include <linux/serial_8250.h> //serial8250_unregister_port
#include <linux/console.h> //console_lock(), console_unlock()
#include <linux/sched.h> //for_each_process, kill_pgrp

static bool serial_swapped = false; //Whether ttyS0 and ttyS1 were swapped
static bool ttyS0_force_initted = false; //Was ttyS0 forcefully initialized by us?

/**
 * After re-registering a console any TTYs (user terminals) will be unusable. This function restarts them.
 *
 * This is only applicable/effective when inserting the module after any tty has started. So in our scenario pretty much
 * only for debugging as during normal operation TTYs are started way later than this module.
 */
static void restart_ttys(void)
{
    pr_loc_dbg("Restarting TTYs");

    struct task_struct *proc;
    for_each_process(proc) {
        if (proc->parent->pid > 2 ||
            (strcmp(proc->parent->comm, "init") != 0 && strcmp(proc->parent->comm, "kthreadd") != 0))
            continue;

        //@todo this will only work for not-loggedin consoles hmm
        if(strcmp(proc->comm, "ash") == 0 || strcmp(proc->comm, "sh") == 0) {
            pr_loc_dbg("Killing TTY process %s (%d)", proc->comm, proc->pid);
            kill_pgrp(task_pid(proc), SIGKILL, 1);
        }
    }
}

/**
 * Swaps first two serial ports & updates kernel console indexing
 *
 * Some kernels are compiled with CONFIG_SYNO_X86_SERIAL_PORT_SWAP set which effectively swaps first two serial ports.
 * This function reverses that. It also makes sure to move kernel console output between them (if configured)
 *
 * Swapping the serials involves two things: swapping console drivers and swapping kernel console printk itself.
 * The first one can be done on any kernel by modifying exported "console_drivers". The second one requires an access
 * to either struct serial8250_ports (drivers/tty/serial/8250/8250_core.c) or to struct console_cmdline
 *  (kernel/printk/printk.c). Both of them are static so they're no-go.
 * Kernels before v4.1 had a convenient method update_console_cmdline(). Unfortunately this method was removed:
 * https://github.com/torvalds/linux/commit/c7cef0a84912cab3c9df8949b034e4aa62982ec9 so there's currently no method
 * of un-swapping on v4 ;<
 *
 * Things we've tried:
 *  - Set the new console as preferred (making it default for /dev/console) -> /dev/ttyS0 and 1 are still wrong
 *  - Unregistering both ports and re-registering them -> we tried a bit and it's a nightmare to re-do and crashes the
 *    kernel
 *  - Unregistering and re-registering consoles
 *  - Recreating the flow for serial8250_isa_init_ports() -> it appears to work (i.e. doesn't crash) but the serial port
 *    is dead afterwards and doesn't pass any traffic
 *
 *  Since there are no platforms running v4 with swapped serials (and hopefully there wouldn't be anymore) we're not
 *  digging any deeper into that. The current implementation is less-than-ideal on v3 as well as /dev/ttyS0 and 1 are
 *  still pointing to broken places... ehh, ffs why.
 *
 *  Hours wasted trying to reverse stupid ports: 37        (increment when you think you've got it and you failed)
 */
static int swap_uarts(int from, int to)
{
    struct console *con;
    struct console *ttyS0_con = NULL; //Populated by a console which was set to be on a *REAL* ttyS0 (ttyS1 swapped)
    struct console *ttyS1_con = NULL; //Populated by a console which was set to be on a *REAL* ttyS1 (ttyS0 swapped)

    console_lock(); //Stops (and buffers) printk calls while pulling console semaphore down
    for_each_console(con) {
        if (strcmp(con->name, STD_COMX_DEV_NAME) != 0)
            continue;

        pr_loc_dbg("Swapping console %s%d index", con->name, con->index);
        if (con->index == 1) {
            con->index = 0;
            ttyS1_con = con;
        } else if (con->index == 0) {
            con->index = 1;
            ttyS0_con = con;
        }
    }
    console_unlock(); //Flushes printks and releases semaphore

    pr_loc_inf("Swapped %s0 & %s1, updating console %s%d => %s%d", STD_COMX_DEV_NAME, STD_COMX_DEV_NAME,
               STD_COMX_DEV_NAME, from, STD_COMX_DEV_NAME, to);

    //This call is only successful when loading early (not in the shell) so it CAN fail
    _update_console_cmdline(STD_COMX_DEV_NAME, from, STD_COMX_DEV_NAME, to, SRD_COMX_BAUD_OPTS);
    serial_swapped = true;
    restart_ttys();

    return 0;
}

/**
 * On some platforms (e.g. 918+) the first serial port appears to not be functional as it's not initialized properly.
 *
 * It is speculated that it has to do with "CONFIG_SYNO_X86_TTY_CONSOLE_OUTPUT=y" but it's not confirmed. If this is not
 * fixed by this function setting kernel console output to ttyS0 will result in earlycon working as expected (as it
 * doesn't use the normal 8250 driver) with nothing being transmitted as soon as earlycon is switched to the proper
 * "console=" port.
 */
static int fix_muted_ttyS0(void)
{
    int out = 0;
    struct uart_port port = {
        .iobase = STD_COM1_IOBASE,
        .uartclk = STD_COMX_BAUD * 16,
        .irq = STD_COM1_IRQ,
        .flags = STD_COMX_FLAGS
    };

    if ((out = _early_serial_setup(&port)) != 0) {
        pr_loc_err("Failed to register ttyS0 to hw port @ %lx", port.iobase);
        return out;
    }

    pr_loc_dbg("Fixed muted ttyS0 to hw port @ %lx", port.iobase);
    ttyS0_force_initted = true;
    return out;
}

static int mute_ttyS0(void)
{
    pr_loc_dbg("Re-muting ttyS0");
    serial8250_unregister_port(0);

    return 0;
}

int register_uart_fixer(const hw_config_uart_fixer *hw)
{
    int out = 0;

    if (
            (hw->swap_serial && (out = swap_uarts(1, 0)) != 0) ||
            (hw->reinit_ttyS0 && (out = fix_muted_ttyS0()) != 0)
       ) {
        pr_loc_err("Failed to register UART fixer");

        return out;
    }

    pr_loc_inf("UART fixer registered");
    return out;
}

int unregister_uart_fixer(void)
{
    int out = 0;

    if (
            (serial_swapped && (out = swap_uarts(0, 1)) != 0) ||
            (ttyS0_force_initted && (out = mute_ttyS0()) != 0)
       ) {
        pr_loc_err("Failed to unregister UART fixer");
        return out;
    }

    pr_loc_inf("UART fixer unregistered");
    return out;
}