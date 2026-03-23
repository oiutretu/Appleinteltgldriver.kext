/*
 * linux_pci.h - Linux PCI subsystem for macOS
 * 
 * This header provides macOS IOPCIDevice equivalents for Linux PCI functions.
 */

#ifndef LINUX_PCI_H
#define LINUX_PCI_H

#include "linux_types.h"
#include <IOKit/pci/IOPCIDevice.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PCI Power Management States */
typedef enum {
    PCI_D0 = 0,
    PCI_D1 = 1,
    PCI_D2 = 2,
    PCI_D3hot = 3,
    PCI_D3cold = 4,
    PCI_UNKNOWN = 5
} pci_power_t;

/* PCI Configuration Space Registers */
#define PCI_VENDOR_ID           0x00    /* 16 bits */
#define PCI_DEVICE_ID           0x02    /* 16 bits */
#define PCI_COMMAND             0x04    /* 16 bits */
#define PCI_STATUS              0x06    /* 16 bits */
#define PCI_REVISION_ID         0x08    /* 8 bits  */
#define PCI_CLASS_PROG          0x09    /* 8 bits  */
#define PCI_CLASS_DEVICE        0x0a    /* 16 bits */
#define PCI_CACHE_LINE_SIZE     0x0c    /* 8 bits  */
#define PCI_LATENCY_TIMER       0x0d    /* 8 bits  */
#define PCI_HEADER_TYPE         0x0e    /* 8 bits  */
#define PCI_BIST                0x0f    /* 8 bits  */
#define PCI_BASE_ADDRESS_0      0x10    /* 32 bits */
#define PCI_BASE_ADDRESS_1      0x14    /* 32 bits */
#define PCI_BASE_ADDRESS_2      0x18    /* 32 bits */
#define PCI_BASE_ADDRESS_3      0x1c    /* 32 bits */
#define PCI_BASE_ADDRESS_4      0x20    /* 32 bits */
#define PCI_BASE_ADDRESS_5      0x24    /* 32 bits */
#define PCI_CARDBUS_CIS         0x28
#define PCI_SUBSYSTEM_VENDOR_ID 0x2c
#define PCI_SUBSYSTEM_ID        0x2e
#define PCI_ROM_ADDRESS         0x30    /* 32 bits */
#define PCI_CAPABILITY_LIST     0x34    /* 8 bits */
#define PCI_INTERRUPT_LINE      0x3c    /* 8 bits */
#define PCI_INTERRUPT_PIN       0x3d    /* 8 bits */
#define PCI_MIN_GNT             0x3e    /* 8 bits */
#define PCI_MAX_LAT             0x3f    /* 8 bits */

/* PCI Command Register bits */
#define PCI_COMMAND_IO          0x1     /* Enable response in I/O space */
#define PCI_COMMAND_MEMORY      0x2     /* Enable response in Memory space */
#define PCI_COMMAND_MASTER      0x4     /* Enable bus mastering */
#define PCI_COMMAND_SPECIAL     0x8     /* Enable special cycles */
#define PCI_COMMAND_INVALIDATE  0x10    /* Use memory write and invalidate */
#define PCI_COMMAND_VGA_PALETTE 0x20    /* VGA palette snooping */
#define PCI_COMMAND_PARITY      0x40    /* Enable parity error response */
#define PCI_COMMAND_WAIT        0x80    /* Wait cycle control */
#define PCI_COMMAND_SERR        0x100   /* Enable SERR */
#define PCI_COMMAND_FAST_BACK   0x200   /* Enable back-to-back writes */
#define PCI_COMMAND_INTX_DISABLE 0x400  /* INTx Emulation Disable */

/* BAR */
#define PCI_BASE_ADDRESS_SPACE          0x01    /* 0 = memory, 1 = I/O */
#define PCI_BASE_ADDRESS_SPACE_IO       0x01
#define PCI_BASE_ADDRESS_SPACE_MEMORY   0x00
#define PCI_BASE_ADDRESS_MEM_TYPE_MASK  0x06
#define PCI_BASE_ADDRESS_MEM_TYPE_32    0x00    /* 32 bit address */
#define PCI_BASE_ADDRESS_MEM_TYPE_1M    0x02    /* Below 1M */
#define PCI_BASE_ADDRESS_MEM_TYPE_64    0x04    /* 64 bit address */
#define PCI_BASE_ADDRESS_MEM_PREFETCH   0x08    /* prefetchable? */
#define PCI_BASE_ADDRESS_MEM_MASK       (~0x0fUL)
#define PCI_BASE_ADDRESS_IO_MASK        (~0x03UL)

