// This file simulates the actual hexagon DMA driver functions to run
// the DMA Examples on host machine. The definitions in this file are
// a weak reference so that these will be called only in case of
// unavailability of actual DMA functions.
#include "HalideRuntime.h"

#define WEAK __attribute__((weak))
#include "../../src/runtime/hexagon_dma_pool.h"
#include "../../src/runtime/mini_hexagon_dma.h"
#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#define HALIDE_MOCK_DMA_DEBUG

// Mock Global Descriptor
typedef struct {
    struct {
        uintptr_t des_pointer;  // for chain to next "desc" or NULL to terminate the chain
        uint32 dst_pix_fmt : 3;
        uint32 dst_is_ubwc : 1;
        uint32 src_pix_fmt : 3;
        uint32 src_is_ubwc : 1;
        uint32 dst_is_tcm : 1;
        uint32 _unused0 : 3;
        uint32 src_is_tcm : 1;
        uint32 _unused1 : 3;
        uint32 dst_pix_padding : 1;
        uint32 _unused2 : 3;
        uint32 src_pix_padding : 1;
        uint32 _unused3 : 11;
        uint32 frm_height : 16;
        uint32 frm_width : 16;
        uint32 roiY : 16;
        uint32 roiX : 16;
    } stWord0;
    struct {
        uint32 roiH : 16;
        uint32 roiW : 16;
        uint32 src_roi_stride : 16;
        uint32 dst_roi_stride : 16;
        uintptr_t src_frm_base_addr;
        uintptr_t dst_frm_base_addr;
        uint32 src_roi_start_addr : 32;
        uint32 dst_roi_start_addr : 32;
        uint32 ubwc_stat_pointer : 32;  // use reserved3 for gralloc ubwc_stat_pointer
    } stWord1;
    struct {
        uint32 pix_fmt;
        uint32 _unused0;
        uint32 _unused1;
        uint32 _unused2;
    } stWord2;
} t_st_hw_descriptor;

typedef struct {
    int x;  // in case we want to keep a count
    t_st_hw_descriptor *ptr;
} dma_handle_t;

int halide_hexagon_free_l2_pool(void *user_context) {
    halide_print(user_context, "halide_hexagon_free_l2_pool mock implementation \n");
    return halide_error_code_success;
}

static int nDmaPixelSize(int pix_fmt) {
    int nRet = 0;
    switch (pix_fmt) {
    case eDmaFmt_RawData:
    case eDmaFmt_NV12:
    case eDmaFmt_NV12_Y:
    case eDmaFmt_NV12_UV:
    case eDmaFmt_NV124R:
    case eDmaFmt_NV124R_Y:
    case eDmaFmt_NV124R_UV:
        nRet = 1;
        break;
    case eDmaFmt_P010:
    case eDmaFmt_P010_Y:
    case eDmaFmt_P010_UV:
    case eDmaFmt_TP10:
    case eDmaFmt_TP10_Y:
    case eDmaFmt_TP10_UV:
        nRet = 2;
        break;
    }
    assert(nRet != 0);

    return nRet;
}

void *HAP_cache_lock(unsigned int size, void **paddr_ptr) {
    void *alloc = 0;
    if (size != 0) {
        alloc = malloc(size);
    }
    return alloc;
}

int HAP_cache_unlock(void *vaddr_ptr) {
    if (vaddr_ptr != 0) {
        free(vaddr_ptr);
        vaddr_ptr = 0;
        return 0;
    }
    return 1;
}

int32 nDmaWrapper_PowerVoting(uint32 cornercase) {
    printf("In nDmaWrapper_PowerVoting %d \n", cornercase);
    return 0;
}

t_DmaWrapper_DmaEngineHandle hDmaWrapper_AllocDma(void) {
    dma_handle_t *handle = (dma_handle_t *)malloc(sizeof(dma_handle_t));
    handle->ptr = NULL;
    return (void *)handle;
}

int32 nDmaWrapper_FreeDma(t_DmaWrapper_DmaEngineHandle dma_handle) {
    dma_handle_t *desc = (dma_handle_t *)dma_handle;
    assert(desc != NULL);
    assert(desc->ptr == NULL);
    free(desc);
    return 0;
}

