#include <kernel/argv.h>
#include <kernel/errno.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/uaccess.h>

void argv_proc_clear(process_t *p)
{
    if (!p)
        return;
    p->argv.count = 0;
    memset(p->argv.args, 0, sizeof(p->argv.args));
}

int argv_proc_set(process_t *p, const char *const *argv, int argc)
{
    int i;

    if (!p)
        return -ESRCH;
    if (!argv || argc <= 0) {
        argv_proc_clear(p);
        return 0;
    }
    if (argc > PROC_ARGC_MAX)
        return -E2BIG;

    argv_proc_clear(p);
    for (i = 0; i < argc; i++) {
        const char *s = argv[i];
        size_t n;

        if (!s)
            return -EINVAL;
        n = strlen(s);
        if (n >= PROC_ARGV_MAX)
            return -E2BIG;
        memcpy(p->argv.args[i], s, n + 1);
    }
    p->argv.count = argc;
    return argc;
}

int argv_proc_count(const process_t *p)
{
    if (!p)
        return -ESRCH;
    return p->argv.count;
}

int argv_proc_get(const process_t *p, int index, char *buf, size_t buflen)
{
    if (!p)
        return -ESRCH;
    if (!buf || buflen == 0)
        return -EINVAL;
    if (index < 0 || index >= p->argv.count)
        return -ENOENT;

    strncpy(buf, p->argv.args[index], buflen - 1);
    buf[buflen - 1] = '\0';
    return (int)strlen(buf);
}

int argv_copy_from_user(char storage[][PROC_ARGV_MAX], const char **kargv,
                        int argc, long argv_ptr)
{
    int i;

    if (!argv_ptr || argc <= 0)
        return 0;
    if (argc > PROC_ARGC_MAX)
        return -E2BIG;
    if (!storage || !kargv)
        return -EINVAL;

    for (i = 0; i < argc; i++) {
        long uptr;
        int len;

        if (copy_from_user(&uptr, (const void *)(argv_ptr + (long)i * (long)sizeof(long)),
                           sizeof(uptr)) < 0)
            return -EFAULT;

        len = user_strlen((const char *)(uintptr_t)uptr, PROC_ARGV_MAX);
        if (len < 0)
            return -EFAULT;
        if (copy_from_user(storage[i], (const void *)(uintptr_t)uptr, (size_t)len + 1) < 0)
            return -EFAULT;

        kargv[i] = storage[i];
    }
    kargv[argc] = NULL;
    return argc;
}
