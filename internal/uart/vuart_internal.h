#ifndef REDPILL_VUART_INTERNAL_H
#define REDPILL_VUART_INTERNAL_H

#include <linux/spinlock.h>
#ifndef VUART_USE_TIMER_FALLBACK
#include <linux/wait.h>
#endif


//Lock/unlock vdev for registries operations
#define lock_vuart(vdev) spin_lock_irqsave((vdev)->lock, (vdev)->lock_flags);
#define unlock_vuart(vdev) spin_unlock_irqrestore((vdev)->lock, (vdev)->lock_flags);

//In some circumstances operations may be performed on the chip before or after the chip is initialized. If it is
// initialized we need a lock first; otherwise we do not. This is a shortcut for this opportunistic/conditional locking.
#define lock_vuart_oppr(vdev) if ((vdev)->initialized) { lock_vuart(vdev); }
#define unlock_vuart_oppr(vdev) if ((vdev)->initialized) { unlock_vuart(vdev); }

#define validate_isa_line(line) \
    if (unlikely((line) > SERIAL8250_LAST_ISA_LINE)) { \
        pr_loc_bug("%s failed - requested line %d but kernel supports only %d", __FUNCTION__, line, \
                   SERIAL8250_LAST_ISA_LINE); \
        return -EINVAL; \
    }

/**
 * An emulated 16550A chips internal state
 *
 * See http://caro.su/msx/ocm_de1/16550.pdf for details; registers are on page 9 (Table 2)
 */
struct serial8250_16550A_vdev {
    //Port properties
    u8			line;
    u16			iobase;
    u8			irq;
    unsigned int         baud;

    //The 8250 driver port structure - it will be populated as soon as 8250 gives us the real pointer
    struct uart_port *up;

    //Chip emulated FIFOs
    struct kfifo *tx_fifo; //character to be sent (aka what we've got from the OS)
    struct kfifo *rx_fifo; //characters received (aka what we want the OS to get from us)

    //Chip registries (they're considered volatile but there's a spinlock protecting them)
    u8 rhr; //Receiver Holding Register (characters received)
    u8 thr; //Transmitter Holding Register (characters REQUESTED to be sent, TSR will contain these to be TRANSMITTED)
    u8 ier; //Interrupt Enable Register
    u8 iir; //Interrupt ID Register (same as ISR/Interrupt Status Register)
    u8 fcr; //FIFO Control Register (not really used but holds values written to it)
    u8 lcr; //Line Control Register (not really used but holds values written to it)
    u8 mcr; //Modem Control Register (used to control autoflow)
    u8 lsr; //Line Status Register
    u8 msr; //Modem Status Register
    u8 scr; //SCratch pad Register (in the original docs refered to as SPR, but linux uses SCR name)
    u8 dll; //Divisor Lat Least significant byte (not really used but holds values written to it)
    u8 dlm; //Divisor Lat Most significant byte (not really used but holds values written to it; also called DLH)
    u8 psd; //Prescaler Division (not really used but holds values written to it)

    //Some operations (e.g. FIFO access) must be locked
    bool initialized:1;
    bool registered:1; //whether the vdev is actually registered with 8250 subsystem
    spinlock_t *lock;
    unsigned long lock_flags;

#ifndef VUART_USE_TIMER_FALLBACK
    //We emulate (i.e. self-trigger) interrupts on threads
    struct task_struct *virq_thread; //where fake interrupt code is executed
    wait_queue_head_t *virq_queue; //wait queue used to put thread to sleep
#endif
};

#endif //REDPILL_VUART_INTERNAL_H
