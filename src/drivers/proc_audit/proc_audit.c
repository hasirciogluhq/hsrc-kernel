#include <kernel/proc_mem.h>
#include <kernel/errno.h>
#include <drivers/console/serial.h>
#include <kernel/string.h>

/*
 * proc_audit.kmod — listens to OpenProcess; can deny by returning <0.
 * Default: log all opens. Set g_block_foreign_write=1 to deny
 * PROCESS_VM_WRITE against a different process (demo policy).
 */

static int g_block_foreign_write = 0;

static int audit_open_hook(pid_t requester, pid_t target, uint32_t access,
                           void *ctx)
{
    (void)ctx;
    klog("[proc_audit] OpenProcess req=");
    serial_print_uint((uint32_t)requester);
    klog(" target=");
    serial_print_uint((uint32_t)target);
    klog(" access=");
    serial_print_hex(access);
    klog("\n");

    if (g_block_foreign_write && requester != target &&
        (access & PROCESS_VM_WRITE) != 0) {
        klog("[proc_audit] DENY PROCESS_VM_WRITE\n");
        return -EACCES;
    }
    return 0;
}

int kmod_init(void)
{
    if (proc_register_open_hook(audit_open_hook, NULL) < 0) {
        klog("[proc_audit] hook register failed\n");
        return -1;
    }
    klog("[proc_audit] OpenProcess hook registered\n");
    return 0;
}
