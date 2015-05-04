#include "Halide.h"

using namespace Halide;

Var x, y, c;

Func haar_x(Func in) {
    Func out;
    out(x, y, c) = select(c == 0,
                          (in(2*x, y) + in(2*x+1, y)),
                          (in(2*x, y) - in(2*x+1, y)))/2;
    out.unroll(c, 2);
    return out;
}

Func inverse_haar_x(Func in) {
    Func out;
    out(x, y) = select(x%2 == 0,
                       in(x/2, y, 0) + in(x/2, y, 1),
                       in(x/2, y, 0) - in(x/2, y, 1));
    out.unroll(x, 2);
    return out;
}


const float D0 = 0.4829629131445341f;
const float D1 = 0.83651630373780772f;
const float D2 = 0.22414386804201339f;
const float D3 = -0.12940952255126034f;

/*
const float D0 = 0.34150635f;
const float D1 = 0.59150635f;
const float D2 = 0.15849365f;
const float D3 = -0.1830127f;
*/

Func daubechies_x(Func in) {
    Func out;
    out(x, y, c) = select(c == 0,
                          D0*in(2*x-1, y) + D1*in(2*x, y) + D2*in(2*x+1, y) + D3*in(2*x+2, y),
                          D3*in(2*x-1, y) - D2*in(2*x, y) + D1*in(2*x+1, y) - D0*in(2*x+2, y));
    out.unroll(c, 2);
    return out;
}

Func inverse_daubechies_x(Func in) {
    Func out;
    out(x, y) = select(x%2 == 0,
                       D2*in(x/2, y, 0) + D1*in(x/2, y, 1) + D0*in(x/2+1, y, 0) + D3*in(x/2+1, y, 1),
                       D3*in(x/2, y, 0) - D0*in(x/2, y, 1) + D1*in(x/2+1, y, 0) - D2*in(x/2+1, y, 1));
    out.unroll(x, 2);
    return out;
}

int main(int argc, char **argv) {

    ImageParam image(Float(32), 2);
    ImageParam wavelet(Float(32), 3);

    // Add a boundary condition for daubechies
    Func clamped = BoundaryConditions::repeat_edge(image);

    Func wavelet_clamped = BoundaryConditions::repeat_edge(wavelet);

    Func inv_haar_x = inverse_haar_x(wavelet_clamped);
    inv_haar_x.compile_to_file("inverse_haar_x", {wavelet});

    Func for_haar_x = haar_x(clamped);
    for_haar_x.compile_to_file("haar_x", {image});

    Func inv_daub_x = inverse_daubechies_x(wavelet_clamped);
    inv_daub_x.compile_to_file("inverse_daubechies_x", {wavelet});

    Func for_daub_x = daubechies_x(clamped);
    for_daub_x.compile_to_file("daubechies_x", {image});

    return 0;
}
