/**
 * This tool is an isolated UART port-swapping utility allowing you to swap any two ports on runtime
 *
 * REASONING
 * Some kernels are compiled with CONFIG_SYNO_X86_SERIAL_PORT_SWAP set which effectively swaps first two serial ports.
 * This function reverses that. It also makes sure to move kernel console output between them (if configured).
 *
 *
 * OVERVIEW
 * Swapping the serials involves two things: swapping console drivers and swapping kernel console printk itself.
 * The first one can be done on any kernel by modifying exported "console_drivers". The second one requires an access
 * to either struct serial8250_ports (drivers/tty/serial/8250/8250_core.c) or to struct console_cmdline
 *  (kernel/printk/printk.c). Both of them are static so they're no-go directly.
 * Kernels before v4.1 had a convenient method update_console_cmdline(). Unfortunately this method was removed:
 * https://github.com/torvalds/linux/commit/c7cef0a84912cab3c9df8949b034e4aa62982ec9 so there's currently no method
 * of un-swapping on v4. Even worse calling this method on lower kernels is a combination of luck and timing (as this is
 * a init-only method).
 *
 *
 * IMPLEMENTATION
 * Things we've tried and failed:
 *  - Set the new console as preferred (making it default for /dev/console) -> /dev/ttyS0 and 1 are still wrong
 *  - Unregistering both ports and re-registering them -> we tried a bit and it's a nightmare to re-do and crashes the
 *    kernel
 *  - Unregistering and re-registering consoles -> will fail with KP or do nothing (and even if it worked if a
 *    non-console port is involved it will be broken)
 *  - Recreating the flow for serial8250_isa_init_ports() -> it appears to work (i.e. doesn't crash) but the serial port
 *    is dead afterwards and doesn't pass any traffic (we've never discovered why)
 *  => Hours wasted trying to reverse stupid ports: 37
 *  
 * What actually did work was carefully split-stopping the port (stopping the driver/hardware end of but not the higher
 * level ttyS# side), exchanging the iobase & IRQ (+ some other internal things generated during init), restarting the
 * port and hoping for the best. It does work BUT it's ridden with edge cases. Properly implementing and testing this
 * took ~3 full days and two people... so if you're not sure what you're doing here you can easily break it.
 *
 *
 * INTERNAL DETAILS
 * What we are doing here may not be intuitive if you don't exactly know how the 8250 driver works internally. Let's go
 * over this first. Each port is composed of outer uart_8250_port which contains various driver-specific information.
 * Inside it there's an uart_port struct which contains the information about the actual physical UART channel.
 *
 * When the ports were flipped their position in the internal 8250 list, their line# in uart_8250_port and other
 * internal properties were kept intact. The only two things which were changed are ->port.iobase and ->port.irq
 * Flipping these is enough to make port sort-of-working. The port will pass data BUT only if the other port triggers
 * interrupts (e.g. you type something on ttyS0 and nothing happens, you hold a space bar on ttyS1 afterwards and
 * stuff you've typed on ttyS0 starts appearing). This strange effect is caused by how 8250 driver implements IRQ
 * handling (serial8250_interrupt in 8250_core.c). It does not iterate over ALL ports looking for ports which match
 * the IRQ triggering the function. [FROM NOW ON READ CAREFULLY!] Instead, it uses and struct irq_info passed as
 * "user data" to the IRQ handler function. That struct contains a POINTER to a list_head of the first port, making
 * the irq_struct the list owner. That doubly-linked list ties together multiple uart_8250_port structures (->list)
 * which share the same IRQ. Here's the major problem: changing the NUMERIC IRQ value in uart_8250_port->port.irq
 * does nothing to the actual IRQ handling for that port.
 *
 * When an IRQ happens and serial8250_interrupt() fires, it looks at the irq_struct, gets the pointer to list_head
 * containing all active ports sharing a given IRQ and then just iterates over them triggering their internal
 * handling of stuff. The irq_struct list_head pointer is just a memory address and our swaps of anything will never
 * change it. This means two things: we need to fix the shared IRQ lists in uart_8250_port for ttyS0 and ttyS1 AND
 * fix the mapping of IRQ => uart_8250_port element. Keep in mind that uart_8250_port->list contains only the ports
 * which are currently active (i.e. enabled AND open). Kernel registers for IRQs only if something actually opens
 * the port (as there's no point to receive data where you don't place to deliver them).
 * 
 * Unfortunately, fixing these lists isn't really possible. The irq_struct contains a pointer to list_head which can
 * be in any of the uart_8250_port. If it points to ttyS0 and/or ttyS1 (spoiler: most likely yes, as they're open)
 * deleting an element from that list will break it completely (as irq_struct only knows the address of that one
 * uart_8250_port in practice). If we replace prev/next to point to a different port it will still break because
 * the interrupt handler calls container_of() which make prev/next irrelevant for the first fetch.
 * struct_irq is contained in two places: internally cached in the 8250_core.c and as a pointer in the kernel IRQ
 * handling. It will be unwise to try to modify it (ok, we're saying this only because we found a better way :D).
 *
 * Normally the IRQ manipulation is enabled/disabled by serial_link_irq_chain/serial_unlink_irq_chain in 8250_core.c
 * but they're static. However, we an exploit the fact that 8250 driver is modular and operations on the Linux port
 * are separated from operations on the hardware (which makes sense). We can command the UART chip to shutdown
 * before we touch iobase or irq (which naturally has to remove IRQ if present) and then command the chip to startup
 * which will register IRQ if needed (for the new irq value of course ;))
 *
 * References:
 *  - Linux kernel sources (mainly drivers/tty/serial/8250/8250_core.c and drivers/tty/serial/serial_core.c)
 *  - https://linux-kernel-labs.github.io/refs/heads/master/labs/interrupts.html
 *  - https://www.ti.com/lit/ug/sprugp1/sprugp1.pdf
 */

