/***************************************************************************
* Copyright (c) Date: Mon Nov 24 16:26:02 CST 2008 QUALCOMM INCORPORATED
* All Rights Reserved
* Modified by QUALCOMM INCORPORATED on Mon Nov 24 16:26:03 CST 2008
****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#if defined(__hexagon__)
#include "hexagon_standalone.h"
#endif
#include "io.h"
#include "bilateral.h"


#define KERNEL_SIZE     9
#define Q               8
#define PRECISION       (1<<Q)
#define VLEN (1<<LOG2VLEN)

double getGauss(double sigma,double value)
{
    return exp( -( value/(2.0 * sigma * sigma) ) );

}


int main(int argc, char* argv[])
{
    int i,j;
    int width, height, stride;
    FH fp;

    long long start_time, total_cycles;

    /* -----------------------------------------------------*/
    /*  Get input parameters                                */
    /* -----------------------------------------------------*/
    if (argc != 5){
        printf("usage: %s <width> <height> <input.bin> <output.bin>\n", argv[0]);
        return 1;
    }

#ifdef SYNTHETIC
    width=10;
    height=12;
    stride = width;
    printf("Using synthetic size 12x10\n");
#else
    width  = atoi(argv[1]);
    height = atoi(argv[2]);
    stride = (width + VLEN-1)&(-VLEN);  // make stride a multiple of HVX vector size
#endif

    /* -----------------------------------------------------*/
    /*  Allocate memory for input/output                    */
    /* -----------------------------------------------------*/
    unsigned char *input  = (unsigned char *)memalign(VLEN, stride*height*sizeof(unsigned char));
    unsigned char *output = (unsigned char *)memalign(VLEN, stride*height*sizeof(unsigned char));

    if ( input == NULL || output == NULL ){
        printf("Error: Could not allocate Memory for image\n");
        return 1;
    }

#ifdef SYNTHETIC
  char loc_input[12][10] = {
  38, 50, 46, 46, 45, 44, 45, 45, 44, 46,
  49, 51, 54, 57, 59, 63, 66, 70, 74, 76,
  81, 83, 83, 84, 86, 87, 88, 87, 86, 82,
  81, 79, 76, 73, 71, 66, 62, 59, 56, 53,
  51, 48, 46, 44, 43, 42, 42, 40, 40, 39,
  39, 40, 41, 42, 44, 46, 47, 51, 54, 56,
  60, 61, 64, 66, 67, 66, 67, 68, 67, 65,
  64, 59, 59, 58, 56, 53, 50, 47, 44, 41,
  39, 38, 35, 33, 30, 28, 26, 25, 23, 21,
  21, 20, 19, 18, 17, 16, 15, 14, 15, 15,
  14, 13, 13, 13, 12, 13, 14, 12, 12, 12,
  12, 12, 12, 12, 13, 12, 15, 14, 15, 15,
};
  memcpy(input, loc_input, 12*10);
#else
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
        if(read(fp, &input[i*stride],  sizeof(unsigned char)*width)!=width)
        {
            printf("Error, Unable to read from %s\n", argv[3]);
            close(fp);
            return 1;
        }
    }
    close(fp);
#endif
#if DEBUG
  printf ("finished reading the input.\n");
#endif

#if defined(__hexagon__)
    SIM_ACQUIRE_HVX;
#if LOG2VLEN == 7
    SIM_SET_HVX_DOUBLE_MODE;
