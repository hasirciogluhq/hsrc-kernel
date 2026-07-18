#include <drivers/serial.h>
#include <kernel/process.h>
#include <kernel/types.h>

typedef struct {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} exc_regs_t;

void exc_dispatch(exc_regs_t *r)
{
    process_t *p = process_current();

    klog("\n[exc] FATAL vector=");
    serial_print_uint(r->int_no);
    klog(" err=");
    serial_print_hex(r->err_code);
    klog(" eip=");
    serial_print_hex(r->eip);
    klog(" cs=");
    serial_print_hex(r->cs);
    klog(" eflags=");
    serial_print_hex(r->eflags);
    klog("\n[exc] eax=");
    serial_print_hex(r->eax);
    klog(" ebx=");
    serial_print_hex(r->ebx);
    klog(" ecx=");
    serial_print_hex(r->ecx);
    klog(" edx=");
    serial_print_hex(r->edx);
    klog("\n[exc] proc=");
    klog(p && p->name[0] ? p->name : "(none)");
    klog("\n");

    /* User fault: tear down the process and keep the rest of the OS alive.
     * Kernel faults still halt - those are not recoverable here. */
    if (p && p->is_user && (r->cs & 3) == 3) {
        klog("[exc] killing user process\n");
        process_exit(139);
    }

    for (;;)
        __asm__ volatile("hlt");
}
