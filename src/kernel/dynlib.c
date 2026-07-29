#include <kernel/dynlib.h>
#include <kernel/errno.h>
#include <kernel/heap.h>
#include <kernel/mm.h>
#include <kernel/process.h>
#include <kernel/string.h>
#include <kernel/vfs.h>
#include <kernel/vmm.h>
#include <drivers/console/vga.h>
#include <drivers/console/serial.h>

#define EI_NIDENT 16
#define ET_REL    1
#define EM_386    3
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_REL    9
#define SHT_NOBITS 8
#define SHN_UNDEF  0
#define SHN_ABS    0xFFF1
#define SHN_COMMON 0xFFF2
#define SHF_ALLOC  0x2
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC   2
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_R_SYM(i)  ((i) >> 8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))
#define R_386_NONE 0
#define R_386_32   1
#define R_386_PC32 2

#define DYNLIB_MAX_SH 64

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
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

static dynlib_t g_dynlib[DYNLIB_MAX];
static size_t g_dynlib_count;

static int elf_ok(const Elf32_Ehdr *eh, size_t size)
{
    if (size < sizeof(Elf32_Ehdr))
        return 0;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F')
        return 0;
    if (eh->e_ident[4] != 1)
        return 0;
    if (eh->e_type != ET_REL || eh->e_machine != EM_386)
        return 0;
    if (eh->e_shoff == 0 || eh->e_shentsize != sizeof(Elf32_Shdr))
        return 0;
    if ((size_t)eh->e_shoff + (size_t)eh->e_shnum * sizeof(Elf32_Shdr) > size)
        return 0;
    return 1;
}

static uintptr_t align_up(uintptr_t v, uint32_t a)
{
    if (a <= 1)
        return v;
    return (v + (a - 1)) & ~(uintptr_t)(a - 1);
}

static void *sym_addr(Elf32_Sym *sym, uint8_t **sec_base)
{
    if (sym->st_shndx == SHN_UNDEF)
        return NULL;
    if (sym->st_shndx == SHN_ABS)
        return (void *)(uintptr_t)sym->st_value;
    if (sym->st_shndx == SHN_COMMON)
        return NULL;
    if (!sec_base[sym->st_shndx])
        return NULL;
    return sec_base[sym->st_shndx] + sym->st_value;
}

static const char *sym_name(Elf32_Sym *sym, const char *strtab)
{
    if (!strtab || !sym->st_name)
        return "";
    return strtab + sym->st_name;
}

static int name_eq_lib(const char *a, const char *b)
{
    size_t na, nb;
    if (!a || !b)
        return 0;
    if (strcmp(a, b) == 0)
        return 1;
    na = strlen(a);
    nb = strlen(b);
    /* "libfs" == "libfs.dynlib" */
    if (na + DYNLIB_EXT_LEN == nb && strncmp(a, b, na) == 0 &&
        strcmp(b + na, DYNLIB_EXT) == 0)
        return 1;
    if (nb + DYNLIB_EXT_LEN == na && strncmp(b, a, nb) == 0 &&
        strcmp(a + nb, DYNLIB_EXT) == 0)
        return 1;
    return 0;
}

dynlib_t *dynlib_find(const char *name)
{
    size_t i;
    if (!name || !name[0])
        return NULL;
    for (i = 0; i < g_dynlib_count; i++) {
        if (g_dynlib[i].loaded && name_eq_lib(g_dynlib[i].name, name))
            return &g_dynlib[i];
    }
    return NULL;
}

void *dynlib_lookup(const char *sym)
{
    size_t i;
    int j;
    if (!sym || !sym[0])
        return NULL;
    for (i = 0; i < g_dynlib_count; i++) {
        dynlib_t *m = &g_dynlib[i];
        if (!m->loaded)
            continue;
        for (j = 0; j < m->nsyms; j++) {
            if (strcmp(m->syms[j].name, sym) == 0)
                return m->syms[j].addr;
        }
    }
    return NULL;
}

static void *dynlib_lookup_undef(const char *nm)
{
    void *p = dynlib_lookup(nm);
    return p;
}

