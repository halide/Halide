#include <stdio.h>
#include <Halide.h>

const float pi = 3.14159265f;

using namespace Halide;

// Complex number math. Complex numbers are represented with Tuples.

Expr re(Tuple z) {
    return z[0];
}

Expr im(Tuple z) {
    return z[1];
}

Tuple add(Tuple za, Tuple zb) {
    return Tuple(za[0] + zb[0], za[1] + zb[1]);
}

Tuple sub(Tuple za, Tuple zb) {
    return Tuple(za[0] - zb[0], za[1] - zb[1]);
}

Tuple mul(Tuple za, Tuple zb) {
    return Tuple(za[0]*zb[0] - za[1]*zb[1], za[0]*zb[1] + zb[0]*za[1]);
}

Tuple scale(Expr x, Tuple z) {
    return Tuple(x*z[0], x*z[1]);
}

// Compute exp(j*x)
Tuple expj(Expr x) {
    return Tuple(cos(x), sin(x));
}

Tuple conj(Tuple z) {
    return Tuple(z[0], -z[1]);
}

// Some helpers for doing basic Halide operations with complex numbers.
Tuple sumz(Tuple z, const std::string &s = "sum") {
    return Tuple(sum(z[0], s + "_re"), sum(z[1], s + "_im"));
}

Tuple selectz(Expr cond, Tuple t, Tuple f) {
    return Tuple(select(cond, t[0], f[0]), select(cond, t[1], f[1]));
}

Tuple selectz(Expr cond0, Tuple t0,
              Expr cond1, Tuple t1,
              Expr cond2, Tuple t2,
              Tuple f) {
    return Tuple(select(cond0, t0[0],
                        cond1, t1[0],
                        cond2, t2[0],
                        f[0]),
                 select(cond0, t0[1],
                        cond1, t1[1],
                        cond2, t2[1],
                        f[1]));
}


// Compute the complex DFT of size N on the first dimension of x.
Func dft(Func x, int N, int sign) {
    Var n("n");
    Func ret("dft");
    switch (N) {
    case 2:
        ret(n, _) = selectz(n == 0, add(x(0, _), x(1, _)),
                                    sub(x(0, _), x(1, _)));
        break;
/*
    case 4:
        ret(n, _) = selectz(n == 0, add(x(0, _), x(2, _)),
                            n == 1, add(x(1, _), x(3, _)),
                            n == 2, sub(x(0, _), x(2, _)),
                                    Tuple(x(1, _)[1] - x(3, _)[1], x(3, _)[0], x(1, _)[0]));
        break;
*/
/*
    case 8:
        ret(n, _) = selectz(n == 0, add(x(0, _), x(2, _)),
                            n == 1, add(x(1, _), x(3, _)),
                            n == 2, sub(x(0, _), x(2, _)),
                                    Tuple(x(1, _)[1] - x(3, _)[1], x(3, _)[0], x(1, _)[0]));
        break;
*/
    default:
        RDom k(0, N);
        ret(n, _) = sumz(mul(expj((sign*2*pi*k*n)/N), x(k, _)));
    }
    return ret;
}

// This 2D FFT is an implementation of the algorithm described in
// http://research.microsoft.com/pubs/131400/fftgpusc08.pdf

// Compute the N point DFT of dimension 1 (columns) of x using radix
// R.  We're going to use column FFTs for everything because they
// vectorize well (by doing vector-width FFTs simultaneously).
Func fft_dim1(Func x, int N, int R, int sign) {
    Var n0("n0"), n1("n1");
    Func out = x;
    for (int S = 1; S < N; S *= R) {
        Var j("j"), r("r");

        Expr w = (sign*2*pi*(j%S))/(S*R);
        Func v("v");
        v(r, j, n0) = mul(expj(r*w), out(n0, j + r*(N/R)));
        v.bound(r, 0, R);
        v.bound(j, 0, N/R);
        v.compute_root();

        v = dft(v, R, sign);
        v.compute_root();

        Func temp("temp");
        temp(n0, n1) = Tuple(undef<float>(), undef<float>());
        RDom rj(0, R, 0, N/R);
        temp(n0, (rj.x + (rj.y/S)*R)*S + rj.y%S) = v(rj.x, rj.y, n0);

        out = temp;
    }

    return out;
}

// Compute the NxN 2D DFT of the first two dimensions of real valued x
// using radix R.

// Note that the output is transposed, and the transform domain is
// N/2+1 x N due to the conjugate symmetry of real FFTs.
Func fft2d_r2c(Func x, int N0, int R0, int N1, int R1) {
    Var n0("n0"), n1("n1");

    // Combine pairs of real columns into complex columns.
    Func zip_cols("zip_cols");
    zip_cols(n0, n1) = Tuple(x(n0*2 + 0, n1),
                             x(n0*2 + 1, n1));

    // DFT down the columns first.
    Func dft1 = fft_dim1(zip_cols, N1, R1, -1);

    // Unzip the DFTs of the columns.
    Func unzip_cols;
    // The input to the dft was z = x + j*y.
    // Due to linearity of the DFT, Z = X + j*Y where Z = F[z], X = F[x], ...
    // Computing Z_n + conj(Z_(N-n)) and Z_n - conj(Z_(N-n)) give X_n and Y_n.
    Tuple Z = dft1(n0/2, n1);
    Tuple symZ = dft1(n0/2, (N1 - n1)%N1);
    Tuple X = scale(0.5f, add(Z, conj(symZ)));
    Tuple Y = mul(Tuple(0, -0.5f), sub(Z, conj(symZ)));
    unzip_cols(n0, n1) = selectz(n0%2 == 0, X, Y);

    // Transpose the tile so we can DFT dimension 0 (by making it dimension 1).
    Func transposed;
    transposed(n0, n1) = unzip_cols(n1, n0);

    // DFT down the columns again to get the DFT of the tiles.
    return fft_dim1(transposed, N0, R0, -1);
}

