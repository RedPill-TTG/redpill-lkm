#ifndef REDPILL_UART_SWAPPER_H
#define REDPILL_UART_SWAPPER_H

/**
 * Swaps two given UARTs/serial prots so that their data paths are exchanged without the change of /dev/tty#
 *
 * This method is blind to whether UARTs were swapped during kernel build. However, it's the reason it exists to un-swap
 * these stupid ports. You can swap any ports you want. It's not recommended to swap ports which are in different run
 * state (i.e. one is active/open/running and the other one is not). In such cases the swap will be attempted BUT the
 * port which was active may not be usable until re-opened (usually it will be, but there's a chance).
 *
 * @param from Line number (line = ttyS#, so line=0 = ttyS0; this is universal across Linux UART subsystem))
 * @param to  Line number
 * @return 0 on success or -E on error
 */
int uart_swap_hw_output(unsigned int from, unsigned char to);

#endif //REDPILL_UART_SWAPPER_H
