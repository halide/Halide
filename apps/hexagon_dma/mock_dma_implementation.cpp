/**
 * This file is a duplicate of the actual hexagon_dma_device_shim.cpp used to call the DMA driver functions
 * The definitions in this file a week reference so that these will be called only in case of unavailability of
 * actual DMA functions.
 * This file is need only if there is no hexagon SDK support or NO hexagon DMA support, in either csae we replace
 * the DMA operations with normal memory operations */

#include "pipeline.h"
#include "HalideRuntime.h"
#include "../../src/runtime/mini_hexagon_dma.h"

#include <stdlib.h>
#include <memory.h>

t_StDmaWrapper_DmaTransferSetup* dmaTransferParm;
typedef struct desc_data {
    void* descriptor;
    bool used;
    unsigned int size;
    struct desc_data* next;
} desc_pool_t;

typedef desc_pool_t* pdesc_pool;
static pdesc_pool dma_desc_pool = NULL;


t_DmaWrapper_DmaEngineHandle hDmaWrapper_AllocDma(void) {

    char* a = (char *)malloc(sizeof(char));
    return (void *)a;
}

int32 nDmaWrapper_FreeDma(t_DmaWrapper_DmaEngineHandle hDmaHandle) {

    char *a = (char *)hDmaHandle;
    free(a);
    return 0;
}

int32 nDmaWrapper_Move(t_DmaWrapper_DmaEngineHandle hDmaHandle) {

    if (hDmaHandle != 0) {
        unsigned char* host_addr = (unsigned char*) dmaTransferParm->pFrameBuf;
        unsigned char* dest_addr = (unsigned char*) dmaTransferParm->pTcmDataBuf;

        int x = dmaTransferParm->u16RoiX;
        int y = dmaTransferParm->u16RoiY;
        int w = dmaTransferParm->u16RoiW;
        int h = dmaTransferParm->u16RoiH;

        for (int xii=0;xii<h;xii++) {
            for (int yii=0;yii<w;yii++) {
                int xin = xii*w;
                int yin = yii;
                int RoiOffset = x+y*dmaTransferParm->u16FrameW;
                int xout = xii*dmaTransferParm->u16FrameW;
                int yout = yii;
                dest_addr[yin+xin] = host_addr[RoiOffset + yout + xout];
            }
        }
    }
    dmaTransferParm = 0;
    return 0;
}

int32 nDmaWrapper_Wait(t_DmaWrapper_DmaEngineHandle hDmaHandle) {
    return 0;
}

int32 nDmaWrapper_FinishFrame(t_DmaWrapper_DmaEngineHandle hDmaHandle) {
    return 0;
}

int32 nDmaWrapper_GetRecommendedWalkSize(t_eDmaFmt eFmtId, bool bIsUbwc,
                                         t_StDmaWrapper_RoiAlignInfo* pStWalkSize) {
    // for raw, mimic with NV12 linear, alignment is (W=256, H=1)
    pStWalkSize->u16H = align(pStWalkSize->u16H, 1);
    pStWalkSize->u16W = align(pStWalkSize->u16W, 1);
    return 0;
}

int32 nDmaWrapper_GetRecommendedIntermBufStride(t_eDmaFmt eFmtId,
                                                t_StDmaWrapper_RoiAlignInfo* pStRoiSize,
                                                 bool bIsUbwc) {
    return align(pStRoiSize->u16W, 256);

}

int32 nDmaWrapper_DmaTransferSetup(t_DmaWrapper_DmaEngineHandle hDmaHandle, t_StDmaWrapper_DmaTransferSetup* stpDmaTransferParm) {

    dmaTransferParm = stpDmaTransferParm;
    return 0;
}

int32 nDmaWrapper_GetDescbuffsize(t_eDmaFmt *aeFmtId, uint16 nsize) {

    int32 i, yuvformat=0,desc_size;
    for (i=0;i<nsize;i++) {
        if ((aeFmtId[i]==eDmaFmt_NV12)||(aeFmtId[i]==eDmaFmt_TP10)||
            (aeFmtId[i]==eDmaFmt_NV124R)||(aeFmtId[i]==eDmaFmt_P010)) {
            yuvformat += 1;
        }
    }
    desc_size = (nsize+yuvformat)*64;
    return desc_size;
}

void* desc_pool_get (unsigned int size) {

   /* if pool empty
    * get locked cache    --> multiple of 128B (cache line size) and each "desc" is 64B
    * add new "desc" into pool
    * get "desc" from pool
    * mark "desc" used
    * return "desc" */

    pdesc_pool temp = dma_desc_pool;
    pdesc_pool prev = NULL;
    if (temp != NULL) {
        if (!temp->used) {
            temp->used = true;
            return (void*) temp->descriptor;
        }
        prev = temp;
        temp=temp->next;
    }

    temp = (pdesc_pool) malloc(sizeof(desc_pool_t));
    //allocate descriptor
    char* desc = (char*)malloc(size);
    *desc = 'a';
    temp->descriptor = (void *)desc;
    temp->size = size;
    temp->used = true;
    if (prev != NULL) {
        prev->next = temp;
    } else if (dma_desc_pool == NULL) {
        dma_desc_pool = temp;
    }

    return (void*) temp->descriptor;
}

int desc_pool_put (void *desc) {
    /*find "desc"
    put "desc" into pool
    mark "desc" free*/

    pdesc_pool temp = dma_desc_pool;

    while (temp != NULL) {
        if (temp->descriptor == desc) {
            temp->used = false;
            return 0;
        }
        temp=temp->next;
    }
    return -1;
}

void desc_pool_free () {
    pdesc_pool temp = dma_desc_pool;
    while (temp != NULL) {
        pdesc_pool temp2 = temp;
        temp=temp->next;
        free(temp2->descriptor);
        free(temp2);
    }
}