#include "../../common.h"
#include "../call_protected.h" //early_serial_setup()
#include "../override_symbol.h" //overriding uart_match_port()
#include "../../config/uart_defs.h" //struct uart_port, COM ports definition, UART_NR
#include <linux/serial_8250.h> //struct uart_8250_port
#include <linux/console.h> //console_lock(), console_unlock()
#include <linux/hardirq.h> //synchronize_irq()
#include <linux/list.h> //LIST_POISON1, LIST_POISON2
#include <linux/timer.h> //timer_pending()
#include <linux/interrupt.h> //disable_irq()/enable_irq()
#include <linux/irqdesc.h> //irq_has_action

#define pause_irq_save(irq) ({bool __state = irq_has_action(irq); if (__state) { disable_irq(irq); } __state; })
#define resume_irq_saved(irq, saved) if (saved) { enable_irq(irq); }

/*********************************************** Extracting 8250 ports ************************************************/
static struct uart_8250_port *serial8250_ports[UART_NR] = { NULL }; //recovered ptrs to serial8250_ports structure
static override_symbol_inst *ov_uart_match_port = NULL;

/**
 * Fake uart_match_port() which always returns "no match" but collects all passing ports to serial8250_ports
 *
 * See recover_serial8250_ports() for usage. This is a very specific thing and shouldn't be used standalone.
 *
 * @return 0
 */
static int uart_match_port_collector(struct uart_port *port1, struct uart_port *port2)
{
    //our fake trigger calls with one port being NULL, that's how we can easily detect which one is the one provided by
    //the driver ;]
    struct uart_port *port = port1 ? port1:port2;
    pr_loc_dbg("Found ptr to line=%d iobase=0x%03lx irq=%d", port->line, port->iobase, port->irq);

    serial8250_ports[port->line] = container_of(port, struct uart_8250_port, port);

    return 0;
}

