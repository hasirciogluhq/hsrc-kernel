#ifndef MYKERNEL_KERNEL_UACCESS_H
#define MYKERNEL_KERNEL_UACCESS_H

#include <kernel/types.h>

/* Flat identity map for now — paging will replace bodies later. */
int  copy_from_user(void *dst, const void *src, size_t n);
int  copy_to_user(void *dst, const void *src, size_t n);
int  user_strlen(const char *s, size_t max);

#endif
