/**
 * A true National Semiconductors 16550A software emulator
 *
 * WHAT IS THIS?
 * -------------
 * In short this provides a feature-complete emulation of the now-famous 16550A chip present in IBM/PC compatibles
 * since ~1987. This emulator was prepared to work with the Linux 8250 driver and fool it into believing it talks with a
 * real chip. Moreover, the code isn't hacking around any private parts of the kernel but rather fully emulates
 * registries and their behaviors according to the chip's data sheet.
 * The emulation layer supports standard 8250-compliant feature (in essence UART) set with addition of two 16 bytes
 * TX/RX FIFOs with configurable threshold as well as timer or virtual IRQ model. The code should be pretty
 * straight-forward to read but it contains MANY quirks. All of them however are heavily documented throughout the file.
 *
 * DEALING WITH OPEN PORTS
 * -----------------------
 * While using this module you should know that there's a one important quirk: since we're in the Linux kernel we can
 * do anything with the port even if it's open. It's a blessing and a curse. Even if the physical ttyS1 port is open and
 * you add a virtual ttyS1 all of the sudden all applications will talk to your virtual port. This is great as you don't
 * have to restart them but it's also bad while debugging as you may get input you don't expect. To see what's using the
 * port execute "ls -l /proc/[0-9]<asterisk>/fd/<asterisk> 2>&1 |grep /dev/ttyS1 2>&1 | grep ttyS" (replace <asterisk>)
 * as lsof is not available in pre-boot.
 * A note however: the /dev/ttyS node WILL be recreated, so if you do replce a port which was opened you can expect to
 * see "/dev/ttyS1 (deleted)" in the ls output from above. It is not an issue as we're "taking over" the port anyway so
 * both the /dev/ttyS1 as well as the old fd are pointing to the same place kernel-wise.
 *
 * LIMITATIONS
 * -----------
 *  - For obvious reasons (as we are not working with a real hw) the DMA portion of the chip is not emulated
 *  - On most system the maximum number of UARTs emulated is 4 (driver's limitation, see CONFIG_SERIAL_8250_NR_UARTS)
 *  - FIFOs, true to the original 16550A, are limited to 16 bytes each. In theory, if needed, they can be enlarge up to
 *    even 256 bytes each with chip model change (as 8250 driver actually tests how big a FIFO is on setup)
 *  - FIFO mode is always enabled. There are some not-fully-accurate pieces which don't handle non-FIFO operation. There
 *    is (at least to our knowledge) no reason to use it adn kernel always asks for FIFO to save CPU anyway.
 *
 * USAGE
 * -----
 * See header file docs.
 * 
 * INTERNALS
 * ---------
 *  - To DISABLE vIRQ and fall back to a timer (offered by 8250) define VUART_USE_TIMER_FALLBACK - this will cause the
 *    Linux driver to poll every so often for new data. This is fine if the port is opened-written to-closed but not 
 *    when apps keep it long open (as the APIC timer will constantly fire for nothing)
 *  - To see detailed logs of registries being accessed and modified and what not define VUART_DEBUG_LOG - you will get
 *    all the info you need for debugging. However keep in mind setting this along with VUART_USE_TIMER_FALLBACK will be
 *    pretty catastrophic as you will be flooded with messages about IIR being read as long as the port stays open in 
 *    the userland. This consciously does not use kernel's dynamic debug facilities are some (e.g. 918+) kernels are
 *    compiled without it.
 *  - To change name of the vIRQ thread define VUART_THREAD_FMT which gets a real port IRQ # and ttyS# as its params.
 *  - UART_BUG_SWAPPED (defined in uart_defs.h) is used to detect swapped ports and make sure numbers used here are real
 *    ttyS* values and not swapped bs (as 8250 matches ports by iobase and not line#)
 *
 * References:
 *  - https://github.com/clearlinux/kvmtool/blob/b5891a4337eb6744c8ac22cc02df3257961ae23e/hw/serial.c (inspiration)
 *  - https://www.ti.com/lit/ug/sprugp1/sprugp1.pdf (everything you need to know abt UART, referred in code as "Ti doc")
 *  - http://caro.su/msx/ocm_de1/16550.pdf (useful and short UART know-how with a good registry table in Table 2, p. 9)
 *  - https://www.linuxjournal.com/article/8144 (handling threading in kernel)
 */

//Here are some flags which can be used to modify the behavior of VirtualUART. They're checked by other header files.
//Keep in mind you may need to set the debug in vuart_virtual_irq separatedly (or in common.h)
//#define VUART_DEBUG_LOG
//#define VUART_USE_TIMER_FALLBACK

#include "virtual_uart.h"
#include "vuart_internal.h"
#include "../../common.h" //can set VUART_DEBUG_LOG and others
#include "../../debug/debug_vuart.h" //it will provide normal or nooped versions of macros; CHECKS VUART_DEBUG_LOG
#include "../../config/uart_defs.h" //COM defs & struct uart_port
#include "../../internal/intercept_driver_register.h" //is_driver_registered, watch_driver_register, unwatch_driver_register
#include "vuart_virtual_irq.h" //vIRQ handling & shimming; CHECKS VUART_USE_TIMER_FALLBACK
#include <linux/serial_8250.h> //serial8250_unregister_port, uart_8250_port
#include <linux/serial_reg.h> //UART_* consts
#include <linux/spinlock.h> //locking devices (vdev->lock)
#include <linux/kfifo.h> //kfifo_*

/************************************************* Static definitions *************************************************/
/*
 * According to https://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming bits 6 and 7 must be set to
 * consider FIFO as enabled-and-working (bit 7 only designates "FIFO enabled, but not functioning" (?)
 */
#define UART_IIR_FIFOEN 0xc0
#define UART_IIR_FIFEN_B6 0x40
#define UART_IIR_FIFEN_B7 0x80
#define UART_DRIVER_NAME "serial8250" //see drivers/tty/serial/8250/8250_core.c in "serial8250_isa_driver"

/**
 * Static definition of all possible UARTs in the system supported by 8250 driver
 * These definitions are exactly the same as in arch/x86/include/asm/serial.h
 */
