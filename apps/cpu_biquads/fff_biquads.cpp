// The biquad cascade as a strided 1-D filter, built from the "Finding Fast
// Filters" template library (Ma, Adams and Ragan-Kelley,
// https://arxiv.org/abs/2607.20634) vendored under fff/. With the
// channels innermost, a block of STRIDE channels is one 1-D signal of
// stride STRIDE: each section is a sparse FIR with taps at 0, STRIDE and
// 2*STRIDE for the numerator, cascaded with the library's serial
// second-order IIR at that stride for the denominator. The library uses
// clang's vector extensions; the runner uses it through the class in
// fff_biquads.h.

#include <memory>
#include <vector>
#include <cstring>
#include <utility>

#include "fff_biquads.h"

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

}  // namespace

struct FffBiquads::Impl {
    // One filter per block of channels, built once and reset per run.
    std::vector<std::unique_ptr<Filter>> filters;
};

int FffBiquads::stride() {
    return STRIDE;
}

FffBiquads::FffBiquads(int nblocks, const std::vector<float> &sos)
    : impl(new Impl) {
    for (int b = 0; b < nblocks; b++) {
        impl->filters.emplace_back(new Filter(make_cascade(sos.data(), std::make_index_sequence<N>{})));
    }
}

FffBiquads::~FffBiquads() = default;

void FffBiquads::run_block(int b, const float *x, float *y, long S) {
    Filter &f = *impl->filters[b];
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
