#ifndef HAL_SERIAL_H
#define HAL_SERIAL_H

/* Every architecture must implement these functions in their hal/.../serial.c */
void hal_serial_init(void);
void hal_serial_putchar(char c);

#endif /* HAL_SERIAL_H*/