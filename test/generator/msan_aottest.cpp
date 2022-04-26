#ifdef _WIN32
#include <stdio.h>
int main(int argc, char **argv) {
    printf("[SKIP] MSAN isn't supported for any Windows variant.\n");
    return 0;
}
#else
#include "HalideBuffer.h"
#include "HalideRuntime.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

#include "msan.h"

using namespace std;
using namespace Halide::Runtime;
using MsanBuffer = Halide::Runtime::Buffer<uint8_t, 3>;

enum {
    AnnotateBoundsInferenceBuffer,
    AnnotateBoundsInferenceShape,
    AnnotateIntermediateBuffer,
    AnnotateIntermediateShape,
    AnnotateOutputBuffer,
    AnnotateOutputShape,
    AnnotateIntermediateContents,
    AnnotateOutputContents,
} annotate_stage = AnnotateBoundsInferenceBuffer;

enum {
    CheckInputBuffer,
    CheckInputShape,
    CheckInputContents,
    CheckExternResultBuffer,
    CheckExternResultShape,
    CheckExternResultContents,
} check_stage = CheckInputBuffer;

const void *output_base = nullptr;
const void *output_previous = nullptr;
int bounds_inference_count = 0;
bool expect_intermediate_buffer_error = false;
bool skip_extern_copy = false;
uint64_t input_contents_checked = 0;
uint64_t input_contents_uninitialized = 0;
uint64_t externresult_contents_checked = 0;
uint64_t externresult_contents_uninitialized = 0;
uint64_t output_contents_annotated = 0;

void reset_state(const MsanBuffer &in, const MsanBuffer &out) {
    annotate_stage = AnnotateBoundsInferenceBuffer;
    check_stage = CheckInputBuffer;
    output_base = out.data();
    output_previous = nullptr;
    bounds_inference_count = 0;
    expect_intermediate_buffer_error = false;
    skip_extern_copy = false;
    input_contents_uninitialized = 0;
    input_contents_checked = 0;
    externresult_contents_uninitialized = 0;
    externresult_contents_checked = 0;
    output_contents_annotated = 0;
    // printf("IN-DATA:  %p:%p:%p\n", (void *)in.raw_buffer(), (void *)in.raw_buffer()->dim, (void *)in.data());
    // printf("OUT-DATA: %p:%p:%p\n", (void *)out.raw_buffer(), (void *)out.raw_buffer()->dim, (void *)out.data());
}

// Just copies in -> out.
extern "C" int msan_extern_stage(halide_buffer_t *in, halide_buffer_t *out) {
    if (in->is_bounds_query() || out->is_bounds_query()) {
        if (in->is_bounds_query()) {
            assert(in->dimensions == 3);
            in->dim[0].extent = 4;
            in->dim[1].extent = 4;
            in->dim[2].extent = 3;
            in->dim[0].min = 0;
            in->dim[1].min = 0;
            in->dim[2].min = 0;
        }
        if (out->is_bounds_query()) {
            assert(out->dimensions == 3);
            out->dim[0].extent = 4;
            out->dim[1].extent = 4;
            out->dim[2].extent = 3;
            out->dim[0].min = 0;
            out->dim[1].min = 0;
            out->dim[2].min = 0;
        }
        return 0;
    }

    if (in->type != out->type) {
        fprintf(stderr, "type mismatch\n");
        return -1;
    }
    if (skip_extern_copy) {
        // Fill it with zero to mimic msan "poison".
        MsanBuffer(*out).fill(0);
    } else {
        MsanBuffer(*out).copy_from(MsanBuffer(*in));
    }
    out->set_host_dirty();
    return 0;
}

extern "C" void halide_error(void *user_context, const char *msg) {
    // Emitting "error.*:" to stdout or stderr will cause CMake to report the
    // test as a failure on Windows, regardless of error code returned,
    // hence the abbreviation to "err".
    // fprintf(stderr, "Saw err: %s\n", msg);
    // Do not exit.
}

// Must provide a stub for these since we aren't compiling with LLVM MSAN
// enabled, and the default implementation of our msan-specific runtime needs them.
extern "C" void __msan_check_mem_is_initialized(const void *mem, size_t size) {
    fprintf(stderr, "Impossible\n");
    exit(-1);
}

