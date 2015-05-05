#include "sobel.h"
#include <hexagon_standalone.h>
#include "io.h"
#include <stdio.h>

int main(int argc, char **argv) {
  // Create the Input.
  uint8_t input[8][258];
  int x, y;
  long long start_time, total_cycles;
#if DEBUG
  printf ("initializing inputs\n");
#endif
  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 258; ++x) {
      if (x > 255 || y > 6)
        input[y][x] = 0;
      else
        if (x % 2)
          input[y][x] =  y + 2;
        else
          input[y][x] =  y + 1;
    }
  }
#if DEBUG
  printf ("finished initializing inputs\n");
#endif
  // And the memory where we want to write our output:
  uint8_t output[6][256];
  printf ("initializing output\n");
  for (y = 0; y < 6; ++y) {
    for (x = 0; x < 256; ++x) {
      output[y][x] = 1;
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
  input1_buf.stride[1] = 258;  output_buf.stride[1] = 256;

  // The extent tells us how large the image is in each dimension.
  input1_buf.extent[0] = 258;
  output_buf.extent[0] = 256;
  input1_buf.extent[1] = 8;
  output_buf.extent[1] = 6;

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

  int error = sobel(&input1_buf, &output_buf);

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

  // Now let's check the filter performed as advertised. It was
  // supposed to add the offset to every input1 pixel.
  uint16_t sobel_x_avg[8][256];
  for (int y = 0; y < 8; ++y) {
    for  (int x = 0; x < 256; x++) {
      uint8_t input1_val = input[y][x];
      uint8_t input2_val = 2* input[y][x+1];
      uint8_t input3_val= input[y][x+2];
      sobel_x_avg[y][x] = input1_val + input2_val + input3_val;
    }
  }
  for (int y= 0; y < 6; ++y) {
    for (int x = 0; x< 256; ++x) {
      unsigned a = sobel_x_avg[y][x];
      unsigned b = sobel_x_avg[y+2][x];
      if (a <= b)
        sobel_x_avg[y][x] = b-a;
      else
        sobel_x_avg[y][x] = a-b;
    }
  }
  uint16_t sobel_y_avg[8][258];
  for (int y = 0; y < 6; ++y) {
    for  (int x = 0; x < 258; x++) {
      uint8_t input1_val = input[y][x];
      uint8_t input2_val = 2* input[y+1][x];
      uint8_t input3_val= input[y+2][x];

      sobel_y_avg[y][x] = input1_val + input2_val + input3_val;
    }
  }
  for (int y= 0; y < 6; ++y) {
    for (int x = 0; x < 256; ++x) {
      unsigned a = sobel_y_avg[y][x];
      unsigned b = sobel_y_avg[y][x+2];
      if (a <= b)
        sobel_y_avg[y][x] = b-a;
      else
        sobel_y_avg[y][x] = a-b;
      }
  }
  uint8_t sobel[6][256];
  for (int y= 0; y < 6; ++y) {
    for (int x = 0; x < 256; ++x) {
      uint16_t sum = sobel_x_avg[y][x] + sobel_y_avg[y][x];
      if (sum < 0)
        sum = 0;
      else if (sum > 255)
        sum = 255;
      sobel[y][x] = (uint8_t) sum;
      if (sobel[y][x] != output[y][x]) {
        printf ("output[%d][%d] was %d instead of %d\n",
                y, x, output[y][x], sobel[y][x]);
        return -1;
      }
      // else
      //   printf("output[%d][%d] = %d\n", y, x, output[y][x]);
    }
  }
#if defined(__hexagon__)
  printf("Pcycles: %0.4lld\n",total_cycles);
#endif
    printf("Success!\n");
    return 0;
}