static struct serial8250_16550A_vdev ttySs[] = {
//we're crying too... the issue is normally operate on port lines (=ttyS#) but during port registration ports the driver
// performs matching based on its internal iobase mapping, so we can ask for the port to be line=0 but if the driver
// finds a port with iobase specified under line=1 it will just register is as line=1 instead of line=0. This causes all
// sorts of problems as during reads we expect the vdev line to match what we actually registered. To fix it and make it
// independent of all fucking swapping and reswapping we will have to emit events from uart_swapper and other nonsense
// ...this is ridiculous. So we take a sane assumptions:
//  - if the kernel is broken we accommodate for that assuming no un-swapping will be done afterwards
//  - if the kernel is broken and swap fix is disabled by debug flag we handle the swapping
//  - if something is borked we don't offer a detection comparing lines because before we get a response from the driver
//    registering the port it will call our read function and break everything
// TODO: this whole code should switch to relying on iobases instead o lines. This way when we do reads or writes we
//       don't care if something is swapped - we call for registration on line 0, we lookup what's the expected iobase
//       for that ttyS and we register for it. If the driver decides to use a different line# we shouldn't care.
#if defined(UART_BUG_SWAPPED) && defined(DBG_DISABLE_UART_SWAP_FIX)
    [0]	= { .line = 0, .iobase = STD_COM2_IOBASE, .irq = STD_COM2_IRQ, .baud = STD_COMX_BAUD }, //COM1 aka ttyS1
    [1]	= { .line = 1, .iobase = STD_COM1_IOBASE, .irq = STD_COM1_IRQ, .baud = STD_COMX_BAUD }, //COM2 aka ttyS0
#else
    [0]	= { .line = 0, .iobase = STD_COM1_IOBASE, .irq = STD_COM1_IRQ, .baud = STD_COMX_BAUD }, //COM1 aka ttyS0
    [1]	= { .line = 1, .iobase = STD_COM2_IOBASE, .irq = STD_COM2_IRQ, .baud = STD_COMX_BAUD }, //COM2 aka ttyS1
#endif
    [2]	= { .line = 2, .iobase = STD_COM3_IOBASE, .irq = STD_COM3_IRQ, .baud = STD_COMX_BAUD }, //COM3 aka ttyS2
    [3]	= { .line = 3, .iobase = STD_COM4_IOBASE, .irq = STD_COM4_IRQ, .baud = STD_COMX_BAUD }, //COM4 aka ttyS3
};

//Internal type for callbacks; see vuart_set_tx_callback() for details
struct flush_callback {
    vuart_callback_t *fn;
    void *buffer;
    int threshold;
};
//Storage for all TX callbacks, see vuart_set_tx_callback()
static struct flush_callback *flush_cbs[SERIAL8250_LAST_ISA_LINE] = { NULL };
static volatile bool kernel_driver_ready = false; //Whether the 8250 UART driver is ready

/**************************************** Internal helper function-like macros ****************************************/
//Get vDEV from line/ttyS number (created for consistency)
#define get_line_vdev(line) (&ttySs[(line)])

//8250 driver doesn't give access to the real uart_port upon adding but does it on first read/write
#define capture_uart_port(vdev, port) if (unlikely(!(vdev)->up)) (vdev)->up = port;

//Some functions should warn use out of courtesy that we're running in a stupid environment
#if defined(UART_BUG_SWAPPED) && defined(DBG_DISABLE_UART_SWAP_FIX)
#define warn_bug_swapped(line) \
    if ((line) < 2) { \
        pr_loc_inf( \
                "Requested ttyS%d vUART - this kernel has UART SWAP => modifying what physically is ttyS%d (io=0x%x)", \
                line, !line, get_line_vdev(line)->iobase); \
    }
#else
#define warn_bug_swapped(line) //noop
#endif

#define for_each_vdev() for (int line=0; line < ARRAY_SIZE(ttySs); ++line)

//Before v3.13 the kfifo_put() accepted a pointer, since then it accepts a value
//ffs... https://github.com/torvalds/linux/commit/498d319bb512992ef0784c278fa03679f2f5649d
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,13,0)
#define kfifo_put_val(fifo, val) kfifo_put(fifo, &val)
#else
#define kfifo_put_val(fifo, val) kfifo_put(fifo, val)
#endif

/****************************************** Internal chip emulation functions ******************************************/
/**
 * Updates state of the IIR register
 *
 * In the physical world when the UART chip is connected to the CPU there's an interrupt line which goes high when
 * the chip detects any of the conditions which are interrupt-worthy. Whether something is interrupt-worthy is
 * determined by the driver (i.e. Linux kernel) and set in the IER. When an interrupt is generated the kernel gets only
 * the information "something happened on IRQ 4" (which means SOMETHING happened on COM1 *or* COM 3). To determine
 * what it is (or maybe there multiple things even!) the kernel reads IIR which gives the REASON why a given interrupt
 * happened (and thus also indirectly specifies which channel/chip generated the interrupt).
 *
 * This code below is written based on the Table 3-6 in Ti doc - it summarizes IIR state and how it should change. It
 * should be called AFTER everything else modified registers. In general if you changed some other registries you should
 * call this function. You usually want to do it once upon returning control to an outside caller (after making all
 * all changes).
 *
 * Regardless of whether vIRQ is enabled or not this register MUST be updated.
 */
