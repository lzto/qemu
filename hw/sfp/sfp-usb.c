/*
 * SFP for usb devices
 * 2021 Tong Zhang<ztong0001@gmail.com>
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/qdev-properties.h"
#include "hw/usb.h"
#include "migration/vmstate.h"
#include "../usb/desc.h"
#include "qemu/error-report.h"
#include "qemu/queue.h"
#include "qemu/config-file.h"
#include "sysemu/sysemu.h"
#include "qemu/iov.h"
#include "qemu/module.h"
#include "qemu/cutils.h"

#define USB_SFP_VID 0x12d8
#define USB_SFP_PID 0x0001

enum usbstring_idx {
  STRING_MANUFACTURER = 1,
  STRING_PRODUCT,
  STRING_SERIALNUMBER,
  STRING_CONTROL,
};

static const USBDescStrings usb_sfp_stringtable = {
    [STRING_MANUFACTURER] = "SFP",
    [STRING_PRODUCT] = "USB SFP",
    [STRING_SERIALNUMBER] = "deadbeefdeadbeef",
};

static const USBDescIface desc_iface_sfp[] = {
    {/* CDC Control Interface */
     .bInterfaceNumber = 0,
     .bNumEndpoints = 1,
     .bInterfaceClass = USB_CLASS_COMM,
     .bInterfaceSubClass = 1,
     .bInterfaceProtocol = 0,
     .iInterface = STRING_CONTROL,
     .ndesc = 1,
     .descs =
         (USBDescOther[]){
             {
                 /* Header Descriptor */
                 .data =
                     (uint8_t[]){
                         0x05,                /*  u8    bLength */
                         USB_DT_CS_INTERFACE, /*  u8    bDescriptorType */
                         0x10,                /*  u8    bDescriptorSubType */
                         0x10, 0x01,          /*  le16  bcdCDC */
                     },
             },
         },
     .eps = (USBDescEndpoint[]){
         {
             .bEndpointAddress = USB_DIR_IN | 0x01,
             .bmAttributes = USB_ENDPOINT_XFER_INT,
             .wMaxPacketSize = 0x10,
             .bInterval = 1,
         },
     }}};

static const USBDescDevice desc_device_sfp = {
    .bcdUSB = 0x0200,
    .bDeviceClass = USB_CLASS_COMM,
    .bMaxPacketSize0 = 0x40,
    .bNumConfigurations = 1,
    .confs = (USBDescConfig[]){{
        .bNumInterfaces = 1,
        .bConfigurationValue = 1,
        .iConfiguration = 7,
        .bmAttributes = USB_CFG_ATT_ONE | USB_CFG_ATT_SELFPOWER,
        .bMaxPower = 0x32,
        // TODO: need to randomize this part
        .nif = ARRAY_SIZE(desc_iface_sfp),
        .ifs = desc_iface_sfp,
    }},
};

static USBDesc desc_sfp = {
    .id =
        {
            .idVendor = USB_SFP_VID,
            .idProduct = USB_SFP_PID,
            .bcdDevice = 0,
            .iManufacturer = STRING_MANUFACTURER,
            .iProduct = STRING_PRODUCT,
            .iSerialNumber = STRING_SERIALNUMBER,
        },
    // full speed usb device
    .full = &desc_device_sfp,
    .str = usb_sfp_stringtable,
};

typedef struct USBSFPState {
  USBDevice dev;
  USBEndpoint *intr;
} USBSFPState;

#define TYPE_USB_SFP "usb-sfp"
#define USB_SFP(obj) OBJECT_CHECK(USBSFPState, (obj), TYPE_USB_SFP)

static void usb_sfp_handle_reset(USBDevice *dev) {}

static void usb_sfp_handle_control(USBDevice *dev, USBPacket *p, int request,
                                   int value, int index, int length,
                                   uint8_t *data) {
  // USBSFPState *s = (USBSFPState *) dev;
  int ret;

  ret = usb_desc_handle_control(dev, p, request, value, index, length, data);
  if (ret >= 0) {
    return;
  }
}

static void usb_sfp_handle_datain(USBSFPState *s, USBPacket *p) {}

static void usb_sfp_handle_dataout(USBSFPState *s, USBPacket *p) {}

static void usb_sfp_handle_data(USBDevice *dev, USBPacket *p) {
  USBSFPState *s = (USBSFPState *)dev;

  switch (p->pid) {
  case USB_TOKEN_IN:
    usb_sfp_handle_datain(s, p);
    break;
  case USB_TOKEN_OUT:
    usb_sfp_handle_dataout(s, p);
    break;
  default:
    p->status = USB_RET_STALL;
    break;
  }

  if (p->status == USB_RET_STALL) {
    fprintf(stderr,
            "usb-sfp: failed data transaction: "
            "pid 0x%x ep 0x%x len 0x%zx\n",
            p->pid, p->ep->nr, p->iov.size);
  }
}

static void usb_sfp_unrealize(USBDevice *dev) {}

static void usb_sfp_realize(USBDevice *dev, Error **errp) {
  // USBSFPState *s = USB_SFP(dev);

  usb_desc_create_serial(dev);
  usb_desc_init(dev);

  // usb_desc_set_string(dev, STRING_ETHADDR, s->usbstring_mac);
}

static void usfp_instance_init(Object *obj) {
  // USBDevice *dev = USB_DEVICE(obj);
  // USBSFPState *s = USB_NET(dev);
  printf("%s:%d\n", __FILE__, __LINE__);
}

static const VMStateDescription vmstate_usfp = {
    .name = "usb sfp",
    .unmigratable = 1,
};

static void usfp_class_initfn(ObjectClass *klass, void *data) {
  DeviceClass *dc = DEVICE_CLASS(klass);
  USBDeviceClass *uc = USB_DEVICE_CLASS(klass);
  
  // change vid/pid
  const char *sfpusbvid = getenv("SFP_USB_VID");
  if (sfpusbvid != NULL) {
    uint16_t vid;
    sscanf(sfpusbvid, "%hx", &vid);
    printf("SFP USB VID=%#x\n", vid);
    desc_sfp.id.idVendor = vid;
  }

  const char *sfpusbpid = getenv("SFP_USB_PID");
  if (sfpusbpid != NULL) {
    uint16_t pid;
    sscanf(sfpusbpid, "%hx", &pid);
    printf("SFP USB PID=%#x\n", pid);
    desc_sfp.id.idProduct = pid;
  }

  uc->realize = usb_sfp_realize;
  uc->product_desc = "USB SFP";
  uc->usb_desc = &desc_sfp;
  uc->handle_reset = usb_sfp_handle_reset;
  uc->handle_control = usb_sfp_handle_control;
  uc->handle_data = usb_sfp_handle_data;
  uc->unrealize = usb_sfp_unrealize;

  // TODO: set type
  set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);

  dc->fw_name = "u-sfp";
  dc->vmsd = &vmstate_usfp;
  // device_class_set_props(dc, NULL);
}

static const TypeInfo usfp_info = {
    .name = TYPE_USB_SFP,
    .parent = TYPE_USB_DEVICE,
    .instance_size = sizeof(USBSFPState),
    .class_init = usfp_class_initfn,
    .instance_init = usfp_instance_init,
};

static void usfp_register_types(void) { type_register_static(&usfp_info); }

type_init(usfp_register_types)
