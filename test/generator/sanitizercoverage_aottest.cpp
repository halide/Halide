#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "sanitizercoverage.h"

#include <iostream>
#include <limits>
#include <type_traits>
#include <vector>

using namespace std;
using namespace Halide::Runtime;

bool enable_callbacks = false;

#if defined(__linux__)
// Used by -fsanitize-coverage=stack-depth to track stack depth
__attribute__((tls_model("initial-exec"))) thread_local uintptr_t __sancov_lowest_stack;
#endif

extern "C" void __sanitizer_cov_8bit_counters_init(uint8_t *Start, uint8_t *Stop) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_8bit_counters_init. Success!\n");
}

extern "C" void __sanitizer_cov_pcs_init(const uintptr_t *pcs_beg,
                                         const uintptr_t *pcs_end) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_pcs_init. Success!\n");
}

extern "C" void __sanitizer_cov_trace_cmp1(uint8_t Arg1, uint8_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_cmp1. Success!\n");
}

extern "C" void __sanitizer_cov_trace_cmp4(uint32_t Arg1, uint32_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_cmp4. Success!\n");
}

extern "C" void __sanitizer_cov_trace_cmp8(uint64_t Arg1, uint64_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_cmp8. Success!\n");
}

extern "C" void __sanitizer_cov_trace_const_cmp1(uint8_t Arg1, uint8_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_const_cmp1. Success!\n");
}

extern "C" void __sanitizer_cov_trace_const_cmp2(uint16_t Arg1, uint16_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_const_cmp2. Success!\n");
}

extern "C" void __sanitizer_cov_trace_const_cmp4(uint32_t Arg1, uint32_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_const_cmp4. Success!\n");
}

extern "C" void __sanitizer_cov_trace_const_cmp8(uint64_t Arg1, uint64_t Arg2) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_const_cmp8. Success!\n");
}

extern "C" void __sanitizer_cov_trace_switch(uint64_t Val, uint64_t *Cases) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_switch. Success!\n");
}

extern "C" void __sanitizer_cov_trace_pc_indir(uintptr_t Callee) {
    if (!enable_callbacks) return;
    printf("Hit __sanitizer_cov_trace_pc_indir. Success!\n");
}

template<typename T>
void clear_out(T &image) {
    image.fill(-42);
}

void verify_out(const Buffer<int8_t, 3> &image) {
    image.for_each_element([&](int x, int y, int c) {
        int expected = 42 + c;
        int actual = image(x, y, c);
        if (actual != expected) {
            fprintf(stderr, "Failure @ %d %d %d: expected %d, got %d\n", x, y, c, expected, actual);
            exit(-1);
        }
    });
}

//-----------------------------------------------------------------------------

auto sanitizercoverage_wrapper(struct halide_buffer_t *out) {
    enable_callbacks = true;
    auto status = sanitizercoverage(out);
    enable_callbacks = false;
    return status;
}

int main() {
    fprintf(stderr, "Entering main().\n");
    auto out = Buffer<int8_t, 3>(4, 4, 3);
    fprintf(stderr, "Clearing output buffer.\n");
    clear_out(out);
    fprintf(stderr, "Performing the transformation.\n");
    if (sanitizercoverage_wrapper(out) != 0) {
        fprintf(stderr, "Failure!\n");
        exit(-1);
    }
    fprintf(stderr, "Verifying the transformation.\n");
    verify_out(out);
    // We rely on the callbacks being called and printing Success.
    return 0;
}
