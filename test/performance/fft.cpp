#include <stdio.h>
#include <Halide.h>
#include "clock.h"

const float pi = 3.14159265f;

using namespace Halide;

// Complex number arithmetic. Complex numbers are represented with
// Halide Tuples.
Expr re(Tuple z) {
    return z[0];
}

Expr im(Tuple z) {
    return z[1];
}

Tuple add(Tuple za, Tuple zb) {
    return Tuple(re(za) + re(zb), im(za) + im(zb));
}

Tuple sub(Tuple za, Tuple zb) {
    return Tuple(re(za) - re(zb), im(za) - im(zb));
}

Tuple mul(Tuple za, Tuple zb) {
    return Tuple(re(za)*re(zb) - im(za)*im(zb), re(za)*im(zb) + re(zb)*im(za));
}

// Scalar multiplication.
Tuple scale(Expr x, Tuple z) {
    return Tuple(x*re(z), x*im(z));
}

Tuple conj(Tuple z) {
    return Tuple(re(z), -im(z));
}

// Compute exp(j*x)
Tuple expj(Expr x) {
    return Tuple(cos(x), sin(x));
}

// Some helpers for doing basic Halide operations with complex numbers.
Tuple sumz(Tuple z, const std::string &s = "sum") {
    return Tuple(sum(re(z), s + "_re"), sum(im(z), s + "_im"));
}

Tuple selectz(Expr cond, Tuple t, Tuple f) {
    return Tuple(select(cond, re(t), re(f)), select(cond, im(t), im(f)));
}

Tuple selectz(Expr cond0, Tuple t0,
              Expr cond1, Tuple t1,
              Expr cond2, Tuple t2,
              Tuple f) {
    return Tuple(select(cond0, re(t0),
                        cond1, re(t1),
                        cond2, re(t2),
                               re(f)),
                 select(cond0, im(t0),
                        cond1, im(t1),
                        cond2, im(t2),
                               im(f)));
}

// Compute the complex DFT of size N on dimension 0 of x.
Func dft_dim0(Func x, int N, int sign) {
    Var n("n");
    Func ret("dft_dim0");
    switch (N) {
    case 2:
        ret(n, _) = selectz(n == 0, add(x(0, _), x(1, _)),
                          /*n == 1*/sub(x(0, _), x(1, _)));
        break;
    default:
        // For unknown N, use the naive DFT.
        RDom k(0, N);
        ret(n, _) = sumz(mul(expj((sign*2*pi*k*n)/N), x(k, _)));
    }
    return ret;
}

// This FFT is an implementation of the algorithm described in
// http://research.microsoft.com/pubs/131400/fftgpusc08.pdf

// Compute the N point DFT of dimension 1 (columns) of x using
// radix R.
Func fft_dim1(Func x, int N, int R, int sign) {
    Var n0("n0"), n1("n1");

    std::vector<Func> stages;

    for (int S = 1; S < N; S *= R) {
        Var j("j"), r("r");

        // Twiddle factors. compute_root is good, "compute_static"
        // would be better.
        Func W("W");
        W(r, j) = expj((sign*2*pi*j*r)/(S*R));
        W.compute_root();

        // Load the points from each subtransform and apply the
        // twiddle factors.
        Func v("v");
        v(r, j, n0, _) = mul(W(r, j%S), x(n0, j + r*(N/R), _));

        // Compute the R point DFT of the subtransform.
        Func V = dft_dim0(v, R, sign);

        // Write the subtransform and use it as input to the next
        // pass.
        Func temp("temp");
#if 0
        temp(x.args()) = Tuple(undef<float>(), undef<float>());
        RDom rj(0, R, 0, N/R);
        RVar r_ = rj.x;
        RVar j_ = rj.y;
        temp(n0, r_*S + (j_/S)*S*R + j_%S, _) = V(r_, j_, n0, _);
#else
        Expr r_ = (n1/S)%R;
        Expr j_ = S*(n1/(R*S)) + n1%S; //(n1 - r_*S)/R;
        //r_ = print_when(n0 == 0, r_, j_, "n:", n1, "S:", S, "r");
        temp(n0, n1, _) = V(r_, j_, n0, _);
#endif

        stages.push_back(temp);

        x = temp;
    }

    Var n0i, n0o;
    x.compute_root().split(n0, n0o, n0i, 16).reorder(n0i, n1, n0o).vectorize(n0i);
    for (std::vector<Func>::iterator i = stages.begin(); i + 1 != stages.end(); ++i) {
        i->compute_at(x, n0o).vectorize(n0);
    }
    return x;
}

