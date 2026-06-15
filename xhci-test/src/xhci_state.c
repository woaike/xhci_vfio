// XHCI state save/load/restore.
//
// Serializes enumerated device state to a binary file and
// restores it on subsequent runs.

#include "xhci_state.h"
#include "xhci.h"
#include "xhci_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#define HDR_SIZE 29

/****************************************************************
// CRC32 (IEEE 802.3 polynomial)
 ****************************************************************/

static u32 crc32_table[256];
static int crc32_table_init = 0;

static void
crc32_init_table(void)
{
    if (crc32_table_init) return;
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        for (int j = 0; j < 8; j++)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320 : 0);
        crc32_table[i] = crc;
    }
    crc32_table_init = 1;
}

static u32
compute_crc32(const void *data, size_t len)
{
    crc32_init_table();
    u32 crc = 0xFFFFFFFF;
    const u8 *p = (const u8 *)data;
    for (size_t i = 0; i < len; i++)
        crc = (crc >> 8) ^ crc32_table[(crc ^ p[i]) & 0xFF];
    return crc ^ 0xFFFFFFFF;
}

/****************************************************************
// Binary I/O helpers
 ****************************************************************/

static int
read_exact(int fd, void *buf, size_t len)
{
    ssize_t n = read(fd, buf, len);
    return (size_t)n == len ? 0 : -1;
}

static int
write_exact(int fd, const void *buf, size_t len)
{
    ssize_t n = write(fd, buf, len);
    return (size_t)n == len ? 0 : -1;
}

/****************************************************************
// Save state
 ****************************************************************/

