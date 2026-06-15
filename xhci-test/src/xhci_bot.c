// XHCI USB Mass Storage — Bulk-Only Transport driver.
//
// Protocol: CBW (bulk OUT) → DATA (bulk IN/OUT, optional) → CSW (bulk IN).
// Each phase uses xHCI transfer rings with IOC (interrupt on completion).
//
// Follows SeaBIOS usb-msc.c logic but uses our xHCI user-space API.

#include "xhci_internal.h"
#include "xhci_bot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/****************************************************************
 * Bulk transfer helper — single phase (CBW, data, or CSW).
 *
 * Allocates a DMA buffer, queues one TRB, rings doorbell,
 * waits for the completion event, then frees the buffer.
 * Returns 0 on success, negative on failure.
 ****************************************************************/

static int
bot_bulk_xfer(struct bot_device *bot, int ep,
              void *data, u32 len, int dir_in)
{
    void *handle = bot->handle;
    int slot_id = bot->slot_id;
    void *ring = dir_in ? bot->bulk_in_ring : bot->bulk_out_ring;

    if (!ring) {
        fprintf(stderr, "bot: bulk ring not set up (ep=%d, dir=%s)\n",
                ep, dir_in ? "in" : "out");
        return -1;
    }

    /* Allocate DMA buffer for data (even zero-length for CSW/CBW) */
    u64 data_phys;
    u32 xfer_len = len > 0 ? len : 1;  /* minimum 1 byte for control TRBs */
    void *dma_buf = xhci_dma_alloc(handle, xfer_len, &data_phys);
    if (!dma_buf) {
        fprintf(stderr, "bot: DMA alloc failed (size=%u)\n", xfer_len);
        return -1;
    }

    /* Copy data for OUT transfers */
    if (!dir_in && data && len > 0)
        memcpy(dma_buf, data, len);

    /* Queue a TRB with IOC to get a completion event.
     * xhci_transfer_enqueue() adds TRB_C (cycle state) and TR_NORMAL internally. */
    u32 ctrl = TRB_TR_IOC | TRB_TR_ISP;

    if (dir_in)
        ctrl |= TRB_TR_DIR;  /* bit 16: direction IN */

    xhci_transfer_enqueue(handle, ring, data_phys, xfer_len,
                          0 /* td_size */, 0 /* intr_target */, ctrl);

    /* Ring the doorbell for this endpoint */
    xhci_doorbell(handle, slot_id, ep);

    /* Wait for completion event */
    u64 event;
    int ret = xhci_event_wait(handle, &event, 5000);
    if (ret < 0) {
        fprintf(stderr, "bot: bulk xfer timeout (ep=%d dir=%s len=%u)\n",
                ep, dir_in ? "in" : "out", len);
        xhci_dma_free(handle, dma_buf, xfer_len);
        return -1;
    }

    /* Check completion code — upper 8 bits of control (high 32 of event) */
    u32 event_ctrl = (u32)(event >> 32);
    u32 cc = (event_ctrl >> 24) & 0xff;
    if (cc != CC_SUCCESS) {
        fprintf(stderr, "bot: bulk xfer failed cc=%d (%s) ep=%d\n",
                cc, cc_name_safe(cc), ep);
        xhci_dma_free(handle, dma_buf, xfer_len);
        return -1;
    }

    /* Copy data back for IN transfers */
    if (dir_in && data && len > 0)
        memcpy(data, dma_buf, len);

    xhci_dma_free(handle, dma_buf, xfer_len);
    return 0;
}

/****************************************************************
 * Core BOT transfer: CBW → DATA → CSW
 ****************************************************************/

