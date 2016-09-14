#include "HalideRuntime.h"
#include "HalideBuffer.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

#include "msan.h"

using namespace std;
using namespace Halide;

extern "C" void halide_error(void *user_context, const char *msg) {
    fprintf(stderr, "Saw error: %s\n", msg);
}

// Must provide a stub for this since we aren't compiling with LLVM MSAN
// enabled, and the default implementation of halide_msan_annotate_memory_is_initialized()
// expects this to be present
extern "C" void AnnotateMemoryIsInitialized(const char *file, int line,
                                            const void *mem, size_t size) {
    fprintf(stderr, "Impossible\n");
    exit(-1);
}

bool fail_on_annotate = false;
const void* previous = nullptr;
extern "C" void halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, size_t len) {
    if (fail_on_annotate) {
        fprintf(stderr, "Failure!\nShould not have seen an annotate call here.\n");
        exit(-1);
    }
    if (ptr <= previous) {
        fprintf(stderr, "Failure!\nExpected monotonic increase but saw %p -> %p\n", previous, ptr);
        exit(-1);
    }
    printf("%p:%08x\n", ptr, (unsigned int) len);
    previous = ptr;
}

//-----------------------------------------------------------------------------

int main()
{
    printf("Testing interleaved...\n");
    previous = nullptr;
    {
        auto out = Buffer<int32_t>::make_interleaved(4, 4, 3);
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
    }

    printf("Testing sparse chunky...\n");
    previous = nullptr;
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
    }

    printf("Testing planar...\n");
    previous = nullptr;
    {
        auto out = Buffer<int32_t>(4, 4, 3);
        if (msan(out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
    }

    printf("Testing sparse planar...\n");
    previous = nullptr;
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
    }

    // Buffers should not be marked as "initialized" if the filter fails with an error.
    printf("Testing error case...\n");
    previous = nullptr;
    fail_on_annotate = true;
    {
        auto out = Buffer<int32_t>(1, 1, 1);
        if (msan(out) == 0) {
            fprintf(stderr, "Failure (expected failure but did not)!\n");
            exit(-1);
        }
    }

    printf("Success!\n");
    return 0;
}
