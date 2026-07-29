#include <arch/x86/cpu.h>
#include <arch/x86/lapic.h>
#include <kernel/string.h>
#include <kernel/process.h>
#include <drivers/serial.h>

static cpu_t g_cpus[CPU_MAX];
static int   g_cpu_count;
static int   g_bsp_ready;
static cpu_info_t g_cpu_info;
static int   g_cpu_info_ready;

static void cpuid_raw(uint32_t leaf, uint32_t sub,
                      uint32_t *ea, uint32_t *eb, uint32_t *ec, uint32_t *ed)
{
    uint32_t a, b, c, d;

    __asm__ volatile("cpuid"
                     : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                     : "a"(leaf), "c"(sub)
                     : "memory");
    if (ea) *ea = a;
    if (eb) *eb = b;
    if (ec) *ec = c;
    if (ed) *ed = d;
}

void cpu_detect(void)
{
    uint32_t a, b, c, d;
    uint32_t i;

    memset(&g_cpu_info, 0, sizeof(g_cpu_info));

    cpuid_raw(0, 0, &a, &b, &c, &d);
    g_cpu_info.max_leaf = a;
    memcpy(g_cpu_info.vendor + 0, &b, 4);
    memcpy(g_cpu_info.vendor + 4, &d, 4);
    memcpy(g_cpu_info.vendor + 8, &c, 4);
    g_cpu_info.vendor[12] = '\0';

    if (g_cpu_info.max_leaf >= 1) {
        uint32_t fam, model;

        cpuid_raw(1, 0, &a, &b, &c, &d);
        g_cpu_info.stepping = a & 0xF;
        model = (a >> 4) & 0xF;
        fam = (a >> 8) & 0xF;
        if (fam == 0xF)
            fam += (a >> 20) & 0xFF;
        if (fam == 0x6 || fam == 0xF)
            model += ((a >> 16) & 0xF) << 4;
        g_cpu_info.family = fam;
        g_cpu_info.model = model;
        g_cpu_info.features_edx = d;
        g_cpu_info.features_ecx = c;
        g_cpu_info.logical_per_pkg = (b >> 16) & 0xFF;
        g_cpu_info.has_apic = (d & (1u << 9)) ? 1 : 0;
        g_cpu_info.has_htt = (d & (1u << 28)) ? 1 : 0;
    }

    /* Leaf 4: deterministic cache - cores per package (Intel). */
    if (g_cpu_info.max_leaf >= 4) {
        cpuid_raw(4, 0, &a, &b, &c, &d);
        if (a != 0)
            g_cpu_info.cores_per_pkg = ((a >> 26) & 0x3F) + 1;
    }

    /* x2APIC / topology leaf 0xB if present. */
    if (g_cpu_info.max_leaf >= 0xB) {
        uint32_t pkg_cores = 0;
        uint32_t smt = 0;

        for (i = 0; i < 8; i++) {
            uint32_t level_type;

            cpuid_raw(0xB, i, &a, &b, &c, &d);
            if (((b) & 0xFFFF) == 0 && i > 0)
                break;
            level_type = (c >> 8) & 0xFF;
            if (level_type == 1) /* SMT */
                smt = b & 0xFFFF;
            else if (level_type == 2) /* core */
                pkg_cores = b & 0xFFFF;
        }
        if (pkg_cores)
            g_cpu_info.cores_per_pkg = pkg_cores;
        if (smt && g_cpu_info.logical_per_pkg == 0)
            g_cpu_info.logical_per_pkg = smt;
    }

    cpuid_raw(0x80000000u, 0, &a, &b, &c, &d);
    g_cpu_info.max_ext_leaf = a;
    if (a >= 0x80000004u) {
        uint32_t *w = (uint32_t *)g_cpu_info.brand;

        cpuid_raw(0x80000002u, 0, &w[0], &w[1], &w[2], &w[3]);
        cpuid_raw(0x80000003u, 0, &w[4], &w[5], &w[6], &w[7]);
        cpuid_raw(0x80000004u, 0, &w[8], &w[9], &w[10], &w[11]);
        g_cpu_info.brand[48] = '\0';
        /* Trim leading spaces. */
        {
            char *p = g_cpu_info.brand;
            while (*p == ' ')
                p++;
            if (p != g_cpu_info.brand)
                memmove(g_cpu_info.brand, p, strlen(p) + 1);
        }
    }

    if (g_cpu_info.cores_per_pkg == 0)
        g_cpu_info.cores_per_pkg = 1;
    if (g_cpu_info.logical_per_pkg == 0)
        g_cpu_info.logical_per_pkg = 1;

    g_cpu_info_ready = 1;
}

const cpu_info_t *cpu_info(void)
{
    return g_cpu_info_ready ? &g_cpu_info : NULL;
}