int
bot_transfer(struct bot_device *bot_dev,
             const void *cdb, int cdb_len,
             void *data, u32 data_len, int direction)
{
    /* 1. Build CBW */
    struct bot_cbw cbw;
    memset(&cbw, 0, sizeof(cbw));
    cbw.dCBWSignature = BOT_CBW_SIGNATURE;
    cbw.dCBWTag = ++bot_dev->next_tag;
    cbw.dCBWDataTransferLength = data_len;
    cbw.bmCBWFlags = direction;
    cbw.bCBWLUN = bot_dev->lun;
    cbw.bCBWCBLength = (u8)cdb_len;
    memcpy(cbw.CBWCB, cdb, cdb_len);

    /* 2. Send CBW on bulk OUT */
    int ret = bot_bulk_xfer(bot_dev, bot_dev->bulk_out_ep,
                            &cbw, sizeof(cbw), 0 /* dir_out */);
    if (ret < 0)
        return ret;

    /* 3. Data phase (optional) */
    if (data && data_len > 0) {
        ret = bot_bulk_xfer(bot_dev,
                            direction == BOT_DIR_IN ? bot_dev->bulk_in_ep : bot_dev->bulk_out_ep,
                            data, data_len, direction == BOT_DIR_IN);
        if (ret < 0)
            return ret;
    }

    /* 4. Receive CSW on bulk IN */
    struct bot_csw csw;
    memset(&csw, 0, sizeof(csw));
    ret = bot_bulk_xfer(bot_dev, bot_dev->bulk_in_ep,
                        &csw, sizeof(csw), 1 /* dir_in */);
    if (ret < 0)
        return ret;

    /* 5. Validate CSW */
    if (csw.dCSWSignature != BOT_CSW_SIGNATURE) {
        fprintf(stderr, "bot: bad CSW signature 0x%08x (expected 0x%08x)\n",
                csw.dCSWSignature, BOT_CSW_SIGNATURE);
        return -1;
    }
    if (csw.dCSWTag != bot_dev->next_tag) {
        fprintf(stderr, "bot: CSW tag mismatch 0x%08x vs 0x%08x\n",
                csw.dCSWTag, bot_dev->next_tag);
        return -1;
    }
    if (csw.bCSWStatus != 0) {
        fprintf(stderr, "bot: CSW status %d (%s)\n",
                csw.bCSWStatus,
                csw.bCSWStatus == 1 ? "failed" : "phase error");
        return -1;
    }

    return 0;
}

/****************************************************************
 * SCSI convenience wrappers
 ****************************************************************/

int
bot_inquiry(struct bot_device *bot_dev, struct scsi_inquiry_data *inq)
{
    u8 cdb[6] = { SCSI_INQUIRY, 0, 0, 0, sizeof(*inq), 0 };
    memset(inq, 0, sizeof(*inq));
    return bot_transfer(bot_dev, cdb, sizeof(cdb), inq, sizeof(*inq), BOT_DIR_IN);
}

