#ifndef XHCI_REGS_H
#define XHCI_REGS_H

// Register interface structs - modeled after xHCI spec layout
// and SeaBIOS struct-based register definitions.

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef long long          s64;
typedef long               ssize_t;
typedef unsigned long      size_t;

#ifndef NULL
#define NULL ((void *)0)
#endif

#define PACKED __attribute__((packed))

// --------------------------------------------------------------
// Capability Registers (read-only, offset 0x00 from BAR0)

struct xhci_caps {
    u8  caplength;
    u8  reserved_01;
    u16 hciversion;
    u32 hcsparams1;
    u32 hcsparams2;
    u32 hcsparams3;
    u32 hccparams;
    u32 dboff;
    u32 rtsoff;
    u32 hccparams2;
};

// --------------------------------------------------------------
// Operational Registers (offset = caplength from BAR0)

struct xhci_op {
    u32 usbcmd;
    u32 usbsts;
    u32 pagesize;
    u32 reserved_01[2];
    u32 dnctl;
    u32 crcr_low;
    u32 crcr_high;
    u32 reserved_02[4];
    u32 dcbaap_low;
    u32 dcbaap_high;
    u32 config;
};

// Port Register - stride 0x10 per port
struct xhci_pr {
    u32 portsc;
    u32 portpmsc;
    u32 portli;
    u32 reserved_01;
};

// --------------------------------------------------------------
// Doorbell (offset = dboff from BAR0)

struct xhci_db {
    u32 doorbell;
};

// --------------------------------------------------------------
// Runtime Registers (offset = rtsoff from BAR0)

struct xhci_rts {
    u32 mfindex;
};

// Interrupter Register - stride 0x20 per interrupter
struct xhci_ir {
    u32 iman;
    u32 imod;
    u32 erstsz;
    u32 reserved_01;
    u32 erstba_low;
    u32 erstba_high;
    u32 erdp_low;
    u32 erdp_high;
};

// --------------------------------------------------------------
// bit definitions

// USB Command (USBCMD)
#define XHCI_CMD_RS             (1<<0)
#define XHCI_CMD_HCRST          (1<<1)
#define XHCI_CMD_INTE           (1<<2)
#define XHCI_CMD_HSEE           (1<<3)
#define XHCI_CMD_LHCRST         (1<<7)
#define XHCI_CMD_CSS            (1<<8)
#define XHCI_CMD_RSS            (1<<9)
#define XHCI_CMD_EWE            (1<<10)
#define XHCI_CMD_EU3S           (1<<11)

// USB Status (USBSTS)
#define XHCI_STS_HCH            (1<<0)
#define XHCI_STS_HSE            (1<<2)
#define XHCI_STS_EINT           (1<<3)
#define XHCI_STS_PCD            (1<<4)
#define XHCI_STS_SSS            (1<<8)
#define XHCI_STS_RSS            (1<<9)
#define XHCI_STS_SRE            (1<<10)
#define XHCI_STS_CNR            (1<<11)
#define XHCI_STS_HCE            (1<<12)

// Port Status/Control (PORTSC)
#define XHCI_PORTSC_CCS         (1<<0)
#define XHCI_PORTSC_PED         (1<<1)
#define XHCI_PORTSC_OCA         (1<<3)
#define XHCI_PORTSC_PR          (1<<4)
#define XHCI_PORTSC_PLS_SHIFT   5
#define XHCI_PORTSC_PLS_MASK    0xf
#define XHCI_PORTSC_PP          (1<<9)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_SPEED_MASK  0xf
#define XHCI_PORTSC_PLC         (1<<22)
#define XHCI_PORTSC_CSC         (1<<17)
#define XHCI_PORTSC_PEC         (1<<18)
#define XHCI_PORTSC_WRC         (1<<19)
#define XHCI_PORTSC_OCC         (1<<20)
#define XHCI_PORTSC_BHRC        (1<<21)
#define XHCI_PORTSC_CEC         (1<<23)
#define XHCI_PORTSC_WPR         (1<<31)