/**
 * Enables collecting of 8250 serial port structures
 *
 * Warning: before you do that you MUST disable IRQs or you're risking a serious crash or a silent corruption of the
 * kernel!
 *
 * @return 0 on success, -E on failure
 */
static int __must_check enable_collector_matcher(void)
{
    if (unlikely(ov_uart_match_port))
        return 0; //it's not a problem is we already enabled it as it's enabled all the time

    ov_uart_match_port = override_symbol_ng("uart_match_port", uart_match_port_collector);
    if (unlikely(IS_ERR(ov_uart_match_port))) {
        int out = PTR_ERR(ov_uart_match_port);
        ov_uart_match_port = NULL;
        return out;
    }

    return 0;
}

/**
 * Disabled collecting of 8250 serial port structures (reverses enable_collector_matcher())
 *
 * @return 0 on success or noop, -E on failure
 */
static int disable_collector_matcher(void)
{
    if (unlikely(!ov_uart_match_port))
        return 0; //it's not a problem is we already disabled it

    int out = restore_symbol_ng(ov_uart_match_port);
    ov_uart_match_port = NULL;

    if (unlikely(out != 0))
        pr_loc_err("Failed to disable collector matcher, error=%d", out);

    return out;
}

/**
 * Fish-out 8250 serial driver ports from its internal structures
 *
 * The 8250 serial driver is very secretive of its ports and doesn't allow anyone to access them. This is for a good
 * reason - it's very easy to cause a deadlock, KP, or a runaway CPU-hogging process. However, we must access them as
 * we're intentionally messing up with the structures of them (as SOMEONE had a BRILLIANT idea to break them by swapping
 * iobases and IRQs defined since the 1970s).
 *
 * Ports will be populated in a serial8250_ports.
 */
static int recover_serial8250_ports(void)
{
    int out = 0;
    //Stops and buffers printks while pulling console semaphore down (in case console is active on any of the ports)
    console_lock();
    preempt_disable();

    //We cannot acquire any locks as we don't have ports information. The most we can do is ensure nothing tirggers
    // while we collect them. It's imperfect as some ports are timer based etc. However, the chance is abysmal that
    // with preempt disabled and IRQs disabled something magically triggers ports lookup (which is rare by itself).
    //While there may be more than 4 ports their IRQs aren't well defined by the platform nor kernel.
    //Some of these may be shared but we don't make assumptions here (as it will be a noop if we call it twice)
    bool com1_irq_state = pause_irq_save(STD_COM1_IRQ);
    bool com2_irq_state = pause_irq_save(STD_COM2_IRQ);
    bool com3_irq_state = pause_irq_save(STD_COM3_IRQ);
    bool com4_irq_state = pause_irq_save(STD_COM4_IRQ);

    if (unlikely((out = enable_collector_matcher()) != 0)) { //Install a fake matching function
        pr_loc_err("Failed to enable collector!");
        goto out;
    }

    _serial8250_find_port(NULL); //Force the driver to iterate over all its ports... using our fake matching function

    if (unlikely((out = disable_collector_matcher()) != 0)) //Restore normal matcher
        pr_loc_err("Failed to enable collector!");

    //Other processes will use spinlocks with IRQ-save as we now know the ports
    out:
    resume_irq_saved(STD_COM1_IRQ, com1_irq_state);
    resume_irq_saved(STD_COM2_IRQ, com2_irq_state);
    resume_irq_saved(STD_COM3_IRQ, com3_irq_state);
    resume_irq_saved(STD_COM4_IRQ, com4_irq_state);
    preempt_enable();
    console_unlock();

    return out;
}

/**
 * Gets an internal 8250 driver port structure for the line/ttyS specified
 *
 * Things to know:
 *  - line = ttyS#, so line=0 = ttyS0 (this is universal across Linux UART subsystem)
 *  - this function returns things as-is in the 8250 driver, so if ports are already reversed you will get them reversed
 *  - this function only runs scanning once but only ptrs are stored, so if you flip ports the re-scan is not needed as
 *    8250 builds its internal array (to which elements we get ptrs) only once during boot
 *
 * @return ptr to a port OR error ptr with -E
 */