static void update_interrupts_state(struct serial8250_16550A_vdev *vdev)
{
    uart_prdbg("Recomputing IIR state");
    //Order of these if/elseifs is CRUCIAL - interrupts have priorities and they're masked
    u8 new_iir_int_state = 0;
    if ((vdev->ier & UART_IER_RLSI) &&
        unlikely((vdev->lsr & UART_LSR_OE) || (vdev->lsr & UART_LSR_PE) || (vdev->lsr & UART_LSR_FE) ||
                (vdev->lsr & UART_LSR_BI))) {
        //Kernel enabled OE/PE/FE/BI interrupts and there's one of them
        uart_prdbg("IIR: setting RLS (errors) interrupt");
        new_iir_int_state |= UART_IIR_RLSI;
    } else if ((vdev->ier & UART_IER_RDI) && (vdev->lsr & UART_LSR_DR)) {
        //We don't distinguish between FIFO and non-FIFO mode and just set interrupt if there's some data to be read
        //We also don't support the receiver time-out (kernel should pick up the data in time as it's a virtual port)
        uart_prdbg("IIR: setting RD (data-ready) interrupt");
        new_iir_int_state |= UART_IIR_RDI;
    } else if ((vdev->ier & UART_IER_THRI) && ((vdev->lsr & UART_LSR_TEMT) || kfifo_is_empty(vdev->tx_fifo))) {
        //When THR is empty or FIFO is empty (for us it's the same thing) kernel wants to know about that
        uart_prdbg("IIR: setting THR (transmitter empty) interrupt");
        new_iir_int_state |= UART_IIR_THRI;
    }

    //If any interrupts are triggered (or not) we need to set IPEND accordingly
    if (new_iir_int_state) {
        new_iir_int_state &= ~UART_IIR_NO_INT; //since there were some interrupts we clear IPEND (=interrupts pending)
        vuart_virq_wake_up(vdev);
    } else {
        new_iir_int_state |= UART_IIR_NO_INT; //since there were no interrupts we set IPEND (=no interrupts pending)
    }

    //IIR (despite its name) also contains FIFO status along interrupts
    vdev->iir = new_iir_int_state;
    if (likely(vdev->fcr & UART_FCR_ENABLE_FIFO))
        vdev->iir |= UART_IIR_FIFOEN;

    dump_iir(vdev);
    uart_prdbg("Finished IIR state");
}

/**
 * Put registries into the "chip reset" state as described by the datasheet (see Tables 3-* in Ti doc)
 * You should NOT modify these values under any circumstances as they're meant to represent the real chip RESET state
 */
static void reset_device(struct serial8250_16550A_vdev *vdev)
{
    uart_prdbg("Resetting virtual chip @ ttyS%d", vdev->line);
    lock_vuart_oppr(vdev);

    //Upon reset both FIFOs must be erased
    if (vdev->tx_fifo)
        kfifo_reset(vdev->tx_fifo);
    if (vdev->rx_fifo)
        kfifo_reset(vdev->rx_fifo);

    //Registries for when DLAB=0
    vdev->rhr = 0x00; //no data in receiving channel
    vdev->thr = 0x00; //no data in transmission channel
    vdev->ier = 0x00; //no interrupts enabled
    vdev->iir = UART_IIR_NO_INT; //no pending interrupts, FIFO not active
    vdev->fcr = 0x00; //FIFO disabled (which invalidates other FIFO properties in FCR), DMA disabled
    vdev->lcr = 0x00; //non-DLAB mode, errors cleared, 1 STOP bit, 5 bit words (not that it matters for virtual port)
    vdev->mcr = UART_MCR_OUT2; //autoflow disabled, loop mode disabled, OUT2 enabled as global interrupt
    vdev->lsr = UART_LSR_TEMT | UART_LSR_THRE; //transmitter empty & idle, all errors cleared, break not requested
    vdev->msr = 0x00; //all flow control flags not triggered
    vdev->scr = 0x00; //empty scratchpad

    //Additional registries when DLAB=1
    vdev->dll = 0x00; //undefined divisor LSB latch
    vdev->dlm = 0x00; //undefined divisor MSB latch

    unlock_vuart_oppr(vdev);
    uart_prdbg("Virtual chip @ ttyS%d reset done", vdev->line);
}

/**
 * Allocate/create FIFOs on the device if they don't exist (and if they do you shouldn't call this function)
 *
 * @todo it should free on errors
 */
static int alloc_fifos(struct serial8250_16550A_vdev *vdev)
{
    if (unlikely(vdev->rx_fifo)) { //this shouldn't happen on non-initialized port
        pr_loc_bug("RX FIFO @ %d already alloc'd", vdev->line);
        return -EINVAL;
    }

    if (unlikely(vdev->tx_fifo)) { //this shouldn't happen on non-initialized port
        pr_loc_bug("TX FIFO @ %d already alloc'd", vdev->line);
        return -EINVAL;
    }

    kzalloc_or_exit_int(vdev->rx_fifo, sizeof(struct kfifo));
    kzalloc_or_exit_int(vdev->tx_fifo, sizeof(struct kfifo));

    if (unlikely(kfifo_alloc(vdev->rx_fifo, VUART_FIFO_LEN, GFP_KERNEL) != 0)) {
        pr_loc_crt("kfifo_alloc for RX FIFO elements @ %d failed", vdev->line);
        return -EFAULT;
    }

    if (unlikely(kfifo_alloc(vdev->tx_fifo, VUART_FIFO_LEN, GFP_KERNEL) != 0)) {
        pr_loc_crt("kfifo_alloc for TX FIFO elements @ %d failed", vdev->line);
        return -EFAULT;
    }

    return 0;
}

/**
 * Reverses what alloc_fifos() did
 */
static int free_fifos(struct serial8250_16550A_vdev *vdev)
{
    //This should be called when the vIRQ thread is killed so nothing call the IRQ handler without FIFOs
    if (unlikely(!vdev->rx_fifo || !vdev->tx_fifo)) { //this shouldn't happen on initialized port
        pr_loc_bug("RX and/or TX FIFO @ %d are not alloc'd (nothing to free)", vdev->line);
        return -EINVAL;
    }

    kfifo_free(vdev->rx_fifo);
    kfifo_free(vdev->tx_fifo);

    return 0;
}

/**
 * Deposits the TX queue contents into callbacks set using vuart_set_tx_callback() and clears the FIFO itself
 * If no callbacks were defined it will simply clear.
 *
 * This function does NOT recalculate IIRs (see update_interrupts_state()) and assumes you have vdev lock.
 */
