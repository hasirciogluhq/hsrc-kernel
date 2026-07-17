#ifndef MYKERNEL_ENV_H
#define MYKERNEL_ENV_H

#include <kernel/types.h>

struct process;

#define ENV_KEY_MAX     32
#define ENV_VAL_MAX     128
#define ENV_PROC_MAX    16
#define ENV_GLOBAL_MAX  16

typedef struct env_entry {
    char key[ENV_KEY_MAX];
    char val[ENV_VAL_MAX];
} env_entry_t;

typedef struct proc_env {
    env_entry_t entries[ENV_PROC_MAX];
    int         count;
} proc_env_t;

void env_init(void);
void env_inherit(struct process *child, const struct process *parent);
void env_proc_clear(struct process *p);

int env_get(const struct process *p, const char *key, char *val, size_t valsz);
int env_set(struct process *p, const char *key, const char *val, int global);
int env_unset(struct process *p, const char *key, int global);

int env_load_file(const char *path);
int env_load_initrd(void);

#endif