#endif
#endif

    /* -----------------------------------------------------*/
    /*  Generate coefficients table                         */
    /* -----------------------------------------------------*/
    double sigmaS = 0.6;
    double sigmaR = 0.2;

    unsigned char gauss_LUT[KERNEL_SIZE*KERNEL_SIZE];

    unsigned char *range_LUT = (unsigned char *)memalign(PRECISION,256);

    // Space gaussian coefficients calculation
    int x, y;
    int center = KERNEL_SIZE/2;
    for(y=-center;y<center+1;y++)
    {
        for(x=-center;x<center+1;x++)
        {
            double y_r = y/(double)KERNEL_SIZE;
            double x_r = x/(double)KERNEL_SIZE;
            double gauss = getGauss(sigmaS,(x_r * x_r + y_r * y_r));
            unsigned char gauss_fixedpoint = gauss*PRECISION - 1;
            gauss_LUT[(y+center)*KERNEL_SIZE + x+center] = gauss_fixedpoint;
        }
    }

    // range gaussian coefficients calculation
    for(y=0;y<PRECISION;y++)
    {
        double  y_r = y/(double)PRECISION;
        double     range = getGauss(sigmaR,y_r*y_r);
        unsigned char    range_fixedpoint = range*PRECISION -1;
        range_LUT[y]=range_fixedpoint;
    }

#if DEBUG
  printf ("finished generating gauss_LUT and range_LUT.\n");
  printf("Range_LUT:\n");
  for(y=0;y<PRECISION;y++)
    printf("  %d", range_LUT[y]);
  printf("\nGauss_LUT:\n");
  for(y=0;y<KERNEL_SIZE;y++) {
    for(x=0;x<KERNEL_SIZE;x++)
      printf("  %d", gauss_LUT[y*KERNEL_SIZE+x]);
    printf("\n");
  }
#endif
  buffer_t input1_buf = {0}, output_buf = {0};
  buffer_t gauss_LUT_buf = {0}, range_LUT_buf = {0};

  // The host pointers point to the start of the image data:
  input1_buf.host = (uint8_t *)&input[0];
  output_buf.host = (uint8_t *)&output[0];

  input1_buf.stride[0] = output_buf.stride[0] = 1;
  input1_buf.stride[1] = width;  output_buf.stride[1] = width;
  input1_buf.extent[0] = width;
  output_buf.extent[0] = width;
  input1_buf.extent[1] = height;
  output_buf.extent[1] = height;
  input1_buf.elem_size = 1; output_buf.elem_size = 4;

  gauss_LUT_buf.host = (uint8_t *)&gauss_LUT[0];
  gauss_LUT_buf.stride[0] = 1;
  gauss_LUT_buf.stride[1] = KERNEL_SIZE;
  gauss_LUT_buf.extent[0] = KERNEL_SIZE;
  gauss_LUT_buf.extent[1] = KERNEL_SIZE;
  gauss_LUT_buf.elem_size = 1;

  range_LUT_buf.host = (uint8_t *)&range_LUT[0];
  range_LUT_buf.extent[0] = PRECISION;
  range_LUT_buf.stride[0] = 1;
  range_LUT_buf.elem_size = 1;


    /* -----------------------------------------------------*/
    /*  Call fuction                                        */
    /* -----------------------------------------------------*/
    RESET_PMU();
    start_time = READ_PCYCLES();

    bilateral( &input1_buf, &gauss_LUT_buf, &range_LUT_buf, &output_buf );

    total_cycles = READ_PCYCLES() - start_time;
    DUMP_PMU();

#ifdef SYNTHETIC
  printf("\noutput:\n");
  for(i = 4; i < height-4; i++) {
    for(j = 4; j < width-4; j++)
      printf("  %d", output[i*stride+j]);
    printf("\n");
  }
#endif
#if defined(__hexagon__)
    printf("AppReported (HVX%db-mode): Image %dx%d - bilateral3x3: %0.4f cycles/pixel\n", VLEN, (int)width, (int)height, (float)total_cycles/width/(height-8));
#endif



    /* -----------------------------------------------------*/
    /*  Write image output to file                          */
    /* -----------------------------------------------------*/
    if((fp = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
        printf("Error: Cannot open %s for output\n", argv[4]);
        return 1;
    }


    for(i = 4; i < height-4; i++)
    {
        if(write(fp, &output[i*stride+4], sizeof(unsigned char)*(width-8))!=(width-8)) // exclude the boundary pixels
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