static void flush_tx_fifo(struct serial8250_16550A_vdev *vdev, vuart_flush_reason reason)
{
    uart_prdbg("Flushing TX FIFO now! reason=%d", reason);

    if (likely(flush_cbs[vdev->line])) {
        unsigned int flushed_bytes = 0;
        flushed_bytes = kfifo_out(vdev->tx_fifo, flush_cbs[vdev->line]->buffer, VUART_FIFO_LEN);
        flush_cbs[vdev->line]->fn(vdev->line, flush_cbs[vdev->line]->buffer, flushed_bytes, reason);
    } else {
        uart_prdbg("No callback for TX FIFO @ %d - discarding", vdev->line);
        kfifo_reset(vdev->tx_fifo);
    }

    vdev->lsr |= UART_LSR_TEMT | UART_LSR_THRE; //nothing should be in the buffer
}

/**
 * Pulls a character/byte from RX FIFO and places it into RHR for the driver to read it
 *  - It updates all registers according to the specs
 *  - It assumes you have vdev lock
 *  - It does NOT recalculate IIRs (see update_interrupts_state())
 *  - It will produce an error if you try to do the transfer while FIFO is empty but it will not crash. You should check
 *    UART_LSR_DR before calling this function.
 *
 * @return character which was read
 */
static unsigned char transfer_char_fifo_rhr(struct serial8250_16550A_vdev *vdev)
{
    //Before this function is called UART_LSR_DR should be verified - it wasn't or it was wrong if this exploded
    if(unlikely(kfifo_get(vdev->rx_fifo, &vdev->rhr) == 0))
        pr_loc_bug("Attempted to %s with empty FIFO - that shouldn't happen if the DR flag was checked", __FUNCTION__);

    if (kfifo_is_empty(vdev->rx_fifo))
        vdev->lsr &= ~UART_LSR_DR;

    //See descriptions of these fields in Table 3-12 from TI doc - these flags are cleared on character read
    vdev->lsr &= ~UART_LSR_BI;
    vdev->lsr &= ~UART_LSR_FE;
    vdev->lsr &= ~UART_LSR_PE;
    vdev->lsr &= ~UART_LSR_OE; //by definition, we cannot have overrun if a character was just read

    return vdev->rhr;
}

/**
 * An alternative to transfer_char_fifo_rhr() when FIFOs aren't used for transfers (e.g. in MSR TEST/LOOP mode)
 *
 * This function does NOT recalculate IIRs (see update_interrupts_state()) and assumes you have vdev lock.
 */
static void handle_receive_char(struct serial8250_16550A_vdev *vdev, unsigned char value)
{
    //@todo this only handles overruns in FIFO mode and does not do that in non-FIFO; it behaves correctly but it
    // doesn't report OEs in non-FIFO
    vdev->rhr = value; //RHR is always populated with the value no matter the FIFO or non-FIFO mode

    //Put value in FIFO, it will indicate with return of 0 if it was full before attempted put (overrun/overflow)
    if (kfifo_put_val(vdev->rx_fifo, value) == 0) {
        vdev->lsr |= UART_LSR_OE; //set overrun flag as FIFO detected that

        //During TEST/LOOP mode many overflows are caused on purpose - we don't want to hear about them really
        if (unlikely(!(vdev->mcr & UART_MCR_LOOP)))
            pr_loc_wrn("RX FIFO overflow detected @ ttyS%d", vdev->line);
    } else {
        vdev->lsr &= ~UART_LSR_OE; //no overrun condition - clear OE flag just in case
    }

    vdev->lsr |= UART_LSR_DR; //receiver has something for the kernel to pickup
}

/**
 * Called when kernel sent something to the device and it has to be put into TX FIFO & THR
 *
 * This function does NOT recalculate IIRs (see update_interrupts_state()) and assumes you have vdev lock.
 *
 * CAUTION: order of these "ifs" for flushes here is crucial: we make a guarantee to the reason parameter that if both
 *  VUART_FLUSH_THRESHOLD and VUART_FLUSH_FULL are true (i.e. callback was set with threshold == VUART_FIFO_LEN) we
 *  will prioritize threshold trigger (as a user-specified event takes precedence over internal event of FIFO full)
 * If the threshold specified by the callback setter was met flush the FIFO
 */
static void handle_transmit_char(struct serial8250_16550A_vdev *vdev, unsigned char value)
{
    //@todo this only handle non-FIFO properly: doesn't detect OE, and doesn't reset THRE
    vdev->thr = value; //THR is always populated with the value no matter the FIFO or non-FIFO mode
    vdev->lsr &= ~UART_LSR_THRE;

    int fifo_len = kfifo_len(vdev->tx_fifo);
    uart_prdbg("%s got new char ascii=%c hex=%02x on ttyS%d (FIFO#=%d)", __FUNCTION__, value, value, vdev->line,
               fifo_len);

    //FIFO is full - try to flush it; if we got here it means the threshold is for sure >VUART_FIFO_LEN as this is
    // checked after we put data into the FIFO (to make sure we trigger THRESHOLD event and not FULL)
    //The reason why we check this at the beginning of new char and not after adding to FIFO is that if the transmitting
    // party sends exactly VUART_FIFO_LEN bytes and then ends the transmission we don't want to flush with FULL but with
    // IDLE to give a better sense of what's going on to the caller. FULL implies "we got too much data, there may be
    // more coming" while IDLE implies that the unit of transmission ended.
    if (unlikely(fifo_len == VUART_FIFO_LEN))
        flush_tx_fifo(vdev, VUART_FLUSH_FULL);

    //Put value in FIFO, it will indicate with return of 0 if it was full before attempted put (overrun/overflow)
    //This, if we are correct, cannot happen if the flush_tx_fifo() is functioning correctly as we try to flush above
    int fifo_add = kfifo_put_val(vdev->tx_fifo, value);
    fifo_len += fifo_add; //we can call kfifo_ API for this but why if we have both pieces of info anyway? ;)
    if (unlikely(fifo_add == 0)) {
        vdev->lsr |= UART_LSR_OE; //set overrun flag as FIFO detected that
        pr_loc_wrn("TX FIFO overflow detected");
    } else {
        vdev->lsr &= ~UART_LSR_OE; //no overrun condition - clear OE flag just in case
    }

    vdev->lsr &= ~UART_LSR_TEMT; //transmitter buffers are no longer empty

    //@todo THRE should be reset immediately in non-FIFO mode (i.e. at the same time as TEMT)
    //This is to prevent kernel from freaking out about "blackhole" UART (see https://unix.stackexchange.com/a/387650)
    if (fifo_len >= VUART_FIFO_LEN / 2)
        vdev->lsr &= ~UART_LSR_THRE;

    if (likely(flush_cbs[vdev->line]) && fifo_len >= flush_cbs[vdev->line]->threshold)
        flush_tx_fifo(vdev, VUART_FLUSH_THRESHOLD);
}

