/*
 * This header file should be included as the first one before anything else so other header files can use STEALTH_MODE
 */

#ifndef REDPILLLKM_STEALTH_H
#define REDPILLLKM_STEALTH_H

#define STEALTH_MODE_OFF    0   //Nothing is hidden, useful for full-on debugging
#define STEALTH_MODE_BASIC  1   //Hides basic things like cmdline (which is more to prevent DSM code from complaining about unknown options etc.)
#define STEALTH_MODE_NORMAL 2   //Hides everything except making the module not unloadable
#define STEALTH_MODE_FULL   3   //Same as STEALTH_MODE_NORMAL + removes the module from list of loaded modules & all logs

//Define just after levels so other headers can use it if needed to e.g. replace some macros
#ifndef STEALTH_MODE
//#warning "Stealth mode not specified - using default"
#define STEALTH_MODE STEALTH_MODE_BASIC
#endif

//Some compile-time stealthiness
#if STEALTH_MODE > STEALTH_MODE_OFF //STEALTH_MODE_BASIC or above
#define VIRTUAL_UART_THREAD_FMT "irq/%d-serial" //pattern format for vUART kernel thread which spoofs IRQ one
#endif

struct runtime_config;

int initialize_stealth(void *config);
int uninitialize_stealth(void);

#endif //REDPILLLKM_STEALTH_H