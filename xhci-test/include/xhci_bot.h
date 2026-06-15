#ifndef XHCI_BOT_H
#define XHCI_BOT_H

/* USB Mass Storage — Bulk-Only Transport (BOT) driver.
 *
 * Follows USB MSC Bot spec: CBW → DATA(optional) → CSW.
 * Uses xHCI transfer rings for bulk endpoint I/O.
 */

#include "xhci_regs.h"

/* --------------------------------------------------------------
 * BOT protocol structures (USB-IF "Universal Serial Bus Mass
 * Storage Class Bulk-Only Transport" Rev 1.0)
 * -------------------------------------------------------------- */

#define BOT_CBW_SIGNATURE  0x43425355  /* "USBC" */
#define BOT_CSW_SIGNATURE  0x53425355  /* "USBS" */

struct bot_cbw {
    u32 dCBWSignature;
    u32 dCBWTag;
    u32 dCBWDataTransferLength;
    u8  bmCBWFlags;          /* 0x80 = IN, 0x00 = OUT */
    u8  bCBWLUN;             /* Logical Unit Number */
    u8  bCBWCBLength;        /* valid bytes in CBWCB (1-16) */
    u8  CBWCB[16];           /* SCSI Command Block */
} PACKED;

struct bot_csw {
    u32 dCSWSignature;
    u32 dCSWTag;
    u32 dCSWDataResidue;
    u8  bCSWStatus;          /* 0=OK, 1=Failed, 2=Phase Error */
} PACKED;

/* bmCBWFlags direction */
#define BOT_DIR_OUT   0x00
#define BOT_DIR_IN    0x80

/* Max LUN request — class-specific control transfer */
#define BOT_GET_MAX_LUN  0xFE
#define BOT_MAX_LUN_REQTYPE  (USB_DIR_IN | 0x01 /* class */ | 0x01 /* interface */)

/* --------------------------------------------------------------
 * SCSI Command Block definitions (SPC-4 / SBC-4)
 * -------------------------------------------------------------- */

enum scsi_opcode {
    SCSI_TEST_UNIT_READY   = 0x00,
    SCSI_REQUEST_SENSE     = 0x03,
    SCSI_INQUIRY           = 0x12,
    SCSI_MODE_SENSE_6      = 0x1A,
    SCSI_READ_CAPACITY_10  = 0x25,
    SCSI_READ_CAPACITY_16  = 0x9E,
    SCSI_READ_10           = 0x28,
    SCSI_WRITE_10          = 0x2A,
    SCSI_READ_16           = 0x88,
    SCSI_WRITE_16          = 0x8A,
};

/* CDB: 6 bytes (used for INQUIRY etc. in SeaBIOS) */
struct scsi_cdb_6 {
    u8 command;
    u8 res1;
    u8 res2;
    u8 res3;
    u8 length;
    u8 control;            /* typically 0 */
} PACKED;

/* CDB: 10 bytes (READ_10, WRITE_10, READ_CAPACITY_10) */
struct scsi_cdb_10 {
    u8 command;
    u8 flags;              /* reserved */
    u32 lba;               /* big-endian */
    u8 res1;
    u16 blocks;            /* big-endian */
    u8 control;
} PACKED;

/* CDB: 16 bytes (READ_CAPACITY_16 with service action) */
struct scsi_cdb_16 {
    u8 command;
    u8 service_action;     /* 0x10 for READ CAPACITY (16) */
    u8 res1[6];
    u32 allocation_length; /* big-endian */
    u8 control;
} PACKED;

/* --------------------------------------------------------------
 * SCSI Response structures
 * -------------------------------------------------------------- */

struct scsi_inquiry_data {
    u8 peripheral;         /* bits 7-5: qualifier, 4-0: peripheral type */
    u8 rmb;                /* bit 7: RMB */
    u8 version;
    u8 response_format;    /* bit 7: NormACA, bit 5-4: HiSup, bits 3-0: format */
    u8 additional_length;  /* n-4 */
    u8 flags1;
    u8 flags2;
    u8 flags3;
    u8 vendor[8];
    u8 product[16];
    u8 revision[4];
} PACKED;

struct scsi_read_capacity_10 {
    u32 last_lba;          /* big-endian */
    u32 block_size;        /* big-endian */
} PACKED;