// Link Power States
enum {
    PLS_U0              =  0,
    PLS_U1              =  1,
    PLS_U2              =  2,
    PLS_U3              =  3,
    PLS_DISABLED        =  4,
    PLS_RX_DETECT       =  5,
    PLS_INACTIVE        =  6,
    PLS_POLLING         =  7,
    PLS_RECOVERY        =  8,
    PLS_HOT_RESET       =  9,
    PLS_COMP_MODE       = 10,
    PLS_TEST_MODE       = 11,
    PLS_RESUME          = 15,
};

// --------------------------------------------------------------
// Runtime interrupter bits

#define XHCI_IMAN_IP            (1<<0)
#define XHCI_IMAN_IE            (1<<1)
#define XHCI_ERDP_DEH           (1ULL<<3)

// --------------------------------------------------------------
// Doorbell

#define XHCI_DB_SLOT_CMD        0   // Command Ring

// --------------------------------------------------------------
// TRB and Completion Code definitions

typedef enum TRBType {
    TR_NORMAL            = 1,
    TR_SETUP             = 2,
    TR_DATA              = 3,
    TR_STATUS            = 4,
    TR_ISOCH             = 5,
    TR_LINK              = 6,
    TR_EVDATA            = 7,
    TR_NOOP              = 8,
    CR_ENABLE_SLOT       = 9,
    CR_DISABLE_SLOT      = 10,
    CR_ADDRESS_DEVICE    = 11,
    CR_CONFIGURE_ENDPOINT = 12,
    CR_EVALUATE_CONTEXT  = 13,
    CR_RESET_ENDPOINT    = 14,
    CR_STOP_ENDPOINT     = 15,
    CR_SET_TR_DEQUEUE    = 16,
    CR_RESET_DEVICE      = 17,
    CR_FORCE_EVENT       = 18,
    CR_NEGOTIATE_BW      = 19,
    CR_SET_LATENCY_TOL   = 20,
    CR_GET_PORT_BW       = 21,
    CR_FORCE_HEADER      = 22,
    CR_NOOP              = 23,
    ER_TRANSFER          = 32,
    ER_COMMAND_COMPLETE  = 33,
    ER_PORT_STATUS_CHANGE = 34,
    ER_BANDWIDTH_REQUEST = 35,
    ER_DOORBELL          = 36,
    ER_HOST_CONTROLLER   = 37,
    ER_DEVICE_NOTIF      = 38,
    ER_MFINDEX_WRAP      = 39,
} TRBType;

typedef enum TRBCCode {
    CC_INVALID                = 0,
    CC_SUCCESS,
    CC_DATA_BUFFER_ERROR,
    CC_BABBLE_DETECTED,
    CC_USB_TRANSACTION_ERROR,
    CC_TRB_ERROR,
    CC_STALL_ERROR,
    CC_RESOURCE_ERROR,
    CC_BANDWIDTH_ERROR,
    CC_NO_SLOTS_ERROR,
    CC_STREAM_TYPE_ERROR,
    CC_SLOT_NOT_ENABLED,
    CC_EP_NOT_ENABLED,
    CC_SHORT_PACKET,
    CC_RING_UNDERRUN,
    CC_RING_OVERRUN,
    CC_VF_ER_FULL,
    CC_PARAMETER_ERROR,
    CC_BANDWIDTH_OVERRUN,
    CC_CONTEXT_STATE_ERROR,
    CC_NO_PING_RESPONSE,
    CC_EVENT_RING_FULL,
    CC_INCOMPATIBLE_DEVICE,
    CC_MISSED_SERVICE,
    CC_COMMAND_RING_STOPPED,
    CC_COMMAND_ABORTED,
    CC_STOPPED,
    CC_STOPPED_LENGTH_INVALID,
    CC_MAX_EXIT_LATENCY_LARGE = 29,
    CC_ISOCH_BUFFER_OVERRUN   = 31,
    CC_EVENT_LOST,
    CC_UNDEFINED_ERROR,
} TRBCCode;

