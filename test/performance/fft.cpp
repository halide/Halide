// This FFT is an implementation of the algorithm described in
// http://research.microsoft.com/pubs/131400/fftgpusc08.pdf
// This algorithm is more well suited to Halide than in-place
// algorithms.

#include <stdio.h>
#include <Halide.h>
#include "clock.h"

const float pi = 3.14159265f;

using namespace Halide;

// Find the best radix to use for an FFT of size N. Currently, this is
// always 2.
int find_radix(int N) {
    return 2;
}

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

// Compute the complex DFT of size N on dimension 0 of x.
Func dft_dim0(Func x, int N, float sign) {
    Func ret("dft_dim0");
    Var n("n");
    RDom k(0, N);
    ret(n, _) = sumz(mul(expj((sign*2*pi*k*n)/N), x(k, _)));
    return ret;
}

// Specializations for some small DFTs.
Func dft2_dim0(Func x, float sign) {
    Var n("n");
    Func ret("dft2_dim0");
    ret(n, _) = selectz(n == 0, add(x(0, _), x(1, _)),
                                sub(x(0, _), x(1, _)));
    return ret;
}

Func dft4_dim0(Func x, float sign) {
    Var nx("nx"), ny("ny");
    Func s("dft4_dim0_s");
    s(nx, ny, _) = x(ny*2 + nx, _);

    Func s1("dft4_dim0_1");
    Tuple W3 = selectz(nx == 0, Tuple(1.0f, 0.0f), Tuple(0.0f, sign));
    s1(nx, ny, _) = selectz(ny == 0,
                            add(s(nx, 0, _), s(nx, 1, _)),
                            mul(sub(s(nx, 0, _), s(nx, 1, _)), W3));
    // The twiddle factor.
    //s1(1, 1, _) = mul(s1(1, 1, _), Tuple(0.0f, sign));

    Var n("n");
    Func s2("dft4_dim0_2");
    Expr nx_ = n/2;
    Expr ny_ = n%2;
    s2(n, _) = selectz(nx_ == 0,
                       add(s1(0, ny_, _), s1(1, ny_, _)),
                       sub(s1(0, ny_, _), s1(1, ny_, _)));

    return s2;
}

Func dft8_dim0(Func x, float sign) {
    return dft_dim0(x, 8, sign);
}

struct TwiddleParams {
    int R, S;
    float sign;

    bool operator < (const TwiddleParams &r) const {
        if (R < r.R) {
            return true;
        } else if (R > r.R) {
            return false;
        } else if (S < r.S) {
            return true;
        } else if (S > r.S) {
            return false;
        } else {
            return sign < r.sign;
        }
    }
};

// Return a function computing the twiddle factors.
Func W(int R, int S, float sign) {
    // Global cache of twiddle functions we've already computed.
    static std::map<TwiddleParams, Func> twiddles;

    // Check to see if this set of twiddle factors is already computed.
    TwiddleParams TP = { R, S, sign };
    std::map<TwiddleParams, Func>::iterator w = twiddles.find(TP);
    if (w != twiddles.end()) {
        return w->second;
    }

    Var i("i"), r("r");

    Func W("W");
    W(r, i) = expj((sign*2*pi*i*r)/(S*R));

    Realization compute_static = W.realize(R, S);
    Image<float> reW = compute_static[0];
    Image<float> imW = compute_static[1];

    Func ret;
    ret(r, i) = Tuple(reW(r, i), imW(r, i));

    // Cache this set of twiddle factors.
    twiddles[TP] = ret;

    return ret;
}