/* PCI device structure */
struct pci_dev {
    IOPCIDevice *iokit_device;  /* macOS IOPCIDevice */
    
    u16 vendor;
    u16 device;
    u16 subsystem_vendor;
    u16 subsystem_device;
    u8 revision;
    
    unsigned int irq;           /* IRQ number */
    
    struct device *dev;         /* Generic device */
    
    /* Saved config space */
    u32 saved_config_space[16];
    
    /* Driver data */
    void *driver_data;
};

/* PCI device ID structure */
struct pci_device_id {
    u32 vendor, device;
    u32 subvendor, subdevice;
    u32 dev_class, class_mask;  // 'class' is a C++ keyword, renamed to dev_class
    unsigned long driver_data;
};

/* Get/Set driver data */
static inline void pci_set_drvdata(struct pci_dev *pdev, void *data)
{
    pdev->driver_data = data;
}

static inline void *pci_get_drvdata(struct pci_dev *pdev)
{
    return pdev->driver_data;
}

/* PCI config space access */
static inline int pci_read_config_byte(struct pci_dev *dev, int where, u8 *val)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    *val = dev->iokit_device->configRead8(where);
    return 0;
}

static inline int pci_read_config_word(struct pci_dev *dev, int where, u16 *val)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    *val = dev->iokit_device->configRead16(where);
    return 0;
}

static inline int pci_read_config_dword(struct pci_dev *dev, int where, u32 *val)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    *val = dev->iokit_device->configRead32(where);
    return 0;
}

static inline int pci_write_config_byte(struct pci_dev *dev, int where, u8 val)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    dev->iokit_device->configWrite8(where, val);
    return 0;
}

static inline int pci_write_config_word(struct pci_dev *dev, int where, u16 val)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    dev->iokit_device->configWrite16(where, val);
    return 0;
}

static inline int pci_write_config_dword(struct pci_dev *dev, int where, u32 val)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    dev->iokit_device->configWrite32(where, val);
    return 0;
}

/* PCI device enable/disable */
static inline int pci_enable_device(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    
    // setBusLeadEnable is deprecated in modern macOS - bus mastering controlled via setBusMasterEnable
    dev->iokit_device->setBusMasterEnable(true);
    dev->iokit_device->setMemoryEnable(true);
    dev->iokit_device->setIOEnable(false);
    
    return 0;
}

static inline void pci_disable_device(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return;
    
    dev->iokit_device->setBusMasterEnable(false);
    dev->iokit_device->setMemoryEnable(false);
}

/* PCI bus mastering */
static inline void pci_set_master(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return;
    dev->iokit_device->setBusMasterEnable(true);
}

static inline void pci_clear_master(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return;
    dev->iokit_device->setBusMasterEnable(false);
}

/* PCI resource access */
static inline resource_size_t pci_resource_start(struct pci_dev *dev, int bar)
{
    if (!dev || !dev->iokit_device) return 0;
    
    IOMemoryMap *map = dev->iokit_device->mapDeviceMemoryWithRegister(
        kIOPCIConfigBaseAddress0 + bar * 4);
    if (!map) return 0;
    
    resource_size_t addr = map->getPhysicalAddress();
    map->release();
    return addr;
}

static inline resource_size_t pci_resource_len(struct pci_dev *dev, int bar)
{
    if (!dev || !dev->iokit_device) return 0;
    
    IOMemoryMap *map = dev->iokit_device->mapDeviceMemoryWithRegister(
        kIOPCIConfigBaseAddress0 + bar * 4);
    if (!map) return 0;
    
    resource_size_t len = map->getLength();
    map->release();
    return len;
}

static inline unsigned long pci_resource_flags(struct pci_dev *dev, int bar)
{
    // Simplified - just return memory type
    return PCI_BASE_ADDRESS_SPACE_MEMORY;
}

