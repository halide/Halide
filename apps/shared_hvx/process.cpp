#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

#include "hexagon_standalone.h"
#include <hexagon_sim_timer.h>

#include "HalideBuffer.h"

#if vmpabuu
  #include "vmpabuu_hvx128.h"
#elif simple
  #include "simple_hvx128.h"
#elif gaussian3x3
  #include "gaussian3x3_llvm.h"
  #include "gaussian3x3_halide.h"
  #include "gaussian3x3_pitchfork.h"
  #include "gaussian3x3_hvx128.h"
#elif gaussian5x5
  #include "gaussian5x5_llvm.h"
  #include "gaussian5x5_halide.h"
  #include "gaussian5x5_pitchfork.h"
  #include "gaussian5x5_hvx128.h"
#elif gaussian5x5_sdk
  #include "gaussian5x5_sdk_hvx128.h"
#elif gaussian7x7
  #include "gaussian7x7_llvm.h"
  #include "gaussian7x7_halide.h"
  #include "gaussian7x7_pitchfork.h"
  // #include "gaussian7x7_hvx128.h"
#elif gaussian7x7_sdk
  #include "gaussian7x7_sdk_hvx128.h"
#elif conv3x3_a16
  #include "conv3x3_a16_llvm.h"
  #include "conv3x3_a16_halide.h"
  #include "conv3x3_a16_pitchfork.h"
//   #include "conv3x3_a16_hvx128.h"
#elif conv3x3a16_sdk
  #include "conv3x3a16_sdk_hvx128.h"
#elif conv3x3_a32
  #include "conv3x3_a32_llvm.h"
  #include "conv3x3_a32_halide.h"
  #include "conv3x3_a32_pitchfork.h"
  // #include "conv3x3_a32_hvx128.h"
#elif conv3x3a32_sdk
  #include "conv3x3a32_sdk_hvx128.h"
#elif sobel3x3
  #include "sobel3x3_llvm.h"
  #include "sobel3x3_halide.h"
  #include "sobel3x3_pitchfork.h"
  #include "sobel3x3_hvx128.h"
#elif sobel3x3_sdk
  #include "sobel3x3_sdk_hvx128.h"
#elif blur3x3
  #include "blur3x3_llvm.h"
  #include "blur3x3_halide.h"
  #include "blur3x3_pitchfork.h"
  #include "blur3x3_hvx128.h"
#elif dilate3x3
  #include "dilate3x3_hvx128.h"
#elif median3x3
  #include "median3x3_hvx128.h"
#elif add
  #include "add_llvm.h"
  #include "add_halide.h"
  #include "add_pitchfork.h"
  #include "add_hvx128.h"
#elif mul
  // #include "mul_llvm.h"
  #include "mul_halide.h"
  #include "mul_pitchfork.h"
  #include "mul_hvx128.h"
#elif average_pool
  #include "average_pool_llvm.h"
  #include "average_pool_halide.h"
  #include "average_pool_pitchfork.h"
  #include "average_pool_hvx128.h"
#elif max_pool
  #include "max_pool_hvx128.h"
#elif l2norm
  #include "l2norm_llvm.h"
  #include "l2norm_halide.h"
  #include "l2norm_pitchfork.h"
  #include "l2norm_hvx128.h"
#elif matmul
  // #include "matmul_llvm.h"
  #include "matmul_halide.h"
  #include "matmul_pitchfork.h"
  #include "matmul_hvx128.h"
#elif fully_connected
  #include "fully_connected_llvm.h"
  #include "fully_connected_halide.h"
  #include "fully_connected_pitchfork.h"
  #include "fully_connected_hvx128.h"
#elif conv_nn
  #include "conv_nn_hvx128.h"
#elif debug
#include "debug_hvx128.h"
#elif softmax
  #include "softmax_llvm.h"
  #include "softmax_halide.h"
  #include "softmax_pitchfork.h"
  // #include "softmax_hvx128.h"
#elif camera_pipe
  #include "camera_pipe_llvm.h"
  #include "camera_pipe_halide.h"
  #include "camera_pipe_pitchfork.h"
  // #include "camera_pipe_hvx128.h"
#elif depthwise_conv
  #include "depthwise_conv_llvm.h"
  #include "depthwise_conv_halide.h"
  #include "depthwise_conv_pitchfork.h"
  // #include "depthwise_conv_hvx128.h"
#endif

#define LOG2VLEN 7
#define VLEN (1<<LOG2VLEN)

#define O_CREAT_WRONLY_TRUNC (O_CREAT | O_WRONLY | O_TRUNC)

extern "C" {
ssize_t      write(int, const void *, size_t);
}

int write_file(int fp, unsigned char *src, int height, int width, int border_width) {
  int i;
  for(i = 0; i < height; i++) {
    if(write(fp, &src[i*width], sizeof(unsigned char)*(width))!=(width)) {
      return 1;
    }
  }
  return 0;
}

template<typename F>
long long benchmark(F op) {
  long long start_time = q6sim_read_pcycles();

  op();

  long long total_cycles = q6sim_read_pcycles() - start_time;
  return total_cycles;
}

// This is a basic implementation of the Halide runtime for Hexagon.
void halide_print(void *user_context, const char *str) {
    if (str) {
        //log_printf("%s", str);
    }
}

