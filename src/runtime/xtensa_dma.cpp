#include "HalideRuntime.h"
#include "runtime_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IDMA_USE_INTR 0
#define IDMA_APP_USE_XTOS 1
#define IDMA_USE_MULTICHANNEL 1
#include <xtensa/config/core-isa.h>
#include <xtensa/idma.h>
#include <xtensa/xmem_bank.h>

static void *tcm_alloc_on_bank(size_t size, unsigned char alignment,
                               unsigned char bank) {
    return xmem_bank_alloc(bank, size, alignment, NULL);
}

static void tcm_free(void *ptr) {
    xmem_bank_free(-1, ptr);
}

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

static idma_buffer_t *idma_descriptor_alloc(idma_type_t type, int count) {
    return (idma_buffer_t *)
        xmem_bank_alloc(0, IDMA_BUFFER_SIZE(count, type),
                        /*align */ 4, /*status*/ nullptr);
}

static void idma_descriptor_free(idma_buffer_t *buffer) {
    xmem_bank_free(0, buffer);
}

static const int kMaxChannelCount = XCHAL_IDMA_NUM_CHANNELS;
static const int kMaxRequestCount = 4;

namespace {
void cleanup_on_init_failure(int32_t channel_count, void **dma_desc) {
    if (!dma_desc) {
        return;
    }
    if (channel_count > kMaxChannelCount) {
        channel_count = kMaxChannelCount;
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
        channel_count = kMaxChannelCount;
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

        idma_status_t init_status = idma_init_loop(
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
    if (channel >= kMaxChannelCount) {
        channel = 0;
    }
    while (idma_buffer_status(channel) == kMaxRequestCount) {
    }
    int32_t id =
        idma_copy_desc(channel, (uint8_t *)dst + dst_base * item_size,
                       (uint8_t *)src + src_base * item_size,
                       extent * item_size, DESC_IDMA_PRIOR_H);
    return id;
}

int32_t halide_xtensa_copy_2d(int channel, void *dst, int32_t dst_base,
                              int32_t dst_stride, void *src, int32_t src_base,
                              int32_t src_stride, int extent0, int extent1,
                              int item_size) {
    if (channel >= kMaxChannelCount) {
        channel = 0;
    }
    while (idma_buffer_status(channel) == kMaxRequestCount) {
    }
    int32_t id =
        idma_copy_2d_desc(channel, (uint8_t *)dst + dst_base * item_size,
                          (uint8_t *)src + src_base * item_size,
                          extent0 * item_size, DESC_IDMA_PRIOR_H, extent1,
                          src_stride * item_size, dst_stride * item_size);

    return id;
}

int32_t halide_xtensa_wait_for_copy(int32_t channel) {
    if (channel >= kMaxChannelCount) {
        channel = 0;
    }
    while (idma_buffer_status(channel) > 0) {
    }

    return 0;
}

void halide_release_dma(int32_t channel_count, void **dma_desc) {
    if (channel_count >= kMaxChannelCount) {
        channel_count = kMaxChannelCount;
    }
    for (int ix = 0; ix < channel_count; ix++) {
        halide_xtensa_wait_for_copy(ix);
        idma_descriptor_free((idma_buffer_t *)dma_desc[ix]);
    }

    halide_tcm_free(nullptr, dma_desc);
}

#ifdef __cplusplus
}  // extern "C"
#endif