// Compute the N point DFT of dimension 1 (columns) of x using
// radix R.
Func fft_dim1(Func x, int N, int R, float sign) {
    Var n0("n0"), n1("n1");

    std::vector<Func> stages;

    for (int S = 1; S < N; S *= R) {
        Var i("i"), r("r");

        std::stringstream stage_id;
        stage_id << "S" << S << "_R" << R;

        // Twiddle factors.
        Func w = W(R, S, sign);

        // Load the points from each subtransform and apply the
        // twiddle factors.
        Func v("v_" + stage_id.str());
        v(r, i, n0, _) = mul(w(r, i%S), x(n0, i + r*(N/R), _));

        // Compute the R point DFT of the subtransform.
        Func V;
        switch (R) {
        case 2: V = dft2_dim0(v, sign); break;
        case 4: V = dft4_dim0(v, sign); break;
        case 8: V = dft8_dim0(v, sign); break;
        default: V = dft_dim0(v, R, sign); break;
        }

        // Write the subtransform and use it as input to the next
        // pass.
        Func exchange("exchange_" + stage_id.str());
        Expr r_ = (n1/S)%R;
        Expr i_ = S*(n1/(R*S)) + n1%S;
        exchange(n0, n1, _) = V(r_, i_, n0, _);

        // Remember this stage for scheduling later.
        stages.push_back(exchange);

        x = exchange;
    }

    // Split the tile into groups of DFTs, and vectorize within the
    // group.
    Var n0o;
    x.compute_root().split(n0, n0o, n0, 8).reorder(n0, n1, n0o).vectorize(n0);
    for (std::vector<Func>::iterator i = stages.begin(); i + 1 != stages.end(); ++i) {
        i->compute_at(x, n0o).vectorize(n0);
    }
    return x;
}

// Transpose the first two dimensions of x.
Func transpose(Func x) {
    Var i("i"), j("j");
    Func xT;
    xT(i, j, _) = x(j, i, _);
    //xT.compute_root();
    return xT;
}

// Compute the N0 x N1 2D complex DFT of x using radixes R0, R1.
// sign = -1 indicates a forward DFT, sign = 1 indicates an inverse
// DFT.
Func fft2d_c2c(Func x, int N0, int R0, int N1, int R1, float sign) {
    // Transpose the input to the FFT.
    Func xT = transpose(x);

    // Compute the DFT of dimension 1 (originally dimension 0).
    Func dft1T = fft_dim1(xT, N0, R0, sign);

    // Transpose back.
    Func dft1 = transpose(dft1T);

    // Compute the DFT of dimension 1.
    return fft_dim1(dft1, N1, R1, sign);
}

// Compute the N0 x N1 2D complex DFT of x. sign = -1 indicates a
// forward DFT, sign = 1 indicates an inverse DFT.
Func fft2d_c2c(Func c, int N0, int N1, float sign) {
    return fft2d_c2c(c, N0, find_radix(N0), N1, find_radix(N1), sign);
}