int
xhci_state_save(void *handle, const struct xhci_state *state, const char *path)
{
    (void)handle;

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("xhci_state_save: open");
        return -1;
    }

    u8 hdr[HDR_SIZE];
    memset(hdr, 0, HDR_SIZE);
    memcpy(hdr, XHCI_STATE_MAGIC, XHCI_STATE_MAGIC_LEN);
    u32 ver = XHCI_STATE_VERSION;
    memcpy(hdr + 8, &ver, 4);
    hdr[16] = state->cap_length;
    memcpy(hdr + 17, &state->db_off, 4);
    memcpy(hdr + 21, &state->rts_off, 4);
    u32 n_slots = state->n_slots;
    memcpy(hdr + 25, &n_slots, 4);

    if (write_exact(fd, hdr, HDR_SIZE) < 0) {
        perror("xhci_state_save: write header");
        close(fd);
        return -1;
    }
    fprintf(stderr, "[STATE] Header written: magic=%s ver=%d n_slots=%d\n",
            XHCI_STATE_MAGIC, XHCI_STATE_VERSION, state->n_slots);

    // First pass: compute slot data size
    size_t slot_data_size = 0;
    for (u32 s = 0; s < state->n_slots; s++) {
        const struct xhci_state_slot_entry *sl = &state->slots[s];
        slot_data_size += 4 + 4 + 4 + 1 + 4 + 4;
        slot_data_size += 18;
        slot_data_size += 2;
        slot_data_size += sl->config_descr_total_len;
        slot_data_size += sl->n_endpoints * (4 + 1 + 2 + 4 + 1 + 1 + 1 + 1);
        slot_data_size += sl->n_rings * (4 + 4 + 4);
        for (u32 r = 0; r < sl->n_rings; r++) {
            if (sl->rings[r])
                slot_data_size += sl->rings[r]->n_trbs * 16;
        }
    }

    u8 *slot_buf = malloc(slot_data_size);
    if (!slot_buf) {
        perror("xhci_state_save: malloc");
        close(fd);
        return -1;
    }

    size_t off = 0;
    for (u32 s = 0; s < state->n_slots; s++) {
        const struct xhci_state_slot_entry *sl = &state->slots[s];

        memcpy(slot_buf + off, &sl->slot_id, 4); off += 4;
        memcpy(slot_buf + off, &sl->port_index, 4); off += 4;
        memcpy(slot_buf + off, &sl->portsc_snapshot, 4); off += 4;
        slot_buf[off] = sl->speed; off += 1;
        memcpy(slot_buf + off, &sl->route_string, 4); off += 4;
        memcpy(slot_buf + off, &sl->n_endpoints, 4); off += 4;

        memcpy(slot_buf + off, &sl->dev_descriptor, 18); off += 18;
        memcpy(slot_buf + off, &sl->config_descr_total_len, 2); off += 2;
        if (sl->config_descriptor && sl->config_descr_total_len > 0) {
            memcpy(slot_buf + off, sl->config_descriptor, sl->config_descr_total_len);
            off += sl->config_descr_total_len;
        }

        for (u32 e = 0; e < sl->n_endpoints; e++) {
            const struct xhci_state_ep_info *ep = &sl->endpoints[e];
            memcpy(slot_buf + off, &ep->ep_index, 4); off += 4;
            slot_buf[off] = ep->ep_type; off += 1;
            memcpy(slot_buf + off, &ep->max_packet_size, 2); off += 2;
            memcpy(slot_buf + off, &ep->avg_trb_len, 4); off += 4;
            slot_buf[off] = ep->mult; off += 1;
            slot_buf[off] = ep->cerr; off += 1;
            slot_buf[off] = ep->max_burst; off += 1;
            slot_buf[off] = ep->interval; off += 1;
        }

        for (u32 r = 0; r < sl->n_rings; r++) {
            struct xhci_state_transfer_ring *tr = sl->rings[r];
            memcpy(slot_buf + off, &tr->n_trbs, 4); off += 4;
            memcpy(slot_buf + off, &tr->enqueue_pos, 4); off += 4;
            memcpy(slot_buf + off, &tr->cycle_state, 4); off += 4;
            if (tr->trb_data && tr->n_trbs > 0) {
                memcpy(slot_buf + off, tr->trb_data, tr->n_trbs * 16);
                off += tr->n_trbs * 16;
            }
        }
    }

    // CRC over header (after CRC field) + slot data
    size_t crc_total = (HDR_SIZE - 16) + slot_data_size;
    u8 *crc_buf = malloc(crc_total);
    memcpy(crc_buf, hdr + 16, HDR_SIZE - 16);
    memcpy(crc_buf + HDR_SIZE - 16, slot_buf, slot_data_size);
    u32 crc = compute_crc32(crc_buf, crc_total);
    free(crc_buf);

    // Patch CRC into header
    memcpy(hdr + 12, &crc, 4);
    if (lseek(fd, 0, SEEK_SET) != 0) {
        perror("xhci_state_save: lseek");
        free(slot_buf);
        close(fd);
        return -1;
    }
    if (write_exact(fd, hdr, HDR_SIZE) < 0) {
        perror("xhci_state_save: rewrite header");
        free(slot_buf);
        close(fd);
        return -1;
    }
    off_t end_pos = lseek(fd, 0, SEEK_END);
    if (end_pos < 0) {
        perror("xhci_state_save: lseek SEEK_END");
        free(slot_buf);
        close(fd);
        return -1;
    }

    if (write_exact(fd, slot_buf, slot_data_size) < 0) {
        perror("xhci_state_save: write slot data");
        free(slot_buf);
        close(fd);
        return -1;
    }

    fprintf(stderr, "[STATE] Slot data written: %zu bytes, CRC=0x%08x\n",
            slot_data_size, crc);
    fprintf(stderr, "[STATE] Save complete\n");

    free(slot_buf);
    close(fd);
    return 0;
}

/****************************************************************
// Load state
 ****************************************************************/