static __must_check struct uart_8250_port *get_8250_port(unsigned int line)
{
    if (unlikely(line >= UART_NR)) {
        pr_loc_bug("Requested UART line %u but kernel supports up to %u", line, UART_NR);
        return ERR_PTR(-EINVAL);
    }

    if (!serial8250_ports[0]) //Port not recovered or port 0 doesn't exist (HIGHLY unlikely)
        recover_serial8250_ports(); //there's no point in checking the return code here - it will fail below

    return (likely(serial8250_ports[line])) ? serial8250_ports[line] : ERR_PTR(-ENODEV);
}


/****************************************** Shutting down & restarting ports ******************************************/
#define is_irq_port(uart_port_ptr) ((uart_port_ptr)->irq != 0)

/**
 * Check if IRQ-based port is active (i.e. open and running)
 *
 * To use this function the caller is responsible for obtaining a port spinlock.
 *
 * Warning: it's up to the CALLER to check type of the port (is_irq_port()). Passing a timer-based port here will
 *          always return false, as timer ports don't register for IRQs and are not listed in IRQ-sharing list.
 */
static bool __always_inline is_irq_port_active(struct uart_8250_port *up)
{
    struct uart_port *port = &up->port;

    //if the kernel doesn't have an action for the IRQ there's no way 8250 has the port active in interrupt mode
    if (!irq_has_action(port->irq)) {
        pr_loc_dbg("IRQ=%d not active => port not active", port->irq);
        return false;
    }

    //IRQ port list was never initialized, or it was deleted (which poisons it) => list element is invalid
    // We don't care where prev/next point - they can point both at us (=we're the only ones active on that IRQ),
    // can both point at a single other element (=two element list with us included), or can point to two different
    // elements (=list with >2 elements). Either way WE are active.
    if (!up->list.prev || !up->list.next) {
        pr_loc_dbg("IRQ sharing list not initialized => port not active");
        return false;
    }

    if (up->list.next == LIST_POISON1 && up->list.prev == LIST_POISON2) {
        pr_loc_dbg("IRQ sharing list poisoned/deleted => port not active");
        return false;
    }

    pr_loc_dbg("Port is active (IRQ=%d active, list valid p=%p/n=%p)", port->irq, up->list.prev, up->list.next);
    return true;
}

/**
 * Checks if a timer-based port is active (i.e. open and running)
 *
 * To use this function the caller is responsible for obtaining a port spinlock.
 *
 * For the timer-based port to be active it must: have a function set (=it was configured at least once), and be
 * in active or pending state. We only care about the pending one as time timer cannot be active (=currently
 * executing handler function) when we have a lock on the port.
 *
 * Warning: it's up to the CALLER to check type of the port (is_irq_port()). Passing IRQ port here will always return
 *          false, as IRQ ports don't use timers.
 */
static bool __always_inline is_timer_port_active(struct uart_8250_port *up)
{
    return (likely(up->timer.function) && timer_pending(&up->timer));
}

