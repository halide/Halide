#include "HalideRuntimeHexagonDma.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "hexagon_dma_pool.h"
#include "mini_hexagon_dma.h"
#include "printer.h"
#include "runtime_internal.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace HexagonDma {

extern WEAK halide_device_interface_t hexagon_dma_device_interface;

#define descriptor_size 64

// DMA device handle structure, which holds all the necessary frame related parameters.
// To be used for DMA transfer.
struct dma_device_handle {
    uint8_t *buffer;
    uint16_t offset_rdx;
    uint16_t offset_rdy;
    uint16_t offset_wrx;
    uint16_t offset_wry;
    void *dma_engine;
    int frame_width;
    int frame_height;
    int frame_stride;
    bool is_ubwc;
    bool is_write;
    t_eDmaFmt fmt;
};

// Allocating memory for DMA device handle. The life time of this memory is till the frame
// is active in DMA process.
inline dma_device_handle *malloc_device_handle() {
    dma_device_handle *dev = (dma_device_handle *)malloc(sizeof(dma_device_handle));
    dev->buffer = nullptr;
    dev->offset_rdx = 0;
    dev->offset_rdy = 0;
    dev->offset_wrx = 0;
    dev->offset_wry = 0;
    dev->dma_engine = nullptr;
    dev->frame_width = 0;
    dev->frame_height = 0;
    dev->frame_stride = 0;
    dev->is_ubwc = false;
    dev->fmt = eDmaFmt_RawData;
    dev->is_write = false;
    return dev;
}

// Data Structure for chaining of DMA descriptors.
typedef struct desc_pool {
    void *descriptor;
    bool used;
    struct desc_pool *next;
} desc_pool_t;

typedef desc_pool_t *pdesc_pool;

WEAK pdesc_pool dma_desc_pool = nullptr;
WEAK halide_mutex hexagon_desc_mutex;

}  // namespace HexagonDma
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide

using namespace Halide::Runtime::Internal::HexagonDma;

