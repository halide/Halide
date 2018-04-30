#if !defined(__has_feature)

#include <stdio.h>
int main(int argc, char **argv) {
    printf("MSAN unsupported on this compiler; skipping test.\n");
    return 0;
}

#elif !__has_feature(memory_sanitizer)

#include <stdio.h>
int main(int argc, char **argv) {
    printf("MSAN is not enabled for this build; skipping test.\n");
    return 0;
}

#else

#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <sanitizer/msan_interface.h>
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
    auto *md = msan_metadata();
    const bool filter_msan = strstr(md->target, "msan") != nullptr;

    if (!filter_msan) {
        fprintf(stderr, "MSAN filter was built without MSAN enabled; this is an error.\n");
        return 1;
    }

    printf("Testing interleaved...\n");
    {
        auto out = Buffer<int32_t>::make_interleaved(4, 4, 3);
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        verify(out);
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
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        verify(out);
    }

    printf("Testing planar...\n");
    {
        auto out = Buffer<int32_t>(4, 4, 3);
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        verify(out);
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
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        verify(out);
    }

    // Buffers should not be marked as "initialized" if the filter fails with an error.
    printf("Testing error case...\n");
    {
        auto out = Buffer<int32_t>(1, 1, 1);
        if (msan(out) == 0) {
            fprintf(stderr, "Failure (expected failure but did not)!\n");
            exit(-1);
        }
        // verify(out);  // -- no: would crash
        intptr_t first_poisoned = __msan_test_shadow(out.raw_buffer()->host, sizeof(int32_t));
        if (first_poisoned < 0) {
            fprintf(stderr, "Expected to see poisoned (uninitialized) memory in output, but did not.\n");
            exit(-1);
        }
    }

    printf("Success!\n");
    return 0;
}

#endif
