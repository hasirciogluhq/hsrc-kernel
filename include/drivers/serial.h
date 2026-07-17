#ifndef MYKERNEL_DRIVERS_SERIAL_H
#define MYKERNEL_DRIVERS_SERIAL_H

#include <kernel/types.h>

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *s, size_t n);
void serial_print(const char *s);
void serial_print_uint(uint32_t n);
void serial_print_hex(uint32_t n);

/* Debug log → QEMU (-serial stdio). Always safe to call. */
void klog(const char *s);
void klog_uint(const char *prefix, uint32_t n);
void klog_hex(const char *prefix, uint32_t n);

#endif
