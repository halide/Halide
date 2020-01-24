// This header declares the Hexagon DMA API, without depending on the Hexagon SDK.

#ifndef MINI_HEXAGON_DMA_H
#define MINI_HEXAGON_DMA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef unsigned long addr_t;

typedef unsigned int qurt_size_t;
typedef unsigned int qurt_mem_pool_t;
#define HALIDE_HEXAGON_ENUM enum __attribute__((aligned(4)))

__inline static int align(int x, int a) {
    return ((x + a - 1) & (~(a - 1)));
}

HALIDE_HEXAGON_ENUM{QURT_EOK = 0};

/*!
 * Power Corner vote
 */
#define PW_MIN_SVS 0
#define PW_SVS2 1
#define PW_SVS 2
#define PW_SVS_L1 3
#define PW_NORMAL 4
#define PW_NORMAL_L1 5
#define PW_TURBO 6

/*!
 * Format IDs
 */
typedef HALIDE_HEXAGON_ENUM{
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

/*!
   * DMA status
   * Currently not use, for future development
   */
typedef void *t_stDmaWrapperDmaStats;

/*!
   * Transfer type
   */
typedef HALIDE_HEXAGON_ENUM eDmaWrapper_TransationType{
    //! DDR to L2 transfer
    eDmaWrapper_DdrToL2,
    //! L2 to DDR transfer
    eDmaWrapper_L2ToDdr,
} t_eDmaWrapper_TransationType;

/*!
   * Roi Properties
   */
typedef struct stDmaWrapper_Roi {
    //! ROI x position in pixels
    uint16 u16X;
    //! ROI y position in pixels
    uint16 u16Y;
    //! ROI width in pixels
    uint16 u16W;
    //! ROI height in pixels
    uint16 u16H;
} t_StDmaWrapper_Roi;

/*!
   * Frame Properties
   */
typedef struct stDmaWrapper_FrameProp {
    //! Starting physical address to buffer
    addr_t aAddr;
    //! Frame height in pixels
    uint16 u16H;
    //! Frame width in pixels
    uint16 u16W;
    //! Frame stride in pixels
    uint16 u16Stride;
} t_StDmaWrapper_FrameProp;

/*!
   * Roi alignment
   */
typedef struct stDmaWrapper_RoiAlignInfo {
    //! ROI width in pixels
    uint16 u16W;
    //! ROI height in pixels
    uint16 u16H;
} t_StDmaWrapper_RoiAlignInfo;

/*!
   * DmaTransferSetup Properties
   */

typedef struct stDmaWrapper_DmaTransferSetup {
    //! Frame Width in pixels
    uint16 u16FrameW;
    //! Frame height in pixels
    uint16 u16FrameH;
    //! Frame stride in pixels
    uint16 u16FrameStride;
    //! ROI x position in pixels
    uint16 u16RoiX;
    //! ROI y position in pixels
    uint16 u16RoiY;
    //! ROI width in pixels
    uint16 u16RoiW;
    //! ROI height in pixels
    uint16 u16RoiH;
    //! ROI stride in pixels
    uint16 u16RoiStride;
    //! Virtual address of the HW descriptor buffer (must be locked in the L2$).
    void *pDescBuf;
    //! Virtual address of the TCM pixeldata buffer (must be locked in the L2$).
    void *pTcmDataBuf;
    //! Virtual address of the DDR Frame buffer .
    void *pFrameBuf;
    //UBWC Format
    uint16 bIsFmtUbwc;
    //! Should the intermediate buffer be padded. This only apply for 8bit format sucha NV12, NV12-4R
    uint16 bUse16BitPaddingInL2;
    //! Format
    t_eDmaFmt eFmt;
    //! TransferType: eDmaWrapper_DdrToL2 (Read from DDR), eDmaWrapper_L2ToDDR (Write to DDR);
    t_eDmaWrapper_TransationType eTransferType;
} t_StDmaWrapper_DmaTransferSetup;

/*!
   * @brief  API for Cache Allocation
   * @description Abstraction for allocation of memory in cache and lock
   *
   * @return NULL or Memory
   */
void *HAP_cache_lock(unsigned int size, void **paddr_ptr);

/*!
   * @brief  API for Free
   * @description Abstraction for deallocation of memory and unlock cache
   *
   * @return void
   */
int HAP_cache_unlock(void *vaddr_ptr);

/*!
   * Handle for wrapper DMA engine
   */
typedef void *t_DmaWrapper_DmaEngineHandle;

/*!
   * @brief       Allocates a DMA Engine to be used
   *
   * @description Allocates a DMA Engine to be used by using the default
   *              wait type (polling).
   *
   * @return      Success: Engine's DMA Handle
   * @n           Failure: NULL
   */
extern t_DmaWrapper_DmaEngineHandle hDmaWrapper_AllocDma(void);

/*!
   * @brief       Frees a DMA Engine
   *
   * @description Frees a DMA Engine that was previously allocated by AllocDma().
   *
   * @input       hDmaHandle - Engine's DMA Handle
   *
   * @return      Success: OK
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_FreeDma(t_DmaWrapper_DmaEngineHandle hDmaHandle);

/*!
   * @brief       Starts a transfer request on the DMA engine
   *
   * @description Starts a transfer on the provided DMA engine. The transfer is based
   *              on descriptors constructed in earlier nDmaWrapper_Prepare() and
   *              nDmaWrapper_Update() calls.
   *
   * @input       hDmaHandle - Engine's DMA Handle
   *
   * @return      Success: OK
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_Move(t_DmaWrapper_DmaEngineHandle hDmaHandle);

/*!
   * @brief       Waits for all outstanding transfers on the DMA to complete
   *
   * @description Blocks until all outstanding transfers on the DMA are complete.
   *              The wait type is based on the type specified when allocating the
   *              engine.
   *
   * @input       hDmaHandle - Engine's DMA Handle
   *
   * @return      Success: OK
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_Wait(t_DmaWrapper_DmaEngineHandle hDmaHandle);

/*!
   * @brief       Cleans up all transfers and flushes DMA buffers
   *
   * @description This call flushes the DMA buffers.
   *              Blocks until the flush of the DMA is complete.
   *
   * @input       hDmaHandle - Engine's DMA Handle
   *
   * @return      Success: OK
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_FinishFrame(t_DmaWrapper_DmaEngineHandle hDmaHandle);

/*!
   * @brief       Get the recommended walk ROI width and height
   *
   * @description Get the recommended walk ROI width and height that should
   *              be used if walking the entire frame. The ROI returned is always
   *              in terms of frame dimensions. This function is different from
   *              nDmaWrapper_GetRecommendedRoi() as coordinates are not used.
   *
   * @input       eFmtId - Format ID
   * @input       bIsUbwc - Is the format UBWC (TRUE/FALSE)
   * @inout       pStWalkSize - Initial walk size, will be overwritten with
   *                            the recommended walk size to align with DMA
   *                            requirements
   *
   * @return      Success: OK
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_GetRecommendedWalkSize(t_eDmaFmt eFmtId, bool bIsUbwc,
                                                t_StDmaWrapper_RoiAlignInfo *pStWalkSize);

/*!
   * @brief       Get the HW descriptor buffer size per DMA engine
   *
   * @description Calculates the HW descriptor buffer size based
   *              on the formats that will be used with the engine.
   *
   * @input       aeFmtId - Array of format IDs, such as eDmaFmt_NV12, eDmaFmt_NV12_Y,
   *                        eDmaFmt_NV12_UV etc..
   * @input       nsize - Number of format IDs provided
   *
   * @return      Descriptor buffer size in bytes
   */
extern int32 nDmaWrapper_GetDescbuffsize(t_eDmaFmt *aeFmtId, uint16 nsize);

/*!
   * @brief       Get the recommended intermediate buffer stride.
   *
   * @description Get the recommended (minimum) intermediate buffer stride for the
   *              L2 Cache that is used transfer data from/to DDR. The stride is
   *              greater than or equal to the width and must be a multiple of 256.
   *
   * @input       eFmtId - Format ID
   * @input         pStRoiSize - The ROI that will be used (should be aligned with
   *                           the DMA requirements for the format)
   * @input         bIsUbwc - Is the format UBWC (TRUE/FALSE)
   *
   * @return      Success: The intermediate buffer stride in pixels
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_GetRecommendedIntermBufStride(t_eDmaFmt eFmtId,
                                                       t_StDmaWrapper_RoiAlignInfo *pStRoiSize,
                                                       bool bIsUbwc);

/*!
   * @brief       Get the recommended intermediate buffer size
   *
   * @description Get the recommended intermediate buffer size for the L2 cache
   *              that is used to transfer data to/from DDR.
   *
   * @input       eFmtId - Format ID
   * @input       bUse16BitPaddingInL2 - Is padding to 16 bits done in the L2 (TRUE/FALSE)
   * @input       pStRoiSize - The ROI that will be used (should be aligned with
   *                           DMA requirements for the format). The Chroma ROI must
   *                           follow the standing convention that the provided
   *                           width and height must be specified in terms of the
   *                           Luma plane also such that when the width is divided
   *                           by 2 it specifies the number of interleaved Chroma
   *                           pixels.
   * @input               bIsUbwc - Is the format UBWC (TRUE/FALSE), note that this should
   *                        be set to TRUE if either the DDR input or output will
   *                        be UBWC
   * @input               u16IntermBufStride - The stride (in pixels) to use, the minimum
   *                                   stride may be obtained by calling
   *                                   nDmaWrapper_GetRecommendedIntermBufStride
   *
   * @return      Success: The intermediate buffer size in bytes
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_GetRecommendedIntermBufSize(t_eDmaFmt eFmtId, bool bUse16BitPaddingInL2,
                                                     t_StDmaWrapper_RoiAlignInfo *pStRoiSize,
                                                     bool bIsUbwc, uint16 u16IntermBufStride);

/*!
   * @brief       Dma transfer parameters per HW descriptor
   *
   * @description Setup Dma transfer parameters required to be ready to make DMA transfer.
   *              call this API multiple to create a descriptor link list
   *
   * @input       hDmaHandle - Wrapper's DMA Handle. Represents
   *                  t_StDmaWrapper_DmaEngine.
   * @input       stpDmaTransferParm - Dma Transfer parameters. Each element describes
   *                  complete Frame/ROI details for this Dma transfer
   *
   * @return      Success: OK
   *              Failure: ERR
   */
extern int32 nDmaWrapper_DmaTransferSetup(t_DmaWrapper_DmaEngineHandle hDmaHandle, t_StDmaWrapper_DmaTransferSetup *stpDmaTransferParm);

/*!
   * @brief       DMA power voting based on Cornercase
   *
   * @description DMA power voting
   *
   * @input       cornercase:
   *                            #define PW_MIN_SVS 0
   *                            #define PW_SVS2 1
   *                            #define PW_SVS 2
   *                            #define PW_SVS_L1 3
   *                            #define PW_NORMAL 4
   *                            #define PW_NORMAL_L1 5
   *                            #define PW_TURBO 6
   * @return      Success: OK
   * @n           Failure: ERR
   */
extern int32 nDmaWrapper_PowerVoting(uint32 cornercase);

#ifdef __cplusplus
}
#endif

#endif
