#include <cstdint>
#include <vector>

#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"
#include "SimpleAppAPI.h"
#include "static_image.h"

#include "example.h"
#include "example_glsl.h"

const int kWidth = 1024;
const int kHeight = 1024;
const int kIter = 10;
const int kSeed = 0;

template<typename T>
static int check(const Image<T> &input, const Image<T> &output) {
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

static int run_test(void *uc, int channels, bool use_glsl, bool use_interleaved) {
  std::string name = "Example";
  name += use_glsl ? "_GLSL" : "_CPU";
  name += use_interleaved ? "_Chunky" : "_Planar";
  halide_printf(uc, "\n---------------------------\n%s\n", name.c_str());
  Image<uint8_t> input(kWidth, kHeight, channels, 0, use_interleaved);
  Image<uint8_t> output(kWidth, kHeight, channels, 0, use_interleaved);
  (void) halide_smooth_buffer_host<uint8_t>(uc, kSeed, input);
  if (use_glsl) {
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
    // Call once to compile shader, warm up, etc.
    (void) example_glsl(input, output);
    {
      ScopedTimer timer(uc, name, kIter);
      for (int i = 0; i < kIter; ++i) {
        (void) example_glsl(input, output);
      }
    }
    {
      ScopedTimer timer(uc, name + " halide_copy_to_host");
      halide_copy_to_host(uc, output);
    }
  } else {
    // Call once to warm up cache
    (void) example(input, output);
    {
      ScopedTimer timer(uc, name, kIter);
      for (int i = 0; i < kIter; ++i) {
        (void) example(input, output);
      }
    }
  }
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
  errors += run_test(uc, 4, false, false);
  errors += run_test(uc, 4, false, true);
  // GLSL+Planar is a silly combination; the conversion overhead is high.
  errors += run_test(uc, 4, true, false);
  errors += run_test(uc, 4, true, true);

  // -------- Other stuff

  halide_print(uc, "Here is a random image.\n");

  Image<uint8_t> randomness(300, 400, 3);
  (void) halide_randomize_buffer_host<uint8_t>(uc, 0, 0, 255, randomness);
  halide_buffer_display(randomness);


  halide_print(uc, "Here is a smooth image.\n");

  Image<uint8_t> smoothness(300, 400, 3);
  (void) halide_smooth_buffer_host<uint8_t>(uc, 0, smoothness);
  halide_buffer_display(smoothness);

  return errors > 0;
}

