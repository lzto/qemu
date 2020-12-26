/*
 * symbolic + fuzz test generic PCI device
 * 2020 Tong Zhang<ztong0001@gmail.com>
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/error-report.h"
#include "qemu/thread.h"
#include "hw/block/block.h"
#include "hw/pci/msix.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "sysemu/hostmem.h"
#include "sysemu/block-backend.h"
#include "exec/memory.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/cutils.h"
#include "hw/irq.h"

#include "aplib.h"

#define SFPBARCNT 6


typedef struct SFPMBS {
  void *parent;
  int baridx;
  MemoryRegion iomem;
  uint8_t *bar;
} SFPMBS;

typedef struct SFPCtrl {
  PCIDevice parent_obj;
  SFPMBS bars[SFPBARCNT];
  qemu_irq irq;
  QEMUTimer *timer;
} SFPCtrl;

static const VMStateDescription sfp_vmstate = {
    .name = "sfp",
    .unmigratable = 1,
};

static uint64_t sfp_mmio_read(void *opaque, hwaddr addr, unsigned size) {
  // SFPMBS *n = (SFPMBS *)opaque;
  uint64_t val = 0;
  ap_get_fuzz_data((char*)&val, addr, size);
  // TODO: read from fuzz file @ offset addr and size
  return val;
}

static void sfp_assert_irq(SFPCtrl *n) {
#if 0
    if (msix_enabled(&(n->parent_obj))) {
      // printf("msix_notify\n");
      msix_notify(&(n->parent_obj), 0);
    } else {
      // printf("pci_irq_assert\n");
      pci_irq_assert(&n->parent_obj);
    }
#else
  qemu_set_irq(n->irq, 1);
#endif
}

static void sfp_deassert_irq(SFPCtrl *n) {
#if 0
    if (!msix_enabled(&(n->parent_obj)))
      pci_irq_deassert(&n->parent_obj);
#else
  qemu_set_irq(n->irq, 0);
#endif
}

static void sfp_mmio_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned size) {
  ap_set_fuzz_data(data, addr, size);
  // SFPMBS *n = (SFPMBS *)opaque;
  // SFPCtrl *ctrl = (SFPCtrl *)n->parent;
  // TODO - write the fuzz file or discard?
  // printf("sfp_mmio_write: addr %#lx size %d val=%#lx\n", addr, size, data);
  // uint8_t* val = (uint8_t*)&data;
  // memcpy(&(n->bar[addr]), val, size);
  //
  // assert IRQ
  // timer_mod(ctrl->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 30000000000);
}

static const MemoryRegionOps sfp_mmio_ops = {
    .read = sfp_mmio_read,
    .write = sfp_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl =
        {
            .min_access_size = 1,
            .max_access_size = 8,
        },
};

#define TYPE_SFP "sfp"
#define SFP(obj) OBJECT_CHECK(SFPCtrl, (obj), TYPE_SFP)

static void sfp_gen_irq(void *opaque) {
  SFPCtrl *n = (SFPCtrl *)opaque;
  if (rand() % 100 > 50) {
    sfp_assert_irq(n);
  } else {
    sfp_deassert_irq(n);
  }
  // schedule to trigger another irq
  timer_mod(n->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 30000000000);
}

static void sfp_realize(PCIDevice *pci_dev, Error **errp) {
  SFPCtrl *n = SFP(pci_dev);
#define PIO 0
#define MMIO 1

  int bartype[SFPBARCNT] = {MMIO, MMIO, MMIO, MMIO, MMIO, MMIO};
  int barsize[SFPBARCNT] = {
    64 * 1024 * 1024,
    64 * 1024 * 1024,
    64 * 1024 * 1024,
    64 * 1024 * 1024,
    64 * 1024 * 1024,
    64 * 1024 * 1024};
  for (int i = 0; i < SFPBARCNT; i++) {
    SFPMBS *sfpmbs = &(n->bars[i]);
    // FIXME: fix bar size
    int bar_size = barsize[i];
    uint8_t *bar = (uint8_t *)malloc(bar_size);
    if (!bar) {
      printf("bar %d allocation failed!\n", i);
      exit(-1);
    }
    memset(bar, 0, bar_size);
    sfpmbs->parent = n;
    sfpmbs->baridx = i;
    sfpmbs->bar = bar;
    // TODO: put some initial value here?
    bar[0] = 0x00;
    bar[0x10] = 0x12;
    bar[0x011b] = 0xb3;

    printf("sfp allocated bar[%d] %d bytes\n", i, bar_size);

    char name[16];
    sprintf(name, "sfp-%d", i);
    memory_region_init_io(&sfpmbs->iomem, OBJECT(n), &sfp_mmio_ops, sfpmbs,
                          name, bar_size);
    // register bar
    if (bartype[i]) {
      // - mmio
      pci_register_bar(pci_dev, i, PCI_BASE_ADDRESS_SPACE_MEMORY,
                       &sfpmbs->iomem);
    } else {
      // - pio
      pci_register_bar(pci_dev, i, PCI_BASE_ADDRESS_SPACE_IO, &sfpmbs->iomem);
    }
  }
#undef PIO
#undef MMIO
  // FIXME: IRQ
  uint8_t *pci_conf = pci_dev->config;
  // pci_config_set_prog_interface(pci_conf, 0x2);
  // pci_config_set_class(pci_conf, PCI_CLASS_OTHERS);
  // pcie_endpoint_cap_init(pci_dev, 0x80);
  pci_conf[PCI_INTERRUPT_PIN] = 1;
  n->irq = pci_allocate_irq(pci_dev);
  // if (msix_init_exclusive_bar(pci_dev, 1, 4, errp)) {
  //  return;
  // }
  n->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, sfp_gen_irq, n);
}

static void sfp_exit(PCIDevice *pci_dev) {}

#define PCI_PRODUCT_ID_HAPS_HSOTG 0xabc0

static void sfp_class_init(ObjectClass *oc, void *data) {
  DeviceClass *dc = DEVICE_CLASS(oc);
  PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);
  uint16_t sfpvid = 0x8086;
  uint16_t sfppid = 0x8086;
  const char *svid = getenv("SFPVID");
  const char *spid = getenv("SFPPID");
  if (svid != NULL)
    sscanf(svid, "%hx", &sfpvid);
  if (spid != NULL)
    sscanf(spid, "%hx", &sfppid);
  printf("SFP vid=%#x, pid=%#x\n", sfpvid, sfppid);

  pc->realize = sfp_realize;
  pc->exit = sfp_exit;
  // does not really matter
  pc->class_id = 0x0200;
  // need to get this from driver
  pc->vendor_id = sfpvid;
  // need to get this from driver
  pc->device_id = sfppid;
  pc->revision = 2;

  // does not really matter
  set_bit(DEVICE_CATEGORY_MISC, dc->categories);
  dc->desc = "SFP device";
  dc->vmsd = &sfp_vmstate;

  // initialize AFL client here
  ap_init();
}

static void sfp_instance_init(Object *obj) {}

static const TypeInfo sfp_info = {
    .name = TYPE_SFP,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(SFPCtrl),
    .class_init = sfp_class_init,
    .instance_init = sfp_instance_init,
    .interfaces = (InterfaceInfo[]){{INTERFACE_PCIE_DEVICE}, {}},
};

static void sfp_register_types(void) { type_register_static(&sfp_info); }

type_init(sfp_register_types)