namespace {

// Core logic for DMA descriptor Pooling. The idea is to reuse the Allocated cache for descriptors,
// if it is free. In case of un availability of free descriptors, two new descriptors are allocated in the cache
// and make them available in the pool (128B is the minimum cache size that can be locked)
void *desc_pool_get(void *user_context) {
    ScopedMutexLock lock(&hexagon_desc_mutex);
    pdesc_pool temp = dma_desc_pool;
    pdesc_pool prev = nullptr;
    // Walk the list
    while (temp != nullptr) {
        if (!temp->used) {
            temp->used = true;
            return (void *)temp->descriptor;
        }
        prev = temp;
        temp = temp->next;
    }
    // If we are still here that means temp was null.
    // We have to allocate two descriptors here, to lock a full cache line
    temp = (pdesc_pool)malloc(sizeof(desc_pool_t));
    if (temp == nullptr) {
        error(user_context) << "Hexagon: Out of memory (malloc failed for DMA descriptor pool)\n";
        return nullptr;
    }
    uint8_t *desc = (uint8_t *)HAP_cache_lock(sizeof(char) * descriptor_size * 2, nullptr);
    if (desc == nullptr) {
        free(temp);
        error(user_context) << "Hexagon: Out of memory (HAP_cache_lock failed for descriptor)\n";
        return nullptr;
    }
    temp->descriptor = (void *)desc;
    temp->used = true;

    // Now allocate the second element in list
    temp->next = (pdesc_pool)malloc(sizeof(desc_pool_t));
    if (temp->next != nullptr) {
        (temp->next)->descriptor = (void *)(desc + descriptor_size);
        (temp->next)->used = false;
        (temp->next)->next = nullptr;
    } else {
        // no need to throw error since we allocate two descriptor at a time
        // but only use one
        debug(user_context) << "Hexagon: malloc failed\n";
    }

    if (prev != nullptr) {
        prev->next = temp;
    } else if (dma_desc_pool == nullptr) {
        dma_desc_pool = temp;
    }
    return (void *)temp->descriptor;
}

void desc_pool_put(void *user_context, void *desc) {
    ScopedMutexLock lock(&hexagon_desc_mutex);
    halide_abort_if_false(user_context, desc);
    pdesc_pool temp = dma_desc_pool;
    while (temp != nullptr) {
        if (temp->descriptor == desc) {
            temp->used = false;
            return;
        }
        temp = temp->next;
    }
    error(user_context) << "Hexagon: desc not found " << desc << "\n";
}

// DMA descriptor freeing logic, Two descriptors at a time will be freed.
void desc_pool_free(void *user_context) {
    ScopedMutexLock lock(&hexagon_desc_mutex);
    pdesc_pool temp = dma_desc_pool;
    while (temp != nullptr) {
        pdesc_pool temp2 = temp;
        temp = temp->next;
        if (temp2->descriptor != nullptr) {
            HAP_cache_unlock(temp2->descriptor);
        }
        free(temp2);
        temp2 = temp;
        if (temp != nullptr) {
            temp = temp->next;
            free(temp2);
        }
    }

    // Mark pool is empty, to avoid re-freeing
    dma_desc_pool = nullptr;
}

// User ptovided Image format to DMA format conversion.
inline t_eDmaFmt halide_hexagon_get_dma_format(void *user_context, const halide_hexagon_image_fmt_t format) {
    //A giant switch case to match image formats to dma formats
    switch (format) {
    case halide_hexagon_fmt_NV12:
        return eDmaFmt_NV12;
    case halide_hexagon_fmt_NV12_Y:
        return eDmaFmt_NV12_Y;
    case halide_hexagon_fmt_NV12_UV:
        return eDmaFmt_NV12_UV;
    case halide_hexagon_fmt_P010:
        return eDmaFmt_P010;
    case halide_hexagon_fmt_P010_Y:
        return eDmaFmt_P010_Y;
    case halide_hexagon_fmt_P010_UV:
        return eDmaFmt_P010_UV;
    case halide_hexagon_fmt_TP10:
        return eDmaFmt_TP10;
    case halide_hexagon_fmt_TP10_Y:
        return eDmaFmt_TP10_Y;
    case halide_hexagon_fmt_TP10_UV:
        return eDmaFmt_TP10_UV;
    case halide_hexagon_fmt_NV124R:
        return eDmaFmt_NV124R;
    case halide_hexagon_fmt_NV124R_Y:
        return eDmaFmt_NV124R_Y;
    case halide_hexagon_fmt_NV124R_UV:
        return eDmaFmt_NV124R_UV;
    case halide_hexagon_fmt_RawData:
        return eDmaFmt_RawData;
    default:
        error(user_context) << "Hexagon: DMA Format Mismatch " << format << "\n";
        return eDmaFmt_MAX;
    }
}

// The core logic of DMA Transfer. This API uses the DMA device handle populated prior to calling this
// and does the necessary steps for performing the DMA Operation.
int halide_hexagon_dma_wrapper(void *user_context, struct halide_buffer_t *src,
                               struct halide_buffer_t *dst) {

    dma_device_handle *dev = nullptr;
    dev = (dma_device_handle *)src->device;

    debug(user_context)
        << "Hexagon dev handle: buffer: " << dev->buffer
        << " dev_offset(rdx: : " << dev->offset_rdx << " rdy: " << dev->offset_rdy << ")"
        << " dev_offset(wrx: : " << dev->offset_wrx << " wry: " << dev->offset_wry << ")"
        << " frame(w: " << dev->frame_width << " h: " << dev->frame_height << " s: " << dev->frame_stride << ")"
        << "\n";

    debug(user_context)
        << "size_in_bytes() src: " << static_cast<uint32>(src->size_in_bytes())
        << " dst: " << static_cast<uint32>(dst->size_in_bytes())
        << "\n";

    // Assert if buffer dimensions do not fulfill the format requirements
    if (dev->fmt == eDmaFmt_RawData) {
        halide_abort_if_false(user_context, src->dimensions <= 3);
    }

    if ((dev->fmt == eDmaFmt_NV12_Y) ||
        (dev->fmt == eDmaFmt_P010_Y) ||
        (dev->fmt == eDmaFmt_TP10_Y) ||
        (dev->fmt == eDmaFmt_NV124R_Y)) {
        halide_abort_if_false(user_context, src->dimensions == 2);
    }