struct xhci_state *
xhci_state_load(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("xhci_state_load: open");
        return NULL;
    }

    u8 hdr[HDR_SIZE];
    if (read_exact(fd, hdr, HDR_SIZE) < 0) {
        fprintf(stderr, "xhci_state_load: read header failed\n");
        close(fd);
        return NULL;
    }

    if (memcmp(hdr, XHCI_STATE_MAGIC, XHCI_STATE_MAGIC_LEN) != 0) {
        fprintf(stderr, "xhci_state_load: bad magic\n");
        close(fd);
        return NULL;
    }

    u32 ver;
    memcpy(&ver, hdr + 8, 4);
    if (ver != XHCI_STATE_VERSION) {
        fprintf(stderr, "xhci_state_load: version mismatch %u != %d\n",
                ver, XHCI_STATE_VERSION);
        close(fd);
        return NULL;
    }

    u32 stored_crc;
    memcpy(&stored_crc, hdr + 12, 4);

    off_t file_size = lseek(fd, 0, SEEK_END);
    lseek(fd, HDR_SIZE, SEEK_SET);
    size_t slot_data_len = file_size - HDR_SIZE;

    u8 *slot_buf = malloc(slot_data_len);
    if (!slot_buf) {
        perror("xhci_state_load: malloc");
        close(fd);
        return NULL;
    }
    if (read_exact(fd, slot_buf, slot_data_len) < 0) {
        fprintf(stderr, "xhci_state_load: read slot data failed\n");
        free(slot_buf);
        close(fd);
        return NULL;
    }
    close(fd);

    // Verify CRC
    size_t crc_total = (HDR_SIZE - 16) + slot_data_len;
    u8 *crc_buf = malloc(crc_total);
    memcpy(crc_buf, hdr + 16, HDR_SIZE - 16);
    memcpy(crc_buf + HDR_SIZE - 16, slot_buf, slot_data_len);
    u32 computed_crc = compute_crc32(crc_buf, crc_total);
    free(crc_buf);

    if (computed_crc != stored_crc) {
        fprintf(stderr, "xhci_state_load: CRC mismatch (stored=0x%08x, computed=0x%08x)\n",
                stored_crc, computed_crc);
        free(slot_buf);
        return NULL;
    }

    // Parse header
    struct xhci_state *state = calloc(1, sizeof(*state));
    if (!state) {
        free(slot_buf);
        return NULL;
    }

    state->cap_length = hdr[16];
    memcpy(&state->db_off, hdr + 17, 4);
    memcpy(&state->rts_off, hdr + 21, 4);
    memcpy(&state->n_slots, hdr + 25, 4);
    state->max_slots = state->n_slots;

    fprintf(stderr, "[STATE] CRC verified: 0x%08x, n_slots=%d\n", stored_crc, state->n_slots);

    // Parse slots
    state->slots = calloc(state->n_slots, sizeof(struct xhci_state_slot_entry));
    if (!state->slots) {
        free(slot_buf);
        free(state);
        return NULL;
    }

    size_t pos = 0;
    for (u32 s = 0; s < state->n_slots; s++) {
        struct xhci_state_slot_entry *sl = &state->slots[s];

        memcpy(&sl->slot_id, slot_buf + pos, 4); pos += 4;
        memcpy(&sl->port_index, slot_buf + pos, 4); pos += 4;
        memcpy(&sl->portsc_snapshot, slot_buf + pos, 4); pos += 4;
        sl->speed = slot_buf[pos]; pos += 1;
        memcpy(&sl->route_string, slot_buf + pos, 4); pos += 4;
        memcpy(&sl->n_endpoints, slot_buf + pos, 4); pos += 4;

        memcpy(&sl->dev_descriptor, slot_buf + pos, 18); pos += 18;
        memcpy(&sl->config_descr_total_len, slot_buf + pos, 2); pos += 2;
        if (sl->config_descr_total_len > 0) {
            sl->config_descriptor = malloc(sl->config_descr_total_len);
            if (sl->config_descriptor) {
                memcpy(sl->config_descriptor, slot_buf + pos, sl->config_descr_total_len);
                pos += sl->config_descr_total_len;
            }
        }

        if (sl->n_endpoints > 0) {
            sl->endpoints = calloc(sl->n_endpoints, sizeof(struct xhci_state_ep_info));
            if (sl->endpoints) {
                for (u32 e = 0; e < sl->n_endpoints; e++) {
                    struct xhci_state_ep_info *ep = &sl->endpoints[e];
                    memcpy(&ep->ep_index, slot_buf + pos, 4); pos += 4;
                    ep->ep_type = slot_buf[pos]; pos += 1;
                    memcpy(&ep->max_packet_size, slot_buf + pos, 2); pos += 2;
                    memcpy(&ep->avg_trb_len, slot_buf + pos, 4); pos += 4;
                    ep->mult = slot_buf[pos]; pos += 1;
                    ep->cerr = slot_buf[pos]; pos += 1;
                    ep->max_burst = slot_buf[pos]; pos += 1;
                    ep->interval = slot_buf[pos]; pos += 1;
                }
            }
        }

        sl->n_rings = sl->n_endpoints > 0 ? sl->n_endpoints : 0;
        if (sl->n_rings > 0) {
            sl->rings = calloc(sl->n_rings, sizeof(struct xhci_state_transfer_ring *));
            for (u32 r = 0; r < sl->n_rings; r++) {
                struct xhci_state_transfer_ring *tr = calloc(1, sizeof(*tr));
                memcpy(&tr->n_trbs, slot_buf + pos, 4); pos += 4;
                memcpy(&tr->enqueue_pos, slot_buf + pos, 4); pos += 4;
                memcpy(&tr->cycle_state, slot_buf + pos, 4); pos += 4;
                if (tr->n_trbs > 0) {
                    tr->trb_data = malloc(tr->n_trbs * 16);
                    if (tr->trb_data) {
                        memcpy(tr->trb_data, slot_buf + pos, tr->n_trbs * 16);
                        pos += tr->n_trbs * 16;
                    }
                }
                sl->rings[r] = tr;
            }
        }

        sl->restored = 0;
        sl->ep0_ring = NULL;
    }

    free(slot_buf);
    return state;
}

