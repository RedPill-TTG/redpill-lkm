#ifndef REDPILL_UART_FIXER_H
#define REDPILL_UART_FIXER_H

typedef struct hw_config hw_config_uart_fixer;
int register_uart_fixer(const hw_config_uart_fixer *hw);
int unregister_uart_fixer(void);

#endif //REDPILL_UART_FIXER_H