    if ((dev->fmt == eDmaFmt_NV12_UV) ||
        (dev->fmt == eDmaFmt_P010_UV) ||
        (dev->fmt == eDmaFmt_TP10_UV) ||
        (dev->fmt == eDmaFmt_NV124R_UV)) {
        halide_abort_if_false(user_context, src->dimensions == 3);
        halide_abort_if_false(user_context, src->dim[0].stride == 2);
        halide_abort_if_false(user_context, src->dim[2].stride == 1);
        halide_abort_if_false(user_context, src->dim[2].min == 0);
        halide_abort_if_false(user_context, src->dim[2].extent == 2);
    }

    t_StDmaWrapper_RoiAlignInfo stWalkSize = {
        static_cast<uint16>(dst->dim[0].extent * dst->dim[0].stride),
        static_cast<uint16>(dst->dim[1].extent)};
    int nRet = nDmaWrapper_GetRecommendedWalkSize(dev->fmt, dev->is_ubwc, &stWalkSize);

    int roi_stride = nDmaWrapper_GetRecommendedIntermBufStride(dev->fmt, &stWalkSize, dev->is_ubwc);
    int roi_width = stWalkSize.u16W;
    int roi_height = stWalkSize.u16H;

    debug(user_context)
        << "Hexagon: Recommended ROI(w: " << roi_width << " h: " << roi_height << " s: " << roi_stride << ")\n";

    // account for folding, where the dim[1].stride reflects the fold_storage stride
    if (dst->dim[1].stride > roi_stride) {
        roi_stride = dst->dim[1].stride;
    }

    // Assert if destination stride is a multipe of recommended stride
    halide_abort_if_false(user_context, ((dst->dim[1].stride % roi_stride) == 0));