/* PCI BAR operations */
static inline void __iomem *pci_iomap(struct pci_dev *dev, int bar, unsigned long maxlen)
{
    if (!dev || !dev->iokit_device) return NULL;
    
    IOMemoryMap *map = dev->iokit_device->mapDeviceMemoryWithRegister(
        kIOPCIConfigBaseAddress0 + bar * 4);
    if (!map) return NULL;
    
    return (void __iomem *)map->getVirtualAddress();
}

static inline void pci_iounmap(struct pci_dev *dev, void __iomem *addr)
{
    // In production, need to track and release IOMemoryMap
    (void)dev;
    (void)addr;
}

/* PCI power management */
static inline int pci_set_power_state(struct pci_dev *dev, pci_power_t state)
{
    // IOKit handles power management differently
    (void)dev;
    (void)state;
    return 0;
}

static inline pci_power_t pci_choose_state(struct pci_dev *dev, u32 state)
{
    (void)dev;
    (void)state;
    return PCI_D0;
}

/* PCI save/restore state */
static inline int pci_save_state(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    
    for (int i = 0; i < 16; i++) {
        dev->saved_config_space[i] = dev->iokit_device->configRead32(i * 4);
    }
    return 0;
}

static inline void pci_restore_state(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return;
    
    for (int i = 0; i < 16; i++) {
        dev->iokit_device->configWrite32(i * 4, dev->saved_config_space[i]);
    }
}

/* PCI MSI */
static inline int pci_enable_msi(struct pci_dev *dev)
{
    // MSI handling in IOKit is different
    (void)dev;
    return 0; // Success
}

static inline void pci_disable_msi(struct pci_dev *dev)
{
    (void)dev;
}

static inline int pci_msi_vec_count(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return -ENODEV;
    // getMSICount() is not available in modern IOPCIDevice API
    // Return 1 as default (single MSI vector)
    return 1;
}

/* PCIe capability */
static inline bool pci_is_pcie(struct pci_dev *dev)
{
    if (!dev || !dev->iokit_device) return false;
    
    // Check for PCIe capability
    u8 cap_ptr;
    pci_read_config_byte(dev, PCI_CAPABILITY_LIST, &cap_ptr);
    
    while (cap_ptr) {
        u8 cap_id;
        pci_read_config_byte(dev, cap_ptr, &cap_id);
        if (cap_id == 0x10) // PCIe capability
            return true;
        pci_read_config_byte(dev, cap_ptr + 1, &cap_ptr);
    }
    
    return false;
}

/* PCI device lookup */
static inline struct pci_dev *pci_get_device(unsigned int vendor,
                                              unsigned int device,
                                              struct pci_dev *from)
{
    // This would need proper implementation with device registry
    (void)vendor;
    (void)device;
    (void)from;
    return NULL;
}

/* Device coherent DMA mask */
static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{
    (void)dev;
    (void)mask;
    return 0; // IOKit handles this differently
}

static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{
    (void)dev;
    (void)mask;
    return 0;
}

/* Helper to create pci_dev from IOPCIDevice */
static inline struct pci_dev *pci_dev_from_iokit(IOPCIDevice *iokit_dev)
{
    if (!iokit_dev) return NULL;
    
    struct pci_dev *pdev = (struct pci_dev *)IOMalloc(sizeof(struct pci_dev));
    if (!pdev) return NULL;
    
    memset(pdev, 0, sizeof(struct pci_dev));
    pdev->iokit_device = iokit_dev;
    pdev->vendor = iokit_dev->configRead16(PCI_VENDOR_ID);
    pdev->device = iokit_dev->configRead16(PCI_DEVICE_ID);
    pdev->subsystem_vendor = iokit_dev->configRead16(PCI_SUBSYSTEM_VENDOR_ID);
    pdev->subsystem_device = iokit_dev->configRead16(PCI_SUBSYSTEM_ID);
    pdev->revision = iokit_dev->configRead8(PCI_REVISION_ID);
    
    return pdev;
}

static inline void pci_dev_free(struct pci_dev *pdev)
{
    if (pdev) {
        IOFree(pdev, sizeof(struct pci_dev));
    }
}

#ifdef __cplusplus
}
#endif

#endif /* LINUX_PCI_H */
