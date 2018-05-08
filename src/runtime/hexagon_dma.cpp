#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "HalideRuntimeHexagonDma.h"
#include "printer.h"
#include "mini_hexagon_dma.h"

namespace Halide { namespace Runtime { namespace Internal { namespace HexagonDma {

extern WEAK halide_device_interface_t hexagon_dma_device_interface;

struct dma_device_handle {
    uint8_t *buffer;
    int offset_x;
    int offset_y;
    void *dma_engine;
    int frame_width;
    int frame_height;
    int frame_stride;
    bool is_ubwc;
    bool is_write;
    t_eDmaFmt fmt;
    void *cache_buf;
};

dma_device_handle *malloc_device_handle() {
    dma_device_handle *dev = (dma_device_handle *)malloc(sizeof(dma_device_handle));
    dev->buffer = 0;
    dev->offset_x = 0;
    dev->offset_y = 0;
    dev->dma_engine = 0;
    dev->frame_width = 0;
    dev->frame_height = 0;
    dev->frame_stride = 0;
    dev->cache_buf = 0;
    dev->is_ubwc = 0;
    dev->fmt = eDmaFmt_RawData;
    dev->is_write = 0;
    return dev;
}

typedef struct desc_pool {
    void *descriptor;
    bool used;
    struct desc_pool *next;
} desc_pool_t;

typedef desc_pool_t *pdesc_pool;
static pdesc_pool dma_desc_pool = NULL;
#define descriptor_size 64

static void *desc_pool_get (void *user_context) {
    // TODO: Add Mutex locking for access to dma_desc_pool ( To be Thread safe )
    pdesc_pool temp = dma_desc_pool;
    pdesc_pool prev = NULL;
    // Walk the list
    while (temp != NULL) {
        if (!temp->used) {
            temp->used = true;
            return (void*) temp->descriptor;
        }
        prev = temp;
        temp=temp->next;
    }
    // If we are still here that means temp was null.
    // We have to allocate two descriptors here
    temp = (pdesc_pool) malloc(sizeof(desc_pool_t));
    if (temp == NULL) {
        error(user_context) << "malloc failed\n";
        return NULL;
    }
    uint8_t *desc = (uint8_t *)HAP_cache_lock(sizeof(char) * descriptor_size * 2, NULL);
    if (desc == NULL) {
        free(temp);
        error(user_context) << "HAP_cache_lock failed\n";
        return NULL;
    }
    temp->descriptor = (void *)desc;
    temp->used = true;

    // Now allocate the second element in list
    temp->next = (pdesc_pool) malloc(sizeof(desc_pool_t));
    if (temp->next != NULL) {
        (temp->next)->descriptor = (void *)(desc+descriptor_size);
        (temp->next)->used = false;
        (temp->next)->next = NULL;
    } else {
        // no need to throw error since we allocate two descriptor at a time
        // but only use one
        debug(user_context) << "malloc failed\n" ;
    }

    if (prev != NULL) {
        prev->next = temp;
    } else if (dma_desc_pool == NULL) {
        dma_desc_pool = temp;
    }
    return (void *) temp->descriptor;
}

static void desc_pool_put (void *user_context, void *desc) {
    halide_assert(user_context, desc);
    pdesc_pool temp = dma_desc_pool;
    while (temp != NULL) {
        if (temp->descriptor == desc) {
            temp->used = false;
        }
        temp=temp->next;
    }
}
// Two descriptors at a time
static void desc_pool_free (void *user_context) {
    // TODO: Add Mutex locking for access to dma_desc_pool ( To be Thread safe )
    pdesc_pool temp = dma_desc_pool;
    while (temp != NULL) {
        pdesc_pool temp2 = temp;
        temp=temp->next;
        if (temp2->descriptor != NULL) {
            HAP_cache_unlock(temp2->descriptor);
        }
        free(temp2);
        temp2 = temp;
        if (temp != NULL) {
            temp=temp->next;
            free(temp2);
        }
    }
}

static inline void copy_from_cache(uint8_t *src, uint8_t *dest, int roi_height,
           int roi_stride, int dest_stride, int pixelsize, int linesize) {
     
    for (int y = 0; y < roi_height; y++) {
        int sy = y * roi_stride * pixelsize;
        int dy = y * dest_stride * pixelsize;
        memcpy(&dest[dy], &src[sy], linesize);
    }
} 

static int halide_hexagon_dma_wrapper (void *user_context, struct halide_buffer_t *src,
                                       struct halide_buffer_t *dst) {

