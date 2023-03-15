#include "HalideRuntime.h"
#include "runtime_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

extern void *tcm_alloc_on_bank(size_t size, unsigned char alignment,
                               unsigned char bank);
extern void tcm_free(void *ptr);

void *halide_tcm_malloc(void *user_context, unsigned int x) {
    const size_t alignment = ::halide_internal_malloc_alignment();
    void *ptr = tcm_alloc_on_bank(x, alignment, /*bank=*/0);
    // Try to allocate on the second bank.
    if (!ptr) {
        ptr = tcm_alloc_on_bank(x, alignment, /*bank=*/1);
    }
    return ptr;
}

void halide_tcm_free(void *user_context, void *ptr) {
    tcm_free(ptr);
}

struct idma_buffer_t;

typedef enum {
    IDMA_1D_DESC = 1,
    IDMA_2D_DESC = 2,
    IDMA_64B_DESC = 4
} idma_type_t;

typedef enum {
    IDMA_ERR_NO_BUF = -40,      /* No valid ring buffer */
    IDMA_ERR_BAD_DESC = -20,    /* Descriptor not correct */
    IDMA_ERR_BAD_CHAN,          /* Invalid channel number */
    IDMA_ERR_NOT_INIT,          /* iDMAlib and HW not initialized  */
    IDMA_ERR_TASK_NOT_INIT,     /* Cannot scheduled uninitialized task  */
    IDMA_ERR_BAD_TASK,          /* Task not correct  */
    IDMA_ERR_BUSY,              /* iDMA busy when not expected */
    IDMA_ERR_IN_SPEC_MODE,      /* iDMAlib in unexpected mode */
    IDMA_ERR_NOT_SPEC_MODE,     /* iDMAlib in unexpected mode */
    IDMA_ERR_TASK_EMPTY,        /* No descs in the task/buffer */
    IDMA_ERR_TASK_OUTSTAND_NEG, /* Number of outstanding descs is a negative value
                                 */
    IDMA_ERR_TASK_IN_ERROR,     /* Task in error */
    IDMA_ERR_BUFFER_IN_ERROR,   /* Buffer in error */
    IDMA_ERR_NO_NEXT_TASK,      /* Next task to process is missing  */
    IDMA_ERR_BUF_OVFL,          /* Attempt to schedule too many descriptors */
    IDMA_ERR_HW_ERROR,          /* HW error detected */
    IDMA_ERR_BAD_INIT,          /* Bad idma_init args */
    IDMA_OK = 0,                /* No error */
    IDMA_CANT_SLEEP = 1,        /* Cannot sleep (no pending descriptors) */
} idma_status_t;

typedef void (*idma_callback_fn)(void *arg);

#define DESC_IDMA_PRIOR_H 0x08000    /* QoS high */
#define DESC_NOTIFY_W_INT 0x80000000 /* trigger interrupt on completion */

idma_status_t halide_idma_init_loop(int32_t ch, idma_buffer_t *bufh,
                                    idma_type_t type, int32_t ndescs,
                                    void *cb_data,
                                    idma_callback_fn cb_func);

int32_t halide_idma_copy_desc(int32_t ch, void *dst, void *src, size_t size,
                              uint32_t flags);

int32_t idma_copy_2d_desc(int32_t ch, void *dst, void *src, size_t size,
                          uint32_t flags, uint32_t nrows,
                          uint32_t src_pitch, uint32_t dst_pitch);

int32_t halide_idma_buffer_status(int32_t ch);

idma_status_t halide_idma_sleep(int32_t ch);

idma_buffer_t *idma_descriptor_alloc(idma_type_t type, int count);
void idma_descriptor_free(idma_buffer_t *buffer);

int32_t halide_idma_desc_done(int32_t ch, int32_t index);

static const int kMaxChannelCount = 8;
static const int kMaxRequestCount = 4;

namespace {
void cleanup_on_init_failure(int32_t channel_count, void **dma_desc) {
    if (!dma_desc) {
        return;
    }
    for (int ix = 0; ix < channel_count; ix++) {
        if (dma_desc[ix] != nullptr) {
            idma_descriptor_free((idma_buffer_t *)dma_desc[ix]);
        }
    }
    halide_tcm_free(nullptr, dma_desc);
}
}  // namespace

void **halide_init_dma(int32_t channel_count) {
    if (channel_count > kMaxChannelCount) {
        return nullptr;
    }

    // Allocate storage for DMA buffers/descriptors.
    void **dma_desc = (void **)halide_tcm_malloc(nullptr, sizeof(void *) * kMaxChannelCount);

    if (!dma_desc) {
        return nullptr;
    }

    // Reset pointers to DMA buffers/descriptors.
    for (int ix = 0; ix < kMaxChannelCount; ix++) {
        dma_desc[ix] = nullptr;
    }

    // Allocate DMA descriptors and initialize DMA loop.
    for (int ix = 0; ix < channel_count; ix++) {
        dma_desc[ix] =
            idma_descriptor_alloc(IDMA_2D_DESC, /*count=*/kMaxRequestCount);
        if (!dma_desc[ix]) {
            cleanup_on_init_failure(channel_count, dma_desc);
            return nullptr;
        }

        idma_status_t init_status = halide_idma_init_loop(
            ix, (idma_buffer_t *)dma_desc[ix], IDMA_2D_DESC, kMaxRequestCount, nullptr, nullptr);

        if (init_status != IDMA_OK) {
            cleanup_on_init_failure(channel_count, dma_desc);
            return nullptr;
        }
    }

    return dma_desc;
}

int32_t halide_xtensa_copy_1d(int channel, void *dst, int32_t dst_base,
                              void *src, int32_t src_base, int extent,
                              int item_size) {
    while (halide_idma_buffer_status(channel) == kMaxRequestCount) {
    }
    int32_t id =
        halide_idma_copy_desc(channel, (uint8_t *)dst + dst_base * item_size,
                              (uint8_t *)src + src_base * item_size,
                              extent * item_size, DESC_IDMA_PRIOR_H);
    return id;
}

int32_t halide_xtensa_copy_2d(int channel, void *dst, int32_t dst_base,
                              int32_t dst_stride, void *src, int32_t src_base,
                              int32_t src_stride, int extent0, int extent1,
                              int item_size) {
    while (halide_idma_buffer_status(channel) == kMaxRequestCount) {
    }
    int32_t id =
        idma_copy_2d_desc(channel, (uint8_t *)dst + dst_base * item_size,
                          (uint8_t *)src + src_base * item_size,
                          extent0 * item_size, DESC_IDMA_PRIOR_H, extent1,
                          src_stride * item_size, dst_stride * item_size);

    return id;
}

int32_t halide_xtensa_wait_for_copy(int32_t channel) {
    while (halide_idma_buffer_status(channel) > 0) {
    }

    return 0;
}

void halide_release_dma(int32_t channel_count, void **dma_desc) {
    for (int ix = 0; ix < channel_count; ix++) {
        halide_xtensa_wait_for_copy(ix);
        idma_descriptor_free((idma_buffer_t *)dma_desc[ix]);
    }

    halide_tcm_free(nullptr, dma_desc);
}

#ifdef __cplusplus
}  // extern "C"
#endif