// Z_n + conj(Z_(N-n)) = X_n + j*Y_n + conj(X_(N-n) + j*Y_(N-n))
//                     = X_n + j*Y_n + conj(X_(N-n)) - j*conj(Y_(N-n))
//                     = X_n + j*Y_n + X_n - j*Y_n
//                     = 2*X_n

// X_n = (Z_n + conj(Z_(N-n)))/2

// Z_n - conj(Z_(N-n)) = X_n + j*Y_n - conj(X_(N-n) + j*Y_(N-n))
//                     = X_n + j*Y_n - conj(X_(N-n)) + j*conj(Y_(N-n))
//                     = X_n + j*Y_n - X_n + j*Y_n
//                     = 2*j*Y_n

// Y_n = (Z_n - conj(Z_(N-n)))/(2*j)


// Compute the NxN 2D inverse DFT of the first two dimensions of real
// valued x using radix R.
// Note that the input is transposed, and the input domain is
// N/2+1 x N due to the conjugate symmetry of real FFTs.
Func fft2d_c2r(Func x, int N0, int R0, int N1, int R1) {
    Var n0("n0"), n1("n1");

    // Take the inverse DFT of the columns.
    Func dft1 = fft_dim1(x, N0, R0, 1);

    // Transpose so we can take the DFT of the columns again.
    Func transposed;
    transposed(n0, n1) = dft1(n1, n0);

    // Zip two real DFTs X and Y into one complex DFT Z = X + j*Y
    Func zipped;
    Tuple X = selectz(n1 < N1/2 + 1, transposed(n0*2 + 0, n1), conj(transposed(n0*2 + 0, (N1 - n1)%N1)));
    Tuple Y = selectz(n1 < N1/2 + 1, transposed(n0*2 + 1, n1), conj(transposed(n0*2 + 1, (N1 - n1)%N1)));
    zipped(n0, n1) = add(X, mul(Tuple(0.0f, 1.0f), Y));

    Func dft = fft_dim1(zipped, N1, R1, 1);

    Func unzipped;
    unzipped(n0, n1) = select(n0%2 == 0,
                              re(dft(n0/2, n1)),
                              im(dft(n0/2, n1)));

    return unzipped;
}

// Compute the N0 x N1 2D complex DFT of complex valued x using
// radixes R0, R1. sign = -1 indicates a forward DFT, sign = 1
// indicates an inverse DFT.

// Note that the output DFT is transposed.
Func fft2d_c2c(Func x, int N0, int R0, int N1, int R1, int sign) {
    Func dft1 = fft_dim1(x, N1, R1, sign);

    Func transposed;
    Var n0("n0"), n1("n1");
    transposed(n0, n1) = dft1(n1, n0);

    return fft_dim1(transposed, N0, R0, sign);
}

// The naive version just uses radix 2 FFTs. N0, N1 must be a power of 2.
Func fft2d_r2c(Func x, int N0, int N1) {
    return fft2d_r2c(x, N0, 2, N1, 2);
}
Func fft2d_c2r(Func x, int N0, int N1) {
    return fft2d_c2r(x, N0, 2, N1, 2);
}
Func fft2d_c2c(Func x, int N0, int N1, int sign) {
    return fft2d_c2c(x, N0, 2, N1, 2, sign);
}

template <typename T>
Func make_real(Image<T> &img) {
    Var x, y;
    Func ret;
    ret(x, y) = img(x, y);
    return ret;
}

template <typename T>
Func make_complex(Image<T> &img) {
    Var x, y;
    Func ret;
    ret(x, y) = Tuple(img(x, y), 0.0f);
    return ret;
}

int main(int argc, char **argv) {

    const int W = 64, H = 32;

    Image<float> in(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            in(x, y) = rand() % 50 + 10;
        }
    }

    // Construct a box filter kernel.
    Image<float> kernel(W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            kernel(x, y) = std::min(x, W - x) <= 1 && std::min(y, H - y) <= 1 ? 1.0f/9.0f : 0.0f;
        }
    }

    Var x("x"), y("y");

#if 0
    Func dft_in = fft2d_c2c(make_complex(in), W, H, -1);
    Func dft_kernel = fft2d_c2c(make_complex(kernel), W, H, -1);

    Func dft_filtered;
    dft_filtered(x, y) = mul(dft_kernel(x, y), dft_in(x, y));

    Func dft_out = fft2d_c2c(dft_filtered, W, H, 1);

    Func filtered;
    filtered(x, y) = re(dft_out(x, y));
#else
    Func dft_in = fft2d_r2c(make_real(in), W, H);
    Func dft_kernel = fft2d_r2c(make_real(kernel), W, H);

    Func dft_filtered;
    dft_filtered(x, y) = mul(dft_in(x, y), dft_kernel(x, y));

    Func filtered = fft2d_c2r(dft_filtered, W, H);
#endif

    // Normalize the result.
    RDom xy(0, W, 0, H);
    filtered(xy.x, xy.y) /= cast<float>(W*H);

    Target target = get_target_from_environment();

    Image<float> result = filtered.realize(W, H, target);

    for (int y = 1; y < H-1; y++) {
        for (int x = 1; x < W-1; x++) {
            float correct = 0;
            for (int i = -1; i <= 1; i++) {
                for (int j = -1; j <= 1; j++) {
                    correct += in(x + j, y + i);
                }
            }
            correct /= 9.0f;
            if (fabs(result(x, y) - correct) > 1e-4f) {
                printf("result(%d, %d) = %f instead of %f\n", x, y, result(x, y), correct);
                return -1;
            }
        }
    }

    printf("Success!\n");

    return 0;

}
