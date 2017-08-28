/*!
 *  This file is a mini version of dma_defs.h
 * This file has only the mandatory definitions
 * needed for hexagon DMA transfer
 */
#ifndef _MINIDMA_H
#define _MINIDMA_H

/*!
 * Format IDs
 */
typedef enum {
    eDmaFmt_RawData,
    eDmaFmt_NV12,
    eDmaFmt_NV12_Y,
    eDmaFmt_NV12_UV,
    eDmaFmt_P010,
    eDmaFmt_P010_Y,
    eDmaFmt_P010_UV,
    eDmaFmt_TP10,
    eDmaFmt_TP10_Y,
    eDmaFmt_TP10_UV,
    eDmaFmt_NV124R,
    eDmaFmt_NV124R_Y,
    eDmaFmt_NV124R_UV,
    eDmaFmt_Invalid,
    eDmaFmt_MAX,
} t_eDmaFmt;

#endif