/****************************************************************
// Restore hardware state
 ****************************************************************/

static int
build_input_context(void *handle,
                    const struct xhci_state_slot_entry *slot,
                    u64 ep0_ring_phys,
                    u64 *phys_out)
{
    struct xhci_inctx { u32 del; u32 add; u32 reserved_01[6]; };

    u32 hcc = xhci_read32(handle, 0x10);
    int context64 = (hcc >> 2) & 1;

    size_t ctx_size = 4096;

    void *ctx_virt = xhci_dma_alloc(handle, ctx_size, phys_out);
    if (!ctx_virt) {
        fprintf(stderr, "build_input_context: DMA alloc failed\n");
        return -1;
    }
    memset(ctx_virt, 0, ctx_size);

    struct xhci_inctx *in = (struct xhci_inctx *)ctx_virt;

    // Input Context: ADD is always at DWORD 1 for both 32B and 64B
    u32 *ctx = (u32 *)ctx_virt;
    ctx[0] = 0;  // DEL
    ctx[1] = 0x03;  // ADD: Slot(bit0) + EP0(bit1) = 0x03

    // Slot Context: depends on context64
    struct xhci_slotctx *slot_ctx = (void *)&in[1 << context64];
    if (context64) {
        // 64B context: Slot Context field layout same as 32B:
        //   DWORD 0: ContextEntries[31:27] | Speed[26:20]
        //   DWORD 1: InterrupterTarget[31:16] | RHPortNumber[15:0]
        slot_ctx->ctx[0] = (1 << 27) | ((u32)slot->speed << 20);  // ContextEntries=1, Speed
        slot_ctx->ctx[1] = (u32)(slot->port_index + 1) << 16;  // RHPortNumber at bits 31:16
    } else {
        // 32B context: DWORD 0 = ContextEntries[31:27] | Speed[26:20]
        //              DWORD 1 = RHPortNumber[31:16]
        slot_ctx->ctx[0] = (1 << 27) | ((u32)slot->speed << 20);
        slot_ctx->ctx[1] = (u32)(slot->port_index + 1) << 16;
    }

    // EP0 Context: per SeaBIOS approach
    // 32B: Slot at DWORD 8, EP0 at DWORD 16
    // 64B: Slot at DWORD 16, EP0 at DWORD 32
    struct xhci_epctx *ep0_ctx;
    if (context64) {
        ep0_ctx = (void *)((u8 *)ctx_virt + 128);  // DWORD 32 for 64B
    } else {
        ep0_ctx = (void *)((u8 *)ctx_virt + 64);   // DWORD 16 for 32B
    }
    u16 mps0 = (slot->speed == 4) ? 512 : 64;
    // EP0 Context per xHCI spec:
    // DWORD 1: CErr[1:0] | EP Type[5:3] | MaxPacketSize[31:16]
    // EP Type 4 = Control Endpoint, CErr 3 = 3 errors allowed
    ep0_ctx->ctx[0] = 0;
    ep0_ctx->ctx[1] = (4 << 3) | (3 << 0) | ((u32)mps0 << 16);  // Control, CErr=3, MaxPacketSize
    ep0_ctx->length = (u32)mps0;
    ep0_ctx->deq_low  = (u32)ep0_ring_phys | 1;
    ep0_ctx->deq_high = (u32)(ep0_ring_phys >> 32);

    return 0;
}