extern "C" void __msan_unpoison(const void *mem, size_t size) {
    fprintf(stderr, "Impossible\n");
    exit(-1);
}

extern "C" long __msan_test_shadow(const void *mem, size_t size) {
    fprintf(stderr, "Impossible\n");
    exit(-1);
}

extern "C" int halide_msan_check_memory_is_initialized(void *user_context, const void *ptr, uint64_t len, const char *name) {
    // printf("CHECK-MEM: %d:%p:%08x for buf %s\n", (int)check_stage, ptr, (unsigned int)len, name);
    if (check_stage == CheckInputBuffer) {
        if (len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int)len);
            exit(-1);
        }
        check_stage = CheckInputShape;
    } else if (check_stage == CheckInputShape) {
        if (len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int)len);
            exit(-1);
        }
        check_stage = CheckInputContents;
    } else if (check_stage == CheckInputContents) {
        for (uint64_t i = 0; i < len; ++i) {
            input_contents_uninitialized += (((const uint8_t *)ptr)[i] == 0);
        }
        input_contents_checked += len;
        check_stage = CheckExternResultBuffer;
    } else if (check_stage == CheckExternResultBuffer) {
        if (len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int)len);
            exit(-1);
        }
        check_stage = CheckExternResultShape;
    } else if (check_stage == CheckExternResultShape) {
        if (len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int)len);
            exit(-1);
        }
        check_stage = CheckExternResultContents;
    } else if (check_stage == CheckExternResultContents) {
        for (uint64_t i = 0; i < len; ++i) {
            externresult_contents_uninitialized += (((const uint8_t *)ptr)[i] == 0);
        }
        externresult_contents_checked += len;
    } else {
        fprintf(stderr, "Failure: bad enum\n");
        exit(-1);
    }
    return 0;
}

extern "C" int halide_msan_annotate_memory_is_initialized(void *user_context, const void *ptr, uint64_t len) {
    // printf("ANNOTATE: %d:%p:%08x\n", (int)annotate_stage, ptr, (unsigned int)len);
    if (annotate_stage == AnnotateBoundsInferenceBuffer) {
        if (output_previous != nullptr || len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int)len);
            exit(-1);
        }
        annotate_stage = AnnotateBoundsInferenceShape;
    } else if (annotate_stage == AnnotateBoundsInferenceShape) {
        if (output_previous != nullptr || len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int)len);
            exit(-1);
        }
        bounds_inference_count += 1;
        if (bounds_inference_count == 4) {
            annotate_stage = AnnotateIntermediateBuffer;
        } else {
            annotate_stage = AnnotateBoundsInferenceBuffer;
        }
    } else if (annotate_stage == AnnotateIntermediateBuffer) {
        if (expect_intermediate_buffer_error) {
            if (len != 80) {
                fprintf(stderr, "Failure: Expected error message of len=80, saw %d bytes\n", (unsigned int)len);
                exit(-1);
            }
            return 0;  // stay in this state
        }
        if (output_previous != nullptr || len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int)len);
            exit(-1);
        }
        annotate_stage = AnnotateIntermediateShape;
    } else if (annotate_stage == AnnotateIntermediateShape) {
        if (output_previous != nullptr || len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int)len);
            exit(-1);
        }
        annotate_stage = AnnotateOutputBuffer;
    } else if (annotate_stage == AnnotateOutputBuffer) {
        if (output_previous != nullptr || len != sizeof(halide_buffer_t)) {
            fprintf(stderr, "Failure: Expected sizeof(halide_buffer_t), saw %d\n", (unsigned int)len);
            exit(-1);
        }
        annotate_stage = AnnotateOutputShape;
    } else if (annotate_stage == AnnotateOutputShape) {
        if (output_previous != nullptr || len != sizeof(halide_dimension_t) * 3) {
            fprintf(stderr, "Failure: Expected sizeof(halide_dimension_t) * 3, saw %d\n", (unsigned int)len);
            exit(-1);
        }
        annotate_stage = AnnotateIntermediateContents;
    } else if (annotate_stage == AnnotateIntermediateContents) {
        if (output_previous != nullptr || len != 4 * 4 * 3) {
            fprintf(stderr, "Failure: Expected %d, saw %d\n", 4 * 4 * 3, (unsigned int)len);
            exit(-1);
        }
        annotate_stage = AnnotateOutputContents;
    } else if (annotate_stage == AnnotateOutputContents) {
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
        output_contents_annotated += len;
    } else {
        fprintf(stderr, "Failure: bad enum\n");
        exit(-1);
    }
    return 0;
}