/**
 * Checks if a given port is active (i.e. open and running)
 *
 * The startup & shutdown of the port is needed any time the port is active/open. The port is formally shut down if
 *  there's nooone using it. The 8250 driver doesn't really know that (ok, it does if you try to probe the chip etc)
 *  directly, as only the TTY serial layer tracks that (drivers/tty/serial/serial_core.c).
 *
 * The trick here is that  We cannot check if the driver has the
 *  interrupt for a given port directly (as the irq_lists is static). However, we can derive this by checking if the
 *  kernel has the IRQ handler registered for the given IRQ# *AND* if the port in question is part of the list for
 *  the IRQ. We can cheat here as we only need to know our own state. So in practice we need to just check if our
 *  list element (embedded list_head) is valid. See code for details.
 * While technically we CAN re-shutdown a port as many times as we want AS LONG AS it's not using the IRQ subsystem we
 *  shouldn't re-start a port which wasn't started before we tinkered with it! This is why we take care of IRQ and non-
 *  IRQ ports in the same. If you attempt to shutdown already shutdown port which is an IRQ one it will result in a
 *  kernel BUG() as the driver detects that something went wrong as it expects the IRQ to be running. If you do the
 *  same with timer-based port it will simply re-clear registries on the UART chip which will be a noop hardware-wise.
 *  This is because the 8250 and derivates cannot be really turned off once they start/reset. They can only be set in a
 *  way that they don't deliver interrupts for new data (and any new data will just override existing one). With timer-
 *  based port the kernel simply don't ask the chip if there's any data but the chip is still running. This is exactly
 *  why the 8250 driver will always attempt a read before "starting" the port and clear FIFOs on it.
 */
static bool is_port_active(struct uart_8250_port *up)
{
    bool out;
    struct uart_port *port = &up->port;
    pr_loc_dbg("Checking if port iobase=0x%03lx irq=%d (mapped to ttyS%d) active", port->iobase, port->irq, port->line);

    //Most of the ports will be IRQs unless something's broken/special about the platform
    if (likely(is_irq_port(port)))
        out = is_irq_port_active(up);
    else
        out = is_timer_port_active(up);

    return out;
}

/**
 * Shuts down the port if it's active
 *
 * You should NOT call this function with a lock active!
 *
 * @return 0 if the operation resulted in noop, 1 if the port was actually shut down; currently there are no error
 *         conditions
 */
static inline int try_shutdown_port(struct uart_8250_port *up)
{
    struct uart_port *port = &up->port;
    pr_loc_dbg("Shutting down physical port iobase=0x%03lx (mapped to ttyS%d)", port->iobase, port->line);

    if (!is_port_active(up)) {
        pr_loc_dbg("Port not active - noop");
        return 0;
    }

    port->ops->shutdown(port); //this must be called with the lock released or otherwise a deadlock may occur
    if (is_irq_port(port))
        synchronize_irq(port->irq); //Make sure interrupt handler is not running on another CPU/core

    pr_loc_dbg("Port iobase=0x%03lx ttyS%d is now DOWN", port->iobase, port->line);

    return 1;
}

/**
 * Restart previously stopped port
 *
 * Warnings:
 *  - you shouldn't attempt to restart ports which weren't configured; this can lead to a KP
 *  - you should NOT call this function when holding a lock
 *
 */
static inline void restart_port(struct uart_8250_port *up)
{
    struct uart_port *port = &up->port;
    pr_loc_dbg("Restarting physical port iobase=0x%03lx (mapped to ttyS%d)", port->iobase, port->line);

    //We are not checking if the port is active here due to an edge case of swap between one port which is active where
    // another one isn't. In such case when we shut down that active port and try to activate the other (to keep the
    // userland state happy) the check will lead to a false-negative state saying the port is already active. This is
    // because we did swap IRQ values. However, we MUST restart such port not to reinit the hardware (which doesn't
    // care) but to fix the interrupt mapping in the kernel!
    //skip extensive tests - it was working before
    port->flags |= UPF_NO_TXEN_TEST;
    port->flags |= UPF_SKIP_TEST;
    port->ops->startup(port); //this must be called with the lock released or otherwise a deadlock may occur

    pr_loc_dbg("Port iobase=0x%03lx ttyS%d is now UP", port->iobase, port->line);
}


/*************************************************** Swapping logic ***************************************************/
/**
 * Swaps two UART data lines with proper locking
 *
 * This function assumes ports are already stopped.
 */
