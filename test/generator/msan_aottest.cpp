#ifdef _WIN32
#include <stdio.h>
// MSAN isn't supported for any Windows variant
int main(int argc, char **argv) {
    printf("Skipping test on Windows\n");
    return 0;
}
#else
#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

#include "msan.h"

using namespace std;
using namespace Halide::Runtime;

// Just copies in -> out.
extern "C" int msan_extern_stage(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query()) {
        in->dim[0].extent = 4;
        in->dim[1].extent = 4;
        in->dim[2].extent = 3;
        in->dim[0].min = 0;
        in->dim[1].min = 0;
        in->dim[2].min = 0;
        return 0;
    }
    if (!out->host) {
        fprintf(stderr, "msan_extern_stage failure\n");
        return -1;
    }
    if (in->type != out->type) {
        return -1;
    }
    Buffer<int32_t>(*out).copy_from(Buffer<int32_t>(*in));
    out->set_host_dirty();
    return 0;
}

extern "C" void halide_error(void *user_context, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    fprintf(stderr, "Saw err: %s\n", msg);
    // Do not exit.
}

// Must provide a stub for this since we aren't compiling with LLVM MSAN
// enabled, and the default implementation of halide_msan_annotate_memory_is_initialized()
// expects this to be present
extern "C" void AnnotateMemoryIsInitialized(const char *file, int line,
                                            const void *mem, size_t size) {
    fprintf(stderr, "Impossible\n");
    exit(-1);
}

enum {
  expect_bounds_inference_buffer,
  expect_intermediate_buffer,
  expect_intermediate_shape,
  expect_output_buffer,
  expect_output_shape,
  expect_intermediate_contents,
  expect_output_contents,
} annotate_stage = expect_bounds_inference_buffer;
const void* output_base = nullptr;
const void* output_previous = nullptr;
int bounds_inference_count = 0;
bool expect_error = false;

void reset_state(const void* base) {
    annotate_stage = expect_bounds_inference_buffer;
    output_base = base;
    output_previous = nullptr;
    bounds_inference_count = 0;
    expect_error = false;
}

extern "C" void halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) {
    printf("%d:%p:%08x\n", (int)annotate_stage, ptr, (unsigned int) len);
    if (annotate_stage == expect_bounds_inference_buffer) {
        if (output_previous != nullptr || len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int) len);
            exit(-1);
        }
        bounds_inference_count += 1;
        if (bounds_inference_count == 4) {
            annotate_stage = expect_intermediate_buffer;
        }
    } else if (annotate_stage == expect_intermediate_buffer) {
        if (expect_error) {
            if (len != 87) {
                fprintf(stderr, "Failure: Expected error message of len=87, saw %d bytes\n", (unsigned int) len);
                exit(-1);
            }
            return;  // stay in this state
        }
        if (output_previous != nullptr || len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int) len);
            exit(-1);
        }
        annotate_stage = expect_intermediate_shape;
    } else if (annotate_stage == expect_intermediate_shape) {
        if (output_previous != nullptr || len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int) len);
            exit(-1);
        }
        annotate_stage = expect_output_buffer;
    } else if (annotate_stage == expect_output_buffer) {
        if (output_previous != nullptr || len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int) len);
            exit(-1);
        }
        annotate_stage = expect_output_shape;
    } else if (annotate_stage == expect_output_shape) {
        if (output_previous != nullptr || len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int) len);
            exit(-1);
        }
        annotate_stage = expect_intermediate_contents;
    } else if (annotate_stage == expect_intermediate_contents) {
        if (output_previous != nullptr || len != 4 * 4 * 3 * 4) {
            fprintf(stderr, "Failure: Expected %d, saw %d\n", 4 * 4 * 3 * 4, (unsigned int) len);
            exit(-1);
        }
        annotate_stage = expect_output_contents;
    } else if (annotate_stage == expect_output_contents) {
        if (output_previous == nullptr) {
            if (ptr != output_base) {
                fprintf(stderr, "Failure: Expected base p %p but saw %p\n", output_base, ptr);
                exit(-1);
            }
            if (ptr <= output_previous) {
                fprintf(stderr, "Failure: Expected monotonic increase but saw %p -> %p\n", output_previous, ptr);
                exit(-1);
            }
            output_previous = ptr;
        }
    } else {
        fprintf(stderr, "Failure: bad enum\n");
        exit(-1);
    }
}

template<typename T>
void verify(const T &image) {
    image.for_each_element([&](int x, int y, int c) {
        int expected = 3;
        for (int i = 0; i < 4; ++i) {
            expected += (int32_t)(i + y + c);
        }
        int actual = image(x, y, c);
        if (actual != expected) {
            fprintf(stderr, "Failure @ %d %d %d: expected %d, got %d\n", x, y, c, expected, actual);
            exit(-1);
        }
    });
}

//-----------------------------------------------------------------------------

int main()
{
    printf("Testing interleaved...\n");
    {
        auto out = Buffer<int32_t>::make_interleaved(4, 4, 3);
        reset_state(out.data());
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        verify(out);
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }

    printf("Testing sparse chunky...\n");
    {
        const int kPad = 1;
        halide_dimension_t shape[3] = {
            { 0, 4, 3 },
            { 0, 4, (4 * 3) + kPad },
            { 0, 3, 1 },
        };
        std::vector<int32_t> data(((4 * 3) + kPad) * 4);
        auto out = Buffer<int32_t>(data.data(), 3, shape);
        reset_state(out.data());
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }

    printf("Testing planar...\n");
    {
        auto out = Buffer<int32_t>(4, 4, 3);
        reset_state(out.data());
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }

    printf("Testing sparse planar...\n");
    {
        const int kPad = 1;
        halide_dimension_t shape[3] = {
            { 0, 4, 1 },
            { 0, 4, 4 + kPad },
            { 0, 3, (4 + kPad) * 4 },
        };
        std::vector<int32_t> data((4 + kPad) * 4 * 3);
        auto out = Buffer<int32_t>(data.data(), 3, shape);
        reset_state(out.data());
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }
    // Buffers should not be marked as "initialized" if the filter fails with an error.
    printf("Testing error case...\n");
    {
        auto out = Buffer<int32_t>(1, 1, 1);
        reset_state(out.data());
        expect_error = true;
        if (msan(out) == 0) {
            fprintf(stderr, "Failure (expected failure but did not)!\n");
            exit(-1);
        }
        if (output_previous != nullptr) {
            fprintf(stderr, "Failure: Expected NOT to see annotations.\n");
            exit(-1);
        }
    }

    printf("Success!\n");
    return 0;
}

#endif