/**
 * The main READ routing passed to the 8250 driver. It should be as fast as possible and MUST be multithread-safe
 *
 * Device ==responding-to==> kernel; aka "do you have something for me?"
 * This function is used to read data and registers.
 *
 * @param offset This is really the register value. It's named "offset" in accordance with Linux nomenclature which
 *               makes sense for physical chips (as this is a memory offset from chip's memory base)
 */
static unsigned int serial_remote_read(struct uart_port *port, int offset)
{
    uart_prdbg("Serial READ for line=%d/%d", port->line, ttySs[port->line].line);

    struct serial8250_16550A_vdev *vdev = get_line_vdev(port->line);
    lock_vuart(vdev);
    capture_uart_port(vdev, port);
    unsigned int out;
	switch (offset) {
        case UART_RX:
            //if DLAB is enabled DLL registry is desired; otherwise we should send THR
            //See Table 2 in the chip manual. DLAB controls access to address 000, 001, and 101. When DLAB=1 these
            //addrs respond with DLL, DLM, and PSD respectively, when DLAB=0 they respond with RHR/THR, IER/DLM, and LSR
            if (vdev->lcr & UART_LCR_DLAB) {
                out = vdev->dll;
                reg_read("DLL");
            } else if (vdev->lsr & UART_LSR_BI) { //chip wants a break?
                out = 0;
                vdev->lsr &= ~UART_LSR_BI; //clear the break for the next cycle; see BI in Table 3-12 from TI doc
                uart_prdbg("LSR indicated break request, cleared");
                dump_lsr(vdev);
            }  else if(vdev->lsr & UART_LSR_DR) { //Did we receive anything?
                out = transfer_char_fifo_rhr(vdev);
                dump_lsr(vdev);
                uart_prdbg("Providing RHR registry (val=%x DLAB=0 LSR_DR=1)", out);
            } else {
                out = 0;
                //Such read isn't invalid. However, it is done e.g. in the init sequence as a workaround for some
                // physical chips bugs in the past or to clear the RHR before other operations (even if LSR DR=0)
                uart_prdbg("Nothing in RHR (DLAB=0; LSR_DR=0) - noop");
                dump_lsr(vdev);
            }
            break;
        case UART_IER:
            if (vdev->lcr & UART_LCR_DLAB) {
                out = vdev->dlm;
                reg_read("DLM");
            } else {
                out = vdev->ier;
                reg_read_dump(vdev, ier, "IER");
            }
            break;
        case UART_IIR:
            out = vdev->iir;
            reg_read_dump(vdev, iir, "IIR/ISR");
            break;
	    //case UART_FCR not present - write only register
        case UART_LCR:
            out = vdev->lcr;
            reg_read_dump(vdev, lcr, "LCR");
            break;
        case UART_MCR:
            out = vdev->mcr;
            reg_read_dump(vdev, mcr, "MCR");
            break;
        case UART_LSR:
            out = vdev->lsr;
            reg_read_dump(vdev, lsr, "LSR");
            vdev->lsr &= ~UART_LSR_OE; //See "OE" Table 3-12 or Table 3-6 - it needs to be cleared on LSR read
            break;
        case UART_MSR:
            out = vdev->msr;
            reg_read_dump(vdev, msr, "MSR");

            //See table 3-13 in Ti doc; MSR is masked with values from MCR when MCR indicates test/loop mode
            if (unlikely(vdev->mcr & UART_MCR_LOOP)) {
                if (vdev->mcr & UART_MCR_RTS) out |= UART_MSR_CTS; else out &= ~UART_MSR_CTS;
                if (vdev->mcr & UART_MCR_DTR) out |= UART_MSR_DSR; else out &= ~UART_MSR_DSR;
                if (vdev->mcr & UART_MCR_OUT1) out |= UART_MSR_RI; else out &= ~UART_MSR_RI;
                if (vdev->mcr & UART_MCR_OUT2) out |= UART_MSR_DCD; else out &= ~UART_MSR_DCD;
                uart_prdbg("[!] Masked real MSR values to: CTS=%d | DSR=%d | RI=%d | DCD=%d",
                           out&UART_MSR_CTS?1:0, out&UART_MSR_DSR?1:0, out&UART_MSR_RI?1:0, out&UART_MSR_DCD?1:0);
            }
            break;
        case UART_SCR:
            out = vdev->scr;
            reg_read("SCR/SPR");
            break;
        default:
            pr_loc_bug("Unknown registry %x read attempt on ttyS%d", offset, vdev->line);
            out = 0;
            break;
	}

	update_interrupts_state(vdev);
	unlock_vuart(vdev);

    return out;
}

/**
 * The main WRITE routing passed to the 8250 driver. It should be as fast as possible and MUST be multithread-safe
 *
 * Kernel => device, aka "I have something FOR YOU, send it along"
 * This function is also used to write registers.
 *
 * @param offset This is really the register value. It's named "offset" in accordance with Linux nomenclature which
 *               makes sense for physical chips (as this is a memory offset from chip's memory base)
 */
