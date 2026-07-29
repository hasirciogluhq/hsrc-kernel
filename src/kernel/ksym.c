#include <kernel/ksym.h>
#include <kernel/string.h>
#include <kernel/heap.h>
#include <kernel/module.h>
#include <kernel/disp_api.h>
#include <kernel/vfs_api.h>
#include <kernel/block_api.h>
#include <kernel/netif.h>
#include <kernel/socket.h>
#include <kernel/epoll.h>
#include <kernel/initrd_store.h>
#include <kernel/process.h>
#include <kernel/proc_mem.h>
#include <kernel/sync.h>
#include <kernel/scheduler.h>
#include <kernel/vmm.h>
#include <arch/x86/irq.h>
#include <drivers/driver.h>
#include <drivers/bus/pci.h>
#include <drivers/display/display.h>
#include <drivers/display/gpu.h>
#include <drivers/console/vga.h>
#include <drivers/console/serial.h>
#include <drivers/input/mouse.h>
#include <drivers/input/keyboard.h>
#include <drivers/input/ps2.h>
#include <drivers/console/console.h>

uint64_t __udivdi3(uint64_t n, uint64_t d);
uint64_t __umoddi3(uint64_t n, uint64_t d);
uint64_t __udivmoddi4(uint64_t n, uint64_t d, uint64_t *rem);

