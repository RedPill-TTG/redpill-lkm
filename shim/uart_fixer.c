#define SHIM_NAME "UART fixer"

#include "uart_fixer.h"
#include "shim_base.h"
#include "../common.h"
#include "../config/runtime_config.h" //STD_COM*
#include "../config/platform_types.h" //hw_config
#include "../internal/call_protected.h" //early_serial_setup()
#include "../internal/override/override_symbol.h" //overriding uart_match_port()
#include <linux/serial_8250.h> //serial8250_unregister_port

#ifdef DBG_DISABLE_UART_SWAP_FIX
static int noinline uart_swap_hw_output(unsigned int from, unsigned char to)
{
    pr_loc_wrn("UART swapping needed for the platform but forcefully disabled via DBG_DISABLE_UART_SWAP");
    return 0;
}
#elif defined(UART_BUG_SWAPPED)
#include "../internal/uart/uart_swapper.h"
#else
static int noinline uart_swap_hw_output(unsigned int from, unsigned char to)
{
    pr_loc_bug("Called %s from uart_fixer context when UART_BUG_SWAPPED is not set", __FUNCTION__);
    return -EINVAL;
}
#endif

static bool ttyS0_force_initted = false; //Was ttyS0 forcefully initialized by us?
static bool serial_swapped = false; //Whether ttyS0 and ttyS1 were swapped

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

/**
 * Reverses what fix_muted_ttyS0() did
 */
static int mute_ttyS0(void)
{
    pr_loc_dbg("Re-muting ttyS0");
    serial8250_unregister_port(0);

    return 0;
}

int register_uart_fixer(const hw_config_uart_fixer *hw)
{
    shim_reg_in();

    int out = 0;
    if (
            (hw->swap_serial && (out = uart_swap_hw_output(1, 0)) != 0) ||
            (hw->reinit_ttyS0 && (out = fix_muted_ttyS0()) != 0)
       ) {
        pr_loc_err("Failed to register UART fixer");

        return out;
    }

    serial_swapped = hw->swap_serial;

    shim_reg_ok();
    return out;
}

int unregister_uart_fixer(void)
{
    shim_ureg_in();

    int out = 0;
    if (
            (serial_swapped && (out = uart_swap_hw_output(0, 1)) != 0) ||
            (ttyS0_force_initted && (out = mute_ttyS0()) != 0)
       ) {
        pr_loc_err("Failed to unregister UART fixer");
        return out;
    }

    shim_ureg_ok();
    return out;
}