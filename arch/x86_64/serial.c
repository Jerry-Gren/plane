#include <machine/serial.h>
#include <x86_64/pio.h>

#define COM1_PORT 0x3f8

void serial_init(void)
{
	/* initialize 16550 chip */
	outb(COM1_PORT + 1, 0x00);    /* disable interrupt */
	outb(COM1_PORT + 3, 0x80);    /* enable dlab */
	outb(COM1_PORT + 0, 0x01);    /* set baud rate (1 = 115200 baud) */ 
	outb(COM1_PORT + 1, 0x00);    
	outb(COM1_PORT + 3, 0x03);    /* lock dlab */
	outb(COM1_PORT + 2, 0xc7);    /* enable fifo */
	outb(COM1_PORT + 4, 0x0b);    /* enable irqs */
}

static int serial_transmit_is_empty(void)
{
	return inb(COM1_PORT + 5) & 0x20;
}

void serial_putchar(char c)
{
	if (c == '\n') {
		serial_putchar('\r');
	}

	while (serial_transmit_is_empty() == 0) {
		/* do nothing */
	}

	outb(COM1_PORT, c);
}
