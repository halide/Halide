#include "invsqrt.h"
#include <hexagon_standalone.h>
#include "io.h"
#include <stdio.h>
#include <stdlib.h>

void fillRand_u8(unsigned char *buf, int len)
{
    // generate pseudo-random image
    uint8_t m_w = 0x76;
    uint8_t m_z = 0x21;
    int i;

    for(i = 0; i < len; i++)
    {
         m_z = 14 * (m_z & 31) + (m_z >> 4);
         m_w = 7 * (m_w & 31) + (m_w >> 4);
         buf[i] = (m_z << 4) + m_w;
    }
}

void invsqrtC(
    unsigned short *input,
    unsigned short *sqrt_recip_shft,
    unsigned short *sqrt_recip_val,
    unsigned        width
    )
{
    unsigned t1, x, i;
    unsigned short shft, t2, idx, frac, y, slope, t3;
    static unsigned short val_table[24] = {
        4096, 3862, 3664, 3493, 3344, 3213, 3096, 2991,
        2896, 2810, 2731, 2658, 2591, 2528, 2470, 2416,
        2365, 2317, 2272, 2230, 2189, 2151, 2115, 2081
    };
    static unsigned short slope_table[24] = {
        234, 198, 171, 149, 131, 117, 105, 95,
        86, 79, 73, 67, 63, 58, 54, 51,
        48, 45, 42, 41, 38, 36, 34, 33
    };
    int     shift_nbits;

    for (i = 0; i<width; i++)
    {
        x = input[i];
        if (x == 0) x = 1;

        if (x >> 30) shft = 15;
        else if (x >> 28) shft = 14;
        else if (x >> 26) shft = 13;
        else if (x >> 24) shft = 12;
        else if (x >> 22) shft = 11;
        else if (x >> 20) shft = 10;
        else if (x >> 18) shft = 9;
        else if (x >> 16) shft = 8;
        else if (x >> 14) shft = 7;
        else if (x >> 12) shft = 6;
        else if (x >> 10) shft = 5;
        else if (x >> 8) shft = 4;
        else if (x >> 6) shft = 3;
        else if (x >> 4) shft = 2;
        else if (x >> 2) shft = 1;
        else              shft = 0;

        shift_nbits = 13 - 2 * shft;
        t1 = (shift_nbits >= 0) ? (x << shift_nbits) : (x >> -shift_nbits);
        t2 = t1 >> 10;
        idx = t2 - 8;

        frac = t1 & 0x3ff;
        y = val_table[idx];
        slope = slope_table[idx];
        t3 = (slope * frac + 512) >> 10;

        sqrt_recip_val[i] = y - t3;
        sqrt_recip_shft[i] = shft;
    }
}

int main(int argc, char **argv) {
    // Create the Input.
    int width  = atoi(argv[1]);
    int height  = 1; //atoi(argv[2]);
    int error = 0;

    long long start_time, total_cycles;

    unsigned short *input  = (unsigned short *)memalign(128, width*sizeof(unsigned short));
    unsigned short *shftRef = (unsigned short *)memalign(128, width*sizeof(unsigned short));
    unsigned short *valRef = (unsigned short *)memalign(128, width*sizeof(unsigned short));
    unsigned short *shftHalide = (unsigned short *)memalign(128, width*sizeof(unsigned short));
    unsigned short *valHalide = (unsigned short *)memalign(128, width*sizeof(unsigned short));


    if(!input || !shftRef || !valRef || !shftHalide || !valHalide)
    {
        free(input);
        free(shftRef);
        free(valRef);
        free(shftHalide);
        free(valHalide);
        return -1;
    }

    // fill input buffer with pseudo-random values.
    fillRand_u8((unsigned char*)input, width*sizeof(unsigned short));

    // Generate ground-truth output.
    invsqrtC(input, shftRef, valRef, width);

    // Run Halide function

    // create buffer structures
    buffer_t input_buf = {
                        .host = (uint8_t*)input,
                        .stride[0] = 1,
                        .stride[1] = width,
                        .extent[0] = width,
                        .extent[1] = 1,
                        .elem_size = sizeof(unsigned short),
                        };

    buffer_t shft_buf = {
                        .host = (uint8_t*)shftHalide,
                        .stride[0] = 1,
                        .stride[1] = width,
                        .extent[0] = width,
                        .extent[1] = 1,
                        .elem_size = sizeof(unsigned short),
                        };

    buffer_t val_buf = {
                        .host = (uint8_t*)valHalide,
                        .stride[0] = 1,
                        .stride[1] = width,
                        .extent[0] = width,
                        .extent[1] = 1,
                        .elem_size = sizeof(unsigned short),
                        };

    SIM_ACQUIRE_HVX;
#if LOG2VLEN == 7
    SIM_SET_HVX_DOUBLE_MODE;
#endif
    RESET_PMU();
    start_time = READ_PCYCLES();

    invsqrt(&input_buf,&shft_buf,&val_buf);
    total_cycles = READ_PCYCLES() - start_time;
    DUMP_PMU();

    SIM_RELEASE_HVX;
#if DEBUG
    printf ("Done calling the halide func. and released the vector context\n");
#endif

    if (error) {
        printf("Halide returned an error: %d\n", error);
        return -1;
    }

    error = 0;
    // Verify output
    for(int i = 0; i < width; i++)
    {
        if(shftRef[i] != shftHalide[i] || valRef[i] != valHalide[i])
        {
            printf("MISMATCH (%d) ref: val = %x, shft = %x. Halide: val = %x, shft = %x\n",i,valRef[i],shftRef[i],valHalide[i],shftHalide[i]);
            error = 1;
            break;
        }
    }
    FH Outfile;
    /* -----------------------------------------------------*/
    /*  Write image output to file                          */
    /* -----------------------------------------------------*/
    if((Outfile = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
        printf("Error: Cannot open %s for output\n", argv[1]);
        return 1;
    }

    if (write(Outfile, valHalide, sizeof(valHalide[0])* width) != sizeof(valHalide[0])* width)
    {
        printf("Error, Unable to write to output\n");
        return 1;
    }
    if (write(Outfile, shftHalide, sizeof(shftHalide[0])* width) != sizeof(shftHalide[0])* width)
    {
        printf("Error, Unable to write to output\n");
        return 1;
    }

    close(Outfile);

    free(input);
    free(shftRef);
    free(valRef);
    free(shftHalide);
    free(valHalide);

    error ? printf("FAIL!\n") : printf("PASS!\n");
#if defined(__hexagon__)
    printf("AppReported (HVX%db-mode): Image %dx%d - sigma3x3: %0.4f cycles/pixel\n", 1<<LOG2VLEN, (int)width, (int)height, (float)total_cycles/width/height);
#endif

    return 0;
}
