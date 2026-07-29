#include <kernel/exec.h>
#include <kernel/dynlib.h>
#include <kernel/argv.h>
#include <kernel/errno.h>
#include <kernel/env.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/initrd.h>
#include <kernel/initrd_store.h>
#include <kernel/vfs.h>
#include <kernel/syscall.h>
#include <kernel/mm.h>
#include <kernel/heap.h>
#include <kernel/vmm.h>
#include <drivers/console/vga.h>
#include <drivers/console/serial.h>

/* ---- ELF32 (userspace ET_EXEC) ------------------------------------------ */

#define EI_NIDENT 16
#define ELFMAG0   0x7f
#define ELFMAG1   'E'
#define ELFMAG2   'L'
#define ELFMAG3   'F'
#define ELFCLASS32 1
#define ELFDATA2LSB 1
#define ET_EXEC   2
#define EM_386    3
#define PT_LOAD   1
#define PT_DYNAMIC 2
#define SHT_STRTAB 3
#define DT_NULL   0
#define DT_NEEDED 1
#define DT_STRTAB 5

typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    int32_t  d_tag;
    uint32_t d_val;
} Elf32_Dyn;

typedef struct {
    uint32_t entry;       /* absolute VA */
    uint32_t load_addr;   /* image base VA */
    uint32_t image_bytes; /* mapped span */
    uint32_t imports_off; /* relative to load_addr; 0 = none */
    uint32_t stack_size;
    char     name[EXEC_NAME_MAX];
    char     needed[EXEC_NEEDED_MAX][DYNLIB_NAME_MAX];
} exec_image_info_t;

static int name_ends_with(const char *name, const char *ext, size_t extlen)
{
    size_t n;
    if (!name || !ext)
        return 0;
    n = strlen(name);
    if (n < extlen)
        return 0;
    return strcmp(name + n - extlen, ext) == 0;
}

static int exec_file_readable(const char *path)
{
    int fd;
    if (!path || !path[0])
        return 0;
    fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    (void)vfs_close(fd);
    return 1;
}

static int exec_try_one(const char *path, char *out, size_t outsz)
{
    if (!path || !out || outsz < 2)
        return -EINVAL;
    if (strlen(path) >= outsz)
        return -ENAMETOOLONG;
    if (!exec_file_readable(path))
        return -ENOENT;
    strcpy(out, path);
    return 0;
}

static int exec_try_path(const char *path, char *out, size_t outsz)
{
    char with_ext[VFS_PATH_MAX];
    size_t plen;
    int rc;

    rc = exec_try_one(path, out, outsz);
    if (rc == 0)
        return 0;

    plen = strlen(path);
    if (!name_ends_with(path, EXEC_EXT, EXEC_EXT_LEN) &&
        !name_ends_with(path, EXEC_EXT_ELF, EXEC_EXT_ELF_LEN)) {
        if (plen + EXEC_EXT_LEN < sizeof(with_ext)) {
            strcpy(with_ext, path);
            strcpy(with_ext + plen, EXEC_EXT);
            rc = exec_try_one(with_ext, out, outsz);
            if (rc == 0)
                return 0;
        }
        if (plen + EXEC_EXT_ELF_LEN < sizeof(with_ext)) {
            strcpy(with_ext, path);
            strcpy(with_ext + plen, EXEC_EXT_ELF);
            rc = exec_try_one(with_ext, out, outsz);
            if (rc == 0)
                return 0;
        }
    }
    return -ENOENT;
}

static int exec_path_has_slash(const char *s)
{
    if (!s)
        return 0;
    while (*s) {
        if (*s == '/')
            return 1;
        s++;
    }
    return 0;
}

