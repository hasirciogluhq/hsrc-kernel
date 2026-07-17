#include <drivers/pci.h>
#include <arch/x86/io.h>

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

void pci_init(void)
{
}

uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
    uint32_t addr = (1u << 31) |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) |
                    (offset & 0xFCu);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value)
{
    uint32_t addr = (1u << 31) |
                    ((uint32_t)bus << 16) |
                    ((uint32_t)slot << 11) |
                    ((uint32_t)func << 8) |
                    (offset & 0xFCu);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, value);
}

uint32_t pci_bar_addr(uint32_t bar)
{
    if (bar & 1u)
        return bar & ~0x3u; /* I/O */
    return bar & ~0xFu;     /* MMIO */
}

static void pci_read_device(uint8_t bus, uint8_t slot, uint8_t func, pci_device_t *out)
{
    uint32_t id = pci_config_read(bus, slot, func, 0x00);
    uint32_t classreg = pci_config_read(bus, slot, func, 0x08);
    int i;

    out->bus = bus;
    out->slot = slot;
    out->func = func;
    out->vendor = (uint16_t)(id & 0xFFFF);
    out->device = (uint16_t)(id >> 16);
    out->revision = (uint8_t)(classreg & 0xFF);
    out->prog_if = (uint8_t)((classreg >> 8) & 0xFF);
    out->subclass = (uint8_t)((classreg >> 16) & 0xFF);
    out->class_code = (uint8_t)((classreg >> 24) & 0xFF);

    for (i = 0; i < 6; i++)
        out->bar[i] = pci_config_read(bus, slot, func, (uint8_t)(0x10 + i * 4));
}

int pci_enumerate(pci_enum_fn fn, void *ctx)
{
    int count = 0;
    uint8_t bus, slot, func;

    if (!fn)
        return -1;

    for (bus = 0; bus < 8; bus++) {
        for (slot = 0; slot < 32; slot++) {
            uint32_t id = pci_config_read(bus, slot, 0, 0x00);
            uint16_t vendor = (uint16_t)(id & 0xFFFF);
            uint8_t header;
            uint8_t max_func = 1;

            if (vendor == PCI_VENDOR_INVALID)
                continue;

            header = (uint8_t)(pci_config_read(bus, slot, 0, 0x0C) >> 16);
            if (header & 0x80)
                max_func = 8;

            for (func = 0; func < max_func; func++) {
                pci_device_t dev;
                id = pci_config_read(bus, slot, func, 0x00);
                vendor = (uint16_t)(id & 0xFFFF);
                if (vendor == PCI_VENDOR_INVALID)
                    continue;
                pci_read_device(bus, slot, func, &dev);
                count++;
                if (fn(&dev, ctx) != 0)
                    return count;
            }
        }
    }
    return count;
}

typedef struct {
    uint16_t vendor;
    uint16_t device;
    pci_device_t *out;
    int found;
} pci_find_ctx_t;

static int pci_find_cb(const pci_device_t *dev, void *ctx)
{
    pci_find_ctx_t *c = (pci_find_ctx_t *)ctx;
    if (dev->vendor == c->vendor && dev->device == c->device) {
        *c->out = *dev;
        c->found = 1;
        return 1;
    }
    return 0;
}

int pci_find(uint16_t vendor, uint16_t device, pci_device_t *out)
{
    pci_find_ctx_t ctx;
    if (!out)
        return -1;
    ctx.vendor = vendor;
    ctx.device = device;
    ctx.out = out;
    ctx.found = 0;
    pci_enumerate(pci_find_cb, &ctx);
    return ctx.found ? 0 : -1;
}

typedef struct {
    uint8_t class_code;
    uint8_t subclass;
    pci_device_t *out;
    int found;
} pci_class_ctx_t;

static int pci_class_cb(const pci_device_t *dev, void *ctx)
{
    pci_class_ctx_t *c = (pci_class_ctx_t *)ctx;
    if (dev->class_code == c->class_code && dev->subclass == c->subclass) {
        *c->out = *dev;
        c->found = 1;
        return 1;
    }
    return 0;
}

int pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out)
{
    pci_class_ctx_t ctx;
    if (!out)
        return -1;
    ctx.class_code = class_code;
    ctx.subclass = subclass;
    ctx.out = out;
    ctx.found = 0;
    pci_enumerate(pci_class_cb, &ctx);
    return ctx.found ? 0 : -1;
}
