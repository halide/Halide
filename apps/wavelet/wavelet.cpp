#include "Halide.h"

using namespace Halide;

Var x, y, c;

/*
class Lambda {
public:
    Lambda(Var x, Var y) : x(x), y(y) {
    }

    Func operator=(Expr rhs) {
        f(x, y) = rhs;
        return f;
    }

private:
    Var x, y;
    Func f;
};
*/

Func haar_x(Func in) {
    Func out;
    out(x, y) = (in(2*x, y) + in(2*x+1, y),
                 in(2*x, y) - in(2*x+1, y))/2;
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

Func daubechies_x(Func in) {
    Func out;
    out(x, y) = (D0*in(2*x-1, y) + D1*in(2*x, y) + D2*in(2*x+1, y) + D3*in(2*x+2, y),
                 D3*in(2*x-1, y) - D2*in(2*x, y) + D1*in(2*x+1, y) - D0*in(2*x+2, y));
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

    UniformImage image(Float(32), 2);    
    UniformImage wavelet(Float(32), 3);

    // Add a boundary condition for daubechies
    Func clamped;
    clamped(x, y) = image(clamp(x, 0, image.width()-1),
                          clamp(y, 0, image.height()-1));
    Func wavelet_clamped;
    wavelet_clamped(x, y, c) = wavelet(clamp(x, 0, wavelet.width()-1),
                                       clamp(y, 0, wavelet.height()-1), c);

    Func inv_haar_x = inverse_haar_x(wavelet_clamped);
    if (use_gpu()) inv_haar_x.cudaTile(x, y, 8, 8);
    inv_haar_x.compileToFile("inverse_haar_x");

    Func for_haar_x = haar_x(clamped);
    if (use_gpu()) for_haar_x.cudaTile(x, y, 8, 8);
    for_haar_x.compileToFile("haar_x");

    Func inv_daub_x = inverse_daubechies_x(wavelet_clamped);
    if (use_gpu()) inv_daub_x.cudaTile(x, y, 8, 8);
    inv_daub_x.compileToFile("inverse_daubechies_x");

    Func for_daub_x = daubechies_x(clamped);
    if (use_gpu()) for_daub_x.cudaTile(x, y, 8, 8);
    for_daub_x.compileToFile("daubechies_x");

    return 0;
}



