#ifndef REDPILL_VUART_VIRTUAL_IRQ_H
#define REDPILL_VUART_VIRTUAL_IRQ_H

#ifdef VUART_USE_TIMER_FALLBACK
#define vuart_virq_supported() 0
#define vuart_virq_wake_up(dummy) //noop
#define vuart_enable_interrupts(dummy) (0)
#define vuart_disable_interrupts(dummy) (0)

#else //VUART_USE_TIMER_FALLBACK
#include "vuart_internal.h"

#define vuart_virq_supported() 1
#define vuart_virq_active(vdev) (!!(vdev)->virq_thread)
#define vuart_virq_wake_up(vdev) if (vuart_virq_active(vdev)) { wake_up_interruptible(vdev->virq_queue); }
int vuart_enable_interrupts(struct serial8250_16550A_vdev *vdev);
int vuart_disable_interrupts(struct serial8250_16550A_vdev *vdev);
#endif //VUART_USE_TIMER_FALLBACK

#endif //REDPILL_VUART_VIRTUAL_IRQ_H