int
bot_read_capacity(struct bot_device *bot_dev, u64 *last_lba, u32 *block_size)
{
    /* Try READ CAPACITY (16) first, fall back to (10) */
    u8 cdb16[16];
    memset(cdb16, 0, sizeof(cdb16));
    cdb16[0] = SCSI_READ_CAPACITY_16;
    cdb16[1] = 0x10;  /* service action */
    cdb16[10] = 0x00; /* allocation length big-endian */
    cdb16[11] = 0x00;
    cdb16[12] = 0x00;
    cdb16[13] = 0x20; /* 32 bytes */

    struct scsi_read_capacity_16 cap16;
    int ret = bot_transfer(bot_dev, cdb16, sizeof(cdb16), &cap16, sizeof(cap16), BOT_DIR_IN);
    if (ret == 0) {
        u64 lba = (u64)cap16.last_lba;
        /* Convert from big-endian */
        lba = ((lba & 0xff00000000000000ULL) >> 56) |
              ((lba & 0x00ff000000000000ULL) >> 40) |
              ((lba & 0x0000ff0000000000ULL) >> 24) |
              ((lba & 0x000000ff00000000ULL) >>  8) |
              ((lba & 0x00000000ff000000ULL) <<  8) |
              ((lba & 0x0000000000ff0000ULL) << 24) |
              ((lba & 0x000000000000ff00ULL) << 40) |
              ((lba & 0x00000000000000ffULL) << 56);
        u32 bs = (u32)cap16.block_size;
        bs = ((bs >> 24) & 0xff) | ((bs >> 8) & 0xff00) |
             ((bs << 8) & 0xff0000) | ((bs << 24) & 0xff000000);
        *last_lba = lba;
        *block_size = bs;
        return 0;
    }

    /* Fall back to READ CAPACITY (10) */
    u8 cdb10[10] = { SCSI_READ_CAPACITY_10, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    struct scsi_read_capacity_10 cap10;
    ret = bot_transfer(bot_dev, cdb10, sizeof(cdb10), &cap10, sizeof(cap10), BOT_DIR_IN);
    if (ret == 0) {
        u32 lba32 = (cap10.last_lba >> 24) | ((cap10.last_lba >> 8) & 0xff00) |
                    ((cap10.last_lba << 8) & 0xff0000) | (cap10.last_lba << 24);
        u32 bs = (cap10.block_size >> 24) | ((cap10.block_size >> 8) & 0xff00) |
                 ((cap10.block_size << 8) & 0xff0000) | (cap10.block_size << 24);
        *last_lba = lba32;
        *block_size = bs;
        return 0;
    }

    fprintf(stderr, "bot: both READ CAPACITY (16) and (10) failed\n");
    return -1;
}

int
bot_read_10(struct bot_device *bot_dev, u32 lba, u16 blocks, void *buf)
{
    u8 cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_READ_10;
    cdb[2] = (lba >> 24) & 0xff;
    cdb[3] = (lba >> 16) & 0xff;
    cdb[4] = (lba >> 8) & 0xff;
    cdb[5] = lba & 0xff;
    cdb[7] = (blocks >> 8) & 0xff;
    cdb[8] = blocks & 0xff;

    u32 xfer_len = blocks * 512;  /* block size assumed 512, adjust if needed */
    return bot_transfer(bot_dev, cdb, sizeof(cdb), buf, xfer_len, BOT_DIR_IN);
}

int
bot_write_10(struct bot_device *bot_dev, u32 lba, u16 blocks, const void *buf)
{
    u8 cdb[10];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_WRITE_10;
    cdb[2] = (lba >> 24) & 0xff;
    cdb[3] = (lba >> 16) & 0xff;
    cdb[4] = (lba >> 8) & 0xff;
    cdb[5] = lba & 0xff;
    cdb[7] = (blocks >> 8) & 0xff;
    cdb[8] = blocks & 0xff;

    u32 xfer_len = blocks * 512;
    return bot_transfer(bot_dev, cdb, sizeof(cdb), (void *)buf, xfer_len, BOT_DIR_OUT);
}

int
bot_test_unit_ready(struct bot_device *bot_dev)
{
    u8 cdb[6] = { SCSI_TEST_UNIT_READY, 0, 0, 0, 0, 0 };
    return bot_transfer(bot_dev, cdb, sizeof(cdb), NULL, 0, 0);
}

int
bot_request_sense(struct bot_device *bot_dev, struct scsi_request_sense *sense)
{
    u8 cdb[6];
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SCSI_REQUEST_SENSE;
    cdb[4] = sizeof(*sense);

    memset(sense, 0, sizeof(*sense));
    return bot_transfer(bot_dev, cdb, sizeof(cdb), sense, sizeof(*sense), BOT_DIR_IN);
}

int
bot_get_max_lun(void *handle, int slot_id, void *ep0_ring, int *max_lun)
{
    u8 setup[8] = {
        0xA1,  /* bmRequestType: Device-to-Host, class, interface */
        BOT_GET_MAX_LUN,
        0x00, 0x00,  /* wValue */
        0x00, 0x00,  /* wIndex (interface 0) */
        0x01, 0x00,  /* wLength: 1 */
    };

    u8 result;
    int ret = xhci_control_transfer(handle, slot_id, ep0_ring, setup,
                                    &result, 1, 1);
    if (ret < 0) {
        *max_lun = 0;  /* device doesn't support — assume single LUN */
        return 0;
    }
    *max_lun = result & 0x0f;
    return 0;
}

/****************************************************************
 * BOT device setup — find endpoints, initialise transfer rings
 ****************************************************************/

int
bot_setup(void *handle, int slot_id, void *ep0_ring,
          struct bot_device *bot_dev)
{
    struct xhci_state *state = xhci_state_load("xhci_state.dat");
    if (!state) {
        fprintf(stderr, "bot: cannot load xhci_state.dat\n");
        return -1;
    }

    /* Find the slot matching our slot_id */
    struct xhci_state_slot_entry *sl = NULL;
    for (u32 i = 0; i < state->n_slots; i++) {
        if ((int)state->slots[i].slot_id == slot_id) {
            sl = &state->slots[i];
            break;
        }
    }
    if (!sl) {
        fprintf(stderr, "bot: slot %d not found in state\n", slot_id);
        xhci_state_free(state);
        return -1;
    }

    /* Scan endpoints for bulk IN/OUT */
    int bulk_in_ep = 0, bulk_out_ep = 0;
    u32 max_packet = 0;

    for (u32 i = 0; i < sl->n_endpoints; i++) {
        struct xhci_state_ep_info *ep = &sl->endpoints[i];
        if (ep->ep_index == 0)
            continue;  /* skip EP0 */

        int ep_num = ep->ep_index & 0x0f;
        int dir = ep->ep_index & 0x10;  /* 0x10 = IN */

        /* Check endpoint type: 2 = bulk */
        if (ep->ep_type != 2)
            continue;

        if (dir) {
            if (bulk_in_ep == 0) {
                bulk_in_ep = ep_num;
                if (ep->max_packet_size > max_packet)
                    max_packet = ep->max_packet_size;
            }
        } else {
            if (bulk_out_ep == 0) {
                bulk_out_ep = ep_num;
                if (ep->max_packet_size > max_packet)
                    max_packet = ep->max_packet_size;
            }
        }
    }

    if (!bulk_in_ep || !bulk_out_ep) {
        fprintf(stderr, "bot: no bulk endpoints found (in=%d out=%d)\n",
                bulk_in_ep, bulk_out_ep);
        xhci_state_free(state);
        return -1;
    }

    fprintf(stderr, "bot: bulk IN ep=%d, bulk OUT ep=%d, max_packet=%u\n",
            bulk_in_ep, bulk_out_ep, max_packet);

    /* Create transfer rings */
    bot_dev->bulk_in_ring = xhci_transfer_ring_create(handle, 32);
    bot_dev->bulk_out_ring = xhci_transfer_ring_create(handle, 32);
    if (!bot_dev->bulk_in_ring || !bot_dev->bulk_out_ring) {
        fprintf(stderr, "bot: transfer ring creation failed\n");
        if (bot_dev->bulk_in_ring)
            xhci_transfer_ring_free(handle, bot_dev->bulk_in_ring);
        xhci_state_free(state);
        return -1;
    }

    /* Configure the transfer rings as normal transfer rings in the device context.
     * We need to issue a Configure Endpoint command to set up the endpoint contexts
     * with the transfer ring addresses.
     *
     * xHCI device context layout (context entry indices):
     *   0  = Slot Context
     *   1  = EP0
     *   2  = EP1 OUT, 3 = EP1 IN
     *   4  = EP2 OUT, 5 = EP2 IN
     *   ...
     *   30 = EP15 OUT, 31 = EP15 IN
     */

    /* Allocate input context for Configure Endpoint */
    u64 input_ctx_phys;
    void *ctx_virt = xhci_dma_alloc(handle, 4096, &input_ctx_phys);
    if (!ctx_virt) {
        fprintf(stderr, "bot: input context alloc failed\n");
        xhci_state_free(state);
        return -1;
    }
    memset(ctx_virt, 0, 4096);

    u32 *ctx = ctx_virt;

    /* Map ep_index to xHCI context entry index.
     * ep_index = ep_num | (dir << 4) where dir=1 for IN.
     * Context entry = ep_num * 2 + dir (for ep_num > 0). */
    int bulk_in_ctx  = bulk_in_ep  * 2 + 1;  /* dir=1 for IN  */
    int bulk_out_ctx = bulk_out_ep * 2 + 0;  /* dir=0 for OUT */

    /* A0C: add bulk IN and bulk OUT context entries */
    ctx[0] = (1 << bulk_in_ctx) | (1 << bulk_out_ctx);

    /* Bulk IN endpoint context (context index = bulk_in_ctx) */
    u32 *ep_in_ctx = ctx + bulk_in_ctx * 8;
    ep_in_ctx[0] = 1;  /* EP State = Running */
    u64 ep_in_phys = xhci_transfer_ring_phys(handle, bot_dev->bulk_in_ring);
    int dcs = xhci_transfer_ring_cycle_state(bot_dev->bulk_in_ring);
    ((u64 *)ep_in_ctx)[1] = ep_in_phys | (dcs & 1);  /* Dequeue pointer + DCS */
    ep_in_ctx[3] = (8 << 16) | (0 << 8) | max_packet;  /* interval=8, cerr=3, mps */

    /* Bulk OUT endpoint context (context index = bulk_out_ctx) */
    u32 *ep_out_ctx = ctx + bulk_out_ctx * 8;
    ep_out_ctx[0] = 1;  /* EP State = Running */
    u64 ep_out_phys = xhci_transfer_ring_phys(handle, bot_dev->bulk_out_ring);
    dcs = xhci_transfer_ring_cycle_state(bot_dev->bulk_out_ring);
    ((u64 *)ep_out_ctx)[1] = ep_out_phys | (dcs & 1);
    ep_out_ctx[3] = (8 << 16) | (0 << 8) | max_packet;

    /* Configure Endpoint */
    if (xhci_configure_endpoint(handle, slot_id, input_ctx_phys) < 0) {
        fprintf(stderr, "bot: configure endpoint failed\n");
        xhci_dma_free(handle, ctx_virt, 4096);
        xhci_state_free(state);
        return -1;
    }

    fprintf(stderr, "bot: endpoints configured (IN ep=%d, OUT ep=%d)\n",
            bulk_in_ep, bulk_out_ep);

    /* Fill bot_device struct */
    bot_dev->handle = handle;
    bot_dev->slot_id = slot_id;
    bot_dev->lun = 0;
    bot_dev->ep0_ring = ep0_ring;
    bot_dev->bulk_in_ep = bulk_in_ep;
    bot_dev->bulk_out_ep = bulk_out_ep;
    bot_dev->max_packet = max_packet;
    bot_dev->next_tag = 0;

    /* Get max LUN */
    bot_get_max_lun(handle, slot_id, ep0_ring, &bot_dev->lun);
    fprintf(stderr, "bot: max LUN = %d\n", bot_dev->lun);

    xhci_dma_free(handle, ctx_virt, 4096);
    xhci_state_free(state);
    return 0;
}
