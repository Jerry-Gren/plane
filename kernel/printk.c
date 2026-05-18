#include <stdarg.h>

#include <klib/stdio.h>
#include <hal/serial.h>

#define PRINTK_BUF_SIZE 1024

/* TODO: spinlock */
void printk(const char *fmt, ...) {
    char buf[PRINTK_BUF_SIZE];
    va_list args;
    
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    for (int i = 0; i < len && buf[i] != '\0'; i++) {
        hal_serial_putchar(buf[i]);
    }
}

void panic(const char *fmt, ...) {
    char buf[PRINTK_BUF_SIZE];
    va_list args;
    
    printk("\n==================================================\n");
    printk("KERNEL PANIC!\n");
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    printk("Reason: %s\n", buf);
    printk("==================================================\n");
    
    /* TODO: replace with hal_cpu_hang */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}