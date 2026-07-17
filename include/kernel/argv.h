#ifndef MYKERNEL_ARGV_H
#define MYKERNEL_ARGV_H

#include <kernel/types.h>

struct process;

#define PROC_ARGC_MAX  16
#define PROC_ARGV_MAX  256

typedef struct proc_argv {
    char args[PROC_ARGC_MAX][PROC_ARGV_MAX];
    int  count;
} proc_argv_t;

void argv_proc_clear(struct process *p);
int  argv_proc_set(struct process *p, const char *const *argv, int argc);
int  argv_proc_get(const struct process *p, int index, char *buf, size_t buflen);
int  argv_proc_count(const struct process *p);

/* Copy argc strings from user argv[] (array of char*). Returns argc or negative errno. */
int argv_copy_from_user(char storage[][PROC_ARGV_MAX], const char **kargv,
                        int argc, long argv_ptr);

#endif