struct scsi_read_capacity_16 {
    u64 last_lba;          /* big-endian */
    u32 block_size;        /* big-endian */
    u8 p_type;
    u8 prot_en;
    u8 p_i_exp;
    u8 reserved[20];
} PACKED;

struct scsi_request_sense {
    u8 error_code;
    u8 valid;
    u8 obsolete;
    u8 sense_key_length;
    u32 command_info;
    u8 additional_length;
    u32 cmd_specific;
    u8 asc;                /* Additional Sense Code */
    u8 ascq;               /* Additional Sense Code Qualifier */
    u8 fruc;
    u8 sense_key[4];       /* bits 7-4: sense key */
    u8 extra[18];
} PACKED;

/* Sense Keys */
enum scsi_sense_key {
    SK_NO_SENSE           = 0x00,
    SK_RECOVERED_ERROR    = 0x01,
    SK_NOT_READY          = 0x02,
    SK_MEDIUM_ERROR       = 0x03,
    SK_HARDWARE_ERROR     = 0x04,
    SK_ILLEGAL_REQUEST    = 0x05,
    SK_UNIT_ATTENTION     = 0x06,
    SK_DATA_PROTECT       = 0x07,
};

/* ASC/ASCQ for common conditions */
#define ASC_MEDIUM_NOT_PRESENT    0x3a
#define ASCQ_MEDIUM_NOT_PRESENT   0x00
#define ASC_IN_PROGRESS           0x04
#define ASCQ_IN_PROGRESS          0x01

/* --------------------------------------------------------------
 * BOT device state
 * -------------------------------------------------------------- */

struct bot_device {
    void   *handle;        /* xHCI handle */
    int     slot_id;
    int     lun;
    void   *ep0_ring;
    void   *bulk_in_ring;
    void   *bulk_out_ring;
    int     bulk_in_ep;    /* endpoint number (1-15) */
    int     bulk_out_ep;   /* endpoint number (1-15) */
    u32     max_packet;    /* bulk endpoint max packet size */
    u32     next_tag;      /* monotonically increasing CBW tag */
};

/* --------------------------------------------------------------
 * API
 * -------------------------------------------------------------- */

/* Find and initialise a BOT device on a given slot.
 * Returns 0 on success, negative on failure.
 *
 * handle     — xHCI handle from xhci_open()
 * slot_id    — slot assigned by xhci_enable_slot() + xhci_address_device()
 * ep0_ring   — EP0 transfer ring (already configured)
 * bot_dev    — output: caller-allocated struct bot_device (zeroed)
 */
int bot_setup(void *handle, int slot_id, void *ep0_ring,
              struct bot_device *bot_dev);

/* Issue a BOT command: sends CBW → optional data → receives CSW.
 *
 * bot_dev    — BOT device
 * cdb        — SCSI CDB (up to 16 bytes)
 * cdb_len    — valid CDB length
 * data       — data buffer (CPU virtual, will be DMA-mapped internally)
 * data_len   — bytes to transfer (0 = no data phase)
 * direction  — BOT_DIR_IN or BOT_DIR_OUT
 *
 * Returns 0 on success, negative on failure.
 * The caller must ensure data is a valid buffer (not used as DMA address).
 */
int bot_transfer(struct bot_device *bot_dev,
                 const void *cdb, int cdb_len,
                 void *data, u32 data_len, int direction);

/* Convenience wrappers for common SCSI commands */
int bot_inquiry(struct bot_device *bot_dev, struct scsi_inquiry_data *inq);
int bot_read_capacity(struct bot_device *bot_dev, u64 *last_lba, u32 *block_size);
int bot_read_10(struct bot_device *bot_dev, u32 lba, u16 blocks, void *buf);
int bot_write_10(struct bot_device *bot_dev, u32 lba, u16 blocks, const void *buf);
int bot_test_unit_ready(struct bot_device *bot_dev);
int bot_request_sense(struct bot_device *bot_dev, struct scsi_request_sense *sense);

/* Get max LUN from the device (class-specific control transfer) */
int bot_get_max_lun(void *handle, int slot_id, void *ep0_ring, int *max_lun);

#endif /* XHCI_BOT_H */