// Transpose the first two dimensions of x.
Func transpose(Func x) {
    Var i("i"), j("j");
    Func xT;
    xT(j, i, _) = x(i, j, _);
    return xT;
}

// Compute the N0 x N1 2D complex DFT of x using radixes R0, R1.
// sign = -1 indicates a forward DFT, sign = 1 indicates an inverse
// DFT.
Func fft2d_c2c(Func x, int N0, int R0, int N1, int R1, int sign) {
    // Compute the DFT of dimension 1.
    Func dft1 = fft_dim1(x, N1, R1, sign);

    // Transpose.
    Func dft1T = transpose(dft1);

    // Compute the DFT of dimension 1 (was dimension 0).
    Func dftT = fft_dim1(dft1T, N0, R0, sign);

    // Transpose back.
    return transpose(dftT);
}

// Compute the N0 x N1 2D complex DFT of x. N0, N1 must be a power of
// 2. sign = -1 indicates a forward DFT, sign = 1 indicates an inverse
// DFT.
Func fft2d_c2c(Func c, int N0, int N1, int sign) {
    return fft2d_c2c(c, N0, 2, N1, 2, sign);
}

// Compute the N0 x N1 2D real DFT of x using radixes R0, R1.
// Note that the transform domain is transposed and has dimensions
// N1/2+1 x N0 due to the conjugate symmetry of real DFTs.
Func fft2d_r2cT(Func r, int N0, int R0, int N1, int R1) {
    Var n0("n0"), n1("n1");

    // Combine pairs of real columns x, y into complex columns z = x + j*y.
    Func zipped("zipped");
    zipped(n0, n1, _) = Tuple(r(n0*2 + 0, n1, _),
                              r(n0*2 + 1, n1, _));

    // DFT down the columns first.
    Func dft1 = fft_dim1(zipped, N1, R1, -1);

    // Unzip the DFTs of the columns.
    Func unzipped("unzipped");
    // By linearity of the DFT, Z = X + j*Y, where X, Y, and Z are the
    // DFTs of x, y and z.

    // By the conjugate symmetry of real DFTs, computing Z_n +
    // conj(Z_(N-n)) and Z_n - conj(Z_(N-n)) gives 2*X_n and 2*j*Y_n,
    // respectively.
    Tuple Z = dft1(n0/2, n1, _);
    Tuple symZ = dft1(n0/2, (N1 - n1)%N1, _);
    Tuple X = scale(0.5f, add(Z, conj(symZ)));
    Tuple Y = mul(Tuple(0, -0.5f), sub(Z, conj(symZ)));
    unzipped(n0, n1, _) = selectz(n0%2 == 0, X, Y);

    // Transpose so we can DFT dimension 0 (by making it dimension 1).
    Func transposed = transpose(unzipped);

    // DFT down the columns again (the rows of the original).
    return fft_dim1(transposed, N0, R0, -1);
}

// Compute the N0 x N1 2D inverse DFT of x using radixes R0, R1.
// Note that the input domain is transposed and should have dimensions
// N1/2+1 x N0 due to the conjugate symmetry of real FFTs.
Func fft2d_cT2r(Func cT, int N0, int R0, int N1, int R1) {
    Var n0("n0"), n1("n1");

    // Take the inverse DFT of the columns (rows in the final result).
    Func dft1T = fft_dim1(cT, N0, R0, 1);

    // Transpose so we can take the DFT of the columns again.
    Func dft1 = transpose(dft1T);

    // Zip two real DFTs X and Y into one complex DFT Z = X + j*Y
    Func zipped("zipped");
    // Construct the whole DFT domain of X and Y via conjugate
    // symmetry.
    Tuple X = selectz(n1 < N1/2 + 1,
                      dft1(n0*2 + 0, clamp(n1, 0, N1/2), _),
                      conj(dft1(n0*2 + 0, clamp((N1 - n1)%N1, 0, N1/2), _)));
    Tuple Y = selectz(n1 < N1/2 + 1,
                      dft1(n0*2 + 1, clamp(n1, 0, N1/2), _),
                      conj(dft1(n0*2 + 1, clamp((N1 - n1)%N1, 0, N1/2), _)));
    zipped(n0, n1, _) = add(X, mul(Tuple(0.0f, 1.0f), Y));

    // Take the inverse DFT of the columns again.
    Func dft = fft_dim1(zipped, N1, R1, 1);

    // Extract the real inverse DFTs.
    Func unzipped("unzipped");
    unzipped(n0, n1, _) = select(n0%2 == 0,
                                 re(dft(n0/2, n1, _)),
                                 im(dft(n0/2, n1, _)));

    return unzipped;
}

