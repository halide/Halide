/**
 * This file contains the definitions of the hexagon dma device interface class
 * This file is an interface between Halide Runtime and the DMA driver
 * The functions in this file take care of the various functionalities of the
 * DMA driver through their respective halide device interface functions
 * The function in this file use the Hexagon DMA context structure for sharing
 * the DMA context across various APIs of the
 * DMA device interface */

#include "runtime_internal.h"
#include "device_buffer_utils.h"
#include "device_interface.h"
#include "printer.h"
#include "hexagon_dma_device_shim.h"
#include "halide_hexagon_dma_api.h"
#include "hexagon_dma_api.h"
#include "hexagon_dma_rt.h"

using namespace Halide::Runtime::Internal::Qurt;
namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

p_dma_context WEAK pdma_context = NULL;
}}}}

using namespace Halide::Runtime::Internal::Dma;

//The get function is necessary because in some places like get memory
//we do not have access to the inframe buffer
void halide_hexagon_get_dma_context (void* user_context, p_dma_context* context) {
    halide_assert(user_context, context != NULL);
    *context = pdma_context;
}

void halide_hexagon_acquire_dma_context(void* user_context, p_dma_context* ctx, int num_of_frames, bool create=true) {
    halide_assert(user_context, ctx != NULL);
    // If the context has not been initialized, initialize it now.
    if (!pdma_context && create) {
        halide_hexagon_dmart_create_context(user_context, &pdma_context, num_of_frames);
    }
    *ctx = pdma_context;
}

namespace Halide { namespace Runtime { namespace Internal { namespace Dma {

HexagonDmaContext::HexagonDmaContext(void* user_context): user_ctxt(user_context),
                           context(NULL) {

    halide_hexagon_acquire_dma_context(user_ctxt, &context, 1);
    halide_assert(user_context, context != NULL);
}

HexagonDmaContext::HexagonDmaContext(void* user_context, int num_of_frames): user_ctxt(user_context),
                          context(NULL) {

    halide_hexagon_acquire_dma_context(user_ctxt, &context, num_of_frames);
    halide_assert(user_context, context != NULL);
}

HexagonDmaContext::~HexagonDmaContext() {
    user_ctxt = 0;
    context = 0;
}
}}}}

using namespace Halide::Runtime::Internal::Dma;

