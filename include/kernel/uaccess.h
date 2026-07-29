#ifndef KERNEL_UACCESS_H
#define KERNEL_UACCESS_H

#include <kernel/types.h>

/* Runs with the current process CR3; user VAs are valid for the active AS. */
int  copy_from_user(void *dst, const void *src, size_t n);
int  copy_to_user(void *dst, const void *src, size_t n);
int  user_strlen(const char *s, size_t max);

#endif
