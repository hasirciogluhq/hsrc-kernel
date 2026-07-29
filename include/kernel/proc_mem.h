#ifndef KERNEL_PROC_MEM_H
#define KERNEL_PROC_MEM_H

#include <kernel/types.h>

/*
 * Windows-like process memory / handle API (root-only enforcement for now).
 * Drivers may register open-process hooks to audit/block OpenProcess.
 */

#define PROCESS_TERMINATE         0x0001u
#define PROCESS_VM_OPERATION      0x0008u
#define PROCESS_VM_READ           0x0010u
#define PROCESS_VM_WRITE          0x0020u
#define PROCESS_QUERY_INFORMATION 0x0400u
#define PROCESS_ALL_ACCESS        0x1F0FFFu

/* Windows-like pseudo handle for the calling process (no OpenProcess). */
#define PROCESS_HANDLE_CURRENT    ((int)-1)

#define MEM_COMMIT  0x1000u
#define MEM_RESERVE 0x2000u
#define MEM_RELEASE 0x8000u

#define PAGE_NOACCESS          0x01u
#define PAGE_READONLY          0x02u
#define PAGE_READWRITE         0x04u
#define PAGE_EXECUTE_READ      0x20u
#define PAGE_EXECUTE_READWRITE 0x40u

typedef struct proc_handle {
    int      used;
    int      id;
    pid_t    owner_pid; /* who opened */
    pid_t    target_pid;
    uint32_t access;
} proc_handle_t;

/* Hook: return 0 allow, <0 deny (propagated to OpenProcess caller). */
typedef int (*proc_open_hook_fn)(pid_t requester, pid_t target, uint32_t access,
                                 void *ctx);

void proc_mem_init(void);
int  proc_register_open_hook(proc_open_hook_fn fn, void *ctx);
void proc_unregister_open_hook(proc_open_hook_fn fn);

long sys_open_process(pid_t target, uint32_t access);
long sys_close_handle(int handle);
long sys_read_process_memory(int handle, uint32_t addr, void *buf, size_t len,
                             size_t *out_read);
long sys_write_process_memory(int handle, uint32_t addr, const void *buf,
                              size_t len, size_t *out_written);
long sys_virtual_alloc(uint32_t addr, size_t size, uint32_t type, uint32_t prot);
long sys_virtual_free(uint32_t addr, size_t size, uint32_t type);
long sys_virtual_alloc_ex(int handle, uint32_t addr, size_t size, uint32_t type,
                          uint32_t prot);
long sys_virtual_free_ex(int handle, uint32_t addr, size_t size, uint32_t type);
long sys_query_process(int handle, void *out, size_t out_size);

typedef struct proc_vm_region {
    uint32_t base;
    uint32_t size;
    uint32_t prot;  /* PROT_* */
    uint32_t kind;  /* 1=ustack 2=vma/heap 3=kstack */
} proc_vm_region_t;

/* Fill out[] with up to max_n regions; returns count or -errno. */
long sys_query_process_vm(int handle, void *out, size_t out_bytes);

/* Kernel-driver mirror (same semantics, no user copy). */
void *drv_heap_alloc(size_t n);
void  drv_heap_free(void *p);
void *drv_vm_alloc_pages(size_t npages);
void  drv_vm_free_pages(void *p, size_t npages);
long  drv_open_process(pid_t target, uint32_t access);
long  drv_close_handle(int handle);
long  drv_read_process_memory(int handle, uint32_t addr, void *buf, size_t len);
long  drv_write_process_memory(int handle, uint32_t addr, const void *buf,
                               size_t len);
long  drv_virtual_alloc(uint32_t addr, size_t size, uint32_t type, uint32_t prot);
long  drv_virtual_free(uint32_t addr, size_t size, uint32_t type);
long  drv_virtual_alloc_ex(int handle, uint32_t addr, size_t size, uint32_t type,
                           uint32_t prot);
long  drv_virtual_free_ex(int handle, uint32_t addr, size_t size, uint32_t type);

#endif