    dma_device_handle *dev = (dma_device_handle *)src->device;

    debug(user_context)
        << "Hexagon dev handle: buffer: " << dev->buffer 
        << " dev offset (" << dev->offset_x << ", " << dev->offset_y << ") frame_width: " << dev->frame_width
        << " frame_height: " << dev->frame_height << " frame_stride: " << dev->frame_stride << "\n";

    int roi_stride = 0;
    int roi_width = 0;
    int roi_height = 0;

    // Changing the Format to Chroma or LUMA based on dimension    
    t_eDmaFmt currentFmt = dev->fmt;
    if ((dev->fmt == eDmaFmt_NV12_UV) ||
        (dev->fmt == eDmaFmt_P010_UV) ||
        (dev->fmt == eDmaFmt_TP10_UV) ||
        (dev->fmt == eDmaFmt_NV124R_UV)) {
        halide_assert(user_context, src->dimensions == 3);
        halide_assert(user_context, src->dim[0].stride == 2);
        halide_assert(user_context, src->dim[2].stride == 1);
        halide_assert(user_context, src->dim[2].min == 0);
        halide_assert(user_context, src->dim[2].extent == 2);
    }
    // TODO: Currently we can only handle 2-D RAW Format, Will revisit this later for > 2-D
    t_StDmaWrapper_RoiAlignInfo stWalkSize = {static_cast<uint16>(dst->dim[0].extent * dst->dim[0].stride), static_cast<uint16>(dst->dim[1].extent)};
    int nRet = nDmaWrapper_GetRecommendedWalkSize(dev->fmt, dev->is_ubwc, &stWalkSize);

    roi_stride = nDmaWrapper_GetRecommendedIntermBufStride(currentFmt, &stWalkSize, dev->is_ubwc);
    roi_width = stWalkSize.u16W;
    roi_height = stWalkSize.u16H;

    debug(user_context) << " roi_width " << roi_width;
    debug(user_context) << " roi_height " << roi_height;
    debug(user_context) << " roi_stride " << roi_stride;
    halide_assert(user_context,(dst->dim[1].stride >= roi_stride));

    // DMA driver Expect the Stride to be 256 Byte Aligned
    halide_assert(user_context, (roi_stride % 256) == 0);

    // Return NULL if descriptor is not allocated
    void *desc_addr = desc_pool_get(user_context);
    if (desc_addr == NULL) {
        error(user_context) << "Hexagon: DMA descriptor allocation error \n";
        return halide_error_code_device_buffer_copy_failed;
    }

