#include "gaussian3x3.h"
#include <hexagon_standalone.h>
#include "io.h"
#include <stdio.h>
#include <stdlib.h>


#ifdef SYNTHETIC
int main(int argc, char **argv) {
  // Create the Input.
  uint8_t input[128][128];
  int x, y;
  int i, j;
  long long start_time, total_cycles;
#if DEBUG
  printf ("initializing inputs\n");
#endif
  for (y = 0; y < 128; ++y) {
    for (x = 0; x < 128; ++x) {
        input[y][x] =  y + x;
	// printf ("input[%d][%d] = %u\n", y, x, input[y][x]);
    }
  }
#if DEBUG
  printf ("finished initializing inputs\n");
#endif
  // And the memory where we want to write our output:
  uint8_t output[128][128];
  printf ("initializing output\n");
  for (y = 0; y < 128; ++y) {
    for (x = 0; x < 128; ++x) {
      output[y][x] = 0;
    }
  }
#if DEBUG
  printf ("finished initializing output\n");
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
  input1_buf.stride[1] = 128;  output_buf.stride[1] = 128;

  // The extent tells us how large the image is in each dimension.
  input1_buf.extent[0] = 128;
  output_buf.extent[0] = 128;
  input1_buf.extent[1] = 128;
  output_buf.extent[1] = 128;

  // We'll leave the mins as zero. This is what they typically
  // are. The host pointer points to the memory location of the min
  // coordinate (not the origin!).  See lesson 6 for more detail
  // about the mins.

  // The elem_size field tells us how many bytes each element
  // uses. For the 8-bit image we use in this test it's one.
  input1_buf.elem_size = 1; output_buf.elem_size = 1;

  while (!SIM_ACQUIRE_HVX);
#if DEBUG
  printf ("Acquired vector context\n");
#endif
  RESET_PMU();
  start_time = READ_PCYCLES();

  int error = gaussian3x3(&input1_buf, &output_buf);

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
  
  for (y = 0; y < 128; y++) {
    for (x = 0; x < 128; x++) {
      uint16_t one, two, three, four, five, six, seven, eight, nine;
      one = (uint16_t) input[y][x];
      two = (uint16_t)(x > 126 ? 0 :  input[y][x+1]);
      three = (uint16_t) (x>125 ? 0 : input[y][x+2]);
      uint16_t int1 = (uint16_t) one + 2* two + three;
      four = (uint16_t) (y > 126) ? 0 : input[y+1][x];
      five = (uint16_t) ((x > 126 || y > 126) ? 0 : input[y+1][x+1]);
      six = (uint16_t) ((x > 125 || y > 126) ? 0 : input[y+1][x+2]);
      uint16_t int2 = (uint16_t) four + 2* five + six;
      seven = (uint16_t) (y > 125) ? 0 : input[y+2][x];
      eight = (uint16_t) ((x > 126 || y > 125) ? 0 : input[y+2][x+1]);
      nine = (uint16_t) ((x > 125 || y > 125) ? 0 : input[y+2][x+2]);
      uint16_t int3 = (uint16_t) seven + 2* eight + nine;
      // printf ("int1 = %d\t int2 = %d\t int3 = %d\n", int1, int2, int3);
      uint16_t op = (int1 + 2 *int2 + int3) >> 4;
      if (op < 0) op = 0;
      if (op > 255)
        op = 255;
      if (output[y][x] != op) {
        printf ("output[%d][%d] = %d\n instead of %d\n", y, x, output[y][x], op);
      return 1;
      } // else
        //  printf ("output[%d][%d] = %d\n", y, x, output[y][x]);
      
    }
  }

#if defined(__hexagon__)
    printf("AppReported (HVX64b-mode): Image 128x128 - gaussian3x33x3: %0.4f cycles/pixel\n", (float)total_cycles/128/128);
    printf("Pcycles: %0.4lld\n",total_cycles);
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
  /* -----------------------------------------------------*/
  /*  Allocate memory for input/output                    */
  /* -----------------------------------------------------*/
  unsigned char *input  = (unsigned char *)memalign(64, width*height*sizeof(unsigned char));
  unsigned char *output = (unsigned char *)memalign(64, width*height*sizeof(unsigned char));

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
  // /host + elem_size * ((x - min[0])*stride[0] + (y - min[1])*stride[1])
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

  while (!SIM_ACQUIRE_HVX);
#if DEBUG
  printf ("Acquired vector context\n");
#endif
  RESET_PMU();
  start_time = READ_PCYCLES();

  int error = gaussian3x3(&input1_buf, &output_buf);

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
      if(write(fp, &output[i*width+1], sizeof(unsigned char)*(width-2))!=(width-2)) // exclude the boundary pixels
        {
          printf("Error:  Writing file: %s\n", argv[4]);
          return 1;
        }
    }
  close(fp);

  free(input);
  free(output);

#if defined(__hexagon__)
    printf("AppReported (HVX64b-mode): Image %dx%d - median3x3: %0.4f cycles/pixel\n", (int)width, (int)height, (float)total_cycles/width/height);
#endif
    printf("Success!\n");
    return 0;
}

#endif
