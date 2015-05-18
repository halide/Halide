#include <cstdint>
#include <vector>

#include "HalideRuntime.h"
#include "HalideRuntimeOpenGL.h"
#include "SimpleAppAPI.h"
#include "static_image.h"

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
bool example_test() {

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

  {
    ScopedTimer timer(NULL, "CPU example");
    // Normally you'd check the result, but in this case we'll just rely
    // on halide_error() dumping an error message to our console.
    (void) example(runtime_factor, &buf);
  }

  int errors = check(buf, runtime_factor);
  if (errors > 0) {
    halide_errorf(NULL, "CPU Target had %d errors!", errors);
  }

  halide_buffer_display(&buf);

  // ------ GLSL target

  runtime_factor *= 2;
  halide_print(NULL,"GLSL target\n");

  {
    ScopedTimer timer(NULL, "halide_copy_to_device");
    halide_copy_to_device(NULL, &buf, halide_opengl_device_interface());
  }

  {
    ScopedTimer timer(NULL, "GLSL example");
    (void) example_glsl(runtime_factor, &buf);
  }

  if (!buf.dev) {
      halide_error(NULL, "Expected dev output here");
  }
  {
    ScopedTimer timer(NULL, "halide_copy_to_host");
    halide_copy_to_host(NULL, &buf);
  }

  errors = check(buf, runtime_factor);
  if (errors > 0) {
    halide_errorf(NULL, "GPU Target had %d errors!", errors);
  }

  halide_buffer_display(&buf);

  // -------- Other stuff

  halide_print(NULL, "Here is a random image.\n");

  Image<uint8_t> randomness(300, 400, 3);
  if (!halide_randomize_buffer_host<uint8_t>(0, 0, 255, randomness)) {
    halide_error(NULL, "halide_randomize_buffer_host failed!");
  }
  halide_buffer_display(randomness);

  return errors > 0;
}

