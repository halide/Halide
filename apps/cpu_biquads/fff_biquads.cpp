// The biquad cascade as a strided 1-D filter, built from the "Finding Fast
// Filters" (Ma et al.) template library vendored under fff/. With the
// channels innermost, a block of STRIDE channels is one 1-D signal of
// stride STRIDE: each section is a sparse FIR with taps at 0, STRIDE and
// 2*STRIDE for the numerator, cascaded with the library's serial
// second-order IIR at that stride for the denominator. Compiled with clang
// (the library uses its vector extensions); the runner calls it through
// the C interface below.

#include <memory>
#include <vector>
#include <cstring>
#include <utility>

#include "fff/FloatVector.h"
#include "fff/CircularBuffer.h"
#include "fff/SparseFIR.h"
#include "fff/Cascade.h"
#include "fff/DenseFIR.h"
#include "fff/IIR.h"

#ifndef FFF_STRIDE
#define FFF_STRIDE 32
#endif
#ifndef SECTIONS
#define SECTIONS 8
#endif

namespace {

constexpr int STRIDE = FFF_STRIDE;
constexpr int N = SECTIONS;
constexpr int RATE = STRIDE / vec_lanes;  // vectors per sample of all channels
static_assert(STRIDE % vec_lanes == 0);

using FIR = SparseFIR<RATE, 0, STRIDE, 2 * STRIDE>;
// The pairwise form takes two samples a step with a shorter dependency
// chain; the library has it for one-vector strides only.
#if FFF_PAIRWISE
static_assert(STRIDE == vec_lanes);
using IIR = IIR2Pairwise<STRIDE>;
#else
using IIR = IIR2Serial<STRIDE>;
#endif

// sos is 6 floats per section: b0 b1 b2 a0 a1 a2, with a0 == 1, so that
// y = b0 x + b1 x[-1] + b2 x[-2] - a1 y[-1] - a2 y[-2].
auto make_section(const float *sos, int k) {
    const float *s = sos + 6 * k;
    return Cascade(FIR({s[0], s[1], s[2]}), IIR(-s[4], -s[5]));
}

template<size_t... I>
auto make_cascade(const float *sos, std::index_sequence<I...>) {
    return Cascade(make_section(sos, (int)I)...);
}

using Filter = decltype(make_cascade(nullptr, std::make_index_sequence<N>{}));

struct State {
    // One filter per block of channels, built once and reset per run.
    std::vector<std::unique_ptr<Filter>> filters;
};

}  // namespace

extern "C" {

void *fff_biquads_create(int nblocks, const float *sos) {
    State *st = new State;
    for (int b = 0; b < nblocks; b++) {
        st->filters.emplace_back(new Filter(make_cascade(sos, std::make_index_sequence<N>{})));
    }
    return st;
}

int fff_biquads_stride() {
    return STRIDE;
}

// x and y are block-major: block b's samples are S rows of STRIDE floats,
// channels innermost, at x + b * S * STRIDE. The caller deals blocks to
// threads.
void fff_biquads_run_block(void *handle, int b, const float *x, float *y, long S) {
    State *st = (State *)handle;
    Filter &f = *st->filters[b];
    f.reset();
    const float_vec *in = (const float_vec *)(x + (size_t)b * S * STRIDE);
    float_vec *out = (float_vec *)(y + (size_t)b * S * STRIDE);
    // A step is the cascade's rate in vectors, one or more samples.
    constexpr int STEP = Filter::rate;
    const long nvec = S * RATE;
    for (long v = 0; v < nvec; v += STEP) {
        f.run(in + v, out + v);
    }
}

void fff_biquads_destroy(void *handle) {
    delete (State *)handle;
}
}