// Compute N0 x N1 real DFTs.
Func fft2d_r2cT(Func r, int N0, int N1) {
    return fft2d_r2cT(r, N0, 2, N1, 2);
}
Func fft2d_cT2r(Func cT, int N0, int N1) {
    return fft2d_cT2r(cT, N0, 2, N1, 2);
}


template <typename T>
Func make_real(Image<T> &img) {
    Var x, y, z;
    Func ret;
    switch (img.dimensions()) {
    case 2: ret(x, y) = img(x, y); break;
    case 3: ret(x, y, z) = img(x, y, z); break;
    }
    return ret;
}

template <typename T>
Func make_complex(Image<T> &img) {
    Var x, y, z;
    Func ret;
    switch (img.dimensions()) {
    case 2: ret(x, y) = Tuple(img(x, y), 0.0f); break;
    case 3: ret(x, y, z) = Tuple(img(x, y, z), 0.0f); break;
    }
    return ret;
}

double log2(double x) {
    return log(x)/log(2.0);
}

int main(int argc, char **argv) {

    // Generate a random image to convolve with.
    const int W = 64, H = 64;

    Image<float> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = (float)rand()/(float)RAND_MAX;
        }
    }

    // Construct a box filter kernel centered on the origin.
    const int box = 3;
    Image<float> kernel(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            int u = std::min(x, W - x);
            int v = std::min(y, H - y);
            kernel(x, y) = u <= box/2 && v <= box/2 ? 1.0f/(box*box) : 0.0f;
        }
    }

    Var x("x"), y("y");

    Func filtered_r2c;
    {
        // Compute the DFT of the input and the kernel.
        Func dft_in = fft2d_r2cT(make_real(in), W, H);
        Func dft_kernel = fft2d_r2cT(make_real(kernel), W, H);

        // Compute the convolution.
        Func dft_filtered;
        dft_filtered(x, y) = mul(dft_in(x, y), dft_kernel(x, y));

        // Compute the inverse DFT to get the result.
        filtered_r2c = fft2d_cT2r(dft_filtered, W, H);

        // Normalize the result.
        RDom xy(0, W, 0, H);
        filtered_r2c(xy.x, xy.y) /= cast<float>(W*H);
    }

    Func filtered_c2c;
    {
        // Compute the DFT of the input and the kernel.
        Func dft_in = fft2d_c2c(make_complex(in), W, H, -1);
        Func dft_kernel = fft2d_c2c(make_complex(kernel), W, H, -1);

        // Compute the convolution.
        Func dft_filtered;
        dft_filtered(x, y) = mul(dft_in(x, y), dft_kernel(x, y));

        // Compute the inverse DFT to get the result.
        Func dft_out = fft2d_c2c(dft_filtered, W, H, 1);

        // Extract the real component and normalize.
        filtered_c2c(x, y) = re(dft_out(x, y))/cast<float>(W*H);
    }

    Target target = get_target_from_environment();
    Image<float> result_r2c = filtered_r2c.realize(W, H, target);
    Image<float> result_c2c = filtered_c2c.realize(W, H, target);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float correct = 0;
            for (int i = -box/2; i <= box/2; i++) {
                for (int j = -box/2; j <= box/2; j++) {
                    correct += in((x + j + W)%W, (y + i + H)%H);
                }
            }
            correct /= box*box;
            if (fabs(result_r2c(x, y) - correct) > 1e-6f) {
                printf("result_r2c(%d, %d) = %f instead of %f\n", x, y, result_r2c(x, y), correct);
                return -1;
            }
            if (fabs(result_c2c(x, y) - correct) > 1e-6f) {
                printf("result_c2c(%d, %d) = %f instead of %f\n", x, y, result_c2c(x, y), correct);
                return -1;
            }
        }
    }

    // http://www.fftw.org/speed/method.html
    const int reps = 1000;

    Realization R_r2c = filtered_r2c.realize(W, H, target);
    Realization R_c2c = filtered_c2c.realize(W, H, target);

    double t1 = current_time();
    for (int i = 0; i < reps; i++) {
        filtered_r2c.realize(R_r2c, target);
    }
    double t = (current_time() - t1)/reps;
    printf("r2c time: %f ms, %f GFLOP/s\n", t, 2*(2.5*W*H*(log2(W) + log2(H)))/t*1e3*1e-9);

    t1 = current_time();
    for (int i = 0; i < reps; i++) {
        filtered_c2c.realize(R_c2c, target);
    }
    t = (current_time() - t1)/reps;
    printf("c2c time: %f ms, %f GFLOP/s\n", t, 2*(5*W*H*(log2(W) + log2(H)))/t*1e3*1e-9);

    return 0;
}
