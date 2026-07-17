#include <multiboot.h>
#include <drivers/vga.h>
#include <drivers/driver.h>
#include <drivers/pci.h>
#include <drivers/display.h>
#include <kernel/heap.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/ksym.h>
#include <kernel/module.h>
#include <kernel/mkdx_api.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <user/gx.h>

#define HEAP_SIZE (20u * 1024u * 1024u)
static uint8_t heap_area[HEAP_SIZE] __attribute__((aligned(16)));

void kernel_main(uint32_t magic, multiboot_info_t *mbi)
{
    vga_init();

    if (magic != MULTIBOOT_MAGIC) {
        vga_print("bad multiboot magic\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    heap_init(heap_area, HEAP_SIZE);
    gdt_init();
    idt_init();
    syscall_init();
    vfs_init();
    ksym_init();

    driver_framework_init();
    display_framework_init();
    pci_init();
    drivers_register_internal();
    driver_attach("vga");

    if (drivers_load_all(NULL) < 0) {
        vga_print("drivers_load_all failed\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    /* display_*.kmod then mkdx.kmod from Multiboot module / initrd */
    if (modules_load_from_mbi(mbi) < 0) {
        vga_print("modules_load_from_mbi failed\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    if (!display_active()) {
        vga_print("no active display\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    if (!mkdx_api_get()) {
        vga_print("mkdx not loaded\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    /* Desktop shell — ugx syscalls into mkdx. Never returns. */
    user_os_ui_main();
}
