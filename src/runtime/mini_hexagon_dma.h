/**
 * This file has the necessary structures and APIs for Initiating, executing and finishing a Hexagon DMA transfer
 * The functions in this header file are the interfacing functions between Halide runtime and the Hexagon DMA driver
 * The functions in this file lead to the hexagon DMA driver calls in case of availability of DMA driver and hexagon DMA tools
 * In case of un availabilty of hexagon SDK tools and DMA drivers these function while mimic DMA with dummy transfers  */

#ifndef _DMA_DEVICE_SHIM_H_
#define _DMA_DEVICE_SHIM_H_

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int32_t int32;
typedef unsigned long addr_t;

typedef unsigned int qurt_size_t;
typedef unsigned int qurt_mem_pool_t;
#define ENUM  enum __attribute__((aligned(4)))

__inline static int align(int x,int a) {
    return ( (x+a-1) & (~(a-1)) );    
} 

ENUM { QURT_EOK = 0 };

/*!
 * Format IDs
 */
typedef ENUM {
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
  typedef void* t_stDmaWrapperDmaStats;

  /*!
   * Transfer type
   */
  typedef ENUM eDmaWrapper_TransationType {
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
    void* pDescBuf;
    //! Virtual address of the TCM pixeldata buffer (must be locked in the L2$).
    void*  pTcmDataBuf;
    //! Virtual address of the DDR Frame buffer .
    void*  pFrameBuf;
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
  void* HAP_cache_lock(unsigned int size, void** paddr_ptr);


  /*!
   * @brief  API for Free
   * @description Abstraction for deallocation of memory and unlock cache
   *
   * @return void
   */
  int HAP_cache_unlock(void* vaddr_ptr);

  /*!
   * Handle for wrapper DMA engine
   */
  typedef void* t_DmaWrapper_DmaEngineHandle;

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
                                                  t_StDmaWrapper_RoiAlignInfo* pStWalkSize);

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
                                                         t_StDmaWrapper_RoiAlignInfo* pStRoiSize,
                                                         bool bIsUbwc);

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
  extern int32 nDmaWrapper_DmaTransferSetup(t_DmaWrapper_DmaEngineHandle hDmaHandle, t_StDmaWrapper_DmaTransferSetup* stpDmaTransferParm);

  /** @example dma_memcpy.c
   *  Copies one region of memory to another.
   *  @example dma_memcpy.h
   *  @example dma_memcpy_test.c
   */

  /** @example dma_blend.c
   *  DMA Blend App. Will read 2 frames from DDR, blend them and output
   *  the result to DDR.
   *  @example dma_blend.h
   *  @example dma_blend_test.c
   */


#ifdef __cplusplus
}
#endif

#endif