// TRB bit definitions
#define TRB_C               (1<<0)
#define TRB_TYPE_SHIFT      10
#define TRB_TYPE_MASK       0x3f
#define TRB_TYPE(t)         (((t) >> TRB_TYPE_SHIFT) & TRB_TYPE_MASK)

#define TRB_TR_ISP          (1<<2)
#define TRB_TR_IOC          (1<<5)
#define TRB_TR_IDT          (1<<6)
#define TRB_TR_BEI          (1<<9)
#define TRB_TR_DIR          (1<<16)
#define TRB_LK_TC           (1<<1)
#define TRB_INTR_SHIFT      22
#define TRB_CR_SLOTID_SHIFT 24
#define TRB_CR_EPID_SHIFT   16
#define TRB_CR_DC           (1<<9)

#define xhci_get_field(data, field) \
    (((data) >> field##_SHIFT) & field##_MASK)

// MMIO access macros — for userspace VFIO mmap'd BAR space
#define readl(addr)  (*(volatile u32 *)(addr))
#define writel(addr, v) (*(volatile u32 *)(addr) = (v))

// --------------------------------------------------------------
// Memory data structs

struct xhci_slotctx {
    u32 ctx[4];
    u32 reserved_01[4];
} PACKED;

struct xhci_epctx {
    u32 ctx[2];
    u32 deq_low;
    u32 deq_high;
    u32 length;
    u32 reserved_01[3];
} PACKED;

struct xhci_devlist {
    u32 ptr_low;
    u32 ptr_high;
} PACKED;

struct xhci_inctx_ctrl {
    u32 del;
    u32 add;
    u32 reserved_01[6];
} PACKED;

struct xhci_trb {
    u32 ptr_low;
    u32 ptr_high;
    u32 status;
    u32 control;
} PACKED;

struct xhci_er_seg {
    u32 ptr_low;
    u32 ptr_high;
    u32 size;
    u32 reserved_01;
} PACKED;

// --------------------------------------------------------------
// Standard USB descriptor types

enum {
    USB_DT_DEVICE           = 1,
    USB_DT_CONFIGURATION    = 2,
    USB_DT_STRING           = 3,
    USB_DT_INTERFACE        = 4,
    USB_DT_ENDPOINT         = 5,
    USB_DT_DEVICE_QUALIFIER = 6,
    USB_DT_OTHER_SPEED_CFG  = 7,
    USB_DT_BOS              = 15,
};

enum {
    USB_REQ_GET_STATUS       = 0,
    USB_REQ_CLEAR_FEATURE    = 1,
    USB_REQ_SET_FEATURE      = 3,
    USB_REQ_SET_ADDRESS      = 5,
    USB_REQ_GET_DESCRIPTOR   = 6,
    USB_REQ_SET_DESCRIPTOR   = 7,
    USB_REQ_SET_CONFIGURATION = 9,
};

// --------------------------------------------------------------
// Sizes and limits

#define XHCI_MAX_PORTS          255
#define XHCI_MAX_SLOTS          255
#define XHCI_MAX_ENDPOINTS      31
#define XHCI_PAGE_SIZE          4096
#define XHCI_TRB_SIZE           16
#define XHCI_TRBS_PER_PAGE      (XHCI_PAGE_SIZE / XHCI_TRB_SIZE)

#define XHCI_SLOT_CTX_SIZE      64
#define XHCI_EP_CTX_SIZE        32

// Helper macros for HCSParams1
#define XHCI_HCS1_MAX_SLOTS(v)      ((v) & 0xFF)
#define XHCI_HCS1_MAX_INTRS(v)      (((v) >> 8) & 0x7FF)
#define XHCI_HCS1_MAX_PORTS(v)      (((v) >> 24) & 0xFF)

#endif // XHCI_REGS_H