static void serial_remote_write(struct uart_port *port, int offset, int value)
{
    //uart_prdbg("Serial WRITE for line=%d/%d", port->line, ttySs[port->line].line);

    struct serial8250_16550A_vdev *vdev = get_line_vdev(port->line);
    lock_vuart(vdev);
    capture_uart_port(vdev, port);

    switch (offset) {
        case UART_TX:
            //See "case UART_RX" for explanation
            if (vdev->lcr & UART_LCR_DLAB) { //DLAB overrides everything
                vdev->dll = value;
                reg_write("DLL");
            } else if (vdev->mcr & UART_MCR_LOOP) { //are we in the reflection/loop mode? (=> fake TX->RX connection)
                uart_prdbg("Loopback enabled, writing %x meant for THR to RHR directly", value);
                handle_receive_char(vdev, (unsigned char)value); //loopback emulates receiving char on RX
                dump_mcr(vdev);
                dump_lsr(vdev);
            } else { //just pickup the data from kernel
                handle_transmit_char(vdev, (unsigned char)value);
                reg_write("THR");
                dump_lsr(vdev);
            }
            break;
        case UART_IER:
            if (vdev->lcr & UART_LCR_DLAB) {
                vdev->dlm = value;
                reg_write("DLM");
                break;
            }

            /*
             * This is a little shortcut to deliver data to the callback even if the threshold wasn't met. This is
             * done since kernel DISABLES THR empty interrupts when it finishes writing (which makes sense - otherwise
             * it will be flooded with interrupts all the time as nothing gets written to THR). This means that
             * kernel wrote everything what was there to write and [presumably] nothing else is coming anytime soon
             * So in short: if THReINT was enabled and it JUST got disabled flush the FIFO if it isn't empty
             */
            if ((vdev->ier & UART_IER_THRI) && !(value & UART_IER_THRI) && !kfifo_is_empty(vdev->tx_fifo)) {
                uart_prdbg("Kernel driver disabled THRe interrupt and fifo isn't empty - triggering IDLE flush");
                flush_tx_fifo(vdev, VUART_FLUSH_IDLE);
            }
            vdev->ier = value & 0x0f; //we're not letting kernel set DMA registers since we don't support DMA
            reg_write_dump(vdev, ier, "IER");
            break;
        //case UART_IIR not present - read only register
        case UART_FCR:
            //FIFO registers are guarded by the FIFOEN - if it's not set only FIFOEN can be modified, see p27 of Ti doc
            if (!(vdev->fcr & UART_FCR_ENABLE_FIFO) && !(value & UART_FCR_ENABLE_FIFO))
                value &= UART_FCR_ENABLE_FIFO;

            vdev->fcr = value;
            reg_write_dump(vdev, fcr, "FCR");

            //If the new FCR value called for flush of TX and/or RX do that right away
            if (vdev->fcr & UART_FCR_CLEAR_XMIT) {
                kfifo_reset(vdev->tx_fifo);
                vdev->lsr |= UART_LSR_TEMT | UART_LSR_THRE;
                uart_prdbg("TX FIFO flushed on FCR request");
                dump_lsr(vdev);
            }

            if (vdev->fcr & UART_FCR_CLEAR_RCVR) {
                kfifo_reset(vdev->rx_fifo);
                vdev->lsr &= ~UART_LSR_DR;
                uart_prdbg("RX FIFO flushed on FCR request");
                dump_lsr(vdev);
            }
            break;
        case UART_LCR:
            vdev->lcr = value;
            reg_write_dump(vdev, lcr, "LCR");
            break;
        case UART_MCR:
            vdev->mcr = value;
            reg_write_dump(vdev, mcr, "MCR");
            break;
        case UART_LSR:
            vdev->lsr = value;
            pr_loc_bug("Bogus LSR write attempt on ttyS%d - why?", vdev->line);
            dump_lsr(vdev);
            break;
        case UART_MSR:
            vdev->msr = value;
            pr_loc_bug("Bogus MSR write attempt on ttyS%d - why?", vdev->line);
            dump_msr(vdev);
            break;
        case UART_SCR:
            vdev->scr = value;
            reg_write("SCR");
            break;
        default:
            pr_loc_bug("Unknown registry %x write attempt on ttyS%d with %x", offset, vdev->line, value);
            break;
	}

    update_interrupts_state(vdev);
    unlock_vuart(vdev);
}


/************************************************** vUART Glue Layer **************************************************/
static driver_watcher_instance *driver_watcher = NULL;
static int update_serial8250_isa_port(struct serial8250_16550A_vdev *vdev);
static int restore_serial8250_isa_port(struct serial8250_16550A_vdev *vdev);

/**
 * Initializes/allocates what's needed in a fresh vdev structure (or one which was previously freed)
 */
static int initialize_ttyS(struct serial8250_16550A_vdev *vdev)
{
    int out;

    pr_loc_dbg("Initializing ttyS%d vUART", vdev->line);
    if (unlikely(vdev->initialized)) {
        pr_loc_bug("ttyS%d is already initialized", vdev->line);
        return -EBUSY;
    }

    reset_device(vdev); //Puts device in a known RESET state as defined by the real chip docs
    if ((out = alloc_fifos(vdev) != 0))
        return out;

    kmalloc_or_exit_int(vdev->lock, sizeof(spinlock_t));
    spin_lock_init(vdev->lock);

    //virq_* stuff is allocated/freed by enable_/disable_interrupts()

    vdev->initialized = true;
    pr_loc_dbg("Initialized ttyS%d vUART", vdev->line);

    return 0;
}

/**
 * Deinitializes/frees what was previously built by initialize_ttyS()
 */
static int deinitialize_ttyS(struct serial8250_16550A_vdev *vdev)
{
    int out;

    pr_loc_dbg("Deinitializing ttyS%d vUART", vdev->line);
    if (unlikely(!vdev->initialized)) {
        pr_loc_bug("ttyS%d is not initialized", vdev->line);
        return -ENODEV;
    }

    if ((out = free_fifos(vdev) != 0))
        return out;

    kfree(vdev->lock);
    vdev->initialized = false;
    pr_loc_dbg("Deinitialized ttyS%d vUART", vdev->line);

    return 0;
}

/**
 * Watches for the serial8250 driver to load in order to register ports which were added before the driver loaded
 */
static driver_watch_notify_result serial8250_ready_watcher(struct device_driver *drv, driver_watch_notify_state event)
{
    if (unlikely(event != DWATCH_STATE_LIVE))
        return DWATCH_NOTIFY_CONTINUE;

    pr_loc_dbg("%s driver loaded - adding queued ports", UART_DRIVER_NAME);
    kernel_driver_ready = true;

    int out;
    for_each_vdev() {
        //non-initialized ports are these which were never added as vUARTs
        if (!ttySs[line].initialized || ttySs[line].registered)
            continue;

        pr_loc_dbg("Processing enqueued port %d", line);
        if ((out = update_serial8250_isa_port(&ttySs[line])) != 0) {
            //This is critical as ports were promised to be registered to other parts of the application but we cannot
            // fulfill that promise now
            pr_loc_crt("Failed to process port %d - error=%d", line, out);
        }
    }

    pr_loc_dbg("Finished processing enqueued ports");
    return DWATCH_NOTIFY_DONE;
}

