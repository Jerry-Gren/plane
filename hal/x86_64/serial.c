#include <hal/serial.h>
#include <hal/x86_64/pio.h>

#define COM1_PORT 0x3F8

void hal_serial_init(void) {
	/* initialize 16550 chip */
	outb(COM1_PORT + 1, 0x00);    /* disable interrupt */
	outb(COM1_PORT + 3, 0x80);    /* enable dlab */
	outb(COM1_PORT + 0, 0x01);    /* set baud rate (1 = 115200 baud) */ 
	outb(COM1_PORT + 1, 0x00);    
	outb(COM1_PORT + 3, 0x03);    /* lock dlab */
	outb(COM1_PORT + 2, 0xC7);    /* enable fifo */
	outb(COM1_PORT + 4, 0x0B);    /* enable irqs */
}

static int serial_is_transmit_empty(void) {
	return inb(COM1_PORT + 5) & 0x20;
}

void hal_serial_putchar(char c) {
	if (c == '\n') {
		hal_serial_putchar('\r');
	}

	while (serial_is_transmit_empty() == 0) {
		/* do nothing */
	}

	outb(COM1_PORT, c);
}