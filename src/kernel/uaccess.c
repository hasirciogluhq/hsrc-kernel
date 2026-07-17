#include <kernel/uaccess.h>
#include <kernel/string.h>

int copy_from_user(void *dst, const void *src, size_t n)
{
    if (!dst || (!src && n) || n == 0)
        return n == 0 ? 0 : -1;
    memcpy(dst, src, n);
    return 0;
}

int copy_to_user(void *dst, const void *src, size_t n)
{
    if ((!dst && n) || !src || n == 0)
        return n == 0 ? 0 : -1;
    memcpy(dst, src, n);
    return 0;
}

int user_strlen(const char *s, size_t max)
{
    size_t i;
    if (!s)
        return -1;
    for (i = 0; i < max; i++) {
        if (s[i] == '\0')
            return (int)i;
    }
    return -1;
}