    // Return nullptr if descriptor is not allocated
    void *desc_addr = desc_pool_get(user_context);
    if (desc_addr == nullptr) {
        debug(user_context) << "Hexagon: DMA descriptor allocation error\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    int buf_size = roi_stride * roi_height * src->type.bytes();
    debug(user_context) << "Hexagon: cache buffer size " << buf_size << "\n";

    t_StDmaWrapper_DmaTransferSetup stDmaTransferParm;
    stDmaTransferParm.eFmt = dev->fmt;
    stDmaTransferParm.u16FrameW = dev->frame_width;
    stDmaTransferParm.u16FrameH = dev->frame_height;
    stDmaTransferParm.u16FrameStride = dev->frame_stride;
    stDmaTransferParm.u16RoiW = roi_width;
    stDmaTransferParm.u16RoiH = roi_height;
    stDmaTransferParm.u16RoiStride = roi_stride;
    stDmaTransferParm.bIsFmtUbwc = dev->is_ubwc;
    stDmaTransferParm.bUse16BitPaddingInL2 = 0;
    stDmaTransferParm.pDescBuf = desc_addr;
    stDmaTransferParm.pTcmDataBuf = reinterpret_cast<void *>(dst->host);
    stDmaTransferParm.pFrameBuf = dev->buffer;
    if (dev->is_write) {
        stDmaTransferParm.eTransferType = eDmaWrapper_L2ToDdr;
        stDmaTransferParm.u16RoiX = dev->offset_wrx * dst->dim[0].stride;
        stDmaTransferParm.u16RoiY = dev->offset_wry;
    } else {
        stDmaTransferParm.eTransferType = eDmaWrapper_DdrToL2;
        stDmaTransferParm.u16RoiX = (dev->offset_rdx + dst->dim[0].min) * dst->dim[0].stride;
        stDmaTransferParm.u16RoiY = dev->offset_rdy + dst->dim[1].min;
    }

    // Raw Format Planar
    if ((dev->fmt == eDmaFmt_RawData) &&
        (dst->dimensions == 3)) {
        stDmaTransferParm.u16RoiY = dev->offset_rdy + dst->dim[1].min + (dst->dim[2].min * src->dim[1].stride);
    }

    // DMA Driver implicitly halves the Height and Y Offset for chroma, based on Y/UV
    // planar relation for 4:2:0 format, to adjust the for plane size difference.
    // This driver adjustment is compensated here for Halide that treats Y/UV separately.
    // i.e. ROI size is same for both Luma and Chroma
    if ((dev->fmt == eDmaFmt_NV12_UV) ||
        (dev->fmt == eDmaFmt_P010_UV) ||
        (dev->fmt == eDmaFmt_TP10_UV) ||
        (dev->fmt == eDmaFmt_NV124R_UV)) {
        stDmaTransferParm.u16RoiH = roi_height * 2;
        if (dev->is_write) {
            stDmaTransferParm.u16RoiY = stDmaTransferParm.u16RoiY * 2;
        } else {
            stDmaTransferParm.u16RoiY = (stDmaTransferParm.u16RoiY - dev->frame_height) * 2;
        }
        debug(user_context)
            << "Hexagon: u16Roi(X: " << stDmaTransferParm.u16RoiX << " Y: " << stDmaTransferParm.u16RoiY
            << " W: " << stDmaTransferParm.u16RoiW << " H: " << stDmaTransferParm.u16RoiH << ")"
            << " dst->dim[1].min: " << dst->dim[1].min << "\n";
    }

    void *dma_engine = halide_hexagon_allocate_from_dma_pool(user_context, dev->dma_engine);
    if (!dma_engine) {
        debug(user_context) << "Hexagon: Dma Engine Allocation Faliure\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    debug(user_context)
        << "Hexagon: " << dma_engine << " transfer: " << stDmaTransferParm.pDescBuf << "\n";
    nRet = nDmaWrapper_DmaTransferSetup(dma_engine, &stDmaTransferParm);
    if (nRet != QURT_EOK) {
        error(user_context) << "Hexagon: DMA Transfer Error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    debug(user_context) << "Hexagon: " << dma_engine << " move\n";
    nRet = nDmaWrapper_Move(dma_engine);
    if (nRet != QURT_EOK) {
        error(user_context) << "Hexagon: nDmaWrapper_Move error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    debug(user_context) << "Hexagon: " << dma_engine << " wait\n";
    nRet = nDmaWrapper_Wait(dma_engine);
    if (nRet != QURT_EOK) {
        error(user_context) << "Hexagon: nDmaWrapper_Wait error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    desc_pool_put(user_context, desc_addr);
    nRet = halide_hexagon_free_to_dma_pool(user_context, dma_engine, dev->dma_engine);
    if (nRet != halide_error_code_success) {
        debug(user_context) << "halide_hexagon_free_from_dma_pool error:" << nRet << "\n";
        return nRet;
    }
    return halide_error_code_success;
}

}  // namespace

extern "C" {

WEAK int halide_hexagon_dma_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_malloc (user_context: " << user_context
        << ", buf: " << *buf << ")\n";

    if (buf->device) {
        debug(user_context) << "Hexagon: buffer already has a device. No action required\n";
        return halide_error_code_success;
    }

    size_t size = buf->size_in_bytes();
    halide_abort_if_false(user_context, size != 0);

    void *mem = halide_malloc(user_context, size);
    if (!mem) {
        error(user_context) << "Hexagon: Out of memory (halide_malloc failed for device_malloc)\n";
        return halide_error_code_out_of_memory;
    }

    int err = halide_hexagon_dma_device_wrap_native(user_context, buf,
                                                    reinterpret_cast<uint64_t>(mem));
    if (err != 0) {
        halide_free(user_context, mem);
        return halide_error_code_device_malloc_failed;
    }

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_free(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_free (user_context: " << user_context
        << ", buf: " << *buf << ")\n";

    dma_device_handle *dev = (dma_device_handle *)buf->device;
    void *mem = dev->buffer;
    halide_hexagon_dma_device_detach_native(user_context, buf);

    halide_free(user_context, mem);

    // This is to match what the default implementation of halide_device_free does.
    buf->set_device_dirty(false);
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_allocate_engine(void *user_context, void **dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_allocate_engine (user_context: " << user_context << ")\n";

    halide_abort_if_false(user_context, dma_engine);
    debug(user_context) << "    dma_allocate_dma_engine -> ";
    *dma_engine = halide_hexagon_allocate_dma_resource(user_context);
    debug(user_context) << "        " << dma_engine << "\n";
    if (!*dma_engine) {
        debug(user_context) << "dma_allocate_dma_engine failed.\n";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_deallocate_engine(void *user_context, void *dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_deallocate_engine (user_context: " << user_context
        << ", dma_engine: " << dma_engine << ")\n";

    halide_abort_if_false(user_context, dma_engine);

    // Its safe to free descriptors here, even on 1st engine of multi-engines deallocation, since its called outside of pipeline
    // If descriptors are needed on pipeline re-entry, the pool will also re-populate
    desc_pool_free(user_context);

    // Free DMA Resources
    int err = halide_hexagon_free_dma_resource(user_context, dma_engine);
    debug(user_context) << "Hexagon:     dma_free_dma_pool done\n";
    if (err != 0) {
        debug(user_context) << "Hexagon: Free DMA/Cache Pool failed.\n";
        return halide_error_code_generic_error;
    }
    return halide_error_code_success;
}

namespace {

inline int dma_prepare_for_copy(void *user_context, struct halide_buffer_t *buf, void *dma_engine, bool is_ubwc, t_eDmaFmt fmt, bool is_write) {
    halide_abort_if_false(user_context, dma_engine);
    dma_device_handle *dev = reinterpret_cast<dma_device_handle *>(buf->device);
    dev->dma_engine = dma_engine;
    dev->is_ubwc = is_ubwc;
    dev->fmt = fmt;
    dev->is_write = is_write;
    // To compensate driver's adjustment for UV plane size
    if ((dev->fmt == eDmaFmt_NV12_UV) ||
        (dev->fmt == eDmaFmt_P010_UV) ||
        (dev->fmt == eDmaFmt_TP10_UV) ||
        (dev->fmt == eDmaFmt_NV124R_UV)) {
        dev->frame_height = dev->frame_height * 2;
    }

    return halide_error_code_success;
}

}  // namespace

WEAK int halide_hexagon_dma_prepare_for_copy_to_host(void *user_context, struct halide_buffer_t *buf,
                                                     void *dma_engine, bool is_ubwc, halide_hexagon_image_fmt_t fmt) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_prepare_for_copy_to_host (user_context: " << user_context
        << ", buf: " << *buf << ", dma_engine: " << dma_engine << ")\n";
    t_eDmaFmt format = halide_hexagon_get_dma_format(user_context, fmt);
    return dma_prepare_for_copy(user_context, buf, dma_engine, is_ubwc, format, false);
}

WEAK int halide_hexagon_dma_prepare_for_copy_to_device(void *user_context, struct halide_buffer_t *buf,
                                                       void *dma_engine, bool is_ubwc, halide_hexagon_image_fmt_t fmt) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_prepare_for_copy_to_device (user_context: " << user_context
        << ", buf: " << *buf << ", dma_engine: " << dma_engine << ")\n";
    t_eDmaFmt format = halide_hexagon_get_dma_format(user_context, fmt);
    return dma_prepare_for_copy(user_context, buf, dma_engine, is_ubwc, format, true);
}

WEAK int halide_hexagon_dma_unprepare(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_unprepare (user_context: " << user_context
        << ", buf: " << *buf << ")\n";
    //TODO Now that FinishFrame is called by Hexagon DMA Pool Module, need to check if this function is redundant
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                        const struct halide_device_interface_t *dst_device_interface,
                                        struct halide_buffer_t *dst) {

    halide_abort_if_false(user_context, dst_device_interface == nullptr ||
                                            dst_device_interface == &hexagon_dma_device_interface);

    if (src->device_dirty() &&
        src->device_interface != &hexagon_dma_device_interface) {
        halide_abort_if_false(user_context, dst_device_interface == &hexagon_dma_device_interface);
        // If the source is not hexagon_dma or host memory, ask the source
        // device interface to copy to dst host memory first.
        debug(user_context) << "Hexagon: src->device_interface != &hexagon_dma_device_interface\n";
        int err = src->device_interface->impl->buffer_copy(user_context, src, nullptr, dst);
        if (err) {
            error(user_context) << "Hexagon: halide_hexagon_dma_buffer_copy (not DMA) failed: " << err << "\n";
            return err;
        }
        // Now just copy from src to host
        src = dst;
    }

    bool from_host = !src->device_dirty() && src->host != nullptr;
    bool to_host = !dst_device_interface;

    halide_abort_if_false(user_context, from_host || src->device);
    halide_abort_if_false(user_context, to_host || dst->device);

    halide_abort_if_false(user_context, (!from_host && to_host) || (from_host && !to_host));

    debug(user_context)
        << "Hexagon: halide_hexagon_dma_buffer_copy (user_context: " << user_context
        << ", src: " << src << ", dst: " << dst << "\n"
        << ", DMA Read: " << to_host << ", DMA Write: " << from_host << ")\n";

    int nRet;
    if (dst_device_interface == &hexagon_dma_device_interface) {
        nRet = halide_hexagon_dma_wrapper(user_context, dst, src);
    } else {
        nRet = halide_hexagon_dma_wrapper(user_context, src, dst);
    }

    return nRet;
}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_copy_to_device (user_context: " << user_context
        << ", buf: " << *buf << ")\n";

    error(user_context) << "Hexagon: halide_hexagon_dma_copy_to_device not implemented\n";
    return halide_error_code_copy_to_device_failed;
}

WEAK int halide_hexagon_dma_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_copy_to_host (user_context: " << user_context
        << ", buf: " << *buf << ")\n";

    error(user_context) << "Hexagon: halide_hexagon_dma_copy_to_host not implemented\n";
    return halide_error_code_copy_to_device_failed;
}

WEAK int halide_hexagon_dma_device_crop(void *user_context,
                                        const struct halide_buffer_t *src,
                                        struct halide_buffer_t *dst) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_crop (user_context: " << user_context
        << " src: " << *src << " dst: " << *dst << ")\n";

    dst->device_interface = src->device_interface;

    const dma_device_handle *src_dev = (dma_device_handle *)src->device;
    dma_device_handle *dst_dev = malloc_device_handle();
    halide_abort_if_false(user_context, dst_dev);
    dst_dev->buffer = src_dev->buffer;
    dst_dev->offset_wrx = src_dev->offset_wrx + dst->dim[0].min - src->dim[0].min;
    dst_dev->offset_wry = src_dev->offset_wry + dst->dim[1].min - src->dim[1].min;
    dst_dev->dma_engine = src_dev->dma_engine;
    dst_dev->frame_width = src_dev->frame_width;
    dst_dev->frame_height = src_dev->frame_height;
    dst_dev->frame_stride = src_dev->frame_stride;
    dst_dev->is_ubwc = src_dev->is_ubwc;
    dst_dev->is_write = src_dev->is_write;
    dst_dev->fmt = src_dev->fmt;

    dst->device = reinterpret_cast<uint64_t>(dst_dev);

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_slice(void *user_context,
                                         const struct halide_buffer_t *src,
                                         int slice_dim, int slice_pos, struct halide_buffer_t *dst) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_slice (user_context: " << user_context
        << " src: " << *src << " dst: " << *dst << ")\n";

    halide_abort_if_false(user_context, 0);

    error(user_context) << "Hexagon: halide_hexagon_dma_device_slice not implemented\n";
    return halide_error_code_generic_error;
}

WEAK int halide_hexagon_dma_device_release_crop(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_release_crop (user_context: " << user_context
        << " buf: " << *buf << ")\n";

    halide_abort_if_false(user_context, buf->device);
    free((dma_device_handle *)buf->device);
    buf->device = 0;

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_sync(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_sync (user_context: " << user_context
        << " buf: " << *buf << ")\n";
    // TODO We need to check if any DMA specific action is required here
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_wrap_native(void *user_context, struct halide_buffer_t *buf,
                                               uint64_t handle) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_wrap_native (user_context: " << user_context
        << " buf: " << *buf << " handle: " << handle << ")\n";

    halide_abort_if_false(user_context, buf->device == 0);
    if (buf->device != 0) {
        error(user_context) << "Hexagon: halide_hexagon_dma_device_wrap_native buffer already has a device\n";
        return halide_error_code_device_wrap_native_failed;
    }

    buf->device_interface = &hexagon_dma_device_interface;
    buf->device_interface->impl->use_module();

    dma_device_handle *dev = malloc_device_handle();
    halide_abort_if_false(user_context, dev);
    dev->buffer = reinterpret_cast<uint8_t *>(handle);
    dev->dma_engine = nullptr;
    dev->frame_width = buf->dim[0].extent * buf->dim[0].stride;
    dev->frame_height = buf->dim[1].extent;
    dev->frame_stride = buf->dim[1].stride;
    buf->device = reinterpret_cast<uint64_t>(dev);

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_detach_native(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_detach_native (user_context: " << user_context
        << " buf: " << *buf << ")\n";

    if (buf->device == 0) {
        error(user_context) << "Hexagon: halide_hexagon_dma_device_detach_native buffer without a device\n";
        return halide_error_code_device_detach_native_failed;
    }
    halide_abort_if_false(user_context, buf->device_interface == &hexagon_dma_device_interface);
    dma_device_handle *dev = (dma_device_handle *)buf->device;
    free(dev);
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = nullptr;

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_and_host_malloc (user_context: " << user_context
        << " buf: " << *buf << ")\n";

    return halide_default_device_and_host_malloc(user_context, buf, &hexagon_dma_device_interface);
}

WEAK int halide_hexagon_dma_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_and_host_free (user_context: " << user_context
        << " buf: " << *buf << ")\n";

    return halide_default_device_and_host_free(user_context, buf, &hexagon_dma_device_interface);
}

WEAK const halide_device_interface_t *halide_hexagon_dma_device_interface() {
    return &hexagon_dma_device_interface;
}

WEAK int halide_hexagon_dma_device_release(void *user_context) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_release (user_context: " << user_context << ")\n";

    return 0;
}

WEAK int halide_hexagon_dma_power_mode_voting(void *user_context, halide_hexagon_power_mode_t cornercase) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_power_voting (user_context: " << user_context << ")\n";
    switch (cornercase) {
    case halide_hexagon_power_low_2:
        return nDmaWrapper_PowerVoting(PW_SVS2);
    case halide_hexagon_power_low:
        return nDmaWrapper_PowerVoting(PW_SVS);
    case halide_hexagon_power_low_plus:
        return nDmaWrapper_PowerVoting(PW_SVS_L1);
    case halide_hexagon_power_nominal:
        return nDmaWrapper_PowerVoting(PW_NORMAL);
    case halide_hexagon_power_nominal_plus:
        return nDmaWrapper_PowerVoting(PW_NORMAL_L1);
    case halide_hexagon_power_turbo:
        return nDmaWrapper_PowerVoting(PW_TURBO);
    case halide_hexagon_power_default:
        return nDmaWrapper_PowerVoting(~PW_SVS);
    default:
        error(user_context) << "Hexagon: halide_hexagon_dma_power_voting power mode (" << cornercase << ") not found\n";
        return halide_error_code_generic_error;
    }
}

}  // extern "C" linkage

namespace Halide {
namespace Runtime {
namespace Internal {
namespace HexagonDma {

WEAK halide_device_interface_impl_t hexagon_dma_device_interface_impl = {
    halide_use_jit_module,
    halide_release_jit_module,
    halide_hexagon_dma_device_malloc,
    halide_hexagon_dma_device_free,
    halide_hexagon_dma_device_sync,
    halide_hexagon_dma_device_release,
    halide_hexagon_dma_copy_to_host,
    halide_hexagon_dma_copy_to_device,
    halide_hexagon_dma_device_and_host_malloc,
    halide_hexagon_dma_device_and_host_free,
    halide_hexagon_dma_buffer_copy,
    halide_hexagon_dma_device_crop,
    halide_hexagon_dma_device_slice,
    halide_hexagon_dma_device_release_crop,
    halide_hexagon_dma_device_wrap_native,
    halide_hexagon_dma_device_detach_native,
};

WEAK halide_device_interface_t hexagon_dma_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_buffer_copy,
    halide_device_crop,
    halide_device_slice,
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    nullptr,
    &hexagon_dma_device_interface_impl};

}  // namespace HexagonDma
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