/**
 * Checks the current serial8250 status
 *
 * @return 0 if not loaded, 1 if loaded, -E on error
 */
static int probe_driver(void)
{
    if (kernel_driver_ready)
        return 1; //we've already checked the state and confirmed as ready before

    int driver_ready_tristate = is_driver_registered(UART_DRIVER_NAME, NULL);
    if (driver_ready_tristate < 0) {
        pr_loc_err("Failed to check %s driver state - error=%d", UART_DRIVER_NAME, driver_ready_tristate);
        return -EIO;
    }

    if (driver_ready_tristate == 1)
        kernel_driver_ready = true;

    return driver_ready_tristate;
}

/**
 * Attempt to watch for the serial8250 driver readiness (if needed)
 *
 * @return 0 if driver is not loaded and a watcher has been set up,
 *         1 if driver is already loaded (and nothing needs to be done),
 *         -E on error
 */
static int try_wait_for_serial8250_driver(void)
{
    int driver_ready_tristate = probe_driver();
    if (driver_ready_tristate != 0)
        return driver_ready_tristate; //if the driver is ready (=1) or an error occurred (-E) we don't do anything here

    pr_loc_inf("%s driver is not ready - the port addition will be delayed until the driver loads", UART_DRIVER_NAME);
    driver_watcher = watch_driver_register(UART_DRIVER_NAME, serial8250_ready_watcher, DWATCH_STATE_LIVE);

    if (IS_ERR(driver_watcher)) {
        pr_loc_err("Failed to register driver watcher - no ports can be registered till the driver loads");
        return PTR_ERR(driver_watcher);
    }

    return 0;
}

/**
 * Disable the driver watcher if it was set up
 *
 * @return 0 on success, -E on error
 */
static int try_leave_serial8250_driver(void)
{
    if (!driver_watcher) //we're only concerned about watching the driver
        return 0;

    for_each_vdev() {
        if (ttySs[line].initialized && !ttySs[line].registered) {
            pr_loc_dbg("Cannot leave %s driver yet - port %d is still awaiting registration", UART_DRIVER_NAME, line);
            return 0;
        }
    }

    int out = unwatch_driver_register(driver_watcher);
    driver_watcher = NULL;
    if (out != 0)
        pr_loc_err("Failed to unwatch driver (error=%d)", out);

    return out;
}

/**
 * Asks the Linux 8250 driver to UPDATE properties of a given serial device which matches line & iobase
 *
 * The reason why this function is called update_ rather than add_ is that we're NOT adding anything new to the driver.
 * Rather we're registering a port which is already there (as vUART only deals with COM1-4, i.e. legacy IBM/PC ports)
 * and matches our spec.
 */
static int update_serial8250_isa_port(struct serial8250_16550A_vdev *vdev)
{
    int out;
    pr_loc_dbg("Registering ttyS%d (io=0x%x) in the driver", vdev->line, vdev->iobase);

    if (unlikely(vdev->registered)) {
        pr_loc_bug("Port ttyS%d (io=0x%x) is already registered in the driver", vdev->line, vdev->iobase);
        return -EEXIST;
    }

    int driver_ready_tristate = try_wait_for_serial8250_driver();
    if (driver_ready_tristate == 0) {
        pr_loc_wrn("The %s driver is not ready - vUART port ttyS%d (io=0x%x) will be activated later", UART_DRIVER_NAME,
                   vdev->line, vdev->iobase);
        return 0;
    }

    if (driver_ready_tristate < 0) {
        pr_loc_err("%s failed due to underlining driver error", __FUNCTION__);
        return driver_ready_tristate;
    }


    struct uart_8250_port *up;
    kzalloc_or_exit_int(up, sizeof(struct uart_8250_port));
    struct uart_port *port = &up->port;

    port->line = vdev->line;
    port->iobase = vdev->iobase;
    port->uartclk = vdev->baud * 16;
    port->flags = STD_COMX_FLAGS;

    //This is a silly workaround to let the kernel know "we don't REALLY support IRQ"
    //While the code do support IRQs handling we weren't able to find a smart way to "simulate" IRQ 3-4 (which are
    // normally HW interrupts). However, the 8250 driver will emulate them for us using APIC
    port->irq = (vuart_virq_supported()) ? vdev->irq : SERIAL8250_SOFT_IRQ;
    port->irqflags = 0;
    port->hub6 = 0;
    port->membase = 0;
    port->iotype = 0;
    port->regshift = 0;
    port->serial_in = serial_remote_read;
    port->serial_out = serial_remote_write;
    port->type = PORT_16550A;
    up->cur_iotype = 0xFF;

    //DO NOT EVEN THINK about assigning "port" top vdev->port!!! serial8250_register_8250_port() uses our passed port to
    // match internally reserved (during boot) port structure. Our structure misses a lot of stuff like handlers and so
    //YOU CANNOT ASSIGN IT HERE!

    //This is the most explosion-prone section so logs are useful
    uart_prdbg("Calling serial8250_register_8250_port to register port");
    if ((out = serial8250_register_8250_port(up)) < 0) { //it returns port # on success or -E on error
        pr_loc_err("Failed to register ttyS%d - driver failure (error=%d)", vdev->line, out);
        goto out_free;
    }
    pr_loc_dbg("ttyS%d registered with driver (line=%d)", vdev->line, out);
    out = 0; //serial8250_register_8250_port return serial port line # or -E code
    vdev->registered = true;

    out_free:
    kfree(up);
    return out;
}

/**
 * Restores original UART in 8250 driver
 */
