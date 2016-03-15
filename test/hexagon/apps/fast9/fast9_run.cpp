/***************************************************************************
* Copyright (c) Date: Mon Nov 24 16:26:02 CST 2008 QUALCOMM INCORPORATED
* All Rights Reserved
* Modified by QUALCOMM INCORPORATED on Mon Nov 24 16:26:03 CST 2008
****************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__hexagon__)
#include "hexagon_standalone.h"
#endif
#include "io.h"
#include "fast9.h"


int main(int argc, char* argv[])
{
    int i;
    int width, height, stride;
    FH fp;

    int maxnumcorners    = 3000;
    unsigned int barrier = 50;
    unsigned int border  = 3;
    int numcorners = 0;

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
#ifdef SYNTHETIC
#ifdef SMALLEST_NO_FEATURES
    width = 15;
#else
    width = 300;
#endif
    height = 20;
#endif
    int VLEN=2<<LOG2VLEN;
    stride = (width + VLEN-1)&(-VLEN);  // make stride a multiple of HVX vector size

    /* -----------------------------------------------------*/
    /*  Allocate memory for input/output                    */
    /* -----------------------------------------------------*/
    unsigned char *input  = (unsigned char *)memalign(VLEN, stride*height*sizeof(unsigned char));

    short *output = (short *)malloc(maxnumcorners*2*sizeof(output[0]));

    unsigned char *corner = (unsigned char*)malloc(width);

    if ( input == NULL || output == NULL || corner == NULL){
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
    /*  Buffer Setup                                        */
    /* -----------------------------------------------------*/
    buffer_t input_buf = { 0 }, output_buf = {0};

    input_buf.host = (uint8_t *)&input[0];
    output_buf.host = (uint8_t *)&corner[0];
    input_buf.stride[0] = output_buf.stride[0] = 1;
    input_buf.stride[1] = output_buf.stride[1] = stride;
    input_buf.extent[0] = output_buf.extent[0] = width;
    input_buf.extent[1] = height;
    output_buf.extent[1] = 1;

    /* -----------------------------------------------------*/
    /*  Call fuction                                        */
    /* -----------------------------------------------------*/

    RESET_PMU();
    start_time = READ_PCYCLES();

    numcorners = 0;

    int boundary = border > 3 ? border : 3;
    int x, y;
    bool fdone = false;

    for (y = boundary; !fdone && (y < height - boundary); y++)
    {
        input_buf.host = (uint8_t *)(input + y*stride);
        fast9(&input_buf, barrier, border, &output_buf);
        for (x = boundary; x < width - boundary; x++)
        {
            if (corner[x])
            {
                output[2*numcorners+0] = x;
                output[2*numcorners+1] = y;
                ++(numcorners);

                if (numcorners >= maxnumcorners)
                {
                    fdone = true;
                    break;
                }
            }
        }
    }

    total_cycles = READ_PCYCLES() - start_time;
    DUMP_PMU();

    printf("%d features have been detected.\n", numcorners);

#if defined(__hexagon__)
    printf("AppReported (HVX%db-mode): Image %dx%d - fast9: %0.4f cycles/pixel\n", VLEN, (int)width, (int)height, (float)total_cycles/width/height);
#endif
    /* -----------------------------------------------------*/
    /*  Write image output to file                          */
    /* -----------------------------------------------------*/
    if((fp = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
        printf("Error: Cannot open %s for output\n", argv[4]);
        return 1;
    }

    if(write(fp, output, sizeof(output[0])*2*numcorners)!=(sizeof(output[0])*2*numcorners))
    {
        printf("Error:  Writing file: %s\n", argv[4]);
        return 1;
    }
    close(fp);


    free(input);
    free(output);
    free(corner);

    return 0;
}
