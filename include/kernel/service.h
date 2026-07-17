#ifndef MYKERNEL_KERNEL_SERVICE_H
#define MYKERNEL_KERNEL_SERVICE_H

#include <kernel/types.h>
#include <kernel/vfs.h>

#define SERVICE_MAX      8
#define SERVICE_NAME_MAX 32

typedef struct service_info {
    char  name[SERVICE_NAME_MAX];
    char  path[VFS_PATH_MAX];
    pid_t pid;
    int   respawn;
    int   critical;
    int   running;
    int   last_exit_code;
} service_info_t;

void service_init(void);
void service_register_builtin_defaults(void);
void service_bind_existing_processes(void);
void service_start_critical(void);
void service_reap_dead(void);

int service_register(const char *name, const char *path, int respawn, int critical);
int service_list(service_info_t *out, size_t max, size_t *total_out);
int service_status(const char *name, service_info_t *out);
int service_start(const char *name);
int service_stop(const char *name);

long service_syscall_list(long buf_ptr, long max_entries, long total_ptr);
long service_syscall_start(long name_ptr);
long service_syscall_stop(long name_ptr);
long service_syscall_status(long name_ptr, long out_ptr);

#endif