void cpu_debug_dump(void)
{
    const cpu_info_t *info = &g_cpu_info;
    int i;

    if (!g_cpu_info_ready)
        cpu_detect();

    klog("[cpu] -------- CPU debug dump --------\n");
    klog("[cpu] vendor=");
    klog(info->vendor);
    klog("\n");
    if (info->brand[0]) {
        klog("[cpu] brand=");
        klog(info->brand);
        klog("\n");
    }
    klog_uint("[cpu] family=", info->family);
    klog_uint("[cpu] model=", info->model);
    klog_uint("[cpu] stepping=", info->stepping);
    klog_uint("[cpu] max_leaf=", info->max_leaf);
    klog_hex("[cpu] features_edx=", info->features_edx);
    klog_hex("[cpu] features_ecx=", info->features_ecx);
    klog_uint("[cpu] has_apic=", (uint32_t)info->has_apic);
    klog_uint("[cpu] has_htt=", (uint32_t)info->has_htt);
    klog_uint("[cpu] logical_per_pkg(cpuid)=", info->logical_per_pkg);
    klog_uint("[cpu] cores_per_pkg(cpuid)=", info->cores_per_pkg);
    klog_uint("[cpu] online_logical=", (uint32_t)g_cpu_count);
    klog_uint("[cpu] CPU_MAX=", CPU_MAX);
    klog("[cpu] model: each online logical CPU runs its own OS thread "
         "(private kstack + thread_regs); scheduler picks Ready threads\n");

    for (i = 0; i < g_cpu_count; i++) {
        cpu_t *c = &g_cpus[i];
        process_t *idle = c->idle;
        process_t *cur = c->current;

        klog("[cpu]   logical id=");
        serial_print_uint((uint32_t)c->id);
        klog(" apic=");
        serial_print_uint((uint32_t)c->apic_id);
        klog(" online=");
        serial_print_uint((uint32_t)c->online);
        klog(" pkg=");
        serial_print_uint((uint32_t)c->package);
        klog(" core=");
        serial_print_uint((uint32_t)c->core);
        klog(" smt=");
        serial_print_uint((uint32_t)c->smt);
        if (idle) {
            klog(" idle_tid=");
            serial_print_uint((uint32_t)idle->tid);
            klog(" idle_name=");
            klog(idle->name);
        }
        if (cur) {
            klog(" current_tid=");
            serial_print_uint((uint32_t)cur->tid);
            klog(" current_name=");
            klog(cur->name);
        } else {
            klog(" current=<none>");
        }
        klog("\n");
    }
    klog("[cpu] --------------------------------\n");
}

void cpu_init_bsp(void)
{
    uint8_t apic;

    memset(g_cpus, 0, sizeof(g_cpus));
    g_cpu_count = 0;
    g_bsp_ready = 0;

    cpu_detect();
    lapic_init_bsp();
    apic = (uint8_t)lapic_id();

    g_cpus[0].id = 0;
    g_cpus[0].apic_id = apic;
    g_cpus[0].online = 1;
    g_cpus[0].package = 0;
    g_cpus[0].core = 0;
    g_cpus[0].smt = 0;
    g_cpus[0].started = 1;
    g_cpus[0].current = NULL;
    g_cpus[0].idle = NULL;
    g_cpu_count = 1;
    g_bsp_ready = 1;

    klog("[cpu] BSP online apic=");
    serial_print_uint((uint32_t)apic);
    klog(" vendor=");
    klog(g_cpu_info.vendor);
    klog("\n");
}

cpu_t *cpu_alloc(uint8_t apic_id)
{
    cpu_t *c;

    if (g_cpu_count >= CPU_MAX)
        return NULL;
    c = &g_cpus[g_cpu_count];
    memset(c, 0, sizeof(*c));
    c->id = g_cpu_count;
    c->apic_id = apic_id;
    /* Flat APIC probe: one package, core == dense id, smt=0 until MADT. */
    c->package = 0;
    c->core = c->id;
    c->smt = 0;
    g_cpu_count++;
    return c;
}

void cpu_rollback_last(void)
{
    if (g_cpu_count <= 1)
        return;
    g_cpu_count--;
    memset(&g_cpus[g_cpu_count], 0, sizeof(g_cpus[0]));
}

cpu_t *cpu_get(int id)
{
    if (id < 0 || id >= g_cpu_count)
        return NULL;
    return &g_cpus[id];
}

cpu_t *cpu_by_apic(uint8_t apic_id)
{
    for (int i = 0; i < g_cpu_count; i++) {
        if (g_cpus[i].apic_id == apic_id)
            return &g_cpus[i];
    }
    return NULL;
}

int cpu_count(void)
{
    return g_cpu_count;
}

int cpu_id(void)
{
    cpu_t *c;

    if (!g_bsp_ready)
        return 0;
    c = cpu_by_apic((uint8_t)lapic_id());
    return c ? c->id : 0;
}

cpu_t *cpu_current(void)
{
    cpu_t *c;

    if (!g_bsp_ready)
        return &g_cpus[0];
    c = cpu_by_apic((uint8_t)lapic_id());
    return c ? c : &g_cpus[0];
}
