#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include <kernel/types.h>

#define CPU_MAX 16

struct process;

typedef struct cpu {
    int            id;       /* dense index 0..n-1 */
    uint8_t        apic_id;
    volatile int   online;
    int            package;  /* socket/package id (0 if unknown) */
    int            core;     /* core within package */
    int            smt;      /* SMT/hyperthread sibling index */
    struct process *current;
    struct process *idle;
    uint32_t      *boot_stack;
    int            started;  /* AP entered C runtime */
} cpu_t;

/* Static CPUID snapshot (filled by cpu_detect). */
typedef struct cpu_info {
    char     vendor[13];
    char     brand[49];
    uint32_t max_leaf;
    uint32_t max_ext_leaf;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint32_t features_edx;   /* CPUID.1 EDX */
    uint32_t features_ecx;   /* CPUID.1 ECX */
    uint32_t logical_per_pkg; /* CPUID.1 EBX[23:16], 0 if unknown */
    uint32_t cores_per_pkg;   /* best-effort from leaf 4 / topology */
    int      has_apic;
    int      has_htt;        /* hyper-threading / multi-logic capable */
} cpu_info_t;

void   cpu_init_bsp(void);
void   cpu_fpu_init(void);         /* CR0/CR4: enable FXSAVE path (playbook) */
void   cpu_detect(void);           /* CPUID probe (call once on BSP) */
void   cpu_debug_dump(void);       /* serial debug: topology + online CPUs */
const cpu_info_t *cpu_info(void);

cpu_t *cpu_current(void);
int    cpu_id(void);
int    cpu_count(void);
cpu_t *cpu_get(int id);
cpu_t *cpu_by_apic(uint8_t apic_id);
cpu_t *cpu_alloc(uint8_t apic_id);
void   cpu_rollback_last(void);

#endif