int exec_resolve(const char *in, char *out, size_t outsz)
{
    process_t *p = process_current();
    char pathbuf[ENV_VAL_MAX];
    char cand[VFS_PATH_MAX];
    const char *pathenv;
    const char *start;
    const char *colon;
    size_t dirlen;
    int rc;

    if (!in || !in[0] || !out || outsz < 2)
        return -EINVAL;

    if (in[0] == '/' || exec_path_has_slash(in)) {
        if (in[0] == '/') {
            if (strlen(in) >= sizeof(cand))
                return -ENAMETOOLONG;
            strcpy(cand, in);
        } else {
            size_t cl, il;
            p = process_leader(p);
            if (!p)
                return -ESRCH;
            cl = strlen(p->cwd);
            il = strlen(in);
            if (strcmp(p->cwd, "/") == 0) {
                if (1 + il + 1 > sizeof(cand))
                    return -ENAMETOOLONG;
                cand[0] = '/';
                memcpy(cand + 1, in, il + 1);
            } else {
                if (cl + 1 + il + 1 > sizeof(cand))
                    return -ENAMETOOLONG;
                memcpy(cand, p->cwd, cl);
                cand[cl] = '/';
                memcpy(cand + cl + 1, in, il + 1);
            }
        }
        return exec_try_path(cand, out, outsz);
    }

    pathbuf[0] = 0;
    if (env_get(p, "PATH", pathbuf, sizeof(pathbuf)) < 0 || !pathbuf[0])
        strcpy(pathbuf, "/system/bin:/applications");
    pathenv = pathbuf;
    start = pathenv;
    while (*start) {
        colon = start;
        while (*colon && *colon != ':')
            colon++;
        dirlen = (size_t)(colon - start);
        if (dirlen == 0) {
            rc = exec_try_path(in, out, outsz);
            if (rc == 0)
                return 0;
        } else {
            if (dirlen + 1 + strlen(in) + 1 > sizeof(cand))
                return -ENAMETOOLONG;
            memcpy(cand, start, dirlen);
            cand[dirlen] = '/';
            strcpy(cand + dirlen + 1, in);
            rc = exec_try_path(cand, out, outsz);
            if (rc == 0)
                return 0;
        }
        if (!*colon)
            break;
        start = colon + 1;
    }
    return -ENOENT;
}

static void exec_attach_console(pid_t pid, const char *name, uint32_t spawn_flags)
{
    (void)pid;
    (void)name;
    (void)spawn_flags;
}

static const char *exec_path_basename(const char *path)
{
    const char *base = path;
    if (!path)
        return "";
    while (*path) {
        if (*path == '/')
            base = path + 1;
        path++;
    }
    return base;
}

static const uint8_t *exec_initrd_lookup(const char *path, size_t *size_out)
{
    const char *name = exec_path_basename(path);
    size_t size = 0;
    const initrd_header_t *hdr;
    size_t table_bytes;
    uint32_t i;

    if (size_out)
        *size_out = 0;
    if (!name[0])
        return NULL;

    hdr = (const initrd_header_t *)initrd_store_get(&size);
    if (!hdr || size < sizeof(uint32_t) * 2 || hdr->magic != INITRD_MAGIC ||
        hdr->count == 0 || hdr->count > INITRD_MAX_FILES)
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
        if (size_out)
            *size_out = f->size;
        return (const uint8_t *)hdr + f->offset;
    }
    return NULL;
}

static int blob_is_elf(const void *blob, size_t size)
{
    const uint8_t *p = (const uint8_t *)blob;
    if (!p || size < sizeof(Elf32_Ehdr))
        return 0;
    return p[0] == ELFMAG0 && p[1] == ELFMAG1 && p[2] == ELFMAG2 && p[3] == ELFMAG3;
}

static int blob_is_exec(const void *blob, size_t size)
{
    const exec_header_t *h;
    if (!blob || size < sizeof(exec_header_t))
        return 0;
    h = (const exec_header_t *)blob;
    return h->magic == EXEC_MAGIC && h->version == EXEC_VERSION;
}

static void info_set_name(exec_image_info_t *info, const char *name)
{
    size_t i;
    memset(info->name, 0, sizeof(info->name));
    if (!name)
        return;
    for (i = 0; i < EXEC_NAME_MAX - 1 && name[i]; i++)
        info->name[i] = name[i];
}

static int info_add_needed(exec_image_info_t *info, const char *lib)
{
    int i;
    if (!lib || !lib[0])
        return 0;
    for (i = 0; i < EXEC_NEEDED_MAX; i++) {
        if (info->needed[i][0] == 0) {
            strncpy(info->needed[i], lib, DYNLIB_NAME_MAX - 1);
            return 0;
        }
        if (strcmp(info->needed[i], lib) == 0)
            return 0;
    }
    return -1;
}

/* ---- Legacy .exec ------------------------------------------------------- */

