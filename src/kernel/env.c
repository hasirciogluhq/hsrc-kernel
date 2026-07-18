#include <kernel/env.h>
#include <kernel/errno.h>
#include <kernel/heap.h>
#include <kernel/initrd.h>
#include <kernel/initrd_store.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <drivers/serial.h>

static env_entry_t g_global[ENV_GLOBAL_MAX];
static int         g_global_count;

static int env_name_valid(const char *key)
{
    size_t n;
    size_t i;

    if (!key || !key[0])
        return 0;
    n = strlen(key);
    if (n >= ENV_KEY_MAX)
        return 0;
    for (i = 0; i < n; i++) {
        char c = key[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '_')
            continue;
        return 0;
    }
    return 1;
}

static int env_val_valid(const char *val)
{
    if (!val)
        return 0;
    return strlen(val) < ENV_VAL_MAX;
}

static int env_find_index(const env_entry_t *table, int count, const char *key)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(table[i].key, key) == 0)
            return i;
    }
    return -1;
}

static int env_table_set(env_entry_t *table, int *count, int max, const char *key, const char *val)
{
    int idx;

    if (!env_name_valid(key) || !env_val_valid(val))
        return -EINVAL;

    idx = env_find_index(table, *count, key);
    if (idx >= 0) {
        strncpy(table[idx].val, val, ENV_VAL_MAX - 1);
        table[idx].val[ENV_VAL_MAX - 1] = '\0';
        return 0;
    }

    if (*count >= max)
        return -ENOSPC;

    memset(&table[*count], 0, sizeof(table[0]));
    strncpy(table[*count].key, key, ENV_KEY_MAX - 1);
    strncpy(table[*count].val, val, ENV_VAL_MAX - 1);
    (*count)++;
    return 0;
}

static int env_table_unset(env_entry_t *table, int *count, const char *key)
{
    int idx;
    int last;

    if (!env_name_valid(key))
        return -EINVAL;

    idx = env_find_index(table, *count, key);
    if (idx < 0)
        return -ENOENT;

    last = *count - 1;
    if (idx != last)
        table[idx] = table[last];
    memset(&table[last], 0, sizeof(table[0]));
    (*count)--;
    return 0;
}

static int env_table_get(const env_entry_t *table, int count, const char *key,
                         char *val, size_t valsz)
{
    int idx;

    if (!val || valsz == 0)
        return -EINVAL;

    idx = env_find_index(table, count, key);
    if (idx < 0)
        return -ENOENT;

    strncpy(val, table[idx].val, valsz - 1);
    val[valsz - 1] = '\0';
    return (int)strlen(val);
}

static char *env_find_eq(char *s)
{
    while (*s) {
        if (*s == '=')
            return s;
        s++;
    }
    return NULL;
}

static int env_parse_line(char *line, int global)
{
    char *eq;
    char *key;
    char *val;

    while (*line == ' ' || *line == '\t')
        line++;
    if (!*line || *line == '#')
        return 0;

    eq = env_find_eq(line);
    if (!eq)
        return -EINVAL;

    *eq = '\0';
    key = line;
    val = eq + 1;
    while (*val == ' ' || *val == '\t')
        val++;

    if (!env_name_valid(key) || !env_val_valid(val))
        return -EINVAL;

    if (global)
        return env_table_set(g_global, &g_global_count, ENV_GLOBAL_MAX, key, val);
    return 0;
}

static int env_parse_buffer(char *buf, size_t len, int global)
{
    size_t i = 0;
    int rc = 0;

    while (i < len) {
        size_t start = i;
        char line[ENV_KEY_MAX + ENV_VAL_MAX + 4];

        while (i < len && buf[i] != '\n' && buf[i] != '\r')
            i++;
        if (i > start) {
            size_t n = i - start;
            if (n >= sizeof(line))
                n = sizeof(line) - 1;
            memcpy(line, buf + start, n);
            line[n] = '\0';
            rc = env_parse_line(line, global);
            if (rc < 0)
                return rc;
        }
        while (i < len && (buf[i] == '\n' || buf[i] == '\r'))
            i++;
    }

    return rc;
}

