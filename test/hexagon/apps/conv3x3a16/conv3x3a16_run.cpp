#include "conv3x3a16.h"
#include <hexagon_standalone.h>
#include "io.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef SYNTHETIC
int main(int argc, char **argv) {
  // Create the Input.
  uint8_t input[16][6] = {
  38, 50, 46, 46, 45, 44,
  45, 45, 44, 46, 49, 51,
  54, 57, 59, 63, 66, 70,
  74, 76, 81, 83, 83, 84,
  86, 87, 88, 87, 86, 82,
  81, 79, 76, 73, 71, 66,
  62, 59, 56, 53, 51, 48,
  46, 44, 43, 42, 42, 40,
  40, 39, 39, 40, 41, 42,
  44, 46, 47, 51, 54, 56,
  60, 61, 64, 66, 67, 66,
  67, 68, 67, 65, 64, 59,
  59, 58, 56, 53, 50, 47,
  44, 41, 39, 38, 35, 33,
  30, 28, 26, 25, 23, 21,
  21, 20, 19, 18, 17, 16

  };
  int x, y;
  int width =6;
  int height = 16;
  long long start_time, total_cycles;
  // And the memory where we want to write our output:
  uint8_t output[16][6];
  uint8_t expected_output[16][6] = {
  0,  0,  0,  0,  0, 0,
  0, 45, 50, 51, 53, 0,
  0, 60, 62, 64, 66, 0,
  0, 73, 74, 74, 74, 0,
  0, 76, 75, 74, 70, 0,
  0, 66, 64, 62, 57, 0,
  0, 52, 50, 49, 45, 0,
  0, 41, 41, 41, 39, 0,
  0, 40, 42, 43, 43, 0,
  0, 48, 51, 52, 52, 0,
  0, 57, 58, 58, 55, 0,
  0, 57, 56, 55, 51, 0,
  0, 48, 46, 44, 40, 0,
  0, 35, 34, 31, 29, 0,
  0, 24, 24, 21, 20, 0,
  0,  0,  0,  0,  0, 0
  };

#if DEBUG
  printf ("input and expected output statically generated\n");
#endif

  // The host pointers point to the start of the image data:
  buffer_t input1_buf = {0}, output_buf = {0};
  input1_buf.host = (uint8_t *)&input[0];
  output_buf.host = (uint8_t *)&output[0];

  // To access pixel (x, y) in a two-dimensional buffer_t, Halide
  // looks at memory address:
  // host + elem_size * ((x - min[0])*stride[0] + (y - min[1])*stride[1])
  // The stride in a dimension represents the number of elements in
  // memory between adjacent entries in that dimension. We have a
  // grayscale image stored in scanline order, so stride[0] is 1,
  // because pixels that are adjacent in x are next to each other in
  // memory.
  input1_buf.stride[0] = output_buf.stride[0] = 1;

  // // stride[1] is the width of the image, because pixels that are
  // // adjacent in y are separated by a scanline's worth of pixels in
  // // memory.
  input1_buf.stride[1] = width;  output_buf.stride[1] = width;

  // The extent tells us how large the image is in each dimension.
  input1_buf.extent[0] = width;
  output_buf.extent[0] = width;
  input1_buf.extent[1] = height;
  output_buf.extent[1] = height;

  // We'll leave the mins as zero. This is what they typically
  // are. The host pointer points to the memory location of the min
  // coordinate (not the origin!).  See lesson 6 for more detail
  // about the mins.

  // The elem_size field tells us how many bytes each element
  // uses. For the 8-bit image we use in this test it's one.
  input1_buf.elem_size = 1; output_buf.elem_size = 1;


  SIM_ACQUIRE_HVX;
#if LOG2VLEN == 7
  SIM_SET_HVX_DOUBLE_MODE;
#endif
#if DEBUG
  printf ("Acquired vector context\n");
#endif
  RESET_PMU();
  start_time = READ_PCYCLES();

  int error = conv3x3a16(&input1_buf, &output_buf);

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
  bool itfailed =false;
  // Now let's check the filter performed as advertised.
  for (int y= 1; y < height-1; ++y) {
    for (int x = 1; x < width-1; ++x) {
      if (expected_output[y][x] != output[y][x]) {
        itfailed = true;
        printf ("output[%d][%d] was %d instead of %d\n",
                y, x, (int)output[y][x], (int)expected_output[y][x]);
      }
      // else
      //   printf("output[%d][%d] = %d\n", y, x, output[y][x]);
    }
  }
  /* -----------------------------------------------------*/
  /*  Write image output to file                          */
  /* -----------------------------------------------------*/
  FH fp; int i;
  if((fp = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
      printf("Error: Cannot open %s for output\n", argv[4]);
      return 1;
    }

    for(i = 1; i < height-1; i++)
    {  // printf("%d\n ", (output[i][1]));
        if(write(fp, &output[i][1], sizeof(unsigned char)*(width-2))!=(sizeof(unsigned char)*(width-2)))
        {
            printf("Error:  Writing file: %s\n", argv[4]);
            return 1;
        }
    }
  close(fp);


  if (itfailed) {
     printf("Failed\n");
     return -1;
  }
#if defined(__hexagon__)
  printf("Synthetic Passed\n");
  printf("AppReported (HVX%db-mode): Image %dx%d - conv3x3a16: %0.4f cycles/pixel\n", (int)1<<LOG2VLEN, (int)width, (int)height, (float)total_cycles/width/height);
#endif
    printf("Success!\n");
    return 0;
}
#else
int main(int argc, char **argv) {
  // Create the Input.

  int x, y;
  int i, j;
  int width, height;
  long long start_time, total_cycles;
  FH fp;

  /* -----------------------------------------------------*/
  /*  Get input parameters                                */
  /* -----------------------------------------------------*/
#if DEBUG
  printf ("Marshall inputs.\n");
#endif

  if (argc != 5){
    printf("usage: %s <width> <height> <input.bin> <output.bin>\n", argv[0]);
    return 1;
  }

  width  = atoi(argv[1]);
  height = atoi(argv[2]);
  int VLEN = 1 << LOG2VLEN;
//int stride = (width + VLEN-1)&(-VLEN);
  int stride = width;
  /* -----------------------------------------------------*/
  /*  Allocate memory for input/output                    */
  /* -----------------------------------------------------*/
  unsigned char *input  = (unsigned char *)memalign(1 << LOG2VLEN, stride*height*sizeof(unsigned char));
  unsigned char *output = (unsigned char *)memalign(1 << LOG2VLEN, stride*height*sizeof(unsigned char));

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

#if DEBUG
  printf ("finished reading the input.\n");
#endif

  // In AOT-compiled mode, Halide doesn't manage this memory for
  // you. You should use whatever image data type makes sense for
  // your application. Halide just needs pointers to it.

  // Now we make a buffer_t to represent our input1 and output. It's
  // important to zero-initialize them so you don't end up with
  // garbage fields that confuse Halide.
  buffer_t input1_buf = {0}, output_buf = {0};

  // The host pointers point to the start of the image data:
  input1_buf.host = (uint8_t *)&input[0];
  output_buf.host = (uint8_t *)&output[0];

  // To access pixel (x, y) in a two-dimensional buffer_t, Halide
  // looks at memory address:
  // host + elem_size * ((x - min[0])*stride[0] + (y - min[1])*stride[1])
  // The stride in a dimension represents the number of elements in
  // memory between adjacent entries in that dimension. We have a
  // grayscale image stored in scanline order, so stride[0] is 1,
  // because pixels that are adjacent in x are next to each other in
  // memory.
  input1_buf.stride[0] = output_buf.stride[0] = 1;

  // // stride[1] is the width of the image, because pixels that are
  // // adjacent in y are separated by a scanline's worth of pixels in
  // // memory.
  input1_buf.stride[1] = width;  output_buf.stride[1] = width;

  // The extent tells us how large the image is in each dimension.
  input1_buf.extent[0] = width;
  output_buf.extent[0] = width;
  input1_buf.extent[1] = height;
  output_buf.extent[1] = height;

  // We'll leave the mins as zero. This is what they typically
  // are. The host pointer points to the memory location of the min
  // coordinate (not the origin!).  See lesson 6 for more detail
  // about the mins.

  // The elem_size field tells us how many bytes each element
  // uses. For the 8-bit image we use in this test it's one.
  input1_buf.elem_size = 1; output_buf.elem_size = 1;

  SIM_ACQUIRE_HVX;
#if LOG2VLEN == 7
  SIM_SET_HVX_DOUBLE_MODE;
#endif
#if DEBUG
  printf ("Acquired vector context\n");
#endif
  RESET_PMU();
  start_time = READ_PCYCLES();

  int error = conv3x3a16(&input1_buf, &output_buf);

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

  /* -----------------------------------------------------*/
  /*  Write image output to file                          */
  /* -----------------------------------------------------*/
  if((fp = open(argv[4], O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
      printf("Error: Cannot open %s for output\n", argv[4]);
      return 1;
    }

    for(i = 1; i < height-1; i++)
    {
        if(write(fp, &output[i*stride+1], sizeof(unsigned char)*(width-2))!=(sizeof(unsigned char)*(width-2)))
        {
            printf("Error:  Writing file: %s\n", argv[4]);
            return 1;
        }
    }
  close(fp);

  free(input);
  free(output);

#if defined(__hexagon__)
    printf("AppReported (HVX%db-mode): Image %dx%d - conv3x3a16: %0.4f cycles/pixel\n", (int)1<<LOG2VLEN, (int)width, (int)height, (float)total_cycles/width/height);
#endif
    printf("Success!\n");
    return 0;
}
#endif
