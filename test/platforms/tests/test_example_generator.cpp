#include <cstdint>
#include <vector>

#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"
#include "SimpleAppAPI.h"

#include "example.h"
#include "example_glsl.h"

const int N = 256;
const int C = 4;
const float compiletime_factor = 1.0f;

static int check(const buffer_t &buf, float runtime_factor) {
    int errors = 0;
    for (int x = 0; x < buf.extent[0]; x++) {
        for (int y = 0; y < buf.extent[1]; y++) {
            for (int c = 0; c < buf.extent[2]; c++) {
                float value = (x > y ? x : y) * c * compiletime_factor * runtime_factor;
                uint8_t expected = (uint8_t)(int(value) % 255);
                uint8_t actual   = buf.host[x*buf.stride[0] + y*buf.stride[1] + c*buf.stride[2]];
                if (expected != actual) {
                  errors++;
                }
            }
        }
    }
    return errors;
}

extern "C"
bool test_example_generator() {

  halide_print(NULL, "Running filter example. This should produce two blue and "
               "green patterns.\n");

  float runtime_factor = 2.0f;

  std::vector<uint8_t> host(N*N*C);

  buffer_t buf = {0};
  buf.host = host.data();
  buf.extent[0] = N;
  buf.extent[1] = N;
  buf.extent[2] = C;
  buf.stride[0] = 1;
  buf.stride[1] = N;
  buf.stride[2] = N*N;
  buf.elem_size = sizeof(uint8_t);

  // ------ CPU target

  halide_print(NULL, "CPU target\n");
  if (example(runtime_factor, &buf) != 0) {
    halide_error(NULL, "example failed!");
  }

  int errors = check(buf, runtime_factor);
  if (errors > 0) {
    halide_error(NULL, "CPU Target had errors!");
  }

  halide_buffer_display(&buf);

  // ------ GLSL target

  runtime_factor *= 2;
  halide_print(NULL,"GLSL target\n");
  if (example_glsl(runtime_factor, &buf) != 0) {
    halide_error(NULL, "example_glsl failed!");
  }

  if (!buf.dev) {
      halide_error(NULL, "Expected dev output here");
  }
  halide_copy_to_host(NULL, &buf);

  errors = check(buf, runtime_factor);
  if (errors > 0) {
    halide_error(NULL, "GPU Target had errors!");
  }

  halide_buffer_display(&buf);

  return errors > 0;
}