int
xhci_state_restore(void *handle, struct xhci_state *state)
{
    int failures = 0;

    if (xhci_full_init(handle) < 0) {
        fprintf(stderr, "xhci_state_restore: full_init failed\n");
        return -1;
    }

    fprintf(stdout, "  Controller initialized, restoring %d slots...\n", state->n_slots);

    for (u32 s = 0; s < state->n_slots; s++) {
        struct xhci_state_slot_entry *slot = &state->slots[s];
        slot->restored = 0;

        fprintf(stdout, "  Slot %d (port %d): ", slot->slot_id, slot->port_index);
        fflush(stdout);

        // Warm reset
        if (xhci_port_warm_reset(handle, slot->port_index) < 0) {
            fprintf(stderr, "warm reset failed\n");
            failures++;
            continue;
        }

        usleep(200000);  // 200ms

        u32 portsc = xhci_port_read(handle, slot->port_index);
        if (!(portsc & XHCI_PORTSC_CCS)) {
            fprintf(stderr, "device not connected on port %d\n", slot->port_index);
            failures++;
            continue;
        }

        // Enable slot
        int new_slot_id = xhci_enable_slot(handle);
        if (new_slot_id < 0) {
            fprintf(stderr, "enable_slot failed\n");
            failures++;
            continue;
        }

        if (new_slot_id != (int)slot->slot_id) {
            fprintf(stderr, "slot_id mismatch: got %d, expected %d\n",
                    new_slot_id, slot->slot_id);
            failures++;
            continue;
        }

        // Create EP0 transfer ring
        void *ep0_ring = xhci_transfer_ring_create(handle, 32);
        if (!ep0_ring) {
            fprintf(stderr, "EP0 ring creation failed\n");
            failures++;
            continue;
        }

        // Build input context
        u64 input_ctx_phys;
        if (build_input_context(handle, slot,
                                xhci_transfer_ring_phys(handle, ep0_ring),
                                &input_ctx_phys) < 0) {
            fprintf(stderr, "build_input_context failed\n");
            failures++;
            continue;
        }

        // Address device
        if (xhci_address_device(handle, slot->slot_id, input_ctx_phys) < 0) {
            fprintf(stderr, "address_device failed\n");
            failures++;
            continue;
        }

        // Verify DCBAA
        if (xhci_dcbaa_verify(handle, slot->slot_id) < 0) {
            fprintf(stderr, "DCBAA entry not set after address_device\n");
            failures++;
            continue;
        }

        // Store EP0 ring handle
        slot->ep0_ring = ep0_ring;

        if (slot->n_endpoints > 1) {
            fprintf(stdout, "addressed, DCBAA verified (EPs > 1, configure deferred)\n");
        } else {
            fprintf(stdout, "addressed, DCBAA verified\n");
        }

        slot->restored = 1;
    }

    return failures;
}

/****************************************************************
// Free state
 ****************************************************************/

void
xhci_state_free(struct xhci_state *state)
{
    if (!state) return;

    for (u32 s = 0; s < state->n_slots; s++) {
        struct xhci_state_slot_entry *sl = &state->slots[s];
        if (sl->config_descriptor)
            free(sl->config_descriptor);
        if (sl->endpoints)
            free(sl->endpoints);
        if (sl->rings) {
            for (u32 r = 0; r < sl->n_rings; r++) {
                if (sl->rings[r]) {
                    if (sl->rings[r]->trb_data)
                        free(sl->rings[r]->trb_data);
                    free(sl->rings[r]);
                }
            }
            free(sl->rings);
        }
    }
    if (state->slots)
        free(state->slots);
    free(state);
}
