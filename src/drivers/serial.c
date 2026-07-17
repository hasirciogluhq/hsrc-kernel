#include <drivers/serial.h>
#include <arch/x86/io.h>

#define COM1 0x3F8

static int g_serial_ready;

void serial_init(void)
{
    /* 115200 8N1, FIFO on — QEMU accepts this immediately. */
    outb(COM1 + 1, 0x00); /* disable IRQs */
    outb(COM1 + 3, 0x80); /* DLAB on */
    outb(COM1 + 0, 0x01); /* divisor lo (115200) */
    outb(COM1 + 1, 0x00); /* divisor hi */
    outb(COM1 + 3, 0x03); /* 8N1, DLAB off */
    outb(COM1 + 2, 0xC7); /* FIFO enable/clear */
    outb(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */
    g_serial_ready = 1;

    serial_print("\n[klog] serial COM1 ready\n");
}

static int serial_tx_empty(void)
{
    return inb(COM1 + 5) & 0x20;
}

void serial_putc(char c)
{
    if (!g_serial_ready)
        return;
    if (c == '\n')
        serial_putc('\r');
    while (!serial_tx_empty())
        ;
    outb(COM1, (uint8_t)c);
}

void serial_write(const char *s, size_t n)
{
    size_t i;
    if (!s)
        return;
    for (i = 0; i < n; i++)
        serial_putc(s[i]);
}

void serial_print(const char *s)
{
    if (!s)
        return;
    while (*s)
        serial_putc(*s++);
}

void serial_print_uint(uint32_t n)
{
    char buf[11];
    int i = 0;
    if (n == 0) {
        serial_putc('0');
        return;
    }
    while (n > 0 && i < 10) {
        buf[i++] = (char)('0' + (n % 10));
        n /= 10;
    }
    while (i > 0)
        serial_putc(buf[--i]);
}

void serial_print_hex(uint32_t n)
{
    static const char hex[] = "0123456789abcdef";
    int i;
    serial_print("0x");
    for (i = 7; i >= 0; i--)
        serial_putc(hex[(n >> (i * 4)) & 0xF]);
}

void klog(const char *s)
{
    serial_print(s);
}

void klog_uint(const char *prefix, uint32_t n)
{
    if (prefix)
        serial_print(prefix);
    serial_print_uint(n);
    serial_putc('\n');
}

void klog_hex(const char *prefix, uint32_t n)
{
    if (prefix)
        serial_print(prefix);
    serial_print_hex(n);
    serial_putc('\n');
}