static int dynlib_register_exports(dynlib_t *slot, Elf32_Sym *symtab, uint32_t nsyms,
                                const char *strtab, uint8_t **sec_base)
{
    uint32_t i;
    slot->nsyms = 0;
    for (i = 0; i < nsyms; i++) {
        Elf32_Sym *sym = &symtab[i];
        const char *nm;
        void *addr;
        uint8_t bind = ELF32_ST_BIND(sym->st_info);
        uint8_t type = ELF32_ST_TYPE(sym->st_info);

        if (bind == STB_LOCAL)
            continue;
        if (sym->st_shndx == SHN_UNDEF || sym->st_shndx == SHN_COMMON)
            continue;
        if (type != STT_FUNC && type != STT_OBJECT && type != STT_NOTYPE)
            continue;
        nm = sym_name(sym, strtab);
        if (!nm[0] || nm[0] == '.')
            continue;
        addr = sym_addr(sym, sec_base);
        if (!addr)
            continue;
        if (slot->nsyms >= DYNLIB_SYMS_MAX)
            return -1;
        strncpy(slot->syms[slot->nsyms].name, nm, sizeof(slot->syms[0].name) - 1);
        slot->syms[slot->nsyms].name[sizeof(slot->syms[0].name) - 1] = 0;
        slot->syms[slot->nsyms].addr = addr;
        slot->nsyms++;
    }
    return 0;
}

static int dynlib_load_blob(const char *name, const void *data, size_t size)
{
    const Elf32_Ehdr *eh;
    Elf32_Shdr *shdrs;
    uint8_t *sec_base[DYNLIB_MAX_SH];
    Elf32_Sym *symtab = NULL;
    const char *strtab = NULL;
    uint32_t nsyms = 0;
    uint16_t i, j;
    size_t image_size = 0;
    size_t max_align = 16;
    size_t off;
    uint8_t *image = NULL;
    dynlib_t *slot;

    if (!data || size == 0 || g_dynlib_count >= DYNLIB_MAX)
        return -ENOMEM;

    if (dynlib_find(name))
        return 0;

    eh = (const Elf32_Ehdr *)data;
    if (!elf_ok(eh, size)) {
        klog("[dynlib] bad ELF\n");
        return -ENOEXEC;
    }
    if (eh->e_shnum > DYNLIB_MAX_SH) {
        klog("[dynlib] too many sections\n");
        return -ENOEXEC;
    }

    shdrs = (Elf32_Shdr *)((const uint8_t *)data + eh->e_shoff);
    memset(sec_base, 0, sizeof(sec_base));

    for (i = 0; i < eh->e_shnum; i++) {
        size_t aligned, next;
        uint32_t a;
        if (!(shdrs[i].sh_flags & SHF_ALLOC))
            continue;
        a = shdrs[i].sh_addralign;
        if (a > 0x10000u)
            return -ENOEXEC;
        if (a > 1 && (a & (a - 1u)) != 0)
            return -ENOEXEC;
        if (a > max_align)
            max_align = a;
        aligned = (size_t)align_up((uintptr_t)image_size, a);
        if (aligned < image_size || aligned > (size_t)-1 - shdrs[i].sh_size)
            return -ENOEXEC;
        next = aligned + shdrs[i].sh_size;
        image_size = next;
    }

    image = (uint8_t *)kmalloc_aligned(image_size ? image_size : 1, max_align);
    if (!image)
        return -ENOMEM;
    memset(image, 0, image_size);

    off = 0;
    for (i = 0; i < eh->e_shnum; i++) {
        size_t sec_end;
        if (!(shdrs[i].sh_flags & SHF_ALLOC))
            continue;
        off = (size_t)align_up((uintptr_t)off, shdrs[i].sh_addralign);
        if (off > image_size || shdrs[i].sh_size > image_size - off) {
            kfree(image);
            return -ENOEXEC;
        }
        sec_base[i] = image + off;
        sec_end = off + shdrs[i].sh_size;
        if (shdrs[i].sh_type != SHT_NOBITS) {
            if (shdrs[i].sh_offset + shdrs[i].sh_size > size) {
                kfree(image);
                return -ENOEXEC;
            }
            memcpy(sec_base[i], (const uint8_t *)data + shdrs[i].sh_offset,
                   shdrs[i].sh_size);
        }
        off = sec_end;
    }

    for (i = 0; i < eh->e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_SYMTAB) {
            symtab = (Elf32_Sym *)((const uint8_t *)data + shdrs[i].sh_offset);
            nsyms = shdrs[i].sh_size / sizeof(Elf32_Sym);
            strtab = (const char *)data + shdrs[shdrs[i].sh_link].sh_offset;
            break;
        }
    }
    if (!symtab) {
        klog("[dynlib] no symtab\n");
        kfree(image);
        return -ENOEXEC;
    }

    /* Relocate against image base (rebase) + previously loaded .dynlib exports. */
    for (i = 0; i < eh->e_shnum; i++) {
        Elf32_Rel *rels;
        uint32_t nrels;
        uint16_t target;

        if (shdrs[i].sh_type != SHT_REL)
            continue;
        target = (uint16_t)shdrs[i].sh_info;
        if (!sec_base[target])
            continue;

        rels = (Elf32_Rel *)((const uint8_t *)data + shdrs[i].sh_offset);
        nrels = shdrs[i].sh_size / sizeof(Elf32_Rel);

        for (j = 0; j < nrels; j++) {
            uint32_t sym_idx = ELF32_R_SYM(rels[j].r_info);
            uint32_t type = ELF32_R_TYPE(rels[j].r_info);
            Elf32_Sym *sym = &symtab[sym_idx];
            uint32_t *loc = (uint32_t *)(sec_base[target] + rels[j].r_offset);
            void *S = NULL;
            uint32_t A;
            uintptr_t P;

            if (sym->st_shndx == SHN_UNDEF) {
                const char *nm = sym_name(sym, strtab);
                S = dynlib_lookup_undef(nm);
                if (!S) {
                    klog("[dynlib] unresolved ");
                    klog(nm);
                    klog("\n");
                    kfree(image);
                    return -ENOENT;
                }
            } else {
                S = sym_addr(sym, sec_base);
                if (!S && ELF32_ST_TYPE(sym->st_info) != STT_NOTYPE) {
                    kfree(image);
                    return -ENOEXEC;
                }
            }

            if ((uintptr_t)loc < (uintptr_t)image ||
                (uintptr_t)loc + sizeof(uint32_t) > (uintptr_t)image + image_size) {
                kfree(image);
                return -ENOEXEC;
            }

            A = *loc;
            P = (uintptr_t)loc;
            switch (type) {
            case R_386_NONE:
                break;
            case R_386_32:
                *loc = (uint32_t)(uintptr_t)S + A;
                break;
            case R_386_PC32:
                *loc = (uint32_t)((uintptr_t)S + A - P);
                break;
            default:
                klog("[dynlib] bad reloc type\n");
                kfree(image);
                return -ENOEXEC;
            }
        }
    }

    slot = &g_dynlib[g_dynlib_count];
    memset(slot, 0, sizeof(*slot));
    if (name)
        strncpy(slot->name, name, DYNLIB_NAME_MAX - 1);
    else
        strncpy(slot->name, "dynlib", DYNLIB_NAME_MAX - 1);
    slot->base = image;
    slot->size = image_size;

    if (dynlib_register_exports(slot, symtab, nsyms, strtab, sec_base) < 0) {
        kfree(image);
        return -ENOMEM;
    }

    slot->loaded = 1;
    slot->refcount = 1;
    g_dynlib_count++;

    klog("[dynlib] loaded ");
    klog(slot->name);
    klog(" @ ");
    serial_print_hex((uint32_t)(uintptr_t)image);
    klog(" size=");
    serial_print_uint((uint32_t)image_size);
    klog(" syms=");
    serial_print_uint((uint32_t)slot->nsyms);
    klog("\n");
    return 0;
}

