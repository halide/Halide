#ifndef HALIDE_FFT_H
#define HALIDE_FFT_H

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "Halide.h"
#include "complex.h"

// This is an optional extra description for the details of computing an FFT.
struct Fft2dDesc {
    // Gain to apply to the FFT. This is folded into gains already being applied
    // to the FFT when possible.
    Halide::Expr gain = 1.0f;

    // The following option specifies that a particular vector width should be
    // used when the vector width can change the results of the FFT.
    // Some parts of the FFT algorithm use the vector width to change the way
    // floating point operations are ordered and grouped, which causes the results
    // to vary with respect to the target architecture. Setting this option forces
    // such stages to use the specified vector width (independent of the actual
    // architecture's vector width), which eliminates the architecture specific
    // behavior.
    int vector_width = 0;

    // The following option indicates that the FFT should parallelize within a
    // single FFT. This only makes sense to use on large FFTs, and generally only
    // if there is no outer loop around FFTs that can be parallelized.
    bool parallel = false;

    // This option will schedule the input to the FFT at the innermost location
    // that makes sense.
    bool schedule_input = false;

    // A name to prepend to the name of the Funcs the FFT defines.
    std::string name = "";
};

// Compute the N0 x N1 2D complex DFT of the first 2 dimensions of a complex
// valued function x. The first 2 dimensions of x should be defined on at least
// [0, N0) and [0, N1) for dimensions 0, 1, respectively. sign = -1 indicates a
// forward FFT, sign = 1 indicates an inverse FFT. There is no normalization of
// the FFT in either direction, i.e.:
//
//   X = fft2d_c2c(x, N0, N1, -1);
//   x = fft2d_c2c(X, N0, N1, 1) / (N0 * N1);
ComplexFunc fft2d_c2c(ComplexFunc x, int N0, int N1, int sign,
                      const Halide::Target &target,
                      const Fft2dDesc &desc = Fft2dDesc());

// Compute the N0 x N1 2D complex DFT of the first 2 dimensions of a real valued
// function r. The first 2 dimensions of r should be defined on at least [0, N0)
// and [0, N1) for dimensions 0, 1, respectively. Note that the transform domain
// has dimensions N0 x N1 / 2 + 1 due to the conjugate symmetry of real DFTs.
// There is no normalization.
ComplexFunc fft2d_r2c(Halide::Func r, int N0, int N1,
                      const Halide::Target &target,
                      const Fft2dDesc &desc = Fft2dDesc());

// Compute the real valued N0 x N1 2D inverse DFT of dimensions 0, 1 of c. Note
// that the transform domain has dimensions N0 x N1 / 2 + 1 due to the conjugate
// symmetry of real DFTs. There is no normalization.
Halide::Func fft2d_c2r(ComplexFunc c, int N0, int N1,
                       const Halide::Target &target,
                       const Fft2dDesc &desc = Fft2dDesc());

#endif