static int exec_validate_header(const exec_header_t *hdr, size_t total_size)
{
    uint32_t total;

    if (!hdr || total_size < sizeof(exec_header_t))
        return -1;
    if (hdr->magic != EXEC_MAGIC || hdr->version != EXEC_VERSION)
        return -1;
    if (hdr->header_size != sizeof(exec_header_t))
        return -1;
    if (hdr->load_addr != EXEC_IMAGE_BASE)
        return -1;
    if (hdr->image_size == 0)
        return -1;
    if ((size_t)hdr->header_size + (size_t)hdr->image_size > total_size)
        return -1;
    if (hdr->entry_off >= hdr->image_size + hdr->bss_size)
        return -1;
    total = hdr->image_size + hdr->bss_size;
    if (hdr->image_size + hdr->bss_size < hdr->image_size)
        return -1;
    if (total > 0x04000000u)
        return -1;
    if (hdr->imports_off != 0 && hdr->imports_off >= total)
        return -1;
    return 0;
}

static int exec_load_flat(process_t *child, const uint8_t *img, uint32_t image_size,
                          uint32_t bss_size, uint32_t load_addr)
{
    size_t total = (size_t)image_size + (size_t)bss_size;
    size_t npages = (total + PAGE_SIZE - 1u) / PAGE_SIZE;
    void *pages;
    uint32_t pa;

    if (!child || !child->as || !img)
        return -1;

    pages = mm_alloc_pages(npages);
    if (!pages)
        return -ENOMEM;
    memset(pages, 0, npages * PAGE_SIZE);
    memcpy(pages, img, image_size);

    pa = (uint32_t)(uintptr_t)pages;
    if (vmm_map_pages(child->as, load_addr, pa, npages, VMM_WRITE | VMM_USER) < 0) {
        mm_free_pages(pages, npages);
        return -ENOMEM;
    }

    child->image_pages = pages;
    child->image_npages = npages;
    child->image_bytes = (uint32_t)total;
    child->load_addr = load_addr;
    return 0;
}

/* ---- ELF ET_EXEC -------------------------------------------------------- */

static int elf_parse_needed(const uint8_t *blob, size_t size, const Elf32_Ehdr *eh,
                            exec_image_info_t *info)
{
    const Elf32_Phdr *ph;
    uint32_t i, j;
    const Elf32_Dyn *dyn = NULL;
    uint32_t dyn_bytes = 0;
    const char *strtab = NULL;
    uint32_t strsz = 0;

    if (eh->e_phoff == 0 || eh->e_phentsize != sizeof(Elf32_Phdr))
        return 0;
    if ((size_t)eh->e_phoff + (size_t)eh->e_phnum * sizeof(Elf32_Phdr) > size)
        return -1;

    ph = (const Elf32_Phdr *)(blob + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        if (ph[i].p_type != PT_DYNAMIC)
            continue;
        if (ph[i].p_offset + ph[i].p_filesz > size)
            return -1;
        dyn = (const Elf32_Dyn *)(blob + ph[i].p_offset);
        dyn_bytes = ph[i].p_filesz;
        break;
    }
    if (!dyn)
        return 0;

    for (j = 0; j + sizeof(Elf32_Dyn) <= dyn_bytes; j += sizeof(Elf32_Dyn)) {
        const Elf32_Dyn *d = (const Elf32_Dyn *)((const uint8_t *)dyn + j);
        if (d->d_tag == DT_NULL)
            break;
        if (d->d_tag == DT_STRTAB) {
            /* d_val is VA; convert via PT_LOAD */
            uint32_t k;
            for (k = 0; k < eh->e_phnum; k++) {
                if (ph[k].p_type != PT_LOAD)
                    continue;
                if (d->d_val >= ph[k].p_vaddr &&
                    d->d_val < ph[k].p_vaddr + ph[k].p_filesz) {
                    uint32_t off = ph[k].p_offset + (d->d_val - ph[k].p_vaddr);
                    if (off < size)
                        strtab = (const char *)(blob + off);
                    break;
                }
            }
        }
    }
    if (!strtab)
        return 0;

    /* Bound string table loosely to end of file */
    strsz = (uint32_t)(size - (size_t)((const uint8_t *)strtab - blob));

    for (j = 0; j + sizeof(Elf32_Dyn) <= dyn_bytes; j += sizeof(Elf32_Dyn)) {
        const Elf32_Dyn *d = (const Elf32_Dyn *)((const uint8_t *)dyn + j);
        if (d->d_tag == DT_NULL)
            break;
        if (d->d_tag == DT_NEEDED) {
            if (d->d_val < strsz)
                (void)info_add_needed(info, strtab + d->d_val);
        }
    }
    return 0;
}

