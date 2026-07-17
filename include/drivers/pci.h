#ifndef MYKERNEL_DRIVERS_PCI_H
#define MYKERNEL_DRIVERS_PCI_H

#include <kernel/types.h>

#define PCI_VENDOR_INVALID  0xFFFF

typedef struct pci_device {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  func;
    uint16_t vendor;
    uint16_t device;
    uint8_t  class_code;
    uint8_t  subclass;
    uint8_t  prog_if;
    uint8_t  revision;
    uint32_t bar[6];
} pci_device_t;

typedef int (*pci_enum_fn)(const pci_device_t *dev, void *ctx);

void     pci_init(void);
uint32_t pci_config_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void     pci_config_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint32_t pci_bar_addr(uint32_t bar);
int      pci_find(uint16_t vendor, uint16_t device, pci_device_t *out);
int      pci_find_class(uint8_t class_code, uint8_t subclass, pci_device_t *out);
int      pci_enumerate(pci_enum_fn fn, void *ctx);

#endif