static inline void swap_uart_lanes(struct uart_8250_port *a, struct uart_8250_port *b)
{
    unsigned long flags_a, flags_b;
    spin_lock_irqsave(&a->port.lock, flags_a);
    spin_lock_irqsave(&b->port.lock, flags_b);

    swap(a->port.iobase, b->port.iobase);
    swap(a->port.irq, b->port.irq);
    swap(a->port.uartclk, b->port.uartclk); //Just to be complete we should move flags & clock
    swap(a->port.flags, b->port.flags);     // (they're probably the same anyway)
    swap(a->timer, b->timer); //if one port was timer based and another wasn't this ensures they aren't broken

    spin_unlock_irqrestore(&a->port.lock, flags_b); //flags_a were a property of B
    spin_unlock_irqrestore(&b->port.lock, flags_a);
}

int uart_swap_hw_output(unsigned int from, unsigned int to)
{
    if (unlikely(from == to))
        return -EINVAL;

    pr_loc_dbg("Swapping ttyS%d<=>ttyS%d started", from, to);

    struct uart_8250_port *port_a = get_8250_port(from);
    struct uart_8250_port *port_b = get_8250_port(to);

    if (unlikely(!port_a)) {
        pr_loc_err("Failed to locate ttyS%d port", from);
        return PTR_ERR(port_a);
    }
    if (unlikely(!port_b)) {
        pr_loc_err("Failed to locate ttyS%d port", to);
        return PTR_ERR(port_b);
    }


    pr_loc_dbg("Disabling preempt & locking console");
    pr_loc_inf("======= OUTPUT ON THIS PORT WILL STOP AND CONTINUE ON ANOTHER ONE (swapping ttyS%d & ttyS%d) =======",
               from, to); //That will be the last message user sees before swap on the "old" port

    pr_loc_dbg("### LAST MESSAGE BEFORE SWAP ON \"OLD\" PORT ttyS%d<=>ttyS%d", from, to);
    preempt_disable(); //we cannot be rescheduled here due to timing constraint and possibly IRQ interactions
    console_lock(); //We don't want stray messages landing somewhere randomly when we swap, + the ports will be down
    //this will be the first message after port unlocks after swapping
    pr_loc_dbg("### FIRST MESSAGE AFTER SWAP ON \"NEW\" PORT ttyS%d<=>ttyS%d", from, to);

    //This is an edge case when swapping two ports where one is active and another one is not. Since the active status
    // is a property of the software (i.e. port opened/used by something) and shutting down/starting alters the state
    // of the hardware we may have a problem with restarting the previously inactive port. If WE did shut it down there
    // is no issue as we know the hardware is initialized. But if it wasn't and we try to just start it up without
    // reinit we can either crash the driver or leave the port in inactive state.
    pr_loc_dbg("Disabling ports");
    int port_a_was_running = try_shutdown_port(port_a);
    int port_b_was_running = try_shutdown_port(port_b);
    if (unlikely(port_a_was_running != port_b_was_running))
        pr_loc_wrn("Swapping hw data paths of ttyS%d (was %sactive) and ttyS%d (was %sactive). We will attempt to "
                   "reactivate inactive one but this may fail.", port_a->port.line, port_a_was_running ? "" : "in",
                   port_b->port.line, port_b_was_running ? "" : "in");

    swap_uart_lanes(port_a, port_b);
    //This code IS CORRECT - make sure to read comment next to port_a_was_running/port_b_was_running vars initialization
    //We swapped the data paths but we need to restore the state as the userland expects it.
    pr_loc_dbg("Restarting ports");
    if (port_a_was_running)
        restart_port(port_a);
    if (port_b_was_running)
        restart_port(port_b);

    console_unlock();
    preempt_enable();

    pr_loc_inf("======= OUTPUT ON THIS PORT CONTINUES FROM A DIFFERENT ONE (swapped ttyS%d & ttyS%d) =======", from,
               to);

    pr_loc_dbg("Swapping ttyS%d (curr_iob=0x%03lx) <=> ttyS%d (curr_iob=0x%03lx) finished successfully", from,
               port_a->port.iobase, to, port_b->port.iobase);

    return 0;
}