template<typename T>
void verify(const T &image) {
    image.for_each_element([&](int x, int y, int c) {
        int expected = 7;
        for (int i = 0; i < 4; ++i) {
            expected += (uint8_t)(i + y + c) | 0x01;
        }
        int actual = image(x, y, c);
        if (actual != expected) {
            fprintf(stderr, "Failure @ %d %d %d: expected %d, got %d\n", x, y, c, expected, actual);
            exit(-1);
        }
    });
}

MsanBuffer make_input_for(const MsanBuffer &output) {
    auto input = MsanBuffer::make_with_shape_of(output);
    // Ensure that no 'valid' inputs are all-zero
    input.for_each_element([&](int x, int y, int c) { input(x, y, c) = (uint8_t)(x + y + c) | 0x01; });
    return input;
}

//-----------------------------------------------------------------------------

int main() {
    printf("Testing interleaved...\n");
    {
        auto out = MsanBuffer::make_interleaved(4, 4, 3);
        auto in = make_input_for(out);
        reset_state(in, out);
        if (msan(in, out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        verify(out);
        if (input_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (output_contents_annotated != 4 * 4 * 3) {
            fprintf(stderr, "Failure: output_contents_annotated is wrong (%d).\n", (int)output_contents_annotated);
            exit(-1);
        }
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }

    printf("Testing sparse interleaved...\n");
    {
        const int kPad = 1;
        halide_dimension_t shape[3] = {
            {0, 4, 3},
            {0, 4, (4 * 3) + kPad},
            {0, 3, 1},
        };
        std::vector<uint8_t> data(((4 * 3) + kPad) * 4);
        auto out = MsanBuffer(data.data(), 3, shape);
        auto in = make_input_for(out);
        reset_state(in, out);
        if (msan(in, out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (input_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (externresult_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: externresult_contents_uninitialized is wrong (%d).\n", (int)externresult_contents_uninitialized);
            exit(-1);
        }
        if (externresult_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: externresult_contents_checked is wrong (%d).\n", (int)externresult_contents_checked);
            exit(-1);
        }
        if (output_contents_annotated != 4 * 4 * 3) {
            fprintf(stderr, "Failure: output_contents_annotated is wrong (%d).\n", (int)output_contents_annotated);
            exit(-1);
        }
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }

    printf("Testing planar...\n");
    {
        auto out = MsanBuffer(4, 4, 3);
        auto in = make_input_for(out);
        reset_state(in, out);
        if (msan(in, out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (input_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (externresult_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: externresult_contents_uninitialized is wrong (%d).\n", (int)externresult_contents_uninitialized);
            exit(-1);
        }
        if (externresult_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: externresult_contents_checked is wrong (%d).\n", (int)externresult_contents_checked);
            exit(-1);
        }
        if (output_contents_annotated != 4 * 4 * 3) {
            fprintf(stderr, "Failure: output_contents_annotated is wrong (%d).\n", (int)output_contents_annotated);
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
            {0, 4, 1},
            {0, 4, 4 + kPad},
            {0, 3, (4 + kPad) * 4},
        };
        std::vector<uint8_t> data((4 + kPad) * 4 * 3);
        auto out = MsanBuffer(data.data(), 3, shape);
        auto in = make_input_for(out);
        reset_state(in, out);
        if (msan(in, out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (input_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (externresult_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: externresult_contents_uninitialized is wrong (%d).\n", (int)externresult_contents_uninitialized);
            exit(-1);
        }
        if (externresult_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: externresult_contents_checked is wrong (%d).\n", (int)externresult_contents_checked);
            exit(-1);
        }
        if (output_contents_annotated != 4 * 4 * 3) {
            fprintf(stderr, "Failure: output_contents_annotated is wrong (%d).\n", (int)output_contents_annotated);
            exit(-1);
        }
        if (output_previous == nullptr) {
            fprintf(stderr, "Failure: Expected to see annotations.\n");
            exit(-1);
        }
    }

    // Buffers should not be marked as "initialized" if the filter fails with an error.
    printf("Verifying that output is not marked when error occurs...\n");
    {
        auto out = MsanBuffer(1, 1, 1);
        auto in = make_input_for(out);
        reset_state(in, out);
        expect_intermediate_buffer_error = true;
        if (msan(in, out) == 0) {
            fprintf(stderr, "Failure (expected failure but did not)!\n");
            exit(-1);
        }
        if (input_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 1) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (externresult_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: externresult_contents_uninitialized is wrong (%d).\n", (int)externresult_contents_uninitialized);
            exit(-1);
        }
        if (externresult_contents_checked != 0) {
            fprintf(stderr, "Failure: externresult_contents_checked is wrong (%d).\n", (int)externresult_contents_checked);
            exit(-1);
        }
        if (output_contents_annotated != 0) {
            fprintf(stderr, "Failure: expected no output contents to be annotated.\n");
            exit(-1);
        }
        if (output_previous != nullptr) {
            fprintf(stderr, "Failure: Expected NOT to see annotations.\n");
            exit(-1);
        }
    }

    // Buffers should not be marked as "initialized" if the filter fails with an error.
    // We'll test the mechanism by ensuring that our valid input buffer never has
    // only nonzero elements, and then checking for those.
    printf("Verifying that input is checked for initialization...\n");
    {
        auto out = MsanBuffer::make_interleaved(4, 4, 3);
        auto in = make_input_for(out);
        // Make exactly one element "uninitialized"
        in(3, 2, 1) = 0;
        reset_state(in, out);
        // Note that with "real" msan in place, we would expect this to never return;
        // halide_msan_check_memory_is_initialized() would abort if it encounters
        // uninitialized memory. It's hard to simulate that in our test harness, so
        // we'll actually let it "complete" successfully and check the uninitialized state at the end.
        if (msan(in, out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (input_contents_uninitialized != sizeof(uint8_t)) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (externresult_contents_uninitialized != 0) {
            fprintf(stderr, "Failure: externresult_contents_uninitialized is wrong (%d).\n", (int)externresult_contents_uninitialized);
            exit(-1);
        }
        if (externresult_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: externresult_contents_checked is wrong (%d).\n", (int)externresult_contents_checked);
            exit(-1);
        }
        // Don't bother checking outputs here.
    }

    printf("Verifying that result of define_extern is checked for initialization...\n");
    {
        auto out = MsanBuffer::make_interleaved(4, 4, 3);
        auto in = make_input_for(out);
        // Make exactly one element "uninitialized"
        in(3, 2, 1) = 0;
        reset_state(in, out);
        skip_extern_copy = true;
        // Note that with "real" msan in place, we would expect this to never return;
        // halide_msan_check_memory_is_initialized() would abort if it encounters
        // uninitialized memory. It's hard to simulate that in our test harness, so
        // we'll actually let it "complete" successfully and check the uninitialized state at the end.
        if (msan(in, out) != 0) {
            fprintf(stderr, "Failure!\n");
            exit(-1);
        }
        if (input_contents_uninitialized != sizeof(uint8_t)) {
            fprintf(stderr, "Failure: input_contents_uninitialized is wrong (%d).\n", (int)input_contents_uninitialized);
            exit(-1);
        }
        if (input_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: input_contents_checked is wrong (%d).\n", (int)input_contents_checked);
            exit(-1);
        }
        if (externresult_contents_uninitialized != 4 * 4 * 3) {
            fprintf(stderr, "Failure: externresult_contents_uninitialized is wrong (%d).\n", (int)externresult_contents_uninitialized);
            exit(-1);
        }
        if (externresult_contents_checked != 4 * 4 * 3) {
            fprintf(stderr, "Failure: externresult_contents_checked is wrong (%d).\n", (int)externresult_contents_checked);
            exit(-1);
        }
        // Don't bother checking outputs here.
    }

    printf("Success!\n");
    return 0;
}

#endif
