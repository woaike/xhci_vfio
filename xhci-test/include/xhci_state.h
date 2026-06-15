#ifndef XHCI_STATE_H
#define XHCI_STATE_H

#include "xhci_regs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XHCI_STATE_MAGIC    "XHCI_ST"
#define XHCI_STATE_MAGIC_LEN 8
#define XHCI_STATE_VERSION  1

#define XHCI_STATE_MAX_SLOTS      255
#define XHCI_STATE_MAX_ENDPOINTS  31

// USB device descriptor
struct usb_device_descriptor {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} PACKED;

// USB control request (matching SeaBIOS usb_ctrlrequest)
struct usb_ctrlrequest {
    u8 bRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} PACKED;

// USB direction (matching SeaBIOS usb.h)
#define USB_DIR_OUT                     0               /* to device */
#define USB_DIR_IN                      0x80            /* to host */

// USB request types (matching SeaBIOS usb.h)
#define USB_TYPE_MASK                   (0x03 << 5)
#define USB_TYPE_STANDARD               (0x00 << 5)
#define USB_TYPE_CLASS                  (0x01 << 5)
#define USB_TYPE_VENDOR                 (0x02 << 5)
#define USB_TYPE_RESERVED              (0x03 << 5)

// USB recipients (matching SeaBIOS usb.h)
#define USB_RECIP_MASK                   0x1f
#define USB_RECIP_DEVICE                 0x00
#define USB_RECIP_INTERFACE              0x01
#define USB_RECIP_ENDPOINT               0x02
#define USB_RECIP_OTHER                  0x03

// USB requests (matching SeaBIOS usb.h)
#define USB_REQ_GET_STATUS               0x00
#define USB_REQ_CLEAR_FEATURE            0x01
#define USB_REQ_SET_FEATURE              0x03
#define USB_REQ_SET_ADDRESS              0x05
#define USB_REQ_GET_DESCRIPTOR           0x06
#define USB_REQ_SET_DESCRIPTOR           0x07
#define USB_REQ_GET_CONFIGURATION        0x08
#define USB_REQ_SET_CONFIGURATION        0x09
#define USB_REQ_GET_INTERFACE            0x0A
#define USB_REQ_SET_INTERFACE            0x0B
#define USB_REQ_SYNCH_FRAME              0x0C

// USB descriptor types (matching SeaBIOS usb.h)
#define USB_DT_DEVICE                    0x01
#define USB_DT_CONFIG                    0x02
#define USB_DT_STRING                    0x03
#define USB_DT_INTERFACE                 0x04
#define USB_DT_ENDPOINT                  0x05
#define USB_DT_DEVICE_QUALIFIER          0x06
#define USB_DT_OTHER_SPEED_CONFIGURATION 0x07
#define USB_DT_INTERFACE_POWER           0x08
#define USB_DT_OTG                       0x09
#define USB_DT_DEBUG                     0x0a
#define USB_DT_ASSOCIATION               0x0b
#define USB_DT_SECURITY                  0x0c
#define USB_DT_KEY                       0x0d
#define USB_DT_ENCRYPTION_TYPE           0x0e
#define USB_DT_BOS                       0x0f

// Helper macro for bRequestType
#define USB_REQ_TYPE(dir, type, recip)  ((dir) | (type) | (recip))

// Endpoint configuration info
struct xhci_state_ep_info {
    u32 ep_index;
    u8  ep_type;          // 1=Control, 2=Bulk, 3=Interrupt, 4=Isoch
    u16 max_packet_size;
    u32 avg_trb_len;
    u8  mult;
    u8  cerr;
    u8  max_burst;
    u8  interval;
};

// Transfer ring state (saved/restored)
struct xhci_state_transfer_ring {
    u32 n_trbs;
    u32 enqueue_pos;
    u32 cycle_state;
    u8  *trb_data;
};

// Per-slot state
struct xhci_state_slot_entry {
    u32 slot_id;
    u32 port_index;       // 0-based
    u32 portsc_snapshot;
    u8  speed;
    u32 route_string;

    struct usb_device_descriptor dev_descriptor;
    u16 config_descr_total_len;
    u8  *config_descriptor;

    u32 n_endpoints;
    struct xhci_state_ep_info *endpoints;

    u32 n_rings;
    struct xhci_state_transfer_ring **rings;

    // Live EP0 ring handle (not serialized)
    void *ep0_ring;

    u8  restored;
};

// Complete state object
struct xhci_state {
    u8  cap_length;
    u32 db_off;
    u32 rts_off;
    u32 max_slots;
    u32 n_slots;
    struct xhci_state_slot_entry *slots;
};

// --------------------------------------------------------------
// API

int xhci_state_enumerate(void *handle, struct xhci_state **state_out);
int xhci_state_save(void *handle, const struct xhci_state *state, const char *path);
struct xhci_state *xhci_state_load(const char *path);
int xhci_state_restore(void *handle, struct xhci_state *state);
void xhci_state_free(struct xhci_state *state);

// Control transfer via EP0
int xhci_control_transfer(void *handle, int slot_id, void *ep0_ring,
                          const u8 setup_data[8],
                          void *data_buf, u32 data_len,
                          int direction);

// Control transfer with physical address for data buffer
int xhci_control_transfer_phys(void *handle, int slot_id, void *ep0_ring,
                               const u8 setup_data[8],
                               u64 data_phys, u32 data_len,
                               int direction);

// Get EP0 ring from slot entry
static inline void *xhci_slot_ep0_ring(const struct xhci_state_slot_entry *slot)
{
    return slot ? slot->ep0_ring : NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* XHCI_STATE_H */