static const ksym_t g_ksyms[] = {
    { "kmalloc",            (void *)kmalloc },
    { "kmalloc_aligned",    (void *)kmalloc_aligned },
    { "kfree",              (void *)kfree },
    { "heap_used",          (void *)heap_used },
    { "heap_free",          (void *)heap_free },

    { "vmm_identity_map_range", (void *)vmm_identity_map_range },

    { "memcpy",             (void *)memcpy },
    { "memset",             (void *)memset },
    { "memmove",            (void *)memmove },
    { "memcmp",             (void *)memcmp },

    { "__udivdi3",          (void *)__udivdi3 },
    { "__umoddi3",          (void *)__umoddi3 },
    { "__udivmoddi4",       (void *)__udivmoddi4 },
    { "strlen",             (void *)strlen },
    { "strcmp",             (void *)strcmp },
    { "strncmp",            (void *)strncmp },
    { "strcpy",             (void *)strcpy },
    { "strncpy",            (void *)strncpy },

    { "driver_register",    (void *)driver_register },
    { "driver_unregister",  (void *)driver_unregister },
    { "driver_load",        (void *)driver_load },
    { "driver_unload",      (void *)driver_unload },
    { "driver_find",        (void *)driver_find },
    { "driver_get_state",   (void *)driver_get_state },
    { "drivers_poll",       (void *)drivers_poll },

    { "pci_init",           (void *)pci_init },
    { "pci_config_read",    (void *)pci_config_read },
    { "pci_config_write",   (void *)pci_config_write },
    { "pci_config_read8",   (void *)pci_config_read8 },
    { "pci_config_read16",  (void *)pci_config_read16 },
    { "pci_config_write16", (void *)pci_config_write16 },
    { "pci_bar_addr",       (void *)pci_bar_addr },
    { "pci_bar_phys",       (void *)pci_bar_phys },
    { "pci_enable_bus_master", (void *)pci_enable_bus_master },
    { "pci_find",           (void *)pci_find },
    { "pci_find_class",     (void *)pci_find_class },
    { "pci_enumerate",      (void *)pci_enumerate },

    { "display_register",   (void *)display_register },
    { "display_unregister", (void *)display_unregister },
    { "display_active",     (void *)display_active },
    { "display_get_screen_size", (void *)display_get_screen_size },
    { "display_boot_log",   (void *)display_boot_log },

    { "gpu_framework_init", (void *)gpu_framework_init },
    { "gpu_provider_register", (void *)gpu_provider_register },
    { "gpu_provider_unregister", (void *)gpu_provider_unregister },
    { "gpu_provider_active", (void *)gpu_provider_active },
    { "gpu_get_screen_size", (void *)gpu_get_screen_size },
    { "gpu_provider_register_display", (void *)gpu_provider_register_display },

    { "disp_api_register",  (void *)disp_api_register },
    { "disp_api_get",       (void *)disp_api_get },
    { "input_event_notify", (void *)input_event_notify },
    { "input_event_seq",    (void *)input_event_seq },

    { "vfs_api_register",   (void *)vfs_api_register },
    { "vfs_api_get",        (void *)vfs_api_get },
    { "block_api_register", (void *)block_api_register },
    { "block_api_get",      (void *)block_api_get },
    { "netif_register",     (void *)netif_register },
    { "netif_default",      (void *)netif_default },
    { "netif_by_name",      (void *)netif_by_name },
    { "netif_set_addr",     (void *)netif_set_addr },
    { "netif_input",        (void *)netif_input },
    { "netif_output",       (void *)netif_output },
    { "net_poll",           (void *)net_poll },
    { "sock_create",        (void *)sock_create },
    { "sock_bind",          (void *)sock_bind },
    { "sock_connect",       (void *)sock_connect },
    { "sock_listen",        (void *)sock_listen },
    { "sock_accept",        (void *)sock_accept },
    { "sock_sendto",        (void *)sock_sendto },
    { "sock_recvfrom",      (void *)sock_recvfrom },
    { "sock_send",          (void *)sock_send },
    { "sock_recv",          (void *)sock_recv },
    { "sock_shutdown",      (void *)sock_shutdown },
    { "sock_close",         (void *)sock_close },
    { "sock_poll_events",   (void *)sock_poll_events },
    { "epoll_create_inst",  (void *)epoll_create_inst },
    { "epoll_ctl_inst",     (void *)epoll_ctl_inst },
    { "epoll_wait_inst",    (void *)epoll_wait_inst },
    { "epoll_close_inst",   (void *)epoll_close_inst },
    { "initrd_store_get",   (void *)initrd_store_get },
    { "initrd_store_set",   (void *)initrd_store_set },
    { "process_current",    (void *)process_current },
    { "process_by_tid",     (void *)process_by_tid },
    { "process_suspend",    (void *)process_suspend },
    { "process_block",      (void *)process_suspend },
    { "process_wake",       (void *)process_wake },
    { "schedule",           (void *)schedule },
    { "proc_register_open_hook",   (void *)proc_register_open_hook },
    { "proc_unregister_open_hook", (void *)proc_unregister_open_hook },
    { "drv_heap_alloc",            (void *)drv_heap_alloc },
    { "drv_heap_free",             (void *)drv_heap_free },
    { "drv_vm_alloc_pages",        (void *)drv_vm_alloc_pages },
    { "drv_vm_free_pages",         (void *)drv_vm_free_pages },
    { "drv_open_process",          (void *)drv_open_process },
    { "drv_close_handle",          (void *)drv_close_handle },
    { "drv_read_process_memory",   (void *)drv_read_process_memory },
    { "drv_write_process_memory",  (void *)drv_write_process_memory },
    { "drv_virtual_alloc",         (void *)drv_virtual_alloc },
    { "drv_virtual_free",          (void *)drv_virtual_free },
    { "drv_virtual_alloc_ex",      (void *)drv_virtual_alloc_ex },
    { "drv_virtual_free_ex",       (void *)drv_virtual_free_ex },
    { "irq_timer_ticks",    (void *)irq_timer_ticks },
    { "modules_load_blob",  (void *)modules_load_blob },
    { "module_load_path",   (void *)module_load_path },
    { "module_find",        (void *)module_find },

    { "console_read",       (void *)console_read },
    { "console_write",      (void *)console_write },
    { "console_print",      (void *)console_print },

    { "vga_print",          (void *)vga_print },
    { "vga_putc",           (void *)vga_putc },
    { "vga_print_uint",     (void *)vga_print_uint },
    { "serial_print",       (void *)serial_print },
    { "serial_putc",        (void *)serial_putc },
    { "serial_print_uint",  (void *)serial_print_uint },
    { "serial_print_hex",   (void *)serial_print_hex },
    { "klog",               (void *)klog },
    { "klog_uint",          (void *)klog_uint },
    { "klog_hex",           (void *)klog_hex },

    { "mouse_get",            (void *)mouse_get },
    { "mouse_pop_event",      (void *)mouse_pop_event },
    { "mouse_consume_wheel",  (void *)mouse_consume_wheel },
    { "mouse_set_bounds",     (void *)mouse_set_bounds },
    { "keyboard_getchar",   (void *)keyboard_getchar },
    { "keyboard_modifiers", (void *)keyboard_modifiers },
    { "keyboard_keys_bitmap", (void *)keyboard_keys_bitmap },
    { "ps2_poll",           (void *)ps2_poll },

    { "ksym_lookup",        (void *)ksym_lookup },

    { NULL, NULL }
};

void ksym_init(void)
{
}

void *ksym_lookup(const char *name)
{
    size_t i;
    if (!name)
        return NULL;
    for (i = 0; g_ksyms[i].name; i++) {
        if (strcmp(g_ksyms[i].name, name) == 0)
            return g_ksyms[i].addr;
    }
    return NULL;
}
