#include "nv12torgb888.h"
#include <hexagon_standalone.h>
#include "io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    long long start_time, total_cycles;
    FH Infile;
    FH Outfile;
    int y;

    /* -----------------------------------------------------*/
    /*  Get input parameters                                */
    /* -----------------------------------------------------*/
    if (argc != 5)
    {
        printf("Usage: %s inputfile width height outputfile\n", argv[0]);
        return -1;
    }

    // Create the Input.
    int width  = atoi(argv[2]);
    int height = atoi(argv[3]); // height required to be even!
#ifdef SYNTHETIC
    width = 6;
    height = 8;
#endif
    if(height&1)
    {
        printf("height must be even\n");
        return -1;
    }
    int VLEN = 1<<LOG2VLEN;
    int stride = (width + VLEN - 1)&(-VLEN);
    // dstStride is stride*sizeof(unsigned int)
    int error = 0;

    printf("Width: %d height: %d stride: %d \n",width, height, stride);
    unsigned char *src = (unsigned char *)memalign(1<<LOG2VLEN, sizeof(src[0]) * stride * height * 3 / 2);
    unsigned int *dst = (unsigned int *)memalign(1<<LOG2VLEN, sizeof(dst[0]) * stride * height);
    unsigned int *dstRef = (unsigned int *)memalign(1<<LOG2VLEN, sizeof(dst[0]) * stride * height);

    unsigned char* yuv420sp = src;
    unsigned char* uv420sp = src+stride*height;

    /* -----------------------------------------------------*/
    /*  Read image input from file                          */
    /* -----------------------------------------------------*/
    if((Infile = open(argv[1], O_RDONLY)) < 0 )
    {
        printf("Error: Cannot open %s for input\n", argv[1]);
        return 1;
    }

    for (y = 0; y < height*3/2; y++)
    {
        //printf("Reading %d line\n", y);
        if (read(Infile, &src[y*stride],  sizeof(src[0]) * width) != sizeof(src[0])* width)
        {
            printf("Error, Unable to read from input file %s\n", argv[1]);
            return 1;
        }
    }

#ifdef SYNTHETIC
    memset(dstRef, 0, stride*height*4);
    dstRef[0] = -16727808;
    dstRef[1] = -16719616;
    dstRef[2] = -14298327;
    dstRef[3] = -15745773;
    dstRef[4] = -16729587;
    dstRef[5] = -16725732;
    { printf("src\n");
      for (int i=0; i<height*3/2; i++) {
        for (int j=0; j<width; j++)
           printf("%5d ", src[i*stride+j]);
        printf("\n");
      }
      printf("Ref\n");
      for (int i=0; i<height; i++) {
        for (int j=0; j<width; j++)
           printf("%8d ", dstRef[i*stride+j]);
        printf("\n");
      }
    }
#endif

    // create buffer structures
    buffer_t yuv420_buf = {
                        .host = (uint8_t*)yuv420sp,
                        .stride[0] = 1,
                        .stride[1] = stride,
                        .extent[0] = width,
                        .extent[1] = height,
                        .elem_size = 1,
                        };

    buffer_t uv420_buf = {
                        .host = (uint8_t*)uv420sp,
                        .stride[0] = 1,
                        .stride[1] = stride,
                        .extent[0] = width,
                        .extent[1] = height/2,
                        .elem_size = 1,
                        };

    buffer_t dst_buf = {
                        .host = (uint8_t*)dst,
                        .stride[0] = 1,
                        .stride[1] = stride,
                        .extent[0] = width,
                        .extent[1] = height,
                        .elem_size = 4,
                        };

#if defined(__hexagon__)
    SIM_ACQUIRE_HVX;
#if LOG2VLEN == 7
    SIM_SET_HVX_DOUBLE_MODE;
#endif
#endif
    /* -----------------------------------------------------*/
    /*  Call function                                       */
    /* -----------------------------------------------------*/
    RESET_PMU();
    start_time = READ_PCYCLES();

    nv12torgb888(&yuv420_buf,&uv420_buf,&dst_buf);

    total_cycles = READ_PCYCLES() - start_time;
    DUMP_PMU();
    SIM_RELEASE_HVX;
#if defined(__hexagon__)
    printf("AppReported (HVX%db-mode): Image %dx%d - NV12 to RGB8888: %0.4f cycles/pixel\n", 1<<LOG2VLEN, (int)width, (int)height, (float)total_cycles/width/height);
#endif

    if (error) {
        printf("Halide returned an error: %d\n", error);
        return -1;
    }
    error = 0;
#ifdef SYNTHETIC
    printf("dst\n");
    for (int i=0; i<height; i++) {
      for (int j=0; j<width; j++)
         printf("%8d ", dst[i*stride+j]);
      printf("\n");
    }
    // Verify output
    for(int i = 0; i < height; i++)
    {
        for(int j = 0; j < width; j++)
        {
            if(dstRef[i*stride+j] != dst[i*stride+j])
            {
                printf("MISMATCH (%d,%d): ref = %d, tst = %d\n",j,i,dstRef[i*stride+j],dst[i*stride+j]);
                error = 1;
             //   goto cleanup;
            }
        }
    }
#endif
    /* -----------------------------------------------------*/
    /*  Write image output to file                          */
    /* -----------------------------------------------------*/
    if((Outfile = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
        printf("Error: Cannot open %s for output\n", argv[4]);
        return 1;
    }

    for (y = 0; y < height; y++)
    {
        if (write(Outfile, dst+y*stride, sizeof(dst[0])* width) != sizeof(dst[0])* width)
        {
            printf("Error, Unable to write to output\n");
            return 1;
        }
    }

    error ? printf("FAIL!\n") : printf("PASS!\n");
    return 0;
}
