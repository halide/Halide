// The biquad cascade built from the "Finding Fast Filters" template
// library (Ma, Adams and Ragan-Kelley, https://arxiv.org/abs/2607.20634),
// behind a header that keeps the library's template types in
// fff_biquads.cpp.
#ifndef FFF_BIQUADS_H
#define FFF_BIQUADS_H

#include <memory>
#include <vector>

class FffBiquads {
public:
    // The channels a block spans: a block is one 1-D signal of that stride.
    static int stride();

    // One filter per block of channels, for a cascade whose sections are
    // six floats each (b0 b1 b2 a0 a1 a2, a0 == 1).
    FffBiquads(int nblocks, const std::vector<float> &sos);
    ~FffBiquads();

    // Filter block b of a block-major signal: block b's samples are S rows
    // of stride() floats, channels innermost, at x + b * S * stride().
    // Resets the block's state first. Blocks are independent, so they can
    // run on different threads.
    void run_block(int b, const float *x, float *y, long S);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

#endif