int32 nDmaWrapper_Move(t_DmaWrapper_DmaEngineHandle handle) {

    if (handle != 0) {
        dma_handle_t *dma_handle = (dma_handle_t *)handle;
        t_st_hw_descriptor *desc = dma_handle->ptr;

        while (desc != NULL) {
            unsigned char *src_addr = reinterpret_cast<unsigned char *>(desc->stWord1.src_frm_base_addr);
            unsigned char *dst_addr = reinterpret_cast<unsigned char *>(desc->stWord1.dst_frm_base_addr);

            int x = desc->stWord0.roiX;
            int y = desc->stWord0.roiY;
            int w = desc->stWord1.roiW;
            int h = desc->stWord1.roiH;
            int pixelsize = nDmaPixelSize(desc->stWord2.pix_fmt);
            int y_offset;
            if (desc->stWord2.pix_fmt == eDmaFmt_NV12_UV ||
                desc->stWord2.pix_fmt == eDmaFmt_NV124R_UV ||
                desc->stWord2.pix_fmt == eDmaFmt_P010_UV ||
                desc->stWord2.pix_fmt == eDmaFmt_TP10_UV) {
                y_offset = desc->stWord0.frm_height;
            } else {
                y_offset = 0;
            }
#ifdef HALIDE_MOCK_DMA_DEBUG
            fprintf(stderr, "Processing descriptor %p -- DMAREAD: %d src_addr: %p dst_addr: %p ROI(X: %u, Y: %u, W: %u, H: %u) FrameStride: %u, CacheRoiStride %u, Frm(W: %u, H: %u), y_offset: %u\n",
                    desc, desc->stWord0.dst_is_tcm, src_addr, dst_addr, x, y, w, h,
                    desc->stWord1.src_roi_stride, desc->stWord1.dst_roi_stride, desc->stWord0.frm_width, desc->stWord0.frm_height, y_offset);
            int cnt = 0;  // log first few lines
#endif
            for (int yii = 0; yii < h; yii++) {
                // per line copy
                int ydst = yii * desc->stWord1.dst_roi_stride * pixelsize;
                int ysrc = yii * desc->stWord1.src_roi_stride * pixelsize;
                int len = w * pixelsize;
                int frame_offset = 0;
                if (desc->stWord0.dst_is_tcm) {
                    frame_offset = (x + (y_offset + y) * desc->stWord1.src_roi_stride) * pixelsize + ysrc;
                    memcpy(&dst_addr[ydst], &src_addr[frame_offset], len);
                } else {
                    frame_offset = (x + (y_offset + y) * desc->stWord1.dst_roi_stride) * pixelsize + ydst;
                    memcpy(&dst_addr[frame_offset], &src_addr[ysrc], len);
                }

#ifdef HALIDE_MOCK_DMA_DEBUG
#define DBG_LOG_LINES 2
                if (cnt++ < DBG_LOG_LINES) {
                    fprintf(stderr, "Processing line -- yii: %u ydst: %u frame_offset: %u ysrc: %u len: %u\n",
                            yii, ydst, frame_offset, ysrc, len);
                } else {
                    if (cnt == (h - DBG_LOG_LINES)) cnt = 0;  // log last few lines
                }
#endif
            }
            desc = reinterpret_cast<t_st_hw_descriptor *>(desc->stWord0.des_pointer);
        }
    }
    return 0;
}

int32 nDmaWrapper_Wait(t_DmaWrapper_DmaEngineHandle dma_handle) {
    dma_handle_t *desc = (dma_handle_t *)dma_handle;
    assert(desc != NULL);
    // remove the association from descriptor
    desc->ptr = NULL;
    return 0;
}

int32 nDmaWrapper_FinishFrame(t_DmaWrapper_DmaEngineHandle dma_handle) {
    dma_handle_t *desc = (dma_handle_t *)dma_handle;
    assert(desc != NULL);
    // remove the association from descriptor
    desc->ptr = NULL;
    return 0;
}

int32 nDmaWrapper_GetRecommendedWalkSize(t_eDmaFmt fmt, bool is_ubwc,
                                         t_StDmaWrapper_RoiAlignInfo *walk_size) {
    walk_size->u16H = align(walk_size->u16H, 1);
    walk_size->u16W = align(walk_size->u16W, 1);
    return 0;
}

int32 nDmaWrapper_GetRecommendedIntermBufStride(t_eDmaFmt fmt,
                                                t_StDmaWrapper_RoiAlignInfo *roi_size,
                                                bool is_ubwc) {
    // UBWC Not Supported
    assert(is_ubwc == 0);
    return roi_size->u16W;
}