static const uint8_t *env_initrd_find(const char *name, size_t *size_out)
{
    size_t size = 0;
    const initrd_header_t *hdr;
    size_t table_bytes;
    uint32_t i;

    if (!name || !size_out)
        return NULL;

    {
        const void *base = initrd_store_get(&size);
        hdr = (const initrd_header_t *)base;
        if (!hdr || size < sizeof(uint32_t) * 2)
            return NULL;
        if (hdr->magic != INITRD_MAGIC || hdr->count == 0 || hdr->count > INITRD_MAX_FILES)
            return NULL;

        table_bytes = sizeof(uint32_t) * 2 + (size_t)hdr->count * sizeof(initrd_file_t);
        if (size < table_bytes)
            return NULL;

        for (i = 0; i < hdr->count; i++) {
            const initrd_file_t *f = &hdr->files[i];

            if (strcmp(f->name, name) != 0)
                continue;
            if (f->size == 0 || f->offset + f->size > size)
                return NULL;
            *size_out = f->size;
            return (const uint8_t *)base + f->offset;
        }
    }

    return NULL;
}

void env_init(void)
{
    g_global_count = 0;
    memset(g_global, 0, sizeof(g_global));
    (void)env_table_set(g_global, &g_global_count, ENV_GLOBAL_MAX,
                        "PATH", "/usr/bin:/applications");
}

void env_proc_clear(process_t *p)
{
    if (!p)
        return;
    p->env.count = 0;
    memset(p->env.entries, 0, sizeof(p->env.entries));
}

void env_inherit(process_t *child, const process_t *parent)
{
    int i;

    if (!child)
        return;

    env_proc_clear(child);
    for (i = 0; i < g_global_count; i++) {
        (void)env_table_set(child->env.entries, &child->env.count, ENV_PROC_MAX,
                            g_global[i].key, g_global[i].val);
    }
    if (!parent)
        return;
    for (i = 0; i < parent->env.count; i++) {
        (void)env_table_set(child->env.entries, &child->env.count, ENV_PROC_MAX,
                            parent->env.entries[i].key, parent->env.entries[i].val);
    }
}

int env_get(const process_t *p, const char *key, char *val, size_t valsz)
{
    p = process_leader((process_t *)p);
    if (!p)
        return -ESRCH;
    return env_table_get(p->env.entries, p->env.count, key, val, valsz);
}

int env_set(process_t *p, const char *key, const char *val, int global)
{
    if (global)
        return env_table_set(g_global, &g_global_count, ENV_GLOBAL_MAX, key, val);
    p = process_leader(p);
    if (!p)
        return -ESRCH;
    return env_table_set(p->env.entries, &p->env.count, ENV_PROC_MAX, key, val);
}

int env_unset(process_t *p, const char *key, int global)
{
    if (global)
        return env_table_unset(g_global, &g_global_count, key);
    p = process_leader(p);
    if (!p)
        return -ESRCH;
    return env_table_unset(p->env.entries, &p->env.count, key);
}

int env_load_file(const char *path)
{
    int fd;
    off_t end;
    ssize_t total = 0;
    char *buf;
    int rc;

    if (!path || !path[0])
        return -EINVAL;

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return fd;

    end = vfs_lseek(fd, 0, SEEK_END);
    if (end < 0) {
        (void)vfs_close(fd);
        return (int)end;
    }
    if (end == 0) {
        (void)vfs_close(fd);
        return 0;
    }
    if (vfs_lseek(fd, 0, SEEK_SET) < 0) {
        (void)vfs_close(fd);
        return -EIO;
    }

    buf = (char *)kmalloc((size_t)end + 1);
    if (!buf) {
        (void)vfs_close(fd);
        return -ENOMEM;
    }

    while (total < end) {
        ssize_t n = vfs_read(fd, buf + total, (size_t)(end - total));
        if (n < 0) {
            rc = (int)n;
            kfree(buf);
            (void)vfs_close(fd);
            return rc;
        }
        if (n == 0)
            break;
        total += n;
    }
    (void)vfs_close(fd);

    buf[total] = '\0';
    rc = env_parse_buffer(buf, (size_t)total, 1);
    kfree(buf);
    return rc;
}

int env_load_initrd(void)
{
    static const char *const k_names[] = { "environment", "etc-environment", NULL };
    const uint8_t *blob;
    size_t size = 0;
    char *buf;
    int rc = 0;
    int i;

    for (i = 0; k_names[i]; i++) {
        blob = env_initrd_find(k_names[i], &size);
        if (blob)
            break;
    }
    if (!blob || size == 0)
        return 0;

    buf = (char *)kmalloc(size + 1);
    if (!buf)
        return -ENOMEM;
    memcpy(buf, blob, size);
    buf[size] = '\0';
    rc = env_parse_buffer(buf, size, 1);
    kfree(buf);

    if (rc == 0) {
        klog("[env] loaded initrd environment (");
        serial_print_uint((uint32_t)size);
        klog(" bytes)\n");
    }
    return rc;
}