    // Copy from Locked Cache to a temp DDR buffer
    // TODO: This should be removed once the cache locking is addressed inside Halide Pipeline
    int buf_size = roi_stride * roi_height * dst->type.bytes(); // dst->size_in_bytes();
    debug(user_context) << " cache buffer size " << buf_size << "\n";
    if (dev->cache_buf == 0) {
        dev->cache_buf = HAP_cache_lock(buf_size, 0);
    }
    // TODO: Currently we can only handle 2-D RAW Format, Will revisit this later for > 2-D
    // We need to make some adjustment to H, X and Y parameters for > 2-D RAW Format because DMA treat RAW as a flattened buffer
    t_StDmaWrapper_DmaTransferSetup stDmaTransferParm;
    stDmaTransferParm.eFmt                  = currentFmt; 
    stDmaTransferParm.u16FrameW             = dev->frame_width;
    stDmaTransferParm.u16FrameH             = dev->frame_height;
    stDmaTransferParm.u16FrameStride        = dev->frame_stride;
    stDmaTransferParm.u16RoiW               = roi_width;
    stDmaTransferParm.u16RoiH               = roi_height;
    stDmaTransferParm.u16RoiStride          = roi_stride;
    stDmaTransferParm.bIsFmtUbwc            = dev->is_ubwc;
    stDmaTransferParm.bUse16BitPaddingInL2  = 0;
    stDmaTransferParm.pDescBuf              = desc_addr;
    stDmaTransferParm.pTcmDataBuf           = dev->cache_buf;
    stDmaTransferParm.pFrameBuf             = dev->buffer;
    if (dev->is_write) {
        stDmaTransferParm.eTransferType     = eDmaWrapper_L2ToDdr;
    } else {
        stDmaTransferParm.eTransferType     = eDmaWrapper_DdrToL2;
    }
    stDmaTransferParm.u16RoiX               = dev->offset_x + dst->dim[0].min;
    stDmaTransferParm.u16RoiY               = dev->offset_y + dst->dim[1].min;

    // DMA Driver Halves the Height and Y Offset so that only Half the ROI Size of Luma is transferred  for chroma
    // We are compensating this assumption to meet  Halide perspective. i.e. ROI size is same for both Luma and Chroma    
    if ((currentFmt == eDmaFmt_NV12_UV) ||
        (currentFmt == eDmaFmt_P010_UV) ||
        (currentFmt == eDmaFmt_TP10_UV) ||
        (currentFmt == eDmaFmt_NV124R_UV)) {
        stDmaTransferParm.u16RoiW = roi_width * dst->dim[0].stride;
        stDmaTransferParm.u16RoiH = roi_height * 2;
        stDmaTransferParm.u16RoiY = (stDmaTransferParm.u16RoiY - dev->frame_height) * 2;
    }

    debug(user_context) << "Hexagon: " << dev->dma_engine << " transfer: " << stDmaTransferParm.pDescBuf << "\n" ;
    nRet = nDmaWrapper_DmaTransferSetup(dev->dma_engine, &stDmaTransferParm);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: DMA Transfer Error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }
    
    debug(user_context) << "Hexagon: " << dev->dma_engine << " move\n" ;
    nRet = nDmaWrapper_Move(dev->dma_engine);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: nDmaWrapper_Move error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }
    
    debug(user_context) << "Hexagon: " << dev->dma_engine << " wait\n" ;
    nRet = nDmaWrapper_Wait(dev->dma_engine);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: nDmaWrapper_Wait error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }

    uint8_t *dest = reinterpret_cast<uint8_t *>(dst->host);

    // Copy from Locked Cache to a temp DDR buffer
    // TODO: This should be removed once the cache locking is addressed inside Halide Pipeline
    if (dest) {
        const int pixelsize = dst->type.bytes();
        const int linesize = roi_width * pixelsize;
        uint8_t *cache_buf = reinterpret_cast<uint8_t *>(dev->cache_buf);
        debug(user_context) << "copy cache: " << "pixelsize= " << pixelsize << " linesize= " << linesize << "\n";
        copy_from_cache(cache_buf, dest, roi_height, roi_stride, dst->dim[1].stride, pixelsize, linesize); 
    }

    desc_pool_put(user_context, desc_addr);
    return halide_error_code_success;
}

}}}}  // namespace Halide::Runtime::Internal::HexagonDma

using namespace Halide::Runtime::Internal::HexagonDma;