int32 nDmaWrapper_DmaTransferSetup(t_DmaWrapper_DmaEngineHandle handle, t_StDmaWrapper_DmaTransferSetup *dma_transfer_parm) {

    if (handle == 0)
        return 1;

    if (dma_transfer_parm->pDescBuf == NULL)
        return 1;

    // Add it to the linked list of dma_handle->ptr
    dma_handle_t *dma_handle = (dma_handle_t *)handle;
    t_st_hw_descriptor *temp = dma_handle->ptr;
    t_st_hw_descriptor *desc = (t_st_hw_descriptor *)dma_transfer_parm->pDescBuf;
    desc->stWord0.des_pointer = 0;

    if (temp != NULL) {
        while (temp->stWord0.des_pointer != 0) {
            temp = reinterpret_cast<t_st_hw_descriptor *>(temp->stWord0.des_pointer);
        }
        temp->stWord0.des_pointer = reinterpret_cast<uintptr_t>(desc);
    } else {
        dma_handle->ptr = desc;
    }

    int mul_factor = 1;
    switch (dma_transfer_parm->eFmt) {  // chroma fmt
    case eDmaFmt_NV12_UV:
    case eDmaFmt_NV124R_UV:
    case eDmaFmt_P010_UV:
    case eDmaFmt_TP10_UV: {
        // DMA Driver halves the Y offset and height so that only half the size of roi luma is transferred for chroma
        // Adjusting for that behavior
        mul_factor = 2;
    } break;
    case eDmaFmt_RawData:
    case eDmaFmt_NV12:
    case eDmaFmt_NV12_Y:
    case eDmaFmt_NV124R:
    case eDmaFmt_NV124R_Y:
    case eDmaFmt_P010:
    case eDmaFmt_P010_Y:
    case eDmaFmt_TP10:
    case eDmaFmt_TP10_Y:
    default:
        break;
    }

    desc->stWord0.dst_is_ubwc = dma_transfer_parm->bIsFmtUbwc;
    desc->stWord0.dst_is_tcm = (dma_transfer_parm->eTransferType == eDmaWrapper_DdrToL2) ? 1 : 0;
    desc->stWord0.frm_height = dma_transfer_parm->u16FrameH;
    desc->stWord0.frm_width = dma_transfer_parm->u16FrameW;
    desc->stWord0.roiX = dma_transfer_parm->u16RoiX;
    desc->stWord0.roiY = dma_transfer_parm->u16RoiY / mul_factor;
    desc->stWord1.roiH = dma_transfer_parm->u16RoiH / mul_factor;
    desc->stWord1.roiW = dma_transfer_parm->u16RoiW;
    if (desc->stWord0.dst_is_tcm) {
        desc->stWord1.dst_frm_base_addr = reinterpret_cast<uintptr_t>(dma_transfer_parm->pTcmDataBuf);
        // must always point to start of frame buffer, and not start of component
        desc->stWord1.src_frm_base_addr = reinterpret_cast<uintptr_t>(dma_transfer_parm->pFrameBuf);
        desc->stWord1.src_roi_stride = dma_transfer_parm->u16FrameStride;
        desc->stWord1.dst_roi_stride = dma_transfer_parm->u16RoiStride;
    } else {
        desc->stWord1.src_frm_base_addr = reinterpret_cast<uintptr_t>(dma_transfer_parm->pTcmDataBuf);
        // must always point to start of frame buffer, and not start of component
        desc->stWord1.dst_frm_base_addr = reinterpret_cast<uintptr_t>(dma_transfer_parm->pFrameBuf);
        // We are in dma write so dst roi stride is the frame stride and src stride is tcm stride
        desc->stWord1.dst_roi_stride = dma_transfer_parm->u16FrameStride;
        desc->stWord1.src_roi_stride = dma_transfer_parm->u16RoiStride;
    }
    desc->stWord2.pix_fmt = dma_transfer_parm->eFmt;

    return 0;
}

int32 nDmaWrapper_GetDescbuffsize(t_eDmaFmt *fmt, uint16 nsize) {

    int32 desc_size, yuvformat = 0;
    for (int32 i = 0; i < nsize; i++) {
        if ((fmt[i] == eDmaFmt_NV12) || (fmt[i] == eDmaFmt_TP10) ||
            (fmt[i] == eDmaFmt_NV124R) || (fmt[i] == eDmaFmt_P010)) {
            yuvformat += 1;
        }
    }
    desc_size = (nsize + yuvformat) * 64;
    return desc_size;
}

int32 nDmaWrapper_GetRecommendedIntermBufSize(t_eDmaFmt eFmtId, bool bUse16BitPaddingInL2,
                                              t_StDmaWrapper_RoiAlignInfo *pStRoiSize,
                                              bool bIsUbwc, uint16 u16IntermBufStride) {

    return 0;
}