static int elf_find_dynimports(const uint8_t *blob, size_t size, const Elf32_Ehdr *eh,
                               uint32_t load_addr, uint32_t *imports_off_out)
{
    const Elf32_Shdr *shdrs;
    const char *shstr;
    uint16_t i;

    *imports_off_out = 0;
    if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf32_Shdr) || eh->e_shnum == 0)
        return 0;
    if ((size_t)eh->e_shoff + (size_t)eh->e_shnum * sizeof(Elf32_Shdr) > size)
        return -1;
    if (eh->e_shstrndx >= eh->e_shnum)
        return 0;

    shdrs = (const Elf32_Shdr *)(blob + eh->e_shoff);
    if (shdrs[eh->e_shstrndx].sh_offset + shdrs[eh->e_shstrndx].sh_size > size)
        return -1;
    shstr = (const char *)(blob + shdrs[eh->e_shstrndx].sh_offset);

    for (i = 0; i < eh->e_shnum; i++) {
        const char *nm;
        if (shdrs[i].sh_name >= shdrs[eh->e_shstrndx].sh_size)
            continue;
        nm = shstr + shdrs[i].sh_name;
        if (strcmp(nm, ".dynimports") != 0)
            continue;
        if (shdrs[i].sh_addr < load_addr)
            return -1;
        *imports_off_out = shdrs[i].sh_addr - load_addr;
        return 0;
    }
    return 0;
}

static int elf_load_into(process_t *child, const uint8_t *blob, size_t size,
                         exec_image_info_t *info)
{
    const Elf32_Ehdr *eh;
    const Elf32_Phdr *ph;
    uint32_t i;
    uint32_t lo = 0xffffffffu, hi = 0;
    int have = 0;
    size_t npages, span;
    void *pages;
    uint8_t *dst;

    if (!child || !child->as || !blob || !info || size < sizeof(Elf32_Ehdr))
        return -ENOEXEC;

    eh = (const Elf32_Ehdr *)blob;
    if (eh->e_ident[0] != ELFMAG0 || eh->e_ident[1] != ELFMAG1 ||
        eh->e_ident[2] != ELFMAG2 || eh->e_ident[3] != ELFMAG3)
        return -ENOEXEC;
    if (eh->e_ident[4] != ELFCLASS32 || eh->e_ident[5] != ELFDATA2LSB)
        return -ENOEXEC;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_386)
        return -ENOEXEC;
    if (eh->e_phoff == 0 || eh->e_phentsize != sizeof(Elf32_Phdr) || eh->e_phnum == 0)
        return -ENOEXEC;
    if ((size_t)eh->e_phoff + (size_t)eh->e_phnum * sizeof(Elf32_Phdr) > size)
        return -ENOEXEC;

    ph = (const Elf32_Phdr *)(blob + eh->e_phoff);
    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t vend;
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        if (ph[i].p_filesz > ph[i].p_memsz)
            return -ENOEXEC;
        if (ph[i].p_offset + ph[i].p_filesz > size)
            return -ENOEXEC;
        if (ph[i].p_vaddr < lo)
            lo = ph[i].p_vaddr & ~(PAGE_SIZE - 1u);
        vend = ph[i].p_vaddr + ph[i].p_memsz;
        if (vend < ph[i].p_vaddr)
            return -ENOEXEC;
        vend = (vend + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
        if (vend > hi)
            hi = vend;
        have = 1;
    }
    if (!have || hi <= lo)
        return -ENOEXEC;
    span = (size_t)(hi - lo);
    if (span > 0x04000000u)
        return -ENOEXEC;

    npages = span / PAGE_SIZE;
    pages = mm_alloc_pages(npages);
    if (!pages)
        return -ENOMEM;
    memset(pages, 0, span);
    dst = (uint8_t *)pages;

    for (i = 0; i < eh->e_phnum; i++) {
        uint32_t off;
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        off = ph[i].p_vaddr - lo;
        if ((size_t)off + ph[i].p_memsz > span) {
            mm_free_pages(pages, npages);
            return -ENOEXEC;
        }
        if (ph[i].p_filesz)
            memcpy(dst + off, blob + ph[i].p_offset, ph[i].p_filesz);
        /* BSS already zero from memset */
    }

    if (vmm_map_pages(child->as, lo, (uint32_t)(uintptr_t)pages, npages,
                      VMM_WRITE | VMM_USER) < 0) {
        mm_free_pages(pages, npages);
        return -ENOMEM;
    }

    child->image_pages = pages;
    child->image_npages = npages;
    child->image_bytes = (uint32_t)span;
    child->load_addr = lo;

    memset(info, 0, sizeof(*info));
    info->entry = eh->e_entry;
    info->load_addr = lo;
    info->image_bytes = (uint32_t)span;
    info->stack_size = PROC_USTACK_DEFAULT;
    if (eh->e_entry < lo || eh->e_entry >= hi)
        return -ENOEXEC; /* image left mapped; caller process_kill tears down */

    (void)elf_parse_needed(blob, size, eh, info);
    if (elf_find_dynimports(blob, size, eh, lo, &info->imports_off) < 0) {
        /* non-fatal: leave imports_off 0 */
        info->imports_off = 0;
    }
    return 0;
}