extern "C" {

extern WEAK halide_device_interface_t hexagon_dma_device_interface;

WEAK int halide_hexagon_dma_device_release(void *user_context) {
    return HEX_SUCCESS;
}

WEAK int halide_hexagon_dma_device_malloc(void *user_context, halide_buffer_t *buf) {

    //////////////////////////////////////////////////////////////////////
    p_dma_context dma_handle = reinterpret_cast<p_dma_context>(buf->device);
    uintptr_t  frame = halide_hexagon_dmart_get_frame(user_context, dma_handle);
    //////////////////////////////////////////////////////////////////////////

    //Check for dma_allocation
    bool dma_allocate;
    void* handle = 0;
    if (halide_hexagon_dmart_allocate_dma(user_context, pdma_context, frame, &dma_allocate) == -1) {
        error(user_context) << "Undefined Error";
        return HEX_ERROR;
    }

    if (dma_allocate) {
        handle = dma_allocate_dma_engine();
        if (handle == 0) {
            error(user_context) << "halide_hexagon_dma_device_malloc:Failed to allocate the read DMA engine.\n";
            return HEX_ERROR;
        }

        if (halide_hexagon_dmart_set_dma_handle(user_context, pdma_context, handle, frame)) {
            error(user_context) << "halide_hexagon_dma_device_malloc:Function failed to set DMA Handle to DMA context \n";
            return HEX_ERROR;
        }
    }
    return 0;
}

WEAK int halide_hexagon_dma_device_free(void *user_context, halide_buffer_t *buf) {
    halide_assert(NULL, buf!=NULL);
    HexagonDmaContext hexagon_dma_context(user_context);
    halide_assert(user_context, hexagon_dma_context.get_context() != NULL);
    p_dma_context dma_ctxt = hexagon_dma_context.get_context();

    halide_hexagon_dmart_delete_context(user_context, dma_ctxt);
    return HEX_SUCCESS;
}

WEAK int halide_hexagon_dma_copy_to_device(void *user_context,  halide_buffer_t *buf) {
    void* handle;

    //////////////////////////////////////////////////////////////////////
    p_dma_context dma_handle = reinterpret_cast<p_dma_context>(buf->device);
    uintptr_t  frame = halide_hexagon_dmart_get_frame(user_context, dma_handle);
    //////////////////////////////////////////////////////////////////////////

    handle = halide_hexagon_dmart_get_dma_handle(user_context, dma_handle, frame);
    if (handle == 0) {
        error(user_context) << "Function failed to get DMA Write  Handle  \n";
        return HEX_ERROR;
    }

    int ncomponents;
    ncomponents = halide_hexagon_dmart_get_num_components(user_context, dma_handle, frame);

    t_dma_move_params move_param;

    uintptr_t fold_addr = halide_hexagon_dmart_get_fold_addr(user_context, dma_handle, frame);
    halide_hexagon_dmart_get_update_params(user_context, dma_handle, fold_addr, &move_param);

    move_param.handle = handle;
    move_param.ncomponents = ncomponents;

    int nRet = HEX_SUCCESS;
    nRet = dma_move_data(move_param);

    if (nRet != QURT_EOK) {
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}

WEAK int halide_hexagon_dma_copy_to_host(void *user_context, halide_buffer_t *buf) {
    void* handle;

    //////////////////////////////////////////////////////////////////////
    p_dma_context dma_handle = reinterpret_cast<p_dma_context>(buf->device);
    uintptr_t frame = halide_hexagon_dmart_get_frame(user_context, dma_handle);
    //////////////////////////////////////////////////////////////////////////

    handle = halide_hexagon_dmart_get_dma_handle(user_context, dma_handle, frame);
    if (handle == 0) {
        error(user_context) << "Function failed to get DMA Write  Handle  \n";
        return HEX_ERROR;
    }

    int ncomponents;
    ncomponents = halide_hexagon_dmart_get_num_components(user_context, dma_handle, frame);

    t_dma_move_params move_param;

    uintptr_t fold_addr = halide_hexagon_dmart_get_fold_addr(user_context, dma_handle, frame);
    halide_hexagon_dmart_get_update_params(user_context, dma_handle, fold_addr, &move_param);

    move_param.handle = handle;
    move_param.ncomponents = ncomponents;

    int nRet = HEX_SUCCESS;
    nRet = dma_move_data(move_param);

    if (nRet != QURT_EOK) {
        return HEX_ERROR;
    }
    return HEX_SUCCESS;
}


WEAK int halide_hexagon_dma_device_sync(void* user_context, halide_buffer_t *buf) {

    int nRet = HEX_SUCCESS;
    void* handle=NULL;

    //////////////////////////////////////////////////////////////////////
    p_dma_context dma_handle = reinterpret_cast<p_dma_context>(buf->device);
    uintptr_t  frame = halide_hexagon_dmart_get_frame(user_context, dma_handle);
    //////////////////////////////////////////////////////////////////////////

    handle = halide_hexagon_dmart_get_dma_handle(user_context, dma_handle, frame);
    if (handle == 0) {
        error(user_context) << "Function failed to get DMA Write  Handle  \n";
        return HEX_ERROR;
    }

    nRet = dma_wait(handle);
    return nRet;
}


WEAK int halide_hexagon_dma_device_and_host_malloc(void *user_context, struct halide_buffer_t *buf) {
    int result = halide_hexagon_dma_device_malloc(user_context, buf);
    return result;
}

WEAK int halide_hexagon_dma_device_and_host_free(void *user_context, struct halide_buffer_t *buf) {
    int result = halide_hexagon_dma_device_free(user_context, buf);
    buf->host = NULL;
    return result;
}


WEAK const halide_device_interface_t *halide_hexagon_dma_device_interface() {
    return &hexagon_dma_device_interface;
}

}// exterm C enfs

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
    halide_default_device_wrap_native,
    halide_default_device_detach_native,
};

WEAK struct halide_device_interface_t hexagon_dma_device_interface = {
    halide_device_malloc,
    halide_device_free,
    halide_device_sync,
    halide_device_release,
    halide_copy_to_host,
    halide_copy_to_device,
    halide_device_and_host_malloc,
    halide_device_and_host_free,
    halide_device_wrap_native,
    halide_device_detach_native,
    &hexagon_dma_device_interface_impl
};