// Compute the N0 x N1 2D real DFT of x using radixes R0, R1.
// Note that the transform domain is transposed and has dimensions
// N1/2+1 x N0 due to the conjugate symmetry of real DFTs.
Func fft2d_r2cT(Func r, int N0, int R0, int N1, int R1) {
    Var n0("n0"), n1("n1");

    // Combine pairs of real columns x, y into complex columns
    // z = x + j*y. This allows us to compute two real DFTs using
    // one complex FFT.

    // Grab columns from each half of the input data to improve
    // coherency of the zip/unzip operations, which improves
    // vectorization.
    Func zipped("zipped");
    zipped(n0, n1, _) = Tuple(r(n0, n1, _),
                              r(n0 + N0/2, n1, _));

    // DFT down the columns first.
    Func dft1 = fft_dim1(zipped, N1, R1, -1);

    // Transpose so we can FFT dimension 0 (by making it dimension 1).
    Func dft1T = transpose(dft1);

    // Unzip the DFTs of the columns.
    Func unzippedT("unzippedT");
    // By linearity of the DFT, Z = X + j*Y, where X, Y, and Z are the
    // DFTs of x, y and z.

    // By the conjugate symmetry of real DFTs, computing Z_n +
    // conj(Z_(N-n)) and Z_n - conj(Z_(N-n)) gives 2*X_n and 2*j*Y_n,
    // respectively.
    Tuple Z = dft1T(n1, n0%(N0/2), _);
    Tuple symZ = dft1T((N1 - n1)%N1, n0%(N0/2), _);
    Tuple X = scale(0.5f, add(Z, conj(symZ)));
    Tuple Y = mul(Tuple(0, -0.5f), sub(Z, conj(symZ)));
    unzippedT(n1, n0, _) = selectz(n0 < N0/2, X, Y);
    //unzippedT.compute_root().vectorize(n1, 8);

    // DFT down the columns again (the rows of the original).
    return fft_dim1(unzippedT, N0, R0, -1);
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
                      dft1(n0, clamp(n1, 0, N1/2), _),
                      conj(dft1(n0, clamp((N1 - n1)%N1, 0, N1/2), _)));
    Tuple Y = selectz(n1 < N1/2 + 1,
                      dft1(n0 + N0/2, clamp(n1, 0, N1/2), _),
                      conj(dft1(n0 + N0/2, clamp((N1 - n1)%N1, 0, N1/2), _)));
    zipped(n0, n1, _) = add(X, mul(Tuple(0.0f, 1.0f), Y));
    zipped.compute_root().vectorize(n0, 8);

    // Take the inverse DFT of the columns again.
    Func dft = fft_dim1(zipped, N1, R1, 1);

    // Extract the real inverse DFTs.
    Func unzipped("unzipped");
    unzipped(n0, n1, _) = select(n0 < N0/2,
                                 re(dft(n0%(N0/2), n1, _)),
                                 im(dft(n0%(N0/2), n1, _)));
    unzipped.compute_root().vectorize(n0, 8);

    return unzipped;
}

// Compute N0 x N1 real DFTs.
Func fft2d_r2cT(Func r, int N0, int N1) {
    return fft2d_r2cT(r, N0, find_radix(N0), N1, find_radix(N1));
}
Func fft2d_cT2r(Func cT, int N0, int N1) {
    return fft2d_cT2r(cT, N0, find_radix(N0), N1, find_radix(N1));
}


template <typename T>
Func make_real(Image<T> &img) {
    Var x, y, z;
    Func ret;
    ret(x, y) = img(x, y);
    return ret;
}

template <typename T>
Func make_complex(Image<T> &img) {
    Var x, y, z;
    Func ret;
    ret(x, y) = Tuple(img(x, y), 0.0f);
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

    // For a description of the methodology used here, see
    // http://www.fftw.org/speed/method.html
    Func bench_r2c = fft2d_r2cT(make_real(in), W, H);
    Func bench_c2c = fft2d_c2c(make_complex(in), W, H, -1);

    Realization R_r2c = bench_r2c.realize(H/2 + 1, W, target);
    Realization R_c2c = bench_c2c.realize(W, H, target);

    // Take the minimum time over many of iterations to minimize
    // noise.
    const int reps = 20;
    double t = 1e6f;
    for (int i = 0; i < reps; i++) {
        double t1 = current_time();
        for (int j = 0; j < 10; j++) {
            bench_r2c.realize(R_r2c, target);
        }
        t = std::min((current_time() - t1)/10, t);
    }
    printf("r2c time: %f ms, %f MFLOP/s\n", t, 2.5*W*H*(log2(W) + log2(H))/t*1e3*1e-6);

    t = 1e6f;
    for (int i = 0; i < reps; i++) {
        double t1 = current_time();
        for (int j = 0; j < 10; j++) {
            bench_c2c.realize(R_c2c, target);
        }
        t = std::min((current_time() - t1)/10, t);
    }
    printf("c2c time: %f ms, %f MFLOP/s\n", t, 5*W*H*(log2(W) + log2(H))/t*1e3*1e-6);

    return 0;
}
