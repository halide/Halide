#ifndef GAUSSIAN_ASM_H
#define GAUSSIAN_ASM_H


#include <stdlib.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef enum
{
   /// Border behavior undefined, left to the implementation.
   FASTCV_BORDER_UNDEFINED = 0,

   /// For out-of-bound pixels, apply a user-specified constant value.
   FASTCV_BORDER_CONSTANT,

   /// For out-of-bound pixels, apply values from the nearest edge pixels.
   FASTCV_BORDER_REPLICATE

} fcvBorderType;

//---------------------------------------------------------------------------
/// @brief
///   Downsample to half the input size. The output image width and height
///   are half of the input width and height.
///
/// @detailed
///    TBD.
///
/// @param srcImg
///   Input 8-bit image.
///   \n\b NOTE: data must be 4-byte aligned.
///
/// @param width
///   Image width.
///   \n\b NOTE: must be multiple of 2
///
/// @param height
///   Image height.
///   \n\b NOTE:must be a multiple of 2
///
/// @param stride
///   Image stride.
///   \n\b NOTE: must be multiple of 4 and >= width
///
/// @param dstImg
///   Output 8-bit image.
///   \n\b NOTE: data must be 4-byte aligned.
///
/// @param dstStride
///   destination Image stride.
///   \n\b NOTE: must be multiple of 4 and >= (srcWidth / 2)
//---------------------------------------------------------------------------

void
gaussian( const unsigned  char *imgSrc, unsigned int width, unsigned int height,
    unsigned int stride, unsigned  char *imgDst, unsigned int dstStride);

//---------------------------------------------------------------------------
/// @brief
///   Downsample to half the input size. The output image width and height
///   are half of the input width and height. Note that this function 
///   assumes src and dst are aligned to VLEN boundaries. This keeps the 
///   HVX logic as simple and efficient as possible, for the sake of this
///   simple example function.
///
/// @detailed
///    TBD.
///
/// @param srcImg
///   Input 8-bit image.
///   \n\b NOTE: data must be VLEN aligned.
///
/// @param width
///   Image width.
///   \n\b NOTE: must be multiple of 2
///
/// @param height
///   Image height.
///   \n\b NOTE:must be a multiple of 2
///
/// @param stride
///   Image stride.
///   \n\b NOTE: must be multiple of VLEN and >= width
///
/// @param dstImg
///   Output 8-bit image.
///   \n\b NOTE: data must be VLEN aligned.
///
/// @param dstStride
///   destination Image stride.
///   \n\b NOTE: must be multiple of VLEN and >= (srcWidth / 2)
///
/// @param VLEN
///   HVX vector length. 
///   \n\b NOTE: Must accurately match the current HVX mode configured in HW.
//---------------------------------------------------------------------------

void
gaussian_hvx( const unsigned  char *imgSrc, unsigned int width, unsigned int height,
    unsigned int stride, unsigned  char *imgDst, unsigned int dstStride, unsigned int VLEN);

void
gaussian_top( const unsigned  char *imgSrc, unsigned int width, unsigned int height,
    unsigned int stride, unsigned  char *imgDst, unsigned int dstStride, fcvBorderType borderType, uint8_t borderValue);

void
gaussian_hvx_top(const unsigned char *__restrict imgSrc, unsigned int width,
		unsigned int height, unsigned int stride,
		unsigned char *__restrict imgDst, unsigned int dstStride,
		unsigned int VLEN, fcvBorderType borderType, uint8_t borderValue);

void
gaussian_hvx_bottom(const unsigned char *__restrict imgSrc, unsigned int width,
		unsigned int height, unsigned int stride,
		unsigned char *__restrict imgDst, unsigned int dstStride,
		unsigned int VLEN, fcvBorderType borderType, uint8_t borderValue);



#ifdef __cplusplus
}
#endif

#endif    // GAUSSIAN_ASM_H
