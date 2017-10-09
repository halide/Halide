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
};

dma_device_handle *malloc_device_handle() {
    dma_device_handle *dev = (dma_device_handle *)malloc(sizeof(dma_device_handle));
    dev->buffer = 0;
    dev->offset_x = 0;
    dev->offset_y = 0;
    dev->dma_engine = 0;
    return dev;
}

}}}}  // namespace Halide::Runtime::Internal::HexagonDma

using namespace Halide::Runtime::Internal::HexagonDma;

extern "C" {

WEAK int halide_hexagon_dma_device_malloc(void *user_context, halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_malloc (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    if (buf->device) {
        // This buffer already has a device allocation
        return 0;
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
        return err;
    }

    return 0;
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
    return 0;
}

WEAK int halide_hexagon_dma_allocate_engine(void *user_context, void **dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_allocate_engine (user_context: " << user_context << ")\n";

    halide_assert(user_context, dma_engine);

    debug(user_context) << "    dma_allocate_dma_engine -> ";
    *dma_engine = dma_allocate_dma_engine();
    debug(user_context) << "        " << dma_engine << "\n";
    if (!*dma_engine) {
        error(user_context) << "dma_allocate_dma_engine failed.\n";
        return -1;
    }

    return 0;
}

WEAK int halide_hexagon_dma_deallocate_engine(void *user_context, void *dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_deallocate_engine (user_context: " << user_context
        << ", dma_engine: " << dma_engine << ")\n";


    debug(user_context) << "    dma_free_dma_engine\n";
    dma_free_dma_engine(dma_engine);
    return 0;
}

WEAK int halide_hexagon_dma_prepare_for_copy_to_host(void *user_context, struct halide_buffer_t *buf,
                                                     void *dma_engine) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_allocate_engine (user_context: " << user_context
        << ", buf: " << buf << ", dma_engine: " << dma_engine << ")\n";

    halide_assert(user_context, buf->device_interface == halide_hexagon_dma_device_interface());
    halide_assert(user_context, buf->device);
    halide_assert(user_context, dma_engine);

    dma_device_handle *dev = reinterpret_cast<dma_device_handle *>(buf->device);
    dev->dma_engine = dma_engine;

    t_dma_prepare_params prep;
    prep.handle = dma_engine;
    prep.host_address = reinterpret_cast<uintptr_t>(dev->buffer);
    prep.frame_width = buf->dim[0].extent;
    prep.frame_height = buf->dim[1].extent;
    prep.frame_stride = buf->dim[1].stride;
    if (buf->dimensions == 3) {
        prep.ncomponents = buf->dim[2].extent;
    } else {
        halide_assert(user_context, buf->dimensions == 2);
        prep.ncomponents = 1;
    }
    halide_assert(user_context, buf->dim[0].stride == 1);
    // TODO: Do we really need to specify these here?
    prep.roi_width = 0;
    prep.roi_height = 0;
    // TODO: Support YUV formats.
    prep.luma_stride = 0;
    prep.chroma_stride = 0;
    prep.read = true;
    prep.chroma_type = eDmaFmt_RawData;
    prep.luma_type = eDmaFmt_RawData;
    prep.padding = false;
    prep.is_ubwc = false;
    prep.num_folds = 1;
    prep.desc_address = 0;
    prep.desc_size = 0;

    debug(user_context) << "    dma_prepare_for_transfer -> ";
    int err = dma_prepare_for_transfer(prep);
    debug(user_context) << "        " << err << "\n";
    if (err != 0) {
        error(user_context) << "dma_prepare_for_transfer failed.\n";
        return -1;
    }

    return 0;
}

WEAK int halide_hexagon_dma_unprepare(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_deallocate_engine (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    halide_assert(user_context, buf->device_interface == halide_hexagon_dma_device_interface());
    halide_assert(user_context, buf->device);

    dma_device_handle *dev = reinterpret_cast<dma_device_handle *>(buf->device);

    debug(user_context) << "   dma_finish_frame -> ";
    int err = dma_finish_frame(dev->dma_engine);
    debug(user_context) << "        " << err << "\n";
    if (err != 0) {
        error(user_context) << "dma_finish_frame failed.\n";
        return -1;
    }

    return 0;
}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context, halide_buffer_t* buf) {
    int err = halide_hexagon_dma_device_malloc(user_context, buf);
    if (err) {
        return err;
    }

    // TODO: Implement this with dma_move_data.
    error(user_context) << "haldie_hexagon_dma_copy_to_device not implemented.\n";
    return -1;
}

WEAK int halide_hexagon_dma_copy_to_host(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_copy_to_host (user_context: " << user_context
        << ", buf: " << buf << ")\n";

    halide_assert(user_context, buf->host && buf->device);

    dma_device_handle *dev = (dma_device_handle *)buf->device;

    t_dma_move_params move;
    move.handle = dev->dma_engine;
    move.xoffset = dev->offset_x;
    move.yoffset = dev->offset_y;
    move.roi_width = buf->dim[0].extent;
    move.roi_height = buf->dim[1].extent;
    if (buf->dimensions == 3) {
        move.ncomponents = buf->dim[2].extent;
    } else {
        halide_assert(user_context, buf->dimensions == 2);
        move.ncomponents = 1;
    }
    move.offset = 0;
    move.ping_buffer = reinterpret_cast<uintptr_t>(buf->host);

    debug(user_context) << "    dma_move_data -> ";
    int err = dma_move_data(move);
    debug(user_context) << "        " << err << "\n";
    if (err != 0) {
        error(user_context) << "dma_move_data failed.\n";
        return -1;
    }

    return 0;
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

    return 0;
}

WEAK int halide_hexagon_dma_device_release_crop(void *user_context,
                                                struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_release_crop\n";

    free((dma_device_handle *)buf->device);
    buf->device = 0;

    return 0;
}

WEAK int halide_hexagon_dma_device_sync(void *user_context, struct halide_buffer_t *buf) {
    debug(user_context)
        << "Hexagon: halide_hexagon_dma_device_sync (user_context: " << user_context << ")\n";

    dma_device_handle *dev = (dma_device_handle *)buf->device;
    halide_assert(user_context, dev->dma_engine);
    int err = dma_wait(dev->dma_engine);
    if (err != 0) {
      error(user_context) << "dma_wait failed (" << err << ")\n";
    }

    return err;
}

WEAK int halide_hexagon_dma_device_wrap_native(void *user_context, struct halide_buffer_t *buf,
                                               uint64_t handle) {
    halide_assert(user_context, buf->device == 0);
    if (buf->device != 0) {
        return -2;
    }

    buf->device_interface = &hexagon_dma_device_interface;
    buf->device_interface->impl->use_module();

    dma_device_handle *dev = malloc_device_handle();
    halide_assert(user_context, dev);
    dev->buffer = reinterpret_cast<uint8_t*>(handle);
    dev->dma_engine = 0;
    buf->device = reinterpret_cast<uint64_t>(dev);
    return 0;
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
    return 0;
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
    halide_default_buffer_copy,
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
