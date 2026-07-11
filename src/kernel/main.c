#include <multiboot.h>
#include <drivers/vga.h>
#include <drivers/fb.h>
#include <kernel/heap.h>
#include <gfx/mkdx.h>

extern char _kernel_end[];

#define HEAP_SIZE (20u * 1024u * 1024u)
static uint8_t heap_area[HEAP_SIZE] __attribute__((aligned(16)));

void kernel_main(uint32_t magic, multiboot_info_t *mbi)
{
    /* text-mode fallback until FB is up */
    vga_init();
    vga_set_color(VGA_ATTR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_print("mykernel - MKDX boot\n");
    vga_set_color(VGA_ATTR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    if (magic != MULTIBOOT_MAGIC) {
        vga_print("bad multiboot magic\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    heap_init(heap_area, HEAP_SIZE);
    (void)_kernel_end;

    if (fb_init(mbi) < 0) {
        vga_print("framebuffer unavailable\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    vga_print("FB ");
    vga_print_uint(fb_get()->width);
    vga_print("x");
    vga_print_uint(fb_get()->height);
    vga_print(" @");
    vga_print_uint(fb_get()->bpp);
    vga_print("bpp - starting MKDX\n");

    mkdx_demo();

    for (;;)
        __asm__ volatile("hlt");
}