static const char *basename_of(const char *path)
{
    const char *p;
    if (!path)
        return "dynlib";
    p = path;
    while (*path) {
        if (*path == '/')
            p = path + 1;
        path++;
    }
    return p;
}

int dynlib_load_path(const char *path)
{
    int fd;
    off_t end;
    ssize_t n;
    size_t loaded;
    uint8_t *buf;
    int rc;
    const char *base;

    if (!path || !path[0])
        return -EINVAL;

    base = basename_of(path);
    if (dynlib_find(base)) {
        dynlib_t *m = dynlib_find(base);
        if (m)
            m->refcount++;
        return 0;
    }

    fd = vfs_open(path, O_RDONLY);
    if (fd < 0)
        return fd;

    end = vfs_lseek(fd, 0, SEEK_END);
    if (end <= 0) {
        (void)vfs_close(fd);
        return end < 0 ? (int)end : -ENOEXEC;
    }
    if (vfs_lseek(fd, 0, SEEK_SET) < 0) {
        (void)vfs_close(fd);
        return -EIO;
    }

    buf = (uint8_t *)kmalloc((size_t)end);
    if (!buf) {
        (void)vfs_close(fd);
        return -ENOMEM;
    }

    loaded = 0;
    while (loaded < (size_t)end) {
        n = vfs_read(fd, buf + loaded, (size_t)end - loaded);
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

    if (loaded == 0) {
        kfree(buf);
        return -ENOEXEC;
    }

    rc = dynlib_load_blob(base, buf, loaded);
    kfree(buf);
    return rc;
}

int dynlib_ensure(const char *name)
{
    char path[VFS_PATH_MAX];
    dynlib_t *m;
    size_t n;

    if (!name || !name[0])
        return -EINVAL;

    m = dynlib_find(name);
    if (m) {
        m->refcount++;
        klog("[dynlib] reuse ");
        klog(m->name);
        klog(" refs=");
        serial_print_uint((uint32_t)m->refcount);
        klog("\n");
        return 0;
    }

    n = strlen(name);
    if (n + 1 >= sizeof(path))
        return -ENAMETOOLONG;

    /* Prefer /system/lib/<name>, append .dynlib if missing. */
    strcpy(path, DYNLIB_DIR);
    if (strlen(path) + 1 + n + DYNLIB_EXT_LEN + 1 >= sizeof(path))
        return -ENAMETOOLONG;
    {
        size_t plen = strlen(path);
        path[plen] = '/';
        strcpy(path + plen + 1, name);
    }
    if (n < DYNLIB_EXT_LEN || strcmp(name + n - DYNLIB_EXT_LEN, DYNLIB_EXT) != 0) {
        size_t plen = strlen(path);
        if (plen + DYNLIB_EXT_LEN >= sizeof(path))
            return -ENAMETOOLONG;
        strcpy(path + plen, DYNLIB_EXT);
    }

    return dynlib_load_path(path);
}

static int dynlib_map_into_as(addrspace_t *as, dynlib_t *m)
{
    uint32_t base, end, pa;
    size_t npages;

    if (!as || !m || !m->base || m->size == 0)
        return -1;
    base = (uint32_t)(uintptr_t)m->base & ~(PAGE_SIZE - 1u);
    end = ((uint32_t)(uintptr_t)m->base + (uint32_t)m->size + PAGE_SIZE - 1u) &
          ~(PAGE_SIZE - 1u);
    npages = (end - base) / PAGE_SIZE;
    pa = base; /* identity */
    return vmm_map_pages(as, base, pa, npages, VMM_WRITE | VMM_USER);
}

int dynlib_bind_exec(process_t *proc, const char needed[][DYNLIB_NAME_MAX],
                     int needed_count, uint32_t load_addr, uint32_t imports_off)
{
    int i;
    const dynlib_import_t *imp;
    uint32_t guard = 0;
    uint8_t *image_pa;
    addrspace_t *as;

    if (!proc || !proc->as || !proc->image_pages)
        return -EINVAL;
    as = proc->as;
    image_pa = (uint8_t *)proc->image_pages;

    if (needed && needed_count > 0) {
        for (i = 0; i < needed_count; i++) {
            if (!needed[i][0])
                continue;
            if (dynlib_ensure(needed[i]) < 0) {
                klog("[dynlib] ensure failed: ");
                klog(needed[i]);
                klog("\n");
                return -ENOENT;
            }
        }
    }

    /* Mark every loaded dynlib executable/readable from this process. */
    for (i = 0; i < (int)g_dynlib_count; i++) {
        if (!g_dynlib[i].loaded)
            continue;
        if (dynlib_map_into_as(as, &g_dynlib[i]) < 0) {
            klog("[dynlib] map into AS failed\n");
            return -ENOMEM;
        }
    }

    if (imports_off == 0)
        return 0;

    /* Import table lives in the image; resolve via physical backing. */
    imp = (const dynlib_import_t *)(image_pa + imports_off);
    while (imp->lib[0] && guard < 64) {
        void *addr = dynlib_lookup(imp->sym);
        void **slot;
        uint32_t slot_off;

        if (!addr) {
            klog("[dynlib] bind missing ");
            klog(imp->sym);
            klog("\n");
            return -ENOENT;
        }
        if (imp->slot_addr < load_addr) {
            klog("[dynlib] bad import slot\n");
            return -EFAULT;
        }
        slot_off = imp->slot_addr - load_addr;
        if (slot_off + sizeof(void *) > proc->image_bytes) {
            klog("[dynlib] import slot OOB\n");
            return -EFAULT;
        }
        slot = (void **)(image_pa + slot_off);
        *slot = addr;
        imp++;
        guard++;
    }
    return 0;
}
