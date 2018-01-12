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
    void *desc_addr;
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
    dev->desc_addr = 0;
    return dev;
}

typedef struct desc_pool {
    void* descriptor;
    bool used;
    struct desc_pool* next;
} desc_pool_t;

typedef desc_pool_t* pdesc_pool;
static pdesc_pool dma_desc_pool = NULL;
#define descriptor_size 64

static void* desc_pool_get (void) {

    pdesc_pool temp = dma_desc_pool;
    pdesc_pool prev = NULL;

    //Walk the list
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
    uint8_t* desc = (uint8_t *)HAP_cache_lock(sizeof(char)*descriptor_size*2, NULL);
    temp->descriptor = (void *)desc;
    temp->used = true;
    temp->next = (pdesc_pool) malloc(sizeof(desc_pool_t));
    //hard coding bytes here
    (temp->next)->descriptor = (void *)(desc+descriptor_size);
    (temp->next)->used = false;
    (temp->next)->next = NULL;

    if (prev != NULL) {
        prev->next = temp;
    } else if (dma_desc_pool == NULL) {
        dma_desc_pool = temp;
    }
    return (void*) temp->descriptor;
}

/*find "desc"
 put "desc" into pool
 mark "desc" free*/
static int desc_pool_put (void *desc) {
    pdesc_pool temp = dma_desc_pool;
    while (temp != NULL) {
        if (temp->descriptor == desc) {
            temp->used = false;
        }
        temp=temp->next;
    }
    return -1;
}
//2 descriptor at a time
static void desc_pool_free () {
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

WEAK int halide_hexagon_dma_device_free(void *user_context, halide_buffer_t* buf) {
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
    nDmaWrapper_FreeDma((t_DmaWrapper_DmaEngineHandle)dma_engine);
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_prepare_for_copy_to_host(void *user_context, struct halide_buffer_t *buf,
                                                     void *dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_allocate_engine (user_context: " << user_context
        << ", buf: " << buf << ", dma_engine: " << dma_engine << ")\n";

    dma_device_handle *dev = reinterpret_cast<dma_device_handle *>(buf->device);
    dev->dma_engine = dma_engine;
    return halide_error_code_success;
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
    desc_pool_free();
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

    dma_device_handle *dev = (dma_device_handle *)src->device;

    debug(user_context) << "Hexagon dev handle: buffer: " << dev->buffer << " dev offset (" << dev->offset_x << ", " << dev->offset_y << ") frame_width: " << dev->frame_width << " frame_height: " << dev->frame_height << " frame_stride: " << dev->frame_stride << " desc_addr: " << dev->desc_addr << "\n";

#if 0 // Seems useful, but this flattens the mins
    device_copy c = make_buffer_copy(src, from_host, dst, to_host);
#endif

    t_StDmaWrapper_RoiAlignInfo stWalkSize = {dst->dim[0].extent, dst->dim[1].extent};
    int nRet = nDmaWrapper_GetRecommendedWalkSize(eDmaFmt_RawData, false, &stWalkSize);
    int roi_stride = dst->dim[1].stride; // nDmaWrapper_GetRecommendedIntermBufStride(eDmaFmt_RawData, &stWalkSize, false);
    int roi_width = stWalkSize.u16W;
    int roi_height = stWalkSize.u16H;
    // DMA driver Expect the Stride to be 256 Byte Aligned
    halide_assert(user_context, (roi_stride % 256) == 0);

    // This assert fails. The entire desc_addr concept needs to be removed
    // as the state can likely only exist per call.
    //    halide_assert(user_context, dev->desc_addr == 0);
    dev->desc_addr = desc_pool_get();

    t_StDmaWrapper_DmaTransferSetup stDmaTransferParm;
    stDmaTransferParm.eFmt = eDmaFmt_RawData;
    stDmaTransferParm.u16FrameW = dev->frame_width;
    stDmaTransferParm.u16FrameH = dev->frame_height;
    stDmaTransferParm.u16FrameStride = dev->frame_stride;
    stDmaTransferParm.u16RoiW = roi_width;
    stDmaTransferParm.u16RoiH = roi_height;
    stDmaTransferParm.u16RoiStride = roi_stride;
    stDmaTransferParm.bUse16BitPaddingInL2 = false;
    stDmaTransferParm.pDescBuf = dev->desc_addr;

    stDmaTransferParm.pTcmDataBuf = reinterpret_cast<void *>(dst->host);
    stDmaTransferParm.pFrameBuf = dev->buffer;
    stDmaTransferParm.eTransferType = eDmaWrapper_DdrToL2;

    stDmaTransferParm.u16RoiX = dev->offset_x + dst->dim[0].min;
    stDmaTransferParm.u16RoiY = dev->offset_y + dst->dim[1].min;
    
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
    nRet = nDmaWrapper_Wait(dev->dma_engine);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: nDmaWrapper_Wait error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }
    nRet = nDmaWrapper_FinishFrame(dev->dma_engine);
    if (nRet != QURT_EOK) {
        debug(user_context) << "Hexagon: nDmaWrapper_FinishFrame error: " << nRet << "\n";
        return halide_error_code_device_buffer_copy_failed;
    }
    
    return halide_error_code_success;
}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context, halide_buffer_t* buf) {
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

    t_StDmaWrapper_RoiAlignInfo stWalkSize = {buf->dim[0].extent, buf->dim[1].extent};
    int nRet = nDmaWrapper_GetRecommendedWalkSize(eDmaFmt_RawData, false, &stWalkSize);
    int roi_stride = nDmaWrapper_GetRecommendedIntermBufStride(eDmaFmt_RawData, &stWalkSize, false);
    int roi_width = stWalkSize.u16W;
    int roi_height = stWalkSize.u16H;
    // DMA driver Expect the Stride to be 256 Byte Aligned
    halide_assert(user_context,(buf->dim[1].stride== roi_stride));

    // This assert fails. The entire desc_addr concept needs to be removed
    // as the state can likely only exist per call.
    //    halide_assert(user_context, dev->desc_addr == 0);
    dev->desc_addr = desc_pool_get();

    t_StDmaWrapper_DmaTransferSetup stDmaTransferParm;
    stDmaTransferParm.eFmt = eDmaFmt_RawData;
    stDmaTransferParm.u16FrameW = dev->frame_width;
    stDmaTransferParm.u16FrameH = dev->frame_height;
    stDmaTransferParm.u16FrameStride = dev->frame_stride;
    stDmaTransferParm.u16RoiW = roi_width;
    stDmaTransferParm.u16RoiH = roi_height;
    stDmaTransferParm.u16RoiStride = roi_stride;
    stDmaTransferParm.bUse16BitPaddingInL2 = false;
    stDmaTransferParm.pDescBuf = dev->desc_addr;

    stDmaTransferParm.pTcmDataBuf = reinterpret_cast<void *>(buf->host);
    stDmaTransferParm.pFrameBuf = dev->buffer;
    stDmaTransferParm.eTransferType = eDmaWrapper_DdrToL2;

    stDmaTransferParm.u16RoiX = dev->offset_x;
    stDmaTransferParm.u16RoiY = dev->offset_y;

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
    desc_pool_put(dev->desc_addr);

    // This likely needs to be here, but the entire desc_addr concept
    // needs to be removed as the state can likely only exist per
    // call.
    // dev->desc_addr = 0;
    
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
    dev->frame_width = buf->dim[0].extent;
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
