/**
 * This file is a duplicate of the actual hexagon_dma_device_shim.cpp used to call the DMA driver functions
 * The definitions in this file a week reference so that these will be called only in case of unavailability of
 * actual DMA functions.
 * This file is need only if there is no hexagon SDK support or NO hexagon DMA support, in either csae we replace
 * the DMA operations with normal memory operations */

#include "pipeline.h"
#include "HalideRuntime.h"
#include "../../src/runtime/mini_hexagon_dma.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <memory.h>

//Mock Global Descriptor
typedef struct stDescriptor {
  struct {
      uintptr_t DesPointer    : 32;   // for chain to next "desc" or NULL to terminate the chain
      uint32 DstPixFmt        :  3;
      uint32 DstIsUbwc        :  1;
      uint32 SrcPixFmt        :  3;
      uint32 SrcIsUbwc        :  1;
      uint32 DstIsTcm         :  1;
      uint32 _unused0         :  3;
      uint32 SrcIsTcm         :  1;
      uint32 _unused1         :  3;
      uint32 DstPixPadding    :  1;
      uint32 _unused2         :  3;
      uint32 SrcPixPadding    :  1;
      uint32 _unused3         : 11;
      uint32 FrmHeight        : 16;
      uint32 FrmWidth         : 16;
      uint32 RoiY             : 16;
      uint32 RoiX             : 16;
    } stWord0;
    struct {
      uint32 RoiH             : 16;
      uint32 RoiW             : 16;
      uintptr_t SrcFrmBaseAddr: 32;
      uint32 SrcRoiStartAddr  : 32;
      uint32 SrcRoiStride     : 16;
      uint32 _unused0         : 16;
    } stWord1;
    struct {
      uintptr_t DstFrmBaseAddr: 32;
      uint32 DstRoiStartAddr  : 32;
      uint32 DstRoiStride     : 16;
      uint32 Flush            :  1;
      uint32 _unused0         : 15;
      uint32 _unused1         : 32;
    } stWord2;
    struct {
      uint32 reserved0        : 32;
      uint32 reserved1        : 32;
      uint32 reserved2        : 32;
//    uint32 reserved3        : 32;
      uint32 ubwc_stat_pointer  : 32;// use reserved3 for gralloc ubwc_stat_pointer
    } stWord3;
} t_StHwDescriptor;

typedef struct {
     int x; //in case we want to keep a count
     t_StHwDescriptor *ptr;
} dma_handle_t;

void* HAP_cache_lock(unsigned int size, void** paddr_ptr) {
    void * alloc = 0;
    if (size != 0) {
        alloc = malloc(size);
    }
    return alloc;
}

int HAP_cache_unlock(void* vaddr_ptr) {
    if (vaddr_ptr != 0) {
        free(vaddr_ptr);
        return 0;
    }
    return 1;
}

t_DmaWrapper_DmaEngineHandle hDmaWrapper_AllocDma(void) {
    dma_handle_t* handle = (dma_handle_t*)malloc(sizeof(dma_handle_t));
    handle->ptr = NULL;
    return (void *)handle;
}

int32 nDmaWrapper_FreeDma(t_DmaWrapper_DmaEngineHandle dma_handle) {
    dma_handle_t *desc = (dma_handle_t *)dma_handle;
    assert(desc->ptr == NULL);
    free(desc);
    return 0;
}

int32 nDmaWrapper_Move(t_DmaWrapper_DmaEngineHandle handle) {

    t_StHwDescriptor *desc = 0;
    if(handle != 0) {
        dma_handle_t* dma_handle = (dma_handle_t *)handle;
        desc = dma_handle->ptr;

        while (desc != NULL) {
            unsigned char* host_addr = reinterpret_cast<unsigned char *>(desc->stWord1.SrcFrmBaseAddr);
            unsigned char* dest_addr = reinterpret_cast<unsigned char *>(desc->stWord2.DstFrmBaseAddr);
            int x = desc->stWord0.RoiX;
            int y = desc->stWord0.RoiY;
            int w = desc->stWord1.RoiW;
            int h = desc->stWord1.RoiH;
            for (int xii=0;xii<h;xii++) {
                for (int yii=0;yii<w;yii++) {
                    int xin = xii*desc->stWord2.DstRoiStride;
                    int yin = yii;
                    int RoiOffset = x+y*desc->stWord1.SrcRoiStride;
                    int xout = xii*desc->stWord0.FrmHeight;
                    int yout = yii;
                    dest_addr[yin+xin] = host_addr[RoiOffset + yout + xout];
                }
            }
            desc = reinterpret_cast<t_StHwDescriptor*>(desc->stWord0.DesPointer);
        }
    }
    return 0;
}