void halide_error(void *user_context, const char *str) {
    if (!str) {
        //log_printf("Unknown error\n");
    } else if (*str == '\0' || str[strlen(str) - 1] != '\n') {
        //log_printf("Error: %s\n", str);
    } else {
        //log_printf("Error: %s", str);
    }
}

int main(int argc, char **argv) {
  int i, in_fp;
  
  constexpr int dims = 2;

  /* -----------------------------------------------------*/
  /*  Get input parameters                                */
  /* -----------------------------------------------------*/
  if (argc != 5) {
    printf("usage: %s <width> <height> <input.bin> <output.bin>\n", argv[0]);
    return 1;
  }

  int width  = atoi(argv[1]);
  int height = atoi(argv[2]);
  int stride = (width + (VLEN) - 1)&(-(VLEN));

  /* -----------------------------------------------------*/
  /*  Allocate memory for input/output                    */
  /* -----------------------------------------------------*/

  unsigned char *input  = (unsigned char *)memalign(1 << LOG2VLEN, width*height*sizeof(unsigned char));
  unsigned char *output = (unsigned char *)memalign(1 << LOG2VLEN, width*height*4*sizeof(unsigned char));

  if ( input == NULL || output == NULL ) {
    printf("Error: Could not allocate Memory for image\n");
    return 1;
  }

  /* -----------------------------------------------------*/
  /*  Read image input from file                          */
  /* -----------------------------------------------------*/
  if((in_fp = open(argv[3], O_RDONLY)) < 0 ) {
    printf("Error: Cannot open %s for input\n", argv[3]);
    return 1;
  }

  for(i = 0; i < height; i++) {
    if(read(in_fp, &input[i*width],  sizeof(unsigned char)*width)!=width) {
      printf("Error, Unable to read from %s\n", argv[3]);
      close(in_fp);
      return 1;
    }
  }
  close(in_fp);

  /* -----------------------------------------------------*/
  /*  Run benchmark on the Simulator                      */
  /* -----------------------------------------------------*/
  long long cycles;

#if add
      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      Halide::Runtime::Buffer<uint8_t> input1_buf(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> input2_buf(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = add_llvm(input1_buf, 0, 100, input2_buf, 0, 100, 0, 5, 225, output_buf);
          if (error != 0) {
              printf("add_llvm pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

    //   for (int x = 0; x < 10; x++)
    //       for (int y = 0; y < 10; y++)
    //           printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input1_buf(x, y), output_buf(x, y));

      printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - add(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = add_halide(input1_buf, 0, 100, input2_buf, 0, 100, 0, 5, 225, output_buf);
          if (error != 0) {
              printf("add_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - add(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = add_pitchfork(input1_buf, 0, 100, input2_buf, 0, 100, 0, 5, 225, output_buf);
          if (error != 0) {
              printf("add_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - add(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = add_hvx128(input1_buf, 0, 100, input2_buf, 0, 100, 0, 5, 225, output_buf);
          if (error != 0) {
              printf("add_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Rake: AppReported (HVX128B-mode): Image %dx%d - add(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

#endif

#if mul
      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      Halide::Runtime::Buffer<uint8_t> input1_buf(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> input2_buf(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

      // Run in 128 byte mode
      // SIM_ACQUIRE_HVX;
      // SIM_SET_HVX_DOUBLE_MODE;
      // cycles = benchmark([&]() {
      //     int error = mul_llvm(input1_buf, 2, input2_buf, 5, 5, 10000, 1, 5, 225, output_buf);
      //     if (error != 0) {
      //         printf("mul_llvm pipeline failed: %d\n", error);
      //     }
      //     });
      // SIM_RELEASE_HVX;

      // // for (int x = 0; x < 10; x++)
      // //     for (int y = 0; y < 10; y++)
      // //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input1_buf(x, y), output_buf(x, y));

      // printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));



      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = mul_halide(input1_buf, 2, input2_buf, 5, 5, 10000, 1, 5, 225, output_buf);
          if (error != 0) {
              printf("mul_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));



      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = mul_pitchfork(input1_buf, 2, input2_buf, 5, 5, 10000, 1, 5, 225, output_buf);
          if (error != 0) {
              printf("mul_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = mul_hvx128(input1_buf, 2, input2_buf, 5, 5, 10000, 1, 5, 225, output_buf);
          if (error != 0) {
              printf("mul_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Rake: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


#endif

#if average_pool
      halide_dimension_t c_dim{ 0, 1024, 1 };
      halide_dimension_t x_dim{ 0, width/32, 128 };
      halide_dimension_t y_dim{ 0, height/32, 128 * (width / 32) };
      halide_dimension_t b_dim{ 0, 1, 128 * (width / 32) * (height / 32) };
      halide_dimension_t shape[4] = { c_dim, x_dim, y_dim, b_dim };

      Halide::Runtime::Buffer<uint8_t> input_buf(input, 4, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, 4, shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = average_pool_llvm(input_buf, 2, 2, 8, 8, 5, 225, output_buf);
          if (error != 0) {
              printf("average_pool_llvm pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

    //   for (int x = 0; x < 10; x++)
    //       for (int y = 0; y < 10; y++)
    //           printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

      printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - average_pool(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = average_pool_halide(input_buf, 2, 2, 8, 8, 5, 225, output_buf);
          if (error != 0) {
              printf("average_pool_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - average_pool(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = average_pool_pitchfork(input_buf, 2, 2, 8, 8, 5, 225, output_buf);
          if (error != 0) {
              printf("average_pool_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - average_pool(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = average_pool_hvx128(input_buf, 2, 2, 8, 8, 5, 225, output_buf);
          if (error != 0) {
              printf("average_pool_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Rake: AppReported (HVX128B-mode): Image %dx%d - average_pool(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


#endif

#if max_pool
      halide_dimension_t c_dim{ 0, 1024, 1 };
      halide_dimension_t x_dim{ 0, width / 32, 128 };
      halide_dimension_t y_dim{ 0, height / 32, 128 * (width / 32) };
      halide_dimension_t b_dim{ 0, 1, 128 * (width / 32) * (height / 32) };
      halide_dimension_t shape[4] = { c_dim, x_dim, y_dim, b_dim };

      Halide::Runtime::Buffer<uint8_t> input_buf(input, 4, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, 4, shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = max_pool_hvx128(input_buf, 2, 2, 8, 8, 5, 225, output_buf);
          if (error != 0) {
              printf("max_pool_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      for (int x = 0; x < 10; x++)
          for (int y = 0; y < 10; y++)
              printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

      printf("AppReported (HVX128B-mode): Image %dx%d - max_pool(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

#if l2norm
      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = l2norm_llvm(input_buf, 0, output_buf);
          if (error != 0) {
              printf("l2norm_llvm pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      // for (int x = 0; x < 10; x++)
      //     for (int y = 0; y < 10; y++)
      //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

      printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - l2norm(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = l2norm_halide(input_buf, 0, output_buf);
          if (error != 0) {
              printf("l2norm_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - l2norm(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = l2norm_pitchfork(input_buf, 0, output_buf);
          if (error != 0) {
              printf("l2norm_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - l2norm(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = l2norm_hvx128(input_buf, 0, output_buf);
          if (error != 0) {
              printf("l2norm_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Rake: AppReported (HVX128B-mode): Image %dx%d - l2norm(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


#endif

#if matmul
      int* bias = (int*)memalign(1 << LOG2VLEN, width * height * sizeof(int));
      for (int i=0; i < (width*height); i++)
          bias[i] = 10000;

      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      halide_dimension_t i_dim{ 0, width*height, 1 };
      halide_dimension_t b_shape[2] = { i_dim };

      Halide::Runtime::Buffer<uint8_t> mat_a_(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> mat_b_(input, dims, shape);
      Halide::Runtime::Buffer<int32_t> bias_((long*)bias, 1, b_shape);
      Halide::Runtime::Buffer<uint8_t> output_(output, dims, shape);

      // Run in 128 byte mode
      // SIM_ACQUIRE_HVX;
      // SIM_SET_HVX_DOUBLE_MODE;
      // cycles = benchmark([&]() {
      //     int error = matmul_llvm(mat_a_, mat_b_, bias_, 0, 0, 65536, 1, 0, 5, 250, output_);
      //     if (error != 0) {
      //         printf("matmul_llvm pipeline failed: %d\n", error);
      //     }
      //     });
      // SIM_RELEASE_HVX;

      // // for (int x = 0; x < 10; x++)
      // //     for (int y = 0; y < 10; y++)
      // //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, mat_a_(x, y), output_(x, y));

      // printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - matmul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = matmul_halide(mat_a_, mat_b_, bias_, 0, 0, 65536, 1, 0, 5, 250, output_);
          if (error != 0) {
              printf("matmul_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - matmul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = matmul_pitchfork(mat_a_, mat_b_, bias_, 0, 0, 65536, 1, 0, 5, 250, output_);
          if (error != 0) {
              printf("matmul_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - matmul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = matmul_hvx128(mat_a_, mat_b_, bias_, 0, 0, 65536, 1, 0, 5, 250, output_);
          if (error != 0) {
              printf("matmul_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Rake: AppReported (HVX128B-mode): Image %dx%d - matmul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));



#endif

#if fully_connected
      int* bias = (int*)memalign(1 << LOG2VLEN, width * height * sizeof(int));
      for (int i = 0; i < (width * height); i++)
          bias[i] = 10000;

      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      halide_dimension_t i_dim{ 0, width * height, 1 };
      halide_dimension_t b_shape[2] = { i_dim };

      Halide::Runtime::Buffer<uint8_t> mat_a_(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> mat_b_(input, dims, shape);
      Halide::Runtime::Buffer<int32_t> bias_((long*)bias, 1, b_shape);
      Halide::Runtime::Buffer<uint8_t> output_(output, dims, shape);
      
      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = fully_connected_llvm(mat_a_, 3, mat_b_, 5, bias_, 7, 32767, 1, 5, 250, output_);
          if (error != 0) {
              printf("fully_connected_llvm pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      // for (int x = 0; x < 10; x++)
      //     for (int y = 0; y < 10; y++)
      //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, mat_a_(x, y), output_(x, y));

      printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - fully_connected_hvx128(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = fully_connected_halide(mat_a_, 3, mat_b_, 5, bias_, 7, 32767, 1, 5, 250, output_);
          if (error != 0) {
              printf("fully_connected_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - fully_connected_hvx128(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = fully_connected_pitchfork(mat_a_, 3, mat_b_, 5, bias_, 7, 32767, 1, 5, 250, output_);
          if (error != 0) {
              printf("fully_connected_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - fully_connected_hvx128(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = fully_connected_hvx128(mat_a_, 3, mat_b_, 5, bias_, 7, 32767, 1, 5, 250, output_);
          if (error != 0) {
              printf("fully_connected_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Rake: AppReported (HVX128B-mode): Image %dx%d - fully_connected_hvx128(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


#endif

#if conv_nn
      int* bias = (int*)memalign(1 << LOG2VLEN, width * height * sizeof(int));
      for (int i = 0; i < (width * height); i++)
          bias[i] = 10000;

      width = 128;
      height = 128;

      halide_dimension_t c_dim{ 0, 1024, 1 };
      halide_dimension_t x_dim{ 0, width / 32, 128 };
      halide_dimension_t y_dim{ 0, height / 32, 128 * (width / 32) };
      halide_dimension_t b_dim{ 0, 1, 128 * (width / 32) * (height / 32) };
      halide_dimension_t shape[4] = { c_dim, x_dim, y_dim, b_dim };

      halide_dimension_t i_dim{ 0, width * height, 1 };
      halide_dimension_t b_shape[2] = { i_dim };

      // A 6D array of filter coefficients indexed by ci % n, co % k, ci / n, co / k, x, y,

      halide_dimension_t cim_dim{ 0, 4, 1 };
      halide_dimension_t com_dim{ 0, 4, 4 };
      halide_dimension_t cid_dim{ 0, 4, 4 * 4 };
      halide_dimension_t cod_dim{ 0, 4, 4 * 4 * 4 };
      halide_dimension_t fx_dim{ 0, 1, 4 * 4 * 4 * 4 };
      halide_dimension_t fy_dim{ 0, 1, 4 * 4 * 4 * 4 };
      halide_dimension_t f_shape[6] = { cim_dim, com_dim, cid_dim, cod_dim, x_dim, b_dim };

      Halide::Runtime::Buffer<uint8_t> input_buf(input, 4, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, 4, shape);
      Halide::Runtime::Buffer<uint8_t> filter_buf(input, 6, f_shape);
      Halide::Runtime::Buffer<int32_t> bias_((long*)bias, 1, b_shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = conv_nn_hvx128(input_buf, 3, filter_buf, 5, bias_, 1, 1, 1, 1, 32767, 1, 3, 5, 250, output_buf);
          if (error != 0) {
              printf("conv_nn_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      for (int x = 0; x < 10; x++)
          for (int y = 0; y < 10; y++)
              printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

      printf("AppReported (HVX128B-mode): Image %dx%d - conv_nn_hvx128(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

#if debug
      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      Halide::Runtime::Buffer<uint8_t> input1_buf(input, dims, shape);
      Halide::Runtime::Buffer<int16_t> output_buf((int16_t*)output, dims, shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = debug_hvx128(input1_buf, 20, output_buf);
          if (error != 0) {
              printf("debug_hvx128 pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      for (int x = 0; x < 10; x++)
          for (int y = 0; y < 10; y++)
              printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input1_buf(x, y), output_buf(x, y));

      printf("AppReported (HVX128B-mode): Image %dx%d - debug(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

  #if vmpabuu
    halide_dimension_t x_dim{0, width, 1};
    halide_dimension_t y_dim{0, height, width};
    halide_dimension_t shape[2] = {x_dim, y_dim};

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<int16_t> output_buf((short*)output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = vmpabuu_hvx128(input_buf, output_buf);
        if (error != 0) {
          printf("vmpabuu_hvx128 pipeline failed: %d\n", error);
        }
      });
    SIM_RELEASE_HVX;

    for (int x=0; x<10; x++)
      for (int y=0; y<10; y++)
        printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - simple(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles/(width*height));
  #endif
  
  #if simple
    halide_dimension_t x_dim{0, width/2, 1};
    halide_dimension_t y_dim{0, height/2, width/2};
    halide_dimension_t shape[2] = {x_dim, y_dim};

    Halide::Runtime::Buffer<int16_t> input_buf((short*)input, dims, shape);
    Halide::Runtime::Buffer<int16_t> output_buf((short*)output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = simple_hvx128(input_buf, output_buf);
        if (error != 0) {
          printf("simple_hvx128 pipeline failed: %d\n", error);
        }
      });
    SIM_RELEASE_HVX;

    for (int x=0; x<10; x++)
      for (int y=0; y<10; y++)
        printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - simple(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles/(width*height));
  #endif

#if blur3x3
    halide_dimension_t x_dim{ 0, width/2, 1 };
    halide_dimension_t y_dim{ 0, height, width/2 };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<int16_t> input_buf((short*)input, dims, shape);
    Halide::Runtime::Buffer<int16_t> output_buf((short*)output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = blur3x3_llvm(input_buf, output_buf);
        if (error != 0) {
            printf("blur3x3_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - blur3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = blur3x3_halide(input_buf, output_buf);
        if (error != 0) {
            printf("blur3x3_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - blur3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = blur3x3_pitchfork(input_buf, output_buf);
        if (error != 0) {
            printf("blur3x3_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - blur3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = blur3x3_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("blur3x3_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Rake: AppReported (HVX128B-mode): Image %dx%d - blur3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


  #endif

  #if dilate3x3
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = dilate3x3_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("dilate3x3_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - dilate3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
  #endif

  #if conv3x3_a16
    signed char mask[9] =
    {
        1, 2, 1,
        2, 4, 2,
        1, 2, 1
    };

    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    halide_dimension_t mask_shape[2];
    mask_shape[0].min = 0; mask_shape[0].extent = 3; mask_shape[0].stride = 1;
    mask_shape[1].min = 0; mask_shape[1].extent = 3; mask_shape[1].stride = 3;

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);
    Halide::Runtime::Buffer<int8_t> mask_buf(mask, dims, mask_shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3_a16_llvm(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3a16_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - conv3x3a16(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3_a16_halide(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3a16_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - conv3x3a16(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3_a16_pitchfork(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3a16_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - conv3x3a16(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

  #endif

#if conv3x3a16_sdk
    signed char mask[9] =
    {
        1, 2, 1,
        2, 4, 2,
        1, 2, 1
    };

    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    halide_dimension_t mask_shape[2];
    mask_shape[0].min = 0; mask_shape[0].extent = 3; mask_shape[0].stride = 1;
    mask_shape[1].min = 0; mask_shape[1].extent = 3; mask_shape[1].stride = 3;

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);
    Halide::Runtime::Buffer<int8_t> mask_buf(mask, dims, mask_shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3a16_sdk_hvx128(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3a16_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - conv3x3a16(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

  #if conv3x3_a32
    signed char mask[9] =
    {
        1, 2, 1,
        2, 4, 2,
        1, 2, 1
    };

    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    halide_dimension_t mask_shape[2];
    mask_shape[0].min = 0; mask_shape[0].extent = 3; mask_shape[0].stride = 1;
    mask_shape[1].min = 0; mask_shape[1].extent = 3; mask_shape[1].stride = 3;

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);
    Halide::Runtime::Buffer<int8_t> mask_buf(mask, dims, mask_shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3_a32_llvm(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3_a32_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - conv3x3a32(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3_a32_halide(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3_a32_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - conv3x3a32(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3_a32_pitchfork(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3_a32_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - conv3x3a32(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));




  #endif

#if conv3x3a32_sdk
    signed char mask[9] =
    {
        1, 2, 1,
        2, 4, 2,
        1, 2, 1
    };

    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    halide_dimension_t mask_shape[2];
    mask_shape[0].min = 0; mask_shape[0].extent = 3; mask_shape[0].stride = 1;
    mask_shape[1].min = 0; mask_shape[1].extent = 3; mask_shape[1].stride = 3;

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);
    Halide::Runtime::Buffer<int8_t> mask_buf(mask, dims, mask_shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = conv3x3a32_sdk_hvx128(input_buf, mask_buf, output_buf);
        if (error != 0) {
            printf("conv3x3a32_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - conv3x3a32(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

  #if sobel3x3
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;

    cycles = benchmark([&]() {
        int error = sobel3x3_llvm(input_buf, output_buf);
        if (error != 0) {
            printf("sobel3x3_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - sobel3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;

    cycles = benchmark([&]() {
        int error = sobel3x3_halide(input_buf, output_buf);
        if (error != 0) {
            printf("sobel3x3_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - sobel3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;

    cycles = benchmark([&]() {
        int error = sobel3x3_pitchfork(input_buf, output_buf);
        if (error != 0) {
            printf("sobel3x3_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - sobel3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;

    cycles = benchmark([&]() {
        int error = sobel3x3_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("sobel3x3_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("Rake: AppReported (HVX128B-mode): Image %dx%d - sobel3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


  #endif

#if sobel3x3_sdk
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = sobel3x3_sdk_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("sobel3x3_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - sobel3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

  #if gaussian3x3
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 1, height-1, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian3x3_llvm(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian3x3_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d, true-val: %d\n", x, y, input_buf(x, y), output_buf(x, y),
                
    //             ((static_cast<int16_t>(input_buf(x-1, y-1)) * 1 + static_cast<int16_t>(input_buf(x, y-1)) * 2 + static_cast<int16_t>(input_buf(x+1, y-1)) * 1 +
    //                 static_cast<int16_t>(input_buf(x-1, y)) * 2 + static_cast<int16_t>(input_buf(x, y)) * 4 + static_cast<int16_t>(input_buf(x+1, y)) * 2 +
    //                 static_cast<int16_t>(input_buf(x-1, y+1)) * 1 + static_cast<int16_t>(input_buf(x, y+1)) * 2 + static_cast<int16_t>(input_buf(x+1, y+1) * 1)) + 8) >> 4
                    
    //             );

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - gaussian3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian3x3_halide(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian3x3_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - gaussian3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian3x3_pitchfork(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian3x3_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - gaussian3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian3x3_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian3x3_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Rake: AppReported (HVX128B-mode): Image %dx%d - gaussian3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


  #endif

  #if gaussian5x5
    halide_dimension_t x_dim{0, width, 1};
    halide_dimension_t y_dim{0, height, width};
    halide_dimension_t shape[2] = {x_dim, y_dim};

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian5x5_llvm(input_buf, output_buf);
        if (error != 0) {
          printf("gaussian5x5_llvm pipeline failed: %d\n", error);
        }
      });
    SIM_RELEASE_HVX;

    // for (int x=0; x<10; x++)
    //   for (int y=0; y<10; y++)
    //     printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - gaussian5x5(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles/(width*height));

    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian5x5_halide(input_buf, output_buf);
        if (error != 0) {
          printf("gaussian5x5_halide pipeline failed: %d\n", error);
        }
      });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - gaussian5x5(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles/(width*height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian5x5_pitchfork(input_buf, output_buf);
        if (error != 0) {
          printf("gaussian5x5_pitchfork pipeline failed: %d\n", error);
        }
      });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - gaussian5x5(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles/(width*height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian5x5_hvx128(input_buf, output_buf);
        if (error != 0) {
          printf("gaussian5x5_hvx128 pipeline failed: %d\n", error);
        }
      });
    SIM_RELEASE_HVX;

    printf("Rake: AppReported (HVX128B-mode): Image %dx%d - gaussian5x5(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles/(width*height));

#endif

#if gaussian5x5_sdk
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian5x5_sdk_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian5x5_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - gaussian5x5(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

#if gaussian7x7
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian7x7_llvm(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian7x7_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    // for (int x = 0; x < 10; x++)
    //     for (int y = 0; y < 10; y++)
    //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - gaussian7x7(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian7x7_halide(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian7x7_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - gaussian7x7(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian7x7_pitchfork(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian7x7_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - gaussian7x7(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    // SIM_ACQUIRE_HVX;
    // SIM_SET_HVX_DOUBLE_MODE;
    // cycles = benchmark([&]() {
    //     int error = gaussian7x7_hvx128(input_buf, output_buf);
    //     if (error != 0) {
    //         printf("gaussian7x7_hvx128 pipeline failed: %d\n", error);
    //     }
    //     });
    // SIM_RELEASE_HVX;

    // printf("Rake: AppReported (HVX128B-mode): Image %dx%d - gaussian7x7(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));






#endif

#if gaussian7x7_sdk
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = gaussian7x7_sdk_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("gaussian7x7_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - gaussian7x7(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));
#endif

#if median3x3
    halide_dimension_t x_dim{ 0, width, 1 };
    halide_dimension_t y_dim{ 0, height, width };
    halide_dimension_t shape[2] = { x_dim, y_dim };

    Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
    Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = median3x3_hvx128(input_buf, output_buf);
        if (error != 0) {
            printf("median3x3_hvx128 pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    for (int x = 0; x < 10; x++)
        for (int y = 0; y < 10; y++)
            printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input_buf(x, y), output_buf(x, y));

    printf("AppReported (HVX128B-mode): Image %dx%d - median3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / width / height);
#endif


#if softmax

      halide_dimension_t x_dim{ 0, width, 1 };
      halide_dimension_t y_dim{ 0, height, width };
      halide_dimension_t shape[2] = { x_dim, y_dim };

      Halide::Runtime::Buffer<uint8_t> input_buf(input, dims, shape);
      Halide::Runtime::Buffer<uint8_t> output_buf(output, dims, shape);

      // Run in 128 byte mode
      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = softmax_llvm(input_buf, 16, 4, 5, 10000, 1, output_buf);
          if (error != 0) {
              printf("softmax_llvm pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      // for (int x = 0; x < 10; x++)
      //     for (int y = 0; y < 10; y++)
      //         printf("(x: %d, y: %d) ==> input-val: %d   output-val: %d\n", x, y, input1_buf(x, y), output_buf(x, y));

      printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = softmax_halide(input_buf, 16, 4, 5, 10000, 1, output_buf);
          if (error != 0) {
              printf("softmax_halide pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Halide: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


      SIM_ACQUIRE_HVX;
      SIM_SET_HVX_DOUBLE_MODE;
      cycles = benchmark([&]() {
          int error = softmax_pitchfork(input_buf, 16, 4, 5, 10000, 1, output_buf);
          if (error != 0) {
              printf("softmax_pitchfork pipeline failed: %d\n", error);
          }
          });
      SIM_RELEASE_HVX;

      printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));



      // SIM_ACQUIRE_HVX;
      // SIM_SET_HVX_DOUBLE_MODE;
      // cycles = benchmark([&]() {
      //     int error = softmax_hvx128(input_buf, 16, 4, 5, 10000, 1, output_buf);
      //     if (error != 0) {
      //         printf("softmax_hvx128 pipeline failed: %d\n", error);
      //     }
      //     });
      // SIM_RELEASE_HVX;

      // printf("Rake: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


#endif




#if depthwise_conv

    const int N = 4;
    const int CI = 32;
    const int CO = 16;
    const int W = 112;
    const int H = 112;

    Halide::Runtime::Buffer<uint8_t> input_buf(CI, W, H, N);
    Halide::Runtime::Buffer<uint8_t> filter(CO, W, H);
    Halide::Runtime::Buffer<int32_t> bias(CO);

    for (int c = 0; c < input_buf.dim(3).extent(); c++) {
        for (int z = 0; z < input_buf.channels(); z++) {
            for (int y = 0; y < input_buf.height(); y++) {
                for (int x = 0; x < input_buf.width(); x++) {
                    input_buf(x, y, z, c) = rand();
                }
            }
        }
    }

    for (int c = 0; c < filter.width(); c++) {
        for (int y = 0; y < filter.height(); y++) {
            for (int z = 0; z < filter.channels(); z++) {
                filter(c, y, z) = rand();
            }
        }
    }

    for (int x = 0; x < bias.width(); x++) {
        bias(x) = rand();
    }


    const uint8_t input_zero = 3;
    const uint8_t filter_zero = 5;
    const int depth_multiplier = CI / CO;
    const int stride_x = 1;
    const int stride_y = 1;
    const int dilation_x = 0;
    const int dilation_y = 0;
    const int32_t output_multiplier = 32767;
    const uint32_t output_shift = 1;
    const uint8_t output_zero = 3;
    const uint8_t output_min = 5;
    const uint8_t output_max = 250;


    Halide::Runtime::Buffer<uint8_t> output_buf(CO, W, H, N);

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = depthwise_conv_llvm(input_buf, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_buf);
        if (error != 0) {
            printf("depthwise_conv_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));



    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = depthwise_conv_halide(input_buf, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_buf);
        if (error != 0) {
            printf("depthwise_conv_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = depthwise_conv_pitchfork(input_buf, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_buf);
        if (error != 0) {
            printf("depthwise_conv_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));



    // SIM_ACQUIRE_HVX;
    // SIM_SET_HVX_DOUBLE_MODE;
    // cycles = benchmark([&]() {
    //     int error = depthwise_conv_hvx128(input, input_zero, filter, filter_zero, bias, depth_multiplier, stride_x, stride_y, dilation_x, dilation_y, output_multiplier, output_shift, output_zero, output_min, output_max, output_buf);
    //     if (error != 0) {
    //         printf("depthwise_conv_hvx128 pipeline failed: %d\n", error);
    //     }
    //     });
    // SIM_RELEASE_HVX;

    // printf("Rake: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

#endif


#if camera_pipe

    Halide::Runtime::Buffer<uint16_t> input_buf(width, height);
    Halide::Runtime::Buffer<uint8_t> output_buf(((input_buf.width() - 32) / 32) * 32, ((input_buf.height() - 24) / 32) * 32, 3);

    // These color matrices are for the sensor in the Nokia N900 and are
    // taken from the FCam source.
    float _matrix_3200[][4] = {{1.6697f, -0.2693f, -0.4004f, -42.4346f},
                               {-0.3576f, 1.0615f, 1.5949f, -37.1158f},
                               {-0.2175f, -1.8751f, 6.9640f, -26.6970f}};

    float _matrix_7000[][4] = {{2.2997f, -0.4478f, 0.1706f, -39.0923f},
                               {-0.3826f, 1.5906f, -0.2080f, -25.4311f},
                               {-0.0888f, -0.7344f, 2.2832f, -20.0826f}};
    Halide::Runtime::Buffer<float, 2> matrix_3200(4, 3), matrix_7000(4, 3);
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 4; j++) {
            matrix_3200(j, i) = _matrix_3200[i][j];
            matrix_7000(j, i) = _matrix_7000[i][j];
        }
    }

    float color_temp = 3700;
    float gamma = 2.0f;
    float contrast = 50;
    float sharpen = 1.0;
    int timing_iterations = 100;
    int blackLevel = 25;
    int whiteLevel = 1023;

    // Run in 128 byte mode
    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = camera_pipe_llvm(input_buf, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_buf);
        if (error != 0) {
            printf("camera_pipe_llvm pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("LLVM: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = camera_pipe_halide(input_buf, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_buf);
        if (error != 0) {
            printf("camera_pipe_halide pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Halide: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    SIM_ACQUIRE_HVX;
    SIM_SET_HVX_DOUBLE_MODE;
    cycles = benchmark([&]() {
        int error = camera_pipe_pitchfork(input_buf, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_buf);
        if (error != 0) {
            printf("camera_pipe_pitchfork pipeline failed: %d\n", error);
        }
        });
    SIM_RELEASE_HVX;

    printf("Pitchfork: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));


    // SIM_ACQUIRE_HVX;
    // SIM_SET_HVX_DOUBLE_MODE;
    // cycles = benchmark([&]() {
    //     int error = camera_pipe_hvx128(input_buf, matrix_3200, matrix_7000, color_temp, gamma, contrast, sharpen, blackLevel, whiteLevel, output_buf);
    //     if (error != 0) {
    //         printf("camera_pipe_hvx128 pipeline failed: %d\n", error);
    //     }
    //     });
    // SIM_RELEASE_HVX;

    // printf("Rake: AppReported (HVX128B-mode): Image %dx%d - mul(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles, (float)cycles / (width * height));

#endif

  /* -----------------------------------------------------*/
  /*  Write output image to file                          */
  /* -----------------------------------------------------*/
  char *filename = (char *) malloc(100 * sizeof(char));
  strcpy(filename, argv[4]);
  int out_fp;

  if((out_fp = open(filename, O_CREAT_WRONLY_TRUNC, 0777)) < 0)
    {
      printf("Error: Cannot open %s for output\n", filename);
      return 1;
    }
  if(write_file(out_fp, output, height, width, 2) != 0) {
    printf("Error: Cannot write to file %s\n", filename);
  }

  close(out_fp);

  free(input);
  free(output);
  free(filename);

  printf("Success!\n");

  return 0;
}

/*int main(int argc, char **argv) {

#if gaussian7x7
  // Run in 128 byte mode
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
  cycles_hvx128 = benchmark([&]() {
      int error = gaussian7x7_hvx128(input1_buf, output_buf);
      if (error != 0) {
        printf("gaussian7x7_hvx128 pipeline failed: %d\n", error);
      }
    });
  SIM_RELEASE_HVX;

  printf("AppReported (HVX128B-mode): Image %dx%d - gaussian7x7(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles_hvx128, (float)cycles_hvx128/width/height);
#endif

#if conv3x3a16
  // Run in 128 byte mode
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
  cycles_hvx128 = benchmark([&]() {
      int error = conv3x3a16_hvx128(input1_buf, mask_buf, output_buf);
      if (error != 0) {
        printf("conv3x3a16_hvx128 pipeline failed: %d\n", error);
      }
    });
  SIM_RELEASE_HVX;

  printf("AppReported (HVX128B-mode): Image %dx%d - conv3x3a16(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles_hvx128, (float)cycles_hvx128/width/height);
#endif

#if conv3x3a32
  // Run in 128 byte mode
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
  cycles_hvx128 = benchmark([&]() {
      int error = conv3x3a32_hvx128(input1_buf, mask_buf, output_buf);
      if (error != 0) {
        printf("conv3x3a32_hvx128 pipeline failed: %d\n", error);
      }
    });
  SIM_RELEASE_HVX;

  printf("AppReported (HVX128B-mode): Image %dx%d - conv3x3a32(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles_hvx128, (float)cycles_hvx128/width/height);
#endif

#if sobel3x3
  // Run in 128 byte mode
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
  cycles_hvx128 = benchmark([&]() {
      int error = sobel3x3_hvx128(input1_buf, output_buf);
      if (error != 0) {
        printf("sobel3x3_hvx128 pipeline failed: %d\n", error);
      }
    });
  SIM_RELEASE_HVX;

  printf("AppReported (HVX128B-mode): Image %dx%d - sobel3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles_hvx128, (float)cycles_hvx128/width/height);
#endif

#if blur3x3
  // Run in 128 byte mode
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
  cycles_hvx128 = benchmark([&]() {
      int error = blur3x3_hvx128(input1_buf, output_buf);
      if (error != 0) {
        printf("blur3x3_hvx128 pipeline failed: %d\n", error);
      }
    });
  SIM_RELEASE_HVX;

  printf("AppReported (HVX128B-mode): Image %dx%d - blur3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles_hvx128, (float)cycles_hvx128/width/height);
#endif

#if dilate3x3
  // Run in 128 byte mode
  SIM_ACQUIRE_HVX;
  SIM_SET_HVX_DOUBLE_MODE;
  cycles_hvx128 = benchmark([&]() {
      int error = dilate3x3_hvx128(input1_buf, output_buf);
      if (error != 0) {
        printf("dilate3x3_hvx128 pipeline failed: %d\n", error);
      }
    });
  SIM_RELEASE_HVX;

  printf("AppReported (HVX128B-mode): Image %dx%d - dilate3x3(128B): %lld cycles (%0.4f cycles/pixel)\n", (int)width, (int)height, cycles_hvx128, (float)cycles_hvx128/width/height);
#endif
  
}

//unsigned long long q6sim_read_pcycles(void);

#define FH int
#define O_CREAT_WRONLY_TRUNC (O_CREAT | O_WRONLY | O_TRUNC)
#define IS_INVALID_FILE_HANDLE(_a) (_a < 0)

#define RESET_PMU()     __asm__ __volatile__ (" r0 = #0x48 ; trap0(#0); \n" : : : "r0","r1","r2","r3","r4","r5","r6","r7","memory")
#define DUMP_PMU()      __asm__ __volatile__ (" r0 = #0x4a ; trap0(#0); \n" : : : "r0","r1","r2","r3","r4","r5","r6","r7","memory")
//#define READ_PCYCLES    q6sim_read_pcycles



template<typename F>
long long benchmark(F op) {
  RESET_PMU();
  //long long start_time = READ_PCYCLES();

  op();

  //long long total_cycles = READ_PCYCLES() - start_time;
  DUMP_PMU();
  return 0;//total_cycles;
}

int write_file(FH fp, unsigned char *src, int height, int width, int border_width) {
  int i;
#ifdef BORDERS
  for(i = 0; i < height; i++) {
#else
    for(i = border_width; i < height-border_width; i++) {
#endif

#ifdef BORDERS
      if(write(fp, &src[i*width], sizeof(unsigned char)*(width))!=(width)) {
#else
        if(write(fp, &src[(i*width)+border_width], sizeof(unsigned char)*(width-(border_width*2)))!=(width-(border_width*2))) {
#endif
      return 1;
    }
  }
  return 0;
}

#if gaussian3x3
  #include "gaussian3x3_hvx128.h"
#elif gaussian5x5
  #include "gaussian5x5_hvx128.h"
#elif gaussian7x7
  #include "gaussian7x7_hvx128.h"
#elif conv3x3a16
  #include "conv3x3a16_hvx128.h"
#elif conv3x3a32
  #include "conv3x3a32_hvx128.h"
#elif sobel3x3
  #include "sobel3x3_hvx128.h"
#elif blur3x3
  #include "blur3x3_hvx128.h"
#elif dilate3x3
  #include "dilate3x3_hvx128.h"
#elif median3x3
  #include "median3x3_hvx128.h"
#endif

signed char mask[9] =
{
    1, -4, 7,
    2, -5, 8,
    3, -6, 9
};
*/