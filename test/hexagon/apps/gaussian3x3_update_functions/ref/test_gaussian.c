#include "hexagon_types.h"
#include "hexagon_standalone.h"
#include <stdlib.h>
#include <stdio.h>
#include "q6sim_timer.h"
#if defined(__hexagon__)
#include <unistd.h>
#include <fcntl.h>
// This should be in the unistd for hexagon, but it's not?
ssize_t      write(int, const void *, size_t);

#define FH int
#define O_CREAT_WRONLY_TRUNC (O_CREAT | O_WRONLY | O_TRUNC)
#define IS_INVALID_FILE_HANDLE(_a) (_a < 0)
#endif
int gaussian_wrapper(const uint8_t* imgSrc, int srcLen,
                     uint32_t srcWidth, uint32_t srcHeight, uint32_t srcStride, uint8_t* imgDst,
                     int dstLen, uint32_t dstStride);
#define SYNTHETIC_WIDTH 128
#define SYNTHETIC_HEIGHT 128
#ifdef SYNTHETIC
void setupSyntheticInputOutput(uint8_t **src, uint8_t **dst,
                               uint32_t width, uint32_t height, uint32_t align) {
  (*src) = memalign(align, width*height*sizeof(unsigned char));
  (*dst) = memalign(align, width*height*sizeof(unsigned char));
}
int validateSyntheticOutput(uint8_t *src, uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight, uint32_t dstStride,
                            uint32_t srcWidth, uint32_t srcHeight, uint32_t srcStride, int borders) {
  uint32_t x, y, i , j;
  unsigned int col[3];
  unsigned int refval;
  uint32_t ht_max, width_max;
  y = borders ? 0 : 1;
  x = borders ? 0 : 1;
  ht_max = borders ? dstHeight : dstHeight -1;
  width_max = borders ? dstWidth : dstWidth - 1;
  for (; y < (ht_max); y++) {
    for (; x < (width_max); x++) {
      for (i = 0; i < 3; i++) {
        if (x > 0 && y > 0) {
          col[i] = (unsigned int) (1 * src[(y - 1) * srcStride + x - 1 + i]
                                   + 2 * src[(y + 0) * srcStride + x - 1 + i]
                                   + 1 * src[(y + 1) * srcStride + x - 1 + i]);
        }
      }
      if (y == 0 && x == 0) {
        refval = ((src[0] * 9) + (src[1] * 3) + (src[srcStride] * 3) + (src[srcStride + 1])) >> 4;
      }
      else if (y == 0 && x == (dstWidth - 1)) {
        refval = ((src[srcWidth - 2] * 3) + (src[(srcWidth - 1)] * 9)
                  + src[srcWidth - 2 + srcStride]
                  + (src[srcWidth - 1 + srcStride] * 3)) >> 4;
      }
      else if (y == 0) {
        refval = ((src[x - 1]) + (src[x] << 1) + (src[x + 1])
                  + (src[x - 1] << 1) + (src[x] << 2) + (src[x + 1] << 1)
                  + (src[srcStride + (x - 1)])
                  + (src[srcStride + (x)] << 1)
                  + (src[srcStride + (x + 1)])) >> 4;
      }
      else if (x == 0 && y != (dstHeight - 1)) {
        refval = ((src[(y - 1) * srcStride]) + (src[y * srcStride] << 1)
                  + (src[(y + 1) * srcStride])
                  + (src[(y - 1) * srcStride] << 1)
                  + (src[y * srcStride] << 2)
                  + (src[(y + 1) * srcStride] << 1)
                  + (src[(y - 1) * srcStride + 1])
                  + (src[y * srcStride + 1] << 1)
                  + (src[(y + 1) * srcStride + 1])) >> 4;
      }

      else if (y == (dstHeight - 1) && x == (dstWidth - 1)) {
        refval =
          ((src[srcStride * (srcHeight - 2) + (srcWidth - 2)])
           + (src[srcStride * (srcHeight - 2)
                  + (srcWidth - 1)] * 3)
           + (src[srcStride * (srcHeight - 1)
                  + (srcWidth - 2)] * 3)
           + (src[srcStride * (srcHeight - 1)
                  + (srcWidth - 1)] * 9)) >> 4;
      }
      else if (x == (dstWidth - 1)) {
        refval = ((uint8_t) src[srcStride * (y - 1) + (srcWidth - 1)]
                  + ((uint8_t) src[srcStride * (y) + (srcWidth - 1)] << 1)
                  + (uint8_t) src[srcStride * (y + 1) + (srcWidth - 1)]
                  + ((uint8_t) src[srcStride * (y - 1) + (srcWidth - 1)]
                     << 1)
                  + ((uint8_t) src[srcStride * (y) + (srcWidth - 1)] << 2)
                  + ((uint8_t) src[srcStride * (y + 1) + (srcWidth - 1)]
                     << 1)
                  + ((uint8_t) src[srcStride * (y - 1) + (srcWidth - 2)])
                  + ((uint8_t) src[srcStride * (y) + (srcWidth - 2)] << 1)
                  + (uint8_t) src[srcStride * (y + 1) + (srcWidth - 2)])
          >> 4;
      }

      else if (y == (dstHeight - 1) && x == 0) {
        refval = ((src[srcStride * (srcHeight - 2)] * 3)
                  + (src[srcStride * (srcHeight - 2) + 1])
                  + (src[srcStride * (srcHeight - 1)] * 9)
                  + (src[srcStride * (srcHeight - 1) + 1] * 3)) >> 4;

      }
      else if (y == (dstHeight - 1)) {
        float refval2 =
          ((src[srcStride * (srcHeight - 1) + (x - 1)])
           + (src[srcStride * (srcHeight - 1)
                  + (x)] << 1)
           + (src[srcStride * (srcHeight - 1)
                  + (x + 1)])
           + ( src[srcStride * (srcHeight - 1)
                   + (x - 1)] << 1)
           + (src[srcStride * (srcHeight - 1)
                  + (x)] << 2)
           + ( src[srcStride * (srcHeight - 1)
                   + (x + 1)] << 1)
           + ( src[srcStride * (srcHeight - 2)
                   + (x - 1)])
           + ( src[srcStride * (srcHeight - 2)
                   + (x)] << 1)
           + (src[srcStride * (srcHeight - 2)
                  + (x + 1)])) / 16.0F;

        refval2 = refval2 + 0.5;
        refval = (unsigned int) (refval2);


      }
      else {
        refval = (unsigned int) (uint8_t) ((1 * col[0] + 2 * col[1]
                                            + 1 * col[2] + (1 << 3)) >> 4);
      }

      if (refval != dst[y * dstStride + x]) {
        printf(
               "Bit exact error: y = %d, x = %d, refval = %d, dst = %d\n",
               y, x, refval, dst[y * dstStride + x]);
        //bitexactErrors++;
        /* exit(1); */
	   
      }
      
    }
  }

}
int main() {
  // Create the Input.
  int x, y;


  long long start_time, total_cycles;
  uint8_t *src, *dst;
  uint32_t srcWidth = SYNTHETIC_WIDTH;
  uint32_t srcHeight = SYNTHETIC_HEIGHT;
  uint32_t srcStride = SYNTHETIC_WIDTH;    // keep aligned to 128 bytes!
  int retval;
  uint32_t dstWidth = srcWidth;
#ifdef DEBUG
  printf ("srcWidth = %d, dstWidth = %d\n", srcWidth, dstWidth);
#endif

  uint32_t dstHeight = srcHeight;
  uint32_t dstStride = srcStride;
  int i, j;
  int nErr = 0;
  
  int srcSize = srcStride * srcHeight;
  int dstSize = dstStride * dstHeight;
  setupSyntheticInputOutput(&src, &dst, srcWidth, srcHeight, 128);
#if DEBUG
  printf ("initializing inputs\n");
#endif
  for (y = 0; y < 128; ++y) {
    for (x = 0; x < 128; ++x) {
      src[y*srcStride +x]= y+x;
    }
  }
#if DEBUG
  printf ("finished initializing inputs\n");
#endif
  // And the memory where we want to write our output:                                                                                                                               
  printf ("initializing output\n");
  for (y = 0; y < 128; ++y) {
    for (x = 0; x < 128; ++x) {
      dst[y*dstStride +x] = 0;
    }
  }
#if DEBUG
  printf ("finished initializing output\n");
#endif
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
#ifdef DEBUG
  printf ("dstWidth = %d\n", dstWidth);
#endif

  retval = gaussian_wrapper(src, srcSize, srcWidth, srcHeight, srcStride, dst,
                            dstSize, dstStride);
#ifdef DEBUG
  printf ("dstWidth = %d, retval = %d\n", dstWidth, retval);
#endif
  SIM_RELEASE_HVX;
#ifdef DEBUG
  printf("done with gaussian_wrapper\n");
#endif

#ifdef DEBUG
  printf ("dstWidth = %d, dstHeight = %d\n", dstWidth, dstHeight);
  printf ("srcWidth = %d, srcHeight = %d\n", srcWidth, srcHeight);
#endif
  if (retval ||
      !validateSyntheticOutput(src, dst, dstWidth, dstHeight, dstStride,
                               srcWidth, srcHeight, srcStride, 1 /*borders*/)) 
    exit(1);
  else
    return 0;
}
#else
int main(int argc, char **argv) {

  int x, y;
  int i, j;
  uint32_t width, height;
  FH fp;
  int retval;
  unsigned char *input, *output;
  if (argc != 5){
    printf("usage: %s <width> <height> <input.bin> <output.bin>\n", argv[0]);
    return 1;
  }

  width  = atoi(argv[1]);
  height = atoi(argv[2]);
  input  = (unsigned char *)memalign(128, width*height*sizeof(unsigned char));
  output = (unsigned char *)memalign(128, width*height*sizeof(unsigned char));

  if ( input == NULL || output == NULL ){
    printf("Error: Could not allocate Memory for image\n");
    return 1;
  }

  /* -----------------------------------------------------*/
  /*  Read image input from file                          */
  /* -----------------------------------------------------*/
  if((fp = open(argv[3], O_RDONLY)) < 0 )
    {
      printf("Error: Cannot open %s for input\n", argv[3]);
      return 1;
    }

  for(i = 0; i < height; i++)
    {
      if(read(fp, &input[i*width],  sizeof(unsigned char)*width)!=width)
        {
          printf("Error, Unable to read from %s\n", argv[3]);
          close(fp);
          return 1;
        }
    }
  close(fp);

  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;

  retval = gaussian_wrapper(input, width*height, width, height, width, output,
                            width*height, width);
  SIM_RELEASE_HVX;
  /* -----------------------------------------------------*/
  /*  Write image output to file                          */
  /* -----------------------------------------------------*/
  if((fp = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
      printf("Error: Cannot open %s for output\n", argv[4]);
      return 1;
    }

  for(i = 0; i < height; i++)
    {
      if(write(fp, &output[i*width], sizeof(unsigned char)*(width))!=(width)) // do not exclude the boundary pixels
        {
          printf("Error:  Writing file: %s\n", argv[4]);
          return 1;
        }
    }
  close(fp);

  free(input);
  free(output);

  return 0;
}
#endif
