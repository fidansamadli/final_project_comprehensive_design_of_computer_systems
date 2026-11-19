/*
 * Host-test platform shim for the Morse Messenger application.
 *
 * The real application is compiled unchanged against the Loongson 1B board
 * support package.  For host tests the few board services that main.c uses are
 * replaced by the small interfaces below, which the test program implements.
 */
#ifndef MORSE_HOST_PLATFORM_H
#define MORSE_HOST_PLATFORM_H

#define devUART3 0
#define DIR_OUT  1

/* Serial console, replaced by an in-memory input queue and output buffer. */
void         host_uart_send(const char *text);
int          host_uart_read(char *byte);

/* Millisecond tick, controlled by the test program. */
unsigned int host_now(void);

/* GPIO, recorded so the LED waveform can be checked. */
void gpio_write(int pin, int value);
int  gpio_enable(int pin, int direction);

#endif
