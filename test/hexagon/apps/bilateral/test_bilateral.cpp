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
#include "subsys.h"
#endif
#include "io.h"
#include "hvx.cfg.h"
#include "bilateral.h"


#define KERNEL_SIZE     9
#define Q               8
#define PRECISION       (1<<Q)


double getGauss(double sigma,double value)
{
    return exp( -( value/(2.0 * sigma * sigma) ) );

}


int main(int argc, char* argv[])
{
    int i;
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

    width  = atoi(argv[1]);
    height = atoi(argv[2]);
    stride = (width + VLEN-1)&(-VLEN);  // make stride a multiple of HVX vector size

    /* -----------------------------------------------------*/
    /*  Allocate memory for input/output                    */
    /* -----------------------------------------------------*/
    unsigned char *input  = (unsigned char *)memalign(VLEN, stride*height*sizeof(unsigned char));
    unsigned char *output = (unsigned char *)memalign(VLEN, stride*height*sizeof(unsigned char));

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
        if(read(fp, &input[i*stride],  sizeof(unsigned char)*width)!=width)
        {
            printf("Error, Unable to read from %s\n", argv[3]);
            close(fp);
            return 1;
        }
    }
    close(fp);

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

    unsigned char *range_LUT = (unsigned char *)memalign(VLEN,256);

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

    /* -----------------------------------------------------*/
    /*  Call fuction                                        */
    /* -----------------------------------------------------*/
    RESET_PMU();
    start_time = READ_PCYCLES();

    bilateral9x9( input, stride, width, height, gauss_LUT, range_LUT, output );

    total_cycles = READ_PCYCLES() - start_time;
    DUMP_PMU();

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