extern "C" {

WEAK int halide_hexagon_dma_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    if (buf->device) {
        return halide_error_code_success;
    }

    size_t size = buf->size_in_bytes();
    halide_assert(user_context, size != 0);

    void *mem = halide_malloc(user_context, size);
    if (!mem) {
        error(user_context) << "halide_malloc failed\n";
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
        << ", buf: " << buf << ")\n";

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

    halide_assert(user_context, dma_engine);
    debug(user_context) << "    dma_allocate_dma_engine -> ";
    *dma_engine = (void *)hDmaWrapper_AllocDma();
    debug(user_context) << "        " << dma_engine << "\n";
    if (!*dma_engine) {
        error(user_context) << "dma_allocate_dma_engine failed.\n";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_deallocate_engine(void *user_context, void *dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_deallocate_engine (user_context: " << user_context
        << ", dma_engine: " << dma_engine << ")\n";
    debug(user_context) << "    dma_free_dma_engine\n";
    halide_assert(user_context, dma_engine);
    nDmaWrapper_FreeDma((t_DmaWrapper_DmaEngineHandle)dma_engine);
    return halide_error_code_success;
}


inline int dma_prepare_for_copy(void *user_context, struct halide_buffer_t *buf, void *dma_engine, bool is_ubwc, int fmt, bool is_write ) {

    halide_assert(user_context, dma_engine);
    dma_device_handle *dev = reinterpret_cast<dma_device_handle *>(buf->device);
    dev->dma_engine = dma_engine;
    dev->is_ubwc = is_ubwc;
    dev->fmt = (t_eDmaFmt) fmt;
    dev->is_write = is_write;
    if ((dev->fmt == eDmaFmt_NV12_UV) ||
        (dev->fmt == eDmaFmt_P010_UV) ||
        (dev->fmt == eDmaFmt_TP10_UV) ||
        (dev->fmt == eDmaFmt_NV124R_UV)) {
        dev->frame_height = dev->frame_height * 2;
    }
    return halide_error_code_success;

}


WEAK int halide_hexagon_dma_prepare_for_copy_to_host(void *user_context, struct halide_buffer_t *buf,
                                                     void *dma_engine, bool is_ubwc, int fmt ) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_prepare_for_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ", dma_engine: " << dma_engine << ")\n";
    return dma_prepare_for_copy(user_context, buf, dma_engine, is_ubwc, fmt, 0);
}
WEAK int halide_hexagon_dma_prepare_for_copy_to_device(void *user_context, struct halide_buffer_t *buf,
                                                     void *dma_engine, bool is_ubwc, int fmt ) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_prepare_for_copy_to_device (user_context: " << user_context
        << ", buf: " << buf << ", dma_engine: " << dma_engine << ")\n";
    return dma_prepare_for_copy(user_context, buf, dma_engine, is_ubwc, fmt, 1);
}

WEAK int halide_hexagon_dma_unprepare(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_deallocate_engine (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    halide_assert(user_context, buf->device_interface == halide_hexagon_dma_device_interface());
    halide_assert(user_context, buf->device);

    dma_device_handle *dev = reinterpret_cast<dma_device_handle *>(buf->device);

    debug(user_context) << "   dma_finish_frame -> ";
    int err = nDmaWrapper_FinishFrame(dev->dma_engine);
    desc_pool_free(user_context);
    if (dev->cache_buf) {
        HAP_cache_unlock(dev->cache_buf);
    }
    dev->cache_buf = 0;
    debug(user_context) << "        " << err << "\n";
    if (err != 0) {
        error(user_context) << "dma_finish_frame failed.\n";
        return halide_error_code_generic_error;
    }

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_buffer_copy(void *user_context, struct halide_buffer_t *src,
                                        const struct halide_device_interface_t *dst_device_interface,
                                        struct halide_buffer_t *dst) {
    // We only handle copies to hexagon_dma or to host
    // TODO: does device to device via DMA make sense?
    halide_assert(user_context, dst_device_interface == NULL ||
                  dst_device_interface == &hexagon_dma_device_interface);

    if (src->device_dirty() &&
        src->device_interface != &hexagon_dma_device_interface) {
        halide_assert(user_context, dst_device_interface == &hexagon_dma_device_interface);
        // If the source is not hexagon_dma or host memory, ask the source
        // device interface to copy to dst host memory first.
        int err = src->device_interface->impl->buffer_copy(user_context, src, NULL, dst);
        if (err) {
            return err;
        }
        // Now just copy from src to host
        src = dst;
    }

    bool from_host = !src->device_dirty() && src->host != NULL;
    bool to_host = !dst_device_interface;

    halide_assert(user_context, from_host || src->device);
    halide_assert(user_context, to_host || dst->device);

    // For now only copy device to host.
    // TODO: Figure out which other paths can be supported.
    halide_assert(user_context, !from_host && to_host);

    debug(user_context)
        << "Hexagon: halide_hexagon_dma_buffer_copy (user_context: " << user_context
        << ", src: " << src << ", dst: " << dst << ")\n";

    int nRet = halide_hexagon_dma_wrapper(user_context, src, dst);
   
    return nRet;
}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context, halide_buffer_t *buf) {
    int err = halide_hexagon_dma_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    // TODO: Implement this with dma_move_data.
    error(user_context) << "haldie_hexagon_dma_copy_to_device not implemented.\n";
    return halide_error_code_copy_to_device_failed;
}

WEAK int halide_hexagon_dma_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    halide_assert(user_context, buf->host && buf->device);
    dma_device_handle *dev = (dma_device_handle *)buf->device;

    t_StDmaWrapper_RoiAlignInfo stWalkSize = {static_cast<uint16>(buf->dim[0].extent * buf->dim[0].stride), static_cast<uint16>(buf->dim[1].extent)};
    int nRet = nDmaWrapper_GetRecommendedWalkSize(eDmaFmt_RawData, false, &stWalkSize);
    int roi_stride = nDmaWrapper_GetRecommendedIntermBufStride(eDmaFmt_RawData, &stWalkSize, false);
    int roi_width = stWalkSize.u16W;
    int roi_height = stWalkSize.u16H;
    // DMA driver Expect the Stride to be 256 Byte Aligned
    halide_assert(user_context,(buf->dim[1].stride== roi_stride));

    // The descriptor allocation failure must return Error
    void *desc_addr = desc_pool_get(user_context);
    if (desc_addr == NULL) {
        error(user_context) << "Hexagon: DMA descriptor allocation error \n";
        return halide_error_code_copy_to_host_failed;
    }

    // Here we do Locked L2 allocation for DMA Transfer
    // Since there is allocation of temporary buffer
    // We copy from L2 to temp buffer
    // To Do This needs to be streamline 
    int size = buf->size_in_bytes();
    if (dev->cache_buf == 0) {
        dev->cache_buf = HAP_cache_lock((sizeof(uint8_t) * size), 0);
    }

    t_StDmaWrapper_DmaTransferSetup stDmaTransferParm;
    stDmaTransferParm.eFmt                  = eDmaFmt_RawData;
    stDmaTransferParm.u16FrameW             = dev->frame_width;
    stDmaTransferParm.u16FrameH             = dev->frame_height;
    stDmaTransferParm.u16FrameStride        = dev->frame_stride;
    stDmaTransferParm.u16RoiW               = roi_width;
    stDmaTransferParm.u16RoiH               = roi_height;
    stDmaTransferParm.u16RoiStride          = roi_stride;
    stDmaTransferParm.bUse16BitPaddingInL2  = 0;
    stDmaTransferParm.bIsFmtUbwc            = 0;
    stDmaTransferParm.pDescBuf              = desc_addr;
    stDmaTransferParm.pTcmDataBuf           = dev->cache_buf;
    stDmaTransferParm.pFrameBuf             = dev->buffer;
    stDmaTransferParm.eTransferType         = eDmaWrapper_DdrToL2;
    stDmaTransferParm.u16RoiX               = dev->offset_x;
    stDmaTransferParm.u16RoiY               = dev->offset_y;

    debug(user_context) << "Hexagon:" << dev->dma_engine << "transfer" << stDmaTransferParm.pDescBuf << "\n" ;
    nRet = nDmaWrapper_DmaTransferSetup(dev->dma_engine, &stDmaTransferParm);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: DMA Transfer Error" << "\n"; 
        return halide_error_code_copy_to_host_failed; 
    }

    debug(user_context) << "Hexagon:" << dev->dma_engine << "move\n" ;
    nRet = nDmaWrapper_Move(dev->dma_engine);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: DMA Transfer Error" << "\n";
        return halide_error_code_copy_to_host_failed;
    }
    
    nRet = nDmaWrapper_Wait(dev->dma_engine);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: nDmaWrapper_Wait error: " << nRet << "\n";
        return halide_error_code_copy_to_host_failed;
    }
    // TODO: Doing a manual copy from the DMA'ed Locked L2 cache to Halide destination DDR buffer
    // This should be removed once the cache locking is addressed inside Halide Pipeline
    void *dest = reinterpret_cast<void *>(buf->host);
    if (dest) {
        memcpy(dest, dev->cache_buf, size);
    }
    desc_pool_put(user_context, desc_addr);
    
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_crop(void *user_context,
                                        const struct halide_buffer_t *src,
                                        struct halide_buffer_t *dst) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_crop\n";

    dst->device_interface = src->device_interface;

    const dma_device_handle *src_dev = (dma_device_handle *)src->device;
    dma_device_handle *dst_dev = malloc_device_handle();
    dst_dev->buffer = src_dev->buffer;
    // TODO: It's messy to have both this offset and the buffer mins,
    // try to reduce complexity here.
    dst_dev->offset_x = src_dev->offset_x + dst->dim[0].min - src->dim[0].min;
    dst_dev->offset_y = src_dev->offset_y + dst->dim[1].min - src->dim[1].min;
    dst_dev->dma_engine = src_dev->dma_engine;

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_release_crop(void *user_context,
                                                struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_release_crop\n";

    free((dma_device_handle *)buf->device);
    buf->device = 0;

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_sync(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_sync (user_context: " << user_context << ")\n";

    dma_device_handle *dev = (dma_device_handle *)buf->device;
    halide_assert(user_context, dev->dma_engine);
    int err = nDmaWrapper_Wait(dev->dma_engine);

    if (err != 0) {
        error(user_context) << "dma_wait failed (" << err << ")\n";
        return halide_error_code_device_sync_failed;
    }

    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_wrap_native(void *user_context, struct halide_buffer_t *buf,
                                               uint64_t handle) {
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return halide_error_code_device_wrap_native_failed;
    }

    buf->device_interface = &hexagon_dma_device_interface;
    buf->device_interface->impl->use_module();

    dma_device_handle *dev = malloc_device_handle();
    halide_assert(user_context, dev);
    dev->buffer = reinterpret_cast<uint8_t*>(handle);
    dev->dma_engine = 0;
    dev->frame_width = buf->dim[0].extent * buf->dim[0].stride;
    dev->frame_height = buf->dim[1].extent;
    dev->frame_stride = buf->dim[1].stride;
    buf->device = reinterpret_cast<uint64_t>(dev);
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_detach_native(void *user_context, struct halide_buffer_t *buf) {
    if (buf->device == 0) {
        return NULL;
    }
    halide_assert(user_context, buf->device_interface == &hexagon_dma_device_interface);
    dma_device_handle *dev = (dma_device_handle *)buf->device;
    free(dev);
    buf->device_interface->impl->release_module();
    buf->device = 0;
    buf->device_interface = NULL;
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_malloc(user_context, buf, &hexagon_dma_device_interface);
}

WEAK int halide_hexagon_dma_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    return halide_default_device_and_host_free(user_context, buf, &hexagon_dma_device_interface);
}

WEAK const halide_device_interface_t *halide_hexagon_dma_device_interface() {
    return &hexagon_dma_device_interface;
}

WEAK int halide_hexagon_dma_device_release(void *user_context) { return 0; }

} // extern "C" linkage

namespace Halide { namespace Runtime { namespace Internal { namespace HexagonDma {

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
    halide_device_release_crop,
    halide_device_wrap_native,
    halide_device_detach_native,
    &hexagon_dma_device_interface_impl
};

}}}} // namespace Halide::Runtime::Internal::HexagonDma
