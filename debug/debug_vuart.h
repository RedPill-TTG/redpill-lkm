#ifndef REDPILL_DEBUG_VUART_H
#define REDPILL_DEBUG_VUART_H

//Whether the code will print all internal state changes
#ifdef VUART_DEBUG_LOG
//Main print macro used everywhere below
#define uart_prdbg(f, ...) pr_loc_dbg(f, ##__VA_ARGS__)

#define reg_read(rN) uart_prdbg("Reading " rN " registry");
#define reg_write(rN) uart_prdbg("Writing " rN " registry");
#define reg_read_dump(d, rF, rN) reg_read(rN); dump_##rF(d);
#define reg_write_dump(d, rF, rN) reg_write(rN); dump_##rF(d);
#define dri(vdev, reg, flag) ((vdev)->reg&(flag)) ? 1:0 //Dump Register as 1-0 Integer
#define diiri(vdev, flag) (((vdev)->iir&UART_IIR_ID) == (flag)) ? 1:0 //Dump IIR Interrupt type as 1-0 integer
#define dump_ier(d) \
    uart_prdbg("IER[0x%02x]: DR_int=%d | THRe_int=%d | RLS_int=%d | " \
               "MS_int=%d",  \
               (d)->ier, dri(d,ier,UART_IER_RDI), dri(d,ier,UART_IER_THRI), dri(d,ier,UART_IER_RLSI), \
               dri(d,ier,UART_IER_MSI));
//Be careful interpreting the result of this macro - no_int_pend means "no interrupts pending" (so 0 if there are
// pending interrupts and 1 if there are no interrupts pending); see Table 3-5 in TI doc
//Also FIFO flags are slightly weird (it's 2 bit, see IIR table in https://en.wikibooks.org/wiki/Serial_Programming/8250_UART_Programming)
// so fifoen=0_0 means "FIFO disabled", fifoen=1_1 means "FIFO enabled", and fifoen=0_1 means "FIFO enabled & broken"
//Also, since MSI is 0-0-0 it's a special-ish case: it's only considered enabled when int is pending and all bits are 0
#define dump_iir(d) \
    uart_prdbg("IIR/ISR[0x%02x]: no_int_pend=%d | int_MS=%d | " \
               "int_THRe=%d | int_DR=%d | int_RLS=%d | " \
               "fifoen=%d_%d", \
               (d)->iir, dri(d,iir,UART_IIR_NO_INT), (!((d)->iir&UART_IIR_NO_INT)&&((d)->iir&UART_IIR_ID)==UART_IIR_MSI)?1:0, \
               diiri(d,UART_IIR_THRI), diiri(d,UART_IIR_RDI), diiri(d,UART_IIR_RLSI), \
               (((d)->iir & UART_IIR_FIFEN_B6)?1:0), (((d)->iir & UART_IIR_FIFEN_B7)?1:0));
#define dump_fcr(d) \
    uart_prdbg("FCR[0x%02x]: FIFOon=%d | RxFIFOrst=%d | " \
               "TxFIFOrst=%d | EnDMAend=%d",  \
               (d)->fcr, dri(d,fcr,UART_FCR_ENABLE_FIFO), dri(d,fcr,UART_FCR_CLEAR_RCVR), \
               dri(d,fcr,UART_FCR_CLEAR_XMIT), dri(d,fcr,UART_FCR_DMA_SELECT));
#define dump_lcr(d) \
    uart_prdbg("LCR[0x%02x]: Stop=%d | PairEN=%d | EvenP=%d | " \
               "ForcPair=%d | SetBrk=%d | DLAB=%d",  \
               (d)->lcr, dri(d,lcr,UART_LCR_STOP), dri(d,lcr,UART_LCR_PARITY), dri(d,lcr,UART_LCR_EPAR), \
               dri(d,lcr,UART_LCR_SPAR), dri(d,lcr,UART_LCR_SBC), dri(d,lcr,UART_LCR_DLAB));
#define dump_mcr(d) \
    uart_prdbg("MCR[0x%02x]: DTR=%d | RTS=%d | Out1=%d | " \
               "Out2/IntE=%d | Loop=%d", \
               (d)->mcr, dri(d,mcr,UART_MCR_DTR), dri(d,mcr,UART_MCR_RTS), dri(d,mcr,UART_MCR_OUT1), \
               dri(d,mcr,UART_MCR_OUT2), dri(d,mcr,UART_MCR_LOOP));
#define dump_lsr(d) \
    uart_prdbg("LSR[0x%02x]: data_ready=%d | ovrunE=%d | pairE=%d | " \
                "frE=%d | break_req=%d | THRemp=%d | TransEMP=%d | " \
                "FIFOdE=%d", \
                (d)->lsr, dri(d,lsr,UART_LSR_DR), dri(d,lsr,UART_LSR_OE), dri(d,lsr,UART_LSR_PE),  \
                dri(d,lsr,UART_LSR_FE), dri(d,lsr,UART_LSR_BI), dri(d,lsr,UART_LSR_THRE), dri(d,lsr,UART_LSR_TEMT), \
                dri(d,lsr,UART_LSR_FIFOE));
#define dump_msr(d) \
    uart_prdbg("MSR[0x%02x]: delCTS=%d | delDSR=%d | trEdgRI=%d | " \
               "delCD=%d | CTS=%d | DSR=%d | RI=%d | "              \
               "DCD=%d", \
               (d)->msr, dri(d,msr,UART_MSR_DCTS), dri(d,msr,UART_MSR_DDSR), dri(d,msr,UART_MSR_TERI), \
               dri(d,msr,UART_MSR_DDCD), dri(d,msr,UART_MSR_CTS), dri(d,msr,UART_MSR_DSR), dri(d,msr,UART_MSR_RI), \
               dri(d,msr,UART_MSR_DCD));

#else //VUART_DEBUG_LOG disabled \/
#define uart_prdbg(f, ...) { /* noop */ }
#define reg_read(rN) { /* noop */ }
#define reg_write(rN) { /* noop */ }
#define reg_read_dump(d, rF, rN) { /* noop */ }
#define reg_write_dump(d, rF, rN) { /* noop */ }
#define dump_ier(d) { /* noop */ }
#define dump_iir(d) { /* noop */ }
#define dump_fcr(d) { /* noop */ }
#define dump_lcr(d) { /* noop */ }
#define dump_mcr(d) { /* noop */ }
#define dump_lsr(d) { /* noop */ }
#define dump_msr(d) { /* noop */ }
#endif //VUART_DEBUG_LOG

#endif //REDPILL_DEBUG_VUART_H