static int exec_bind_and_finish(process_t *child, const exec_image_info_t *info,
                                uint32_t spawn_flags, const char *const *argv,
                                int argc)
{
    void (*entry)(void);
    pid_t pid;

    if (!child || !info)
        return -1;

    if (dynlib_bind_exec(child, info->needed, EXEC_NEEDED_MAX, info->load_addr,
                         info->imports_off) < 0) {
        klog("[exec] dynamic lib bind failed\n");
        return -ENOENT;
    }

    entry = (void (*)(void))(uintptr_t)info->entry;
    child->user_entry = entry;
    pid = child->pid;

    if (argv && argc > 0) {
        if (argv_proc_set(child, argv, argc) < 0) {
            (void)process_kill(pid);
            return -EINVAL;
        }
    }

    klog("[exec] spawned ");
    klog(info->name[0] ? info->name : "exec");
    klog(" pid=");
    serial_print_uint((uint32_t)pid);
    klog(" entry=");
    serial_print_hex((uint32_t)(uintptr_t)entry);
    klog("\n");

    exec_attach_console(pid, info->name[0] ? info->name : "exec", spawn_flags);
    return pid;
}

static int exec_spawn_elf_blob(const void *blob, size_t size, uint32_t spawn_flags,
                               const char *const *argv, int argc, const char *name_hint)
{
    exec_image_info_t info;
    pid_t pid;
    process_t *child;
    int rc;

    memset(&info, 0, sizeof(info));
    info_set_name(&info, name_hint ? name_hint : "elf");

    /* Create process with placeholder entry; real entry set after load. */
    pid = process_create_user_stack(info.name, (void (*)(void))(uintptr_t)EXEC_IMAGE_BASE,
                                    PROC_USTACK_DEFAULT);
    if (pid < 0)
        return -1;
    child = process_get(pid);
    if (!child)
        return -1;

    if (elf_load_into(child, (const uint8_t *)blob, size, &info) < 0) {
        klog("[exec] ELF load failed\n");
        (void)process_kill(pid);
        return -ENOEXEC;
    }
    if (!info.name[0])
        info_set_name(&info, name_hint);

    klog("[exec] ELF ");
    klog(info.name);
    klog(" @ ");
    serial_print_hex(info.load_addr);
    klog(" entry=");
    serial_print_hex(info.entry);
    klog("\n");

    rc = exec_bind_and_finish(child, &info, spawn_flags, argv, argc);
    if (rc < 0)
        (void)process_kill(pid);
    return rc;
}

