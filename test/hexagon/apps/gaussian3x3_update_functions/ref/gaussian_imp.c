/**=============================================================================

 @file
 gaussian_imp.cpp
 =============================================================================**/

//==============================================================================
// Include Files
//==============================================================================
#include "hexagon_types.h"
#include "gaussian_asm.h"
#include "hexagon_protos.h"
#define VLEN 128
//#define ASM_HVX 1
//#define INT_HVX 1
#define RESET_PMU()     __asm__ __volatile__ (" r0 = #0x48 ; trap0(#0); \n" : : : "r0","r1","r2","r3","r4","r5","r6","r7","memory")
#define DUMP_PMU()      __asm__ __volatile__ (" r0 = #0x4a ; trap0(#0); \n" : : : "r0","r1","r2","r3","r4","r5","r6","r7","memory")
#define READ_PCYCLES    q6sim_read_pcycles

int gaussian_wrapper(const uint8_t* imgSrc, int srcLen,
		uint32_t srcWidth, uint32_t srcHeight, uint32_t srcStride, uint8_t* imgDst,
                     int dstLen, uint32_t dstStride) {
  long long start_time, total_cycles, end_time;

  // parameter checks for sensible image size/alignment
  if (!(imgSrc && imgDst && (((uint32_t) imgSrc & 3) == 0)
        && (((uint32_t) imgDst & 3) == 0) && (srcWidth >= 16)
        && (srcHeight >= 2) && ((srcWidth & 1) == 0)
        && ((srcHeight & 1) == 0) && (srcStride >= srcWidth)
        && (dstStride >= srcWidth) && ((srcStride & 7) == 0)
        && ((dstStride & 7) == 0))) {
    return 14;
  }
  // This HVX implementation assumes 128-byte aligned buffers (and strides)
   RESET_PMU();
   start_time = READ_PCYCLES();

  if (0 == (127 & ((uint32_t) imgSrc | (uint32_t) imgDst | srcStride | dstStride))) {
 
#ifdef ASM_HVX
    gaussian3x3_hvx(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, VLEN);
    gaussian3x3_hvx_borders(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, VLEN, FASTCV_BORDER_REPLICATE, 10);
#endif
   
#ifdef INT_HVX
    gaussian_hvx(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, VLEN);
    gaussian_hvx_top(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, VLEN, FASTCV_BORDER_REPLICATE, 10);
#endif


#ifdef INT_SCALAR
    gaussian(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride);
    gaussian_top(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, FASTCV_BORDER_REPLICATE, 10);
#endif
#ifdef ASM_SCALAR
    gaussian(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride);
    gaussian_top(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, FASTCV_BORDER_REPLICATE, 10);
#endif

  }

  else {
    printf("Image not aligned for HVX, should fall back to scalar\n");

#ifdef INT_SCALAR
    gaussian(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride);
    gaussian_top(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, FASTCV_BORDER_REPLICATE, 10);
#endif
#ifdef ASM_SCALAR
    gaussian(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride);
    gaussian_top(imgSrc, srcWidth, srcHeight, srcStride, imgDst, dstStride, FASTCV_BORDER_REPLICATE, 10);
#endif


  }	  
  total_cycles = READ_PCYCLES() - start_time;
  DUMP_PMU();
#if ASM_HVX
  printf("ASM: (HVX128B-mode): Image %dx%d - gaussian3x3: %0.4f cycles/pixel (Total Cycles = %lld)\n",
         (int)srcWidth, (int)srcHeight, (float)total_cycles/srcWidth/srcHeight, total_cycles);
#endif
#if INT_HVX
 printf("Intrinsics: (HVX128B-mode): Image %dx%d - gaussian3x3: %0.4f cycles/pixel (Total Cycles = %lld)\n",
         (int)srcWidth, (int)srcHeight, (float)total_cycles/srcWidth/srcHeight, total_cycles);
#endif
  return 0;
}

