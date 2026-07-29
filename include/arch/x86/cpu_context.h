#ifndef ARCH_X86_CPU_CONTEXT_H
#define ARCH_X86_CPU_CONTEXT_H

#include <kernel/types.h>

#define CPU_FPU_AREA_SIZE 512

/*
 * OS-thread CPU context (playbook layout, i386 widths).
 * Live resume = callee-saved + esp on this struct + FXSAVE blob + CR3.
 *
 * Offset (byte):
 *   0  esp
 *   4  ebx
 *   8  esi
 *  12  edi
 *  16  ebp
 *  20  cr3
 *  24  fpu_area*   (16-byte aligned, CPU_FPU_AREA_SIZE)
 */
typedef struct cpu_context {
    uint32_t  esp;
    uint32_t  ebx;
    uint32_t  esi;
    uint32_t  edi;
    uint32_t  ebp;
    uint32_t  cr3;
    void     *fpu_area;
} cpu_context_t;

/*
 * PRE: IRQs off + runqueue/sched lock held by caller.
 * Does not take locks. Swaps GP callee-saved, FPU (FXSAVE), CR3 if changed.
 */
void context_switch_locked(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

#endif