int32 nDmaWrapper_Wait(t_DmaWrapper_DmaEngineHandle dma_handle) {
    return 0;
}

int32 nDmaWrapper_FinishFrame(t_DmaWrapper_DmaEngineHandle dma_handle) {
    dma_handle_t *desc = (dma_handle_t *)dma_handle;
    //remove the association from descriptor
    desc->ptr = NULL;
    return 0;
}

int32 nDmaWrapper_GetRecommendedWalkSize(t_eDmaFmt fmt, bool is_ubwc,
                                         t_StDmaWrapper_RoiAlignInfo* walk_size) {
    walk_size->u16H = align(walk_size->u16H, 1);
    walk_size->u16W = align(walk_size->u16W, 1);
    return 0;
}

int32 nDmaWrapper_GetRecommendedIntermBufStride(t_eDmaFmt fmt,
                                                t_StDmaWrapper_RoiAlignInfo* roi_size,
                                                 bool is_ubwc) {
    return align(roi_size->u16W, 256);
}

int32 nDmaWrapper_DmaTransferSetup(t_DmaWrapper_DmaEngineHandle handle, t_StDmaWrapper_DmaTransferSetup* dma_transfer_parm) {

    if (handle == 0)
        return 1;

    if (dma_transfer_parm->pDescBuf == NULL)
        return 1;

    //Add it to the linked list of dma_handle->ptr
    dma_handle_t *dma_handle = (dma_handle_t *)handle;
    t_StHwDescriptor *temp = dma_handle->ptr;
    t_StHwDescriptor *desc = (t_StHwDescriptor *)dma_transfer_parm->pDescBuf;
    desc->stWord0.DesPointer  = 0;

    if (temp != NULL) {
        while (temp->stWord0.DesPointer != 0) {
            temp =  reinterpret_cast<t_StHwDescriptor *>(temp->stWord0.DesPointer);
        }
        temp->stWord0.DesPointer =  reinterpret_cast<uintptr_t>(desc);
    } else {
        dma_handle->ptr = desc;
    }

    desc->stWord0.DstIsUbwc = dma_transfer_parm->bIsFmtUbwc;
    desc->stWord0.DstIsTcm = (dma_transfer_parm->eTransferType == eDmaWrapper_DdrToL2) ? 1 : 0;
    desc->stWord0.FrmHeight = dma_transfer_parm->u16FrameH;
    desc->stWord0.FrmWidth = dma_transfer_parm->u16FrameW;
    desc->stWord0.RoiX = dma_transfer_parm->u16RoiX;
    desc->stWord0.RoiY = dma_transfer_parm->u16RoiY;
    desc->stWord1.RoiH = dma_transfer_parm->u16RoiH;
    desc->stWord1.RoiW = dma_transfer_parm->u16RoiW;
    desc->stWord1.SrcRoiStride = dma_transfer_parm->u16FrameStride;
    desc->stWord2.DstRoiStride = dma_transfer_parm->u16RoiStride;
    desc->stWord2.DstFrmBaseAddr = reinterpret_cast<uintptr_t>(dma_transfer_parm->pTcmDataBuf);
    desc->stWord1.SrcFrmBaseAddr  = reinterpret_cast<uintptr_t>(dma_transfer_parm->pFrameBuf);
    return 0;

}

int32 nDmaWrapper_GetDescbuffsize(t_eDmaFmt *fmt, uint16 nsize) {

    int32 i, yuvformat=0,desc_size;
    for (i=0;i<nsize;i++) {
        if ((fmt[i]==eDmaFmt_NV12)||(fmt[i]==eDmaFmt_TP10)||
            (fmt[i]==eDmaFmt_NV124R)||(fmt[i]==eDmaFmt_P010)) {
            yuvformat += 1;
        }
    }
    desc_size = (nsize+yuvformat)*64;
    return desc_size;
}