static int restore_serial8250_isa_port(struct serial8250_16550A_vdev *vdev)
{
    int out;
    pr_loc_dbg("Unregistering ttyS%d (io=0x%x) from the driver", vdev->line, vdev->iobase);

    if (unlikely(!vdev->registered)) {
        pr_loc_dbg("Port ttyS%d (io=0x%x) is not registered in the driver - nothing to restore", vdev->line,
                   vdev->iobase);
        return 0;
    }

    if (unlikely(!kernel_driver_ready)) {
        pr_loc_wrn("Port ttyS%d (io=0x%x) cannot be restored - kernel driver not ready", vdev->line, vdev->iobase);
        return 0; //not an error as technically the port is NOT in the driver
    }

    struct uart_8250_port *up;
    kzalloc_or_exit_int(up, sizeof(struct uart_8250_port));
    struct uart_port *port = &up->port;

    port->line = vdev->line;
    up->cur_iotype = 0xFF;
    port->iobase = vdev->iobase;
    port->uartclk = vdev->baud * 16;
    port->irq = vdev->irq; //set a REAL IRQ
    port->flags = STD_COMX_FLAGS;
    up->port = *port;

    //This is the most explosion-prone section so logs are useful
    //This may sound counter-intuitive but we don't want to REMOVE the port, we want to just re-register it with 
    //all default callbacks.
    pr_loc_dbg("Calling serial8250_register_8250_port to restore port");
    if ((out = serial8250_register_8250_port(up)) < 0) { //it returns port # on success or -E on error
        pr_loc_err("Failed to restore ttyS%d - driver failure (error=%d)", vdev->line, out);
        goto out_free;
    }
    pr_loc_dbg("ttyS%d finished unregistraton from driver (line=%d)", vdev->line, out);
    out = 0; //serial8250_register_8250_port return serial port line # or -E code

    vdev->registered = false;
    out = try_leave_serial8250_driver();

    out_free:
    kfree(up);
    return out;
}

int vuart_set_tx_callback(int line, vuart_callback_t *cb, char *buffer, int threshold)
{
    validate_isa_line(line);

    struct serial8250_16550A_vdev *vdev = get_line_vdev(line);
    if (!cb) {
        pr_loc_dbg("Removing TX callback for ttyS%d (line=%d)", line, vdev->line);
        if (unlikely(!flush_cbs[line])) {
            pr_loc_dbg("Nothing to do - no TX callback set");
            return 0;
        }

        //We don't really need to lock for that
        kfree(flush_cbs[line]);
        flush_cbs[line] = NULL;

        pr_loc_dbg("Removed TX callback for ttyS%d (line=%d)", line, vdev->line);
        return 0;
    }

    pr_loc_dbg("Setting TX callback for for ttyS%d (line=%d)", line, vdev->line);
    line = vdev->line; //this looks to make no sense BUT it does when serials are swapped
    if (likely(!flush_cbs[line])) { //if there was already a cb there we don't need to reserve memory
        kmalloc_or_exit_int(flush_cbs[line], sizeof(struct flush_callback));
    }

    //This can technically be called during serial port operation so we need to get a lock before we change these or
    // we risk sending a buffer to a wrong function. That lock may not exist when device is not added yet.
    lock_vuart_oppr(vdev);
    flush_cbs[line]->fn = cb;
    flush_cbs[line]->buffer = buffer;
    flush_cbs[line]->threshold = threshold;
    unlock_vuart_oppr(vdev);

    pr_loc_dbg("Added TX callback for ttyS%d (line=%d)", line, vdev->line);

    return 0;
}

int vuart_inject_rx(int line, const char *buffer, int length)
{
    validate_isa_line(line);
    
    if (unlikely(length > VUART_FIFO_LEN)) {
        pr_loc_bug("Attempted to inject buffer of %d bytes - it's larger than FIFO size (%d bytes)", length, VUART_FIFO_LEN);
        return -E2BIG;
    }
    
    struct serial8250_16550A_vdev *vdev = get_line_vdev(line);
    if (unlikely(!vdev->initialized)) {
        pr_loc_bug("Cannot inject data into non-initialized or non-registered device");
        return -ENXIO;
    }

    if (unlikely(!vdev->registered)) {
        pr_loc_wrn("Cannot inject data into unregistered device"); //...as it will be removed by the driver on reg
        return 0;
    }

    //No space to put data - not an error per-sen as this can be re-run again
    if ((vdev->lsr & UART_LSR_DR) && unlikely(kfifo_is_full(vdev->rx_fifo) || unlikely(vdev->mcr & UART_MCR_LOOP)))
        return 0;
    
    
    int put_bytes = kfifo_in(vdev->rx_fifo, buffer, VUART_FIFO_LEN);
    if (likely(put_bytes > 0))
        vdev->lsr |= UART_LSR_DR;

    uart_prdbg("Injected %d bytes into ttyS%d RX", put_bytes, line);
    update_interrupts_state(vdev);

    return put_bytes;
}

int vuart_add_device(int line)
{
    pr_loc_dbg("Adding vUART ttyS%d", line);

    validate_isa_line(line);
    warn_bug_swapped(line);

    int out;
    struct serial8250_16550A_vdev *vdev = get_line_vdev(line);

    if ((out = initialize_ttyS(vdev)) != 0)
        return out;

    if ((out = update_serial8250_isa_port(vdev)) != 0)
        goto error_deinit;

    if ((out = vuart_enable_interrupts(vdev)) != 0)
        goto error_restore;

    pr_loc_inf("Added vUART at ttyS%d", line);
    return 0;

    error_restore:
    restore_serial8250_isa_port(vdev);

    error_deinit:
    deinitialize_ttyS(vdev);

    return out;
}

int vuart_remove_device(int line)
{
    pr_loc_dbg("Removing vUART ttyS%d", line);

    validate_isa_line(line);
    warn_bug_swapped(line);

    int out;
    struct serial8250_16550A_vdev *vdev = get_line_vdev(line);
    if ((out = vuart_disable_interrupts(vdev)) != 0 || (out = deinitialize_ttyS(vdev)) != 0 ||
        (out = restore_serial8250_isa_port(vdev)) != 0 || (out = vuart_set_tx_callback(line, NULL, NULL, 0)) != 0)
        return out;

    pr_loc_inf("Removed vUART & restored original UART at ttyS%d", line);

    return 0;
}