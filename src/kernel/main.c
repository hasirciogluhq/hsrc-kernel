#include <multiboot.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <drivers/driver.h>
#include <drivers/pci.h>
#include <drivers/display.h>
#include <kernel/heap.h>
#include <kernel/bootmem.h>
#include <kernel/errno.h>
#include <kernel/mm.h>
#include <kernel/syscall.h>
#include <kernel/vfs.h>
#include <kernel/netif.h>
#include <kernel/socket.h>
#include <kernel/ksym.h>
#include <kernel/module.h>
#include <kernel/mkdx_api.h>
#include <kernel/process.h>
#include <kernel/env.h>
#include <kernel/service.h>
#include <kernel/scheduler.h>
#include <kernel/time.h>
#include <arch/x86/irq.h>
#include <kernel/mke.h>
#include <kernel/boot_splash.h>
#include <kernel/smp.h>
#include <arch/x86/gdt.h>
#include <arch/x86/idt.h>
#include <arch/x86/cpu.h>

static void klog_heap(const char *tag)
{
    klog(tag);
    klog(" heap_used=");
    serial_print_uint((uint32_t)heap_used());
    klog(" heap_free=");
    serial_print_uint((uint32_t)heap_free());
    klog("\n");
}

void kernel_main(uint32_t magic, multiboot_info_t *mbi)
{
    bootmem_layout_t mem;

    serial_init();
    vga_init();
    klog("[boot] kernel_main\n");

    if (magic != MULTIBOOT_MAGIC) {
        klog("[boot] bad multiboot magic\n");
        vga_print("bad multiboot magic\n");
        for (;;)
            __asm__ volatile("hlt");
    }

    /*
     * Heap lives in a Multiboot-discovered available region above the fixed
     * .mke load window (see bootmem). Avoid a huge .bss heap — loaders may
     * place the initrd inside the kernel BSS span and then zero it.
     */
    if (bootmem_init(mbi, &mem) < 0 || mem.heap_size == 0) {
        klog("[boot] bootmem_init FAILED\n");
        vga_print("bootmem_init failed\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    heap_init((void *)(uintptr_t)mem.heap_phys, mem.heap_size);
    klog_hex("[boot] heap_phys=", mem.heap_phys);
    klog_uint("[boot] heap_size=", mem.heap_size);
    klog_uint("[boot] total_ram=", mem.total_ram_bytes);
    mm_init();
    gdt_init();
    idt_init();
    cpu_init_bsp();
    irq_init();
    syscall_init();
    vfs_init();
    netif_init();
    socket_init();
    ksym_init();
    process_init();
    env_init();
    service_init();
    scheduler_init();
    smp_init();
    time_init();
    klog("[boot] core init done\n");

    driver_framework_init();
    display_framework_init();
    pci_init();
    drivers_register_internal();
    driver_attach("vga");

    if (drivers_load_all(NULL) < 0) {
        klog("[boot] drivers_load_all FAILED\n");
        vga_print("drivers_load_all failed\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    /* ps2_init used to mask the PIC; re-assert timer after driver load. */
    irq_ensure_timer_unmasked();
    klog("[boot] builtin drivers loaded\n");
    klog_heap("[boot]");

    klog("[boot] loading initrd modules...\n");
    if (modules_load_from_mbi(mbi) < 0) {
        klog("[boot] modules_load_from_mbi FAILED\n");
        vga_print("modules_load_from_mbi failed\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    klog("[boot] modules loaded\n");
    klog_heap("[boot]");

    if (env_load_initrd() < 0)
        klog("[boot] env_load_initrd failed (using defaults)\n");
    if (env_load_file("/etc/environment") < 0)
        klog("[boot] no /etc/environment on disk\n");

    {
        int rc = vfs_mkdir("/applications", 0755);
        if (rc < 0 && rc != -EEXIST)
            klog("[boot] mkdir /applications failed\n");
        rc = vfs_mount("initrd", "/applications", "initrdfs", MS_RDONLY, NULL);
        if (rc < 0)
            klog("[boot] mount /applications initrdfs failed\n");
    }

    if (!display_active()) {
        klog("[boot] no active display\n");
        vga_print("no active display\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    klog("[boot] display active\n");

    if (!mkdx_api_get()) {
        klog("[boot] mkdx NOT loaded — UI cannot start\n");
        vga_print("mkdx not loaded\n");
        for (;;)
            __asm__ volatile("hlt");
    }
    klog("[boot] mkdx ready\n");
    klog_heap("[boot]");

    /* Calm spinner while userspace comes up — covers the raw LFB blue flash. */
    boot_splash_show();

    service_register_builtin_defaults();

    klog("[boot] spawning .mke apps...\n");
    if (mke_spawn_from_mbi(mbi) < 0)
        klog("[boot] no .mke modules in multiboot (service may spawn os-ui)\n");
    klog("[boot] mke spawn done\n");
    service_bind_existing_processes();
    service_start_critical();
    klog_heap("[boot]");

    {
        process_t **table = process_table();
        int i, n = 0;
        for (i = 0; i < PROC_MAX; i++) {
            if (table[i] && table[i]->state != PROC_UNUSED) {
                n++;
                klog("[boot] proc name=");
                klog(table[i]->name);
                klog(" pid=");
                serial_print_uint((uint32_t)table[i]->pid);
                klog(" user=");
                serial_print_uint((uint32_t)table[i]->is_user);
                klog("\n");
            }
        }
        klog_uint("[boot] ready processes=", (uint32_t)n);
    }

    klog("[boot] scheduler_start\n");
    scheduler_start();
}
