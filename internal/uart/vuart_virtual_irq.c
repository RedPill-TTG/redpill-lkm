#ifndef VUART_USE_TIMER_FALLBACK

#include "vuart_virtual_irq.h"
#include "vuart_internal.h"
#include "../../common.h"
#include "../../debug/debug_vuart.h"
#include <linux/serial_reg.h> //UART_* consts
#include <linux/kthread.h> //running vIRQ thread
#include <linux/wait.h> //wait queue handling (init_waitqueue_head etc.)
#include <linux/serial_8250.h> //serial8250_handle_irq

//Default name of the thread for vIRQ
#ifndef VUART_THREAD_FMT
#define VUART_THREAD_FMT "vuart/%d-ttyS%d"
#endif

/**
 * Function running on a separate kernel thread responsible for simulating the IRQ call (normally done via hardware
 * interrupt triggering CPU to invoke Linux IRQ subsystem)
 *
 * There's no sane way to trigger IRQs in the low range used by 8250 UARTs. A pure asm call of "int $4" will result in a
 * crash (yes, we did try first ;)). So instead of hacking around the kernel we simply used the 8250 public interface to
 * trigger interrupt routines and implemented a small IRQ handling subsystem on our own.
 * @param data
 * @return
 */
static int virq_thread(void *data)
{
    allow_signal(SIGKILL);

    int out = 0;
    struct serial8250_16550A_vdev *vdev = data;

    uart_prdbg("%s started for ttyS%d pid=%d", __FUNCTION__, vdev->line, current->pid);
    while(likely(!kthread_should_stop())) {
        wait_event_interruptible(*vdev->virq_queue, !(vdev->iir & UART_IIR_NO_INT) || unlikely(kthread_should_stop()));
        if (unlikely(signal_pending(current))) {
            uart_prdbg("%s started for ttyS%d pid=%d received signal", __FUNCTION__, vdev->line, current->pid);
            out = -EPIPE;
            break;
        }

        if (unlikely(kthread_should_stop()))
            break;

        if (unlikely(!vdev->up)) {
            pr_loc_bug("Cannot call serial8250 interrupt handler - port not captured (yet?)");
            continue;
        }

        uart_prdbg("Calling serial8250 interrupt handler");
        serial8250_handle_irq(vdev->up, vdev->iir);
    }
    uart_prdbg("%s stopped for ttyS%d pid=%d exit=%d", __FUNCTION__, vdev->line, current->pid, out);

    //that can lead to a small memory leak for virq_queue if thread is killed outisde disable_interrupts() but this
    // shouldn't normally happen unless something goes horribly wrong
    vdev->virq_thread = NULL;

    return out;
}

int vuart_enable_interrupts(struct serial8250_16550A_vdev *vdev)
{
    int out;
    pr_loc_dbg("Enabling vIRQ for ttyS%d", vdev->line);
    lock_vuart(vdev);

    if (unlikely(!vdev->initialized)) {
        pr_loc_bug("ttyS%d is not initialized as vUART", vdev->line);
        out = -ENODEV;
        goto error_unlock_free;
    }

    if (unlikely(vuart_virq_active(vdev))) {
        pr_loc_bug("Interrupts are already enabled & scheduled for ttyS%d", vdev->line);
        out = -EBUSY;
        goto error_unlock_free;
    }

    if (!(vdev->virq_queue = kmalloc(sizeof(wait_queue_head_t), GFP_KERNEL)) ||
        !(vdev->virq_thread = kmalloc(sizeof(struct task_struct), GFP_KERNEL))) {
        out = -EFAULT;
        pr_loc_bug("kmalloc failed to reserve memory for vIRQ structures");
        goto error_unlock_free;
    }

    init_waitqueue_head(vdev->virq_queue);
    unlock_vuart(vdev); //we can safely unlock after reserving memory but before starting thread (so we're not atomic)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-extra-args"
    //VUART_THREAD_FMT can resolve to anonymized version without line or even IRQ#
    vdev->virq_thread = kthread_run(virq_thread, vdev, VUART_THREAD_FMT, vdev->irq, vdev->line);
#pragma GCC diagnostic pop
    if (IS_ERR(vdev->virq_thread)) {
        out = PTR_ERR(vdev->virq_thread);
        pr_loc_bug("Failed to start vIRQ thread");
        goto error_free;
    }
    pr_loc_dbg("vIRQ fully enabled for for ttyS%d", vdev->line);

    return 0;

    error_unlock_free:
    unlock_vuart(vdev);
    error_free:
    if (vdev->virq_queue) {
        kfree(vdev->virq_queue);
        vdev->virq_queue = NULL;
    }
    if (vdev->virq_thread) {
        kfree(vdev->virq_thread);
        vdev->virq_thread = NULL;
    }
    return out;
}

int vuart_disable_interrupts(struct serial8250_16550A_vdev *vdev)
{
    int out;
    pr_loc_dbg("Disabling vIRQ for ttyS%d", vdev->line);
    lock_vuart(vdev);

    if (unlikely(!vdev->initialized)) {
        pr_loc_bug("ttyS%d is not initialized as vUART", vdev->line);
        out = -ENODEV;
        goto out_unlock;
    }

    if (unlikely(!vuart_virq_active(vdev))) {
        pr_loc_bug("Interrupts are not enabled/scheduled for ttyS%d", vdev->line);
        out = -EBUSY;
        goto out_unlock;
    }

    out = kthread_stop(vdev->virq_thread);
    if (out < 0) {
        pr_loc_bug("Failed to stop vIRQ thread");
        goto out_unlock;
    }

    kfree(vdev->virq_thread);
    vdev->virq_thread = NULL;
    pr_loc_dbg("vIRQ disabled for ttyS%d", vdev->line);

    out_unlock:
    unlock_vuart(vdev);

    return 0;
}
#endif