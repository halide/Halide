#include <cstdint>
#include <vector>

#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"
#include "SimpleAppAPI.h"

#include "HalideBuffer.h"

#include "example4.h"
#include "example4_glsl.h"
#include "example3.h"
#include "example3_glsl.h"
#include "example2.h"
#include "example2_glsl.h"
#include "example1.h"
#include "example1_glsl.h"

const int kWidth = 1024;
const int kHeight = 1024;
const int kIter = 1;
const int kSeed = 0;

template<typename T>
static int check(const Buffer<T> &input, const Buffer<T> &output) {
    int errors = 0;
    for (int x = 0; x < input.extent(0); x++) {
        for (int y = 0; y < input.extent(1); y++) {
            for (int c = 0; c < input.extent(2); c++) {
                T expected = input(input.extent(0) - x - 1, y, c);
                T actual = output(x, y, c);
                if (expected != actual) {
                  errors++;
                }
            }
        }
    }
    return errors;
}

enum Implementation { kCPU = 0, kGLSL = 1 };
enum Layout { kChunky, kPlanar };

typedef int (*ExampleFunc)(buffer_t* in, buffer_t* out);

static const ExampleFunc exampleFuncs[4][2] = {
  { example1, example1_glsl },
  { example2, example2_glsl },
  { example3, example3_glsl },
  { example4, example4_glsl },
};

static int run_test(void *uc, int channels, Implementation imp, Layout layout) {
  std::string name = "Example_";
  name += std::to_string(channels);
  name += (imp == kGLSL) ? "_GLSL" : "_CPU";
  name += (layout == kChunky) ? "_Chunky" : "_Planar";
  halide_printf(uc, "\n---------------------------\n%s\n", name.c_str());
  Buffer<uint8_t> input(kWidth, kHeight, channels, 0, (layout == kChunky));
  Buffer<uint8_t> output(kWidth, kHeight, channels, 0, (layout == kChunky));
  (void) halide_smooth_buffer_host<uint8_t>(uc, kSeed, input);
  if (imp == kGLSL) {
    // Call once to ensure OpenGL is inited (we want to time the
    // cost of copy-to-device alone)
    halide_copy_to_device(uc, input, halide_opengl_device_interface());
    // Mark as dirty so the next call won't be a no-op
    input.set_host_dirty();
    {
      ScopedTimer timer(uc, name + " halide_copy_to_device input");
      halide_copy_to_device(uc, input, halide_opengl_device_interface());
    }
    {
      ScopedTimer timer(uc, name + " halide_copy_to_device output");
      halide_copy_to_device(uc, output, halide_opengl_device_interface());
    }
  }
  // Call once to compile shader, warm up, etc.
  ExampleFunc example = exampleFuncs[channels-1][imp];
  (void) example(input, output);
  {
    ScopedTimer timer(uc, name, kIter);
    for (int i = 0; i < kIter; ++i) {
      (void) example(input, output);
    }
  }
  if (imp == kGLSL) {
    ScopedTimer timer(uc, name + " halide_copy_to_host");
    halide_copy_to_host(uc, output);
  }
  // halide_buffer_display(input);
  // halide_buffer_print(input);
  // halide_buffer_display(output);
  // halide_buffer_print(output);
  int errors = check<uint8_t>(input, output);
  if (errors) {
    halide_errorf(uc, "Test %s had %d errors!\n\n", name.c_str(), errors);
  } else {
    halide_printf(uc, "Test %s had no errors.\n\n", name.c_str());
  }
  return errors;
}

extern "C"
bool example_test() {
  void *uc = NULL;

  int errors = 0;
  for (int channels = 1; channels <= 4; channels++) {
    errors += run_test(uc, channels, kCPU, kChunky);
    errors += run_test(uc, channels, kCPU, kPlanar);
    errors += run_test(uc, channels, kGLSL, kChunky);
    // GLSL+Planar is a silly combination; the conversion overhead is high.
    // But let's run it anyway, since it should work.
    errors += run_test(uc, channels, kGLSL, kPlanar);
  }

  // -------- Other stuff

  halide_print(uc, "Here is a random image.\n");

  Buffer<uint8_t> randomness(300, 400, 3);
  (void) halide_randomize_buffer_host<uint8_t>(uc, 0, 0, 255, randomness);
  halide_buffer_display(randomness);


  halide_print(uc, "Here is a smooth image.\n");

  Buffer<uint8_t> smoothness(300, 400, 3);
  (void) halide_smooth_buffer_host<uint8_t>(uc, 0, smoothness);
  halide_buffer_display(smoothness);

  return errors > 0;
}

