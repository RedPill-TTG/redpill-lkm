/**
 * This file is meant to be small and portable. It can be included by other parts of the module wishing to get some info
 * about UARTs. It should not contain any extensive definitions or static structures reservation. It is mostly
 * extracting information buried in the Linux serial subsystem into usable constants.
 */
#ifndef REDPILL_UART_DEFS_H
#define REDPILL_UART_DEFS_H

#include <asm/serial.h> //flags for pc_com*
#include <linux/serial_core.h> //struct uart_port

//These definitions are taken from asm/serial.h for a normal (i.e. non-swapped) UART1/COM1 port on an x86 PC
#define STD_COM1_IOBASE 0x3f8
#define STD_COM1_IRQ 4
#define STD_COM2_IOBASE 0x2f8
#define STD_COM2_IRQ 3
#define STD_COM3_IOBASE 0x3e8
#define STD_COM3_IRQ 4
#define STD_COM4_IOBASE 0x2e8
#define STD_COM4_IRQ 3

//They changed name of flags const: https://github.com/torvalds/linux/commit/196cf358422517b3ff3779c46a1f3e26fb084172
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,18,0)
#define STD_COMX_FLAGS STD_COM_FLAGS
#endif

#define STD_COMX_BAUD BASE_BAUD

#define STD_COMX_DEV_NAME "ttyS"
#define SRD_COMX_BAUD_OPTS "115200n8"

#define UART_NR CONFIG_SERIAL_8250_NR_UARTS
#define SERIAL8250_LAST_ISA_LINE (UART_NR-1) //max valid index of ttyS
#define SERIAL8250_SOFT_IRQ 0 //a special IRQ value which, if set on a port, will force 8250 driver to use timers

#ifdef CONFIG_SYNO_X86_SERIAL_PORT_SWAP
#define UART_BUG_SWAPPED //indicates that first two UARTs are swapped (sic!). Yes, we do consider it a fucking bug.
#endif

#endif //REDPILL_UART_DEFS_H