static int exec_spawn_container_blob(const void *blob, size_t size, uint32_t spawn_flags,
                                     const char *const *argv, int argc)
{
    const exec_header_t *hdr;
    const uint8_t *img;
    exec_image_info_t info;
    pid_t pid;
    process_t *child;
    int rc, i;

    if (exec_validate_header((const exec_header_t *)blob, size) < 0)
        return -1;

    hdr = (const exec_header_t *)blob;
    memset(&info, 0, sizeof(info));
    info.entry = EXEC_IMAGE_BASE + hdr->entry_off;
    info.load_addr = EXEC_IMAGE_BASE;
    info.image_bytes = hdr->image_size + hdr->bss_size;
    info.imports_off = hdr->imports_off;
    info.stack_size = hdr->stack_size ? hdr->stack_size : PROC_USTACK_DEFAULT;
    info_set_name(&info, hdr->name);
    for (i = 0; i < EXEC_NEEDED_MAX; i++) {
        if (hdr->needed[i][0])
            strncpy(info.needed[i], hdr->needed[i], DYNLIB_NAME_MAX - 1);
    }

    pid = process_create_user_stack(info.name[0] ? info.name : "exec",
                                    (void (*)(void))(uintptr_t)info.entry,
                                    info.stack_size);
    if (pid < 0)
        return -1;
    child = process_get(pid);
    if (!child)
        return -1;

    img = (const uint8_t *)blob + hdr->header_size;
    if (exec_load_flat(child, img, hdr->image_size, hdr->bss_size, EXEC_IMAGE_BASE) < 0) {
        (void)process_kill(pid);
        return -1;
    }

    rc = exec_bind_and_finish(child, &info, spawn_flags, argv, argc);
    if (rc < 0)
        (void)process_kill(pid);
    return rc;
}

int exec_spawn_flags(const void *blob, size_t size, uint32_t spawn_flags,
                     const char *const *argv, int argc)
{
    if (blob_is_elf(blob, size))
        return exec_spawn_elf_blob(blob, size, spawn_flags, argv, argc, "elf");
    if (blob_is_exec(blob, size))
        return exec_spawn_container_blob(blob, size, spawn_flags, argv, argc);
    klog("[exec] unknown binary format\n");
    return -ENOEXEC;
}

int exec_spawn(const void *blob, size_t size)
{
    return exec_spawn_flags(blob, size, SPAWN_CONSOLE_HIDDEN, NULL, 0);
}

static int exec_read_file(const char *path, uint8_t **out_buf, size_t *out_size)
{
    int fd;
    off_t end;
    size_t size, loaded;
    uint8_t *buf;
    ssize_t n;

    *out_buf = NULL;
    *out_size = 0;

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return fd;

    end = vfs_lseek(fd, 0, SEEK_END);
    if (end <= 0) {
        (void)vfs_close(fd);
        return end < 0 ? (int)end : -ENOEXEC;
    }
    if ((uint32_t)end > 0x04000000u) {
        (void)vfs_close(fd);
        return -EOVERFLOW;
    }
    if (vfs_lseek(fd, 0, SEEK_SET) < 0) {
        (void)vfs_close(fd);
        return -EIO;
    }

    size = (size_t)end;
    buf = (uint8_t *)kmalloc(size);
    if (!buf) {
        (void)vfs_close(fd);
        return -ENOMEM;
    }

    loaded = 0;
    while (loaded < size) {
        n = vfs_read(fd, buf + loaded, size - loaded);
        if (n < 0) {
            kfree(buf);
            (void)vfs_close(fd);
            return (int)n;
        }
        if (n == 0)
            break;
        loaded += (size_t)n;
    }
    (void)vfs_close(fd);
    if (loaded != size) {
        kfree(buf);
        return -EIO;
    }
    *out_buf = buf;
    *out_size = size;
    return 0;
}

int exec_spawn_path_flags(const char *path, uint32_t spawn_flags,
                          const char *const *argv, int argc)
{
    const uint8_t *initrd_blob;
    size_t initrd_size = 0;
    uint8_t *buf = NULL;
    size_t size = 0;
    int rc;
    const char *base;

    if (!path || !path[0])
        return -EINVAL;

    base = exec_path_basename(path);

    initrd_blob = exec_initrd_lookup(path, &initrd_size);
    if (initrd_blob)
        return exec_spawn_flags(initrd_blob, initrd_size, spawn_flags, argv, argc);

    rc = exec_read_file(path, &buf, &size);
    if (rc < 0)
        return rc;

    if (blob_is_elf(buf, size))
        rc = exec_spawn_elf_blob(buf, size, spawn_flags, argv, argc, base);
    else if (blob_is_exec(buf, size))
        rc = exec_spawn_container_blob(buf, size, spawn_flags, argv, argc);
    else {
        klog("[exec] not ELF or .exec: ");
        klog(path);
        klog("\n");
        rc = -ENOEXEC;
    }
    kfree(buf);
    return rc;
}

int exec_spawn_path(const char *path)
{
    return exec_spawn_path_flags(path, SPAWN_CONSOLE_HIDDEN, NULL, 0);
}
