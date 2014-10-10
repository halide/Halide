// This FFT is an implementation of the algorithm described in
// http://research.microsoft.com/pubs/131400/fftgpusc08.pdf
// This algorithm is more well suited to Halide than in-place
// algorithms.

#include <stdio.h>
#include <Halide.h>
#include <vector>
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

Tuple selectz(Expr c, Tuple t, Tuple f) {
    return Tuple(select(c, re(t), re(f)), select(c, im(t), im(f)));
}

// Compute the product of the integers in R.
int product(const std::vector<int> &R) {
    int p = 1;
    for (size_t i = 0; i < R.size(); i++) {
        p *= R[i];
    }
    return p;
}

void add_implicit_args(std::vector<Var> &defined, Func implicit) {
    // Add implicit args for each argument missing in defined from
    // implicit's args.
    for (int i = 0; defined.size() < implicit.dimensions(); i++) {
        defined.push_back(Var::implicit(i));
    }
}

std::vector<Var> add_implicit_args(Var x0, Func implicit) {
    std::vector<Var> ret;
    ret.push_back(x0);
    add_implicit_args(ret, implicit);
    return ret;
}

std::vector<Var> add_implicit_args(Var x0, Var x1, Func implicit) {
    std::vector<Var> ret;
    ret.push_back(x0);
    ret.push_back(x1);
    add_implicit_args(ret, implicit);
    return ret;
}

// Find the first argument of f that is a placeholder, or outermost if
// no placeholders are found.
Var outermost(Func f) {
    for (size_t i = 0; i < f.dimensions(); i++) {
        if (f.args()[i].is_implicit()) {
            return f.args()[i];
        }
    }
    return Var::outermost();
}

// Compute the complex DFT of size N on dimension 0 of x.
Func dft_dim0(Func x, int N, float sign) {
    Var n("n");
    Func X("X");
    if (N < 10) {
        // If N is small, unroll the loop.
        Tuple dft = x(0, _);
        for (int k = 1; k < N; k++) {
            dft = add(dft, mul(expj((sign*2*pi*k*n)/N), x(k, _)));
        }
        X(n, _) = dft;
    } else {
        // If N is larger, we really shouldn't be using this algorithm for the DFT anyways.
        RDom k(0, N);
        X(n, _) = sumz(mul(expj((sign*2*pi*k*n)/N), x(k, _)));
    }
    X.unroll(n);
    return X;
}

// Specializations for some small DFTs.
Func dft2_dim0(Func x, float sign) {
    Var n("n");
    Func X("X2_dim0");
    X(add_implicit_args(n, x)) = Tuple(undef<float>(), undef<float>());

    Tuple x0 = x(0, _), x1 = x(1, _);
    FuncRefExpr X0 = X(0, _), X1 = X(1, _);

    X0 = add(x0, x1);
    X1 = sub(x0, x1);

    return X;
}

Func dft4_dim0(Func x, float sign) {
    Var n("n");
    Func X("X");
    X(add_implicit_args(n, x)) = Tuple(undef<float>(), undef<float>());

    Tuple x0 = x(0, _), x1 = x(1, _), x2 = x(2, _), x3 = x(3, _);
    FuncRefExpr X0 = X(0, _), X1 = X(1, _), X2 = X(2, _), X3 = X(3, _);

    FuncRefExpr T0 = X(-1, _);
    FuncRefExpr T2 = X(-2, _);
    T0 = add(x0, x2);
    T2 = add(x1, x3);
    X0 = add(T0, T2);
    X2 = sub(T0, T2);

    FuncRefExpr T1 = T0;
    FuncRefExpr T3 = T2;
    T1 = sub(x0, x2);
    T3 = mul(sub(x1, x3), Tuple(0.0f, sign)); // W = j*sign
    X1 = add(T1, T3);
    X3 = sub(T1, T3);

    return X;
}

Func dft8_dim0(Func x, float sign) {
    const float sqrt2_2 = 0.70710678f;

    Var n("n");
    Func X("X");
    X(add_implicit_args(n, x)) = Tuple(undef<float>(), undef<float>());

    Tuple x0 = x(0, _), x1 = x(1, _), x2 = x(2, _), x3 = x(3, _);
    Tuple x4 = x(4, _), x5 = x(5, _), x6 = x(6, _), x7 = x(7, _);
    FuncRefExpr X0 = X(0, _), X1 = X(1, _), X2 = X(2, _), X3 = X(3, _);
    FuncRefExpr X4 = X(4, _), X5 = X(5, _), X6 = X(6, _), X7 = X(7, _);

    FuncRefExpr T0 = X(-1, _), T1 = X(-2, _), T2 = X(-3, _), T3 = X(-4, _);
    FuncRefExpr T4 = X(-5, _), T5 = X(-6, _), T6 = X(-7, _), T7 = X(-8, _);

    X0 = add(x0, x4);
    X2 = add(x2, x6);
    T0 = add(X0, X2);
    T2 = sub(X0, X2);

    X1 = sub(x0, x4);
    X3 = mul(sub(x2, x6), Tuple(0.0f, sign));
    T1 = add(X1, X3);
    T3 = sub(X1, X3);

    X4 = add(x1, x5);
    X6 = add(x3, x7);
    T4 = add(X4, X6);
    T6 = mul(sub(X4, X6), Tuple(0.0f, sign));

    X5 = sub(x1, x5);
    X7 = mul(sub(x3, x7), Tuple(0.0f, sign));
    T5 = mul(add(X5, X7), Tuple(sqrt2_2, sign*sqrt2_2));
    T7 = mul(sub(X5, X7), Tuple(-sqrt2_2, sign*sqrt2_2));

    X0 = add(T0, T4);
    X1 = add(T1, T5);
    X2 = add(T2, T6);
    X3 = add(T3, T7);
    X4 = sub(T0, T4);
    X5 = sub(T1, T5);
    X6 = sub(T2, T6);
    X7 = sub(T3, T7);

    return X;
}

std::map<int, Func> twiddles;

// Return a function computing the twiddle factors.
Func W(int N, float sign) {
    // Check to see if this set of twiddle factors is already computed.
    Func &w = twiddles[N*(int)sign];

    Var n("n");
    if (!w.defined()) {
        Func W("W");
        W(n) = expj((sign*2*pi*n)/N);
        Realization compute_static = W.realize(N);
        Image<float> reW = compute_static[0];
        Image<float> imW = compute_static[1];
        w(n) = Tuple(reW(n), imW(n));
    }

    return w;
}

// Compute the N point DFT of dimension 1 (columns) of x using
// radix R.
Func fft_dim1(Func x, const std::vector<int> &NR, float sign, int group_size = 8) {
    int N = product(NR);
    Var n0("n0"), n1("n1");

    std::vector<Func> stages;

    RVar r_, s_;
    int S = 1;
    for (size_t i = 0; i < NR.size(); i++) {
        int R = NR[i];

        std::stringstream stage_id;
        stage_id << "S" << S << "_R" << R;

        Func exchange("x_" + stage_id.str());
        Var r("r"), s("s");

        // Load the points from each subtransform and apply the
        // twiddle factors. Twiddle factors for S = 1 are all expj(0) = 1.
        Func v("v_" + stage_id.str());
        Tuple x_rs = x(n0, s + r*(N/R), _);
        if (S > 1) {
            Func W_RS = W(R*S, sign);
            v(r, s, n0, _) = mul(selectz(r > 0, W_RS(r*(s%S)), Tuple(1.0f, 0.0f)), x_rs);
        } else {
            v(r, s, n0, _) = x_rs;
        }

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
        exchange(add_implicit_args(n0, n1, x)) = Tuple(undef<float>(), undef<float>());
        exchange.bound(n1, 0, N);

        RDom rs(0, R, 0, N/R);
        r_ = rs.x;
        s_ = rs.y;
        exchange(n0, (s_/S)*R*S + s_%S + r_*S, _) = V(r_, s_, n0, _);

        if (S > 1) {
            v.compute_at(exchange, s_).unroll(r);
            v.reorder_storage(n0, r, s);
        }

        V.compute_at(exchange, s_);
        V.reorder_storage(V.args()[2], V.args()[0], V.args()[1]);

        // TODO: Understand why these all vectorize in all but the last stage.
        if (S == N/R) {
            v.vectorize(n0);
            V.vectorize(V.args()[2]);
            for (int i = 0; i < V.num_update_definitions(); i++) {
                V.update(i).vectorize(V.args()[2]);
            }
        }

        exchange.update().unroll(r_);
        // Remember this stage for scheduling later.
        stages.push_back(exchange);

        x = exchange;
        S *= R;
    }

    // Split the tile into groups of DFTs, and vectorize within the
    // group.
    Var group("g");
    x.update().split(n0, group, n0, group_size).reorder(n0, r_, s_, group).vectorize(n0);
    for (size_t i = 0; i < stages.size() - 1; i++) {
        stages[i].compute_at(x, group).update().vectorize(n0);
    }

    return x;
}

// Transpose the first two dimensions of x.
Func transpose(Func f) {
    std::vector<Var> argsT(f.args());
    std::swap(argsT[0], argsT[1]);
    Func fT;
    fT(argsT) = f(f.args());
    return fT;
}

// Compute the N0 x N1 2D complex DFT of x using radixes R0, R1.
// sign = -1 indicates a forward DFT, sign = 1 indicates an inverse
// DFT.
Func fft2d_c2c(Func x, const std::vector<int> &R0, const std::vector<int> &R1, float sign) {
    // Vectorization width.
    const int group = 8;

    // Transpose the input to the FFT.
    Func xT = transpose(x);

    // Compute the DFT of dimension 1 (originally dimension 0).
    Func dft1T = fft_dim1(xT, R0, sign, group);

    // Transpose back.
    Func dft1 = transpose(dft1T);

    // Compute the DFT of dimension 1.
    Func dft = fft_dim1(dft1, R1, sign, group);
    dft.bound(dft.args()[0], 0, product(R0));
    dft.bound(dft.args()[1], 0, product(R1));

    dft1T.compute_at(dft, outermost(dft));
    dft.compute_root();
    return dft;
}

// Compute the N0 x N1 2D real DFT of x using radixes R0, R1.
// The transform domain has dimensions N0 x N1/2+1 due to the
// conjugate symmetry of real DFTs.
Func fft2d_r2c(Func r, const std::vector<int> &R0, const std::vector<int> &R1) {
    // How many columns to group together in one FFT. This is the
    // vectorization width.
    const int group = 8;

    int N0 = product(R0);
    int N1 = product(R1);

    Var n0("n0"), n1("n1");

    // Combine pairs of real columns x, y into complex columns
    // z = x + j*y. This allows us to compute two real DFTs using
    // one complex FFT.

    // Grab columns from each half of the input data to improve
    // coherency of the zip/unzip operations, which improves
    // vectorization.
    // The zip location is aligned to the nearest group.
    int zip_n = ((N0/2 - 1) | (group - 1)) + 1;
    Func zipped("zipped");
    zipped(n0, n1, _) = Tuple(r(n0, n1, _),
                              r(clamp(n0 + zip_n, 0, N0 - 1), n1, _));

    // DFT down the columns first.
    Func dft1 = fft_dim1(zipped, R1, -1.0f, group);

    // Unzip the DFTs of the columns.
    Func unzipped("unzipped");
    // By linearity of the DFT, Z = X + j*Y, where X, Y, and Z are the
    // DFTs of x, y and z.

    // By the conjugate symmetry of real DFTs, computing Z_n +
    // conj(Z_(N-n)) and Z_n - conj(Z_(N-n)) gives 2*X_n and 2*j*Y_n,
    // respectively.
    Tuple Z = dft1(n0%zip_n, n1, _);
    Tuple symZ = dft1(n0%zip_n, (N1 - n1)%N1, _);
    Tuple X = add(Z, conj(symZ));
    Tuple Y = mul(Tuple(0.0f, -1.0f), sub(Z, conj(symZ)));
    unzipped(n0, n1, _) = scale(0.5f, selectz(n0 < zip_n, X, Y));

    // Transpose so we can FFT dimension 0 (by making it dimension 1).
    Func unzippedT = transpose(unzipped);

    // DFT down the columns again (the rows of the original).
    Func dftT = fft_dim1(unzippedT, R0, -1.0f, group);
    // Transpose back.
    Func dft = transpose(dftT);
    dft.bound(dft.args()[0], 0, N0);
    dft.bound(dft.args()[1], 0, N1/2 + 1);

    unzipped.compute_at(dftT, Var("g")).vectorize(n0, group).unroll(n0);
    dft1.compute_at(dftT, outermost(dft));
    dftT.compute_at(dft, outermost(dft));
    dft.compute_root();

    return dft;
}

// Compute the N0 x N1 2D inverse DFT of x using radixes R0, R1.
// The DFT domain should have dimensions N0 x N1/2 + 1 due to the
// conjugate symmetry of real FFTs.
Func fft2d_c2r(Func c,  const std::vector<int> &R0, const std::vector<int> &R1) {
    // How many columns to group together in one FFT. This is the
    // vectorization width.
    const int group = 8;

    int N0 = product(R0);
    int N1 = product(R1);

    Var n0("n0"), n1("n1");

    // Transpose the input.
    Func cT = transpose(c);
    // Take the inverse DFT of the columns (rows in the final result).
    Func dft0T = fft_dim1(cT, R0, 1.0f, group);

    // Transpose so we can take the DFT of the columns again.
    Func dft0 = transpose(dft0T);

    // Zip two real DFTs X and Y into one complex DFT Z = X + j*Y
    Func zipped("zipped");
    // Construct the whole DFT domain of X and Y via conjugate
    // symmetry. At n1 = N1/2, both branches are equal (the dft is
    // real, so the conjugate is a no-op), so the slightly less
    // intuitive form of this expression still works, but vectorizes
    // more cleanly than n1 <= N1/2.
    Tuple X = selectz(n1 < N1/2,
                      dft0(n0, clamp(n1, 0, N1/2), _),
                      conj(dft0(n0, clamp((N1 - n1)%N1, 0, N1/2), _)));

    // The zip point is roughly half of the domain, aligned up to the
    // nearest group.
    int zip_n = ((N0/2 - 1) | (group - 1)) + 1;

    Expr n0_Y = n0 + zip_n;
    if (zip_n*2 != N0) {
        // When the group-aligned zip location isn't exactly half of
        // the domain, we need to clamp excess accesses.
        n0_Y = clamp(n0_Y, 0, N0 - 1);
    }
    Tuple Y = selectz(n1 < N1/2,
                      dft0(n0_Y, clamp(n1, 0, N1/2), _),
                      conj(dft0(n0_Y, clamp((N1 - n1)%N1, 0, N1/2), _)));
    zipped(n0, n1, _) = add(X, mul(Tuple(0.0f, 1.0f), Y));

    // Take the inverse DFT of the columns again.
    Func dft = fft_dim1(zipped, R1, 1.0f, group);

    // Extract the real inverse DFTs.
    Func unzipped("unzipped");
    unzipped(n0, n1, _) = select(n0 < zip_n,
                                 re(dft(n0%zip_n, n1, _)),
                                 im(dft(n0%zip_n, n1, _)));
    unzipped.bound(n0, 0, N0);
    unzipped.bound(n1, 0, N1);

    dft0.compute_at(dft, outermost(dft)).vectorize(dft0.args()[0], group).unroll(dft0.args()[0]);
    dft0T.compute_at(dft, outermost(dft));
    dft.compute_at(unzipped, outermost(unzipped));

    unzipped.compute_root().vectorize(n0, group).unroll(n0);
    return unzipped;
}

// Compute an integer factorization of N made up of composite numbers
std::vector<int> radix_factor(int N) {
    const int radices[] = { 8, 5, 4, 3, 2 };

    std::vector<int> R;
    for (int i = 0; i < sizeof(radices)/sizeof(radices[0]); i++) {
        while (N % radices[i] == 0) {
            R.push_back(radices[i]);
            N /= radices[i];
        }
    }
    if (N != 1 || R.empty()) {
        R.push_back(N);
    }

    return R;
}

// Compute the N0 x N1 2D complex DFT of x. sign = -1 indicates a
// forward DFT, sign = 1 indicates an inverse DFT.
Func fft2d_c2c(Func c, int N0, int N1, float sign) {
    return fft2d_c2c(c, radix_factor(N0), radix_factor(N1), sign);
}

// Compute N0 x N1 real DFTs.
Func fft2d_r2c(Func r, int N0, int N1) {
    return fft2d_r2c(r, radix_factor(N0), radix_factor(N1));
}
Func fft2d_c2r(Func c, int N0, int N1) {
    return fft2d_c2r(c, radix_factor(N0), radix_factor(N1));
}


template <typename T>
Func make_real(const Image<T> &re) {
    Var x, y;
    Func ret;
    ret(x, y) = re(x, y);
    return ret;
}

template <typename T>
Func make_complex(const Image<T> &re) {
    Var x, y;
    Func ret;
    ret(x, y) = Tuple(re(x, y), 0.0f);
    return ret;
}

template <typename T>
Func make_complex(const Image<T> &re, const Image<T> &im) {
    Var x, y;
    Func ret;
    ret(x, y) = Tuple(re(x, y), im(x, y));
    return ret;
}

double log2(double x) {
    return log(x)/log(2.0);
}

int main(int argc, char **argv) {
    const int W = 32, H = 32;

    // Generate a random image to convolve with.
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
            int u = x < (W - x) ? x : (W - x);
            int v = y < (H - y) ? y : (H - y);
            kernel(x, y) = u <= box/2 && v <= box/2 ? 1.0f/(box*box) : 0.0f;
        }
    }

    Target target = get_jit_target_from_environment();

    Var x("x"), y("y");

    Func filtered_c2c;
    {
        // Compute the DFT of the input and the kernel.
        Func dft_in = fft2d_c2c(make_complex(in), W, H, -1);
        Func dft_kernel = fft2d_c2c(make_complex(kernel), W, H, -1);

        // Compute the convolution.
        Func dft_filtered("dft_filtered");
        dft_filtered(x, y) = mul(dft_in(x, y), dft_kernel(x, y));

        // Compute the inverse DFT to get the result.
        Func dft_out = fft2d_c2c(dft_filtered, W, H, 1);

        // Extract the real component and normalize.
        filtered_c2c(x, y) = re(dft_out(x, y))/cast<float>(W*H);
    }

    Func filtered_r2c;
    {
        // Compute the DFT of the input and the kernel.
        Func dft_in = fft2d_r2c(make_real(in), W, H);
        Func dft_kernel = fft2d_r2c(make_real(kernel), W, H);

        // Compute the convolution.
        Func dft_filtered("dft_filtered");
        dft_filtered(x, y) = mul(dft_in(x, y), dft_kernel(x, y));
        //dft_filtered.compute_root().trace_realizations();

        // Compute the inverse DFT to get the result.
        filtered_r2c = fft2d_c2r(dft_filtered, W, H);

        // Normalize the result.
        RDom xy(0, W, 0, H);
        filtered_r2c(xy.x, xy.y) /= cast<float>(W*H);
    }

    Image<float> result_c2c = filtered_c2c.realize(W, H, target);
    Image<float> result_r2c = filtered_r2c.realize(W, H, target);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float correct = 0;
            for (int i = -box/2; i <= box/2; i++) {
                for (int j = -box/2; j <= box/2; j++) {
                    correct += in((x + j + W)%W, (y + i + H)%H);
                }
            }
            correct /= box*box;
            if (fabs(result_c2c(x, y) - correct) > 1e-6f) {
                printf("result_c2c(%d, %d) = %f instead of %f\n", x, y, result_c2c(x, y), correct);
                return -1;
            }
            if (fabs(result_r2c(x, y) - correct) > 1e-6f) {
                printf("result_r2c(%d, %d) = %f instead of %f\n", x, y, result_r2c(x, y), correct);
                return -1;
            }
        }
    }

    // For a description of the methodology used here, see
    // http://www.fftw.org/speed/method.html

    // Take the minimum time over many of iterations to minimize
    // noise.
    const int samples = 10;
    const int reps = 1000;
    double t = 1e6f;

    Var rep("rep");

    Image<float> re_in = lambda(x, y, 0.0f).realize(W, H);
    Image<float> im_in = lambda(x, y, 0.0f).realize(W, H);

    Func c2c_in;
    // Read all reps from the same place in memory. This effectively
    // benchmarks taking the FFT of cached inputs, which is a
    // reasonable assumption for a well optimized program with good
    // locality.
    c2c_in(x, y, rep) = Tuple(re_in(x, y), im_in(x, y));
    Func bench_c2c = fft2d_c2c(c2c_in, W, H, -1);
    Realization R_c2c = bench_c2c.realize(W, H, reps, target);
    // Write all reps to the same place in memory. This means the
    // output appears to be cached on all but the first
    // iteration. This seems to match the behavior of FFTW's benchmark
    // code, and it is again a reasonable assumption for a well
    // optimized real world program.
    R_c2c[0].raw_buffer()->stride[2] = 0;
    R_c2c[1].raw_buffer()->stride[2] = 0;

    for (int i = 0; i < samples; i++) {
        double t1 = current_time();
        bench_c2c.realize(R_c2c, target);
        double dt = (current_time() - t1)*1e3/reps;
        if (dt < t) t = dt;
    }
    printf("c2c  time: %f us, %f MFLOP/s\n", t, 5*W*H*(log2(W) + log2(H))/t);

    Func r2c_in;
    // All reps read from the same input. See notes on c2c_in.
    r2c_in(x, y, rep) = re_in(x, y);
    Func bench_r2c = fft2d_r2c(r2c_in, W, H);
    Realization R_r2c = bench_r2c.realize(W, H/2 + 1, reps, target);
    // Write all reps to the same place in memory. See notes on R_c2c.
    R_r2c[0].raw_buffer()->stride[2] = 0;
    R_r2c[1].raw_buffer()->stride[2] = 0;

    t = 1e6f;
    for (int i = 0; i < samples; i++) {
        double t1 = current_time();
        bench_r2c.realize(R_r2c, target);
        double dt = (current_time() - t1)*1e3/reps;
        if (dt < t) t = dt;
    }
    printf("r2c time: %f us, %f MFLOP/s\n", t, 2.5*W*H*(log2(W) + log2(H))/t);

    Func c2r_in;
    // All reps read from the same input. See notes on c2c_in.
    c2r_in(x, y, rep) = Tuple(re_in(x, y), im_in(x, y));
    Func bench_c2r = fft2d_c2r(c2r_in, W, H);
    Realization R_c2r = bench_c2r.realize(W, H, reps, target);
    // Write all reps to the same place in memory. See notes on R_c2c.
    R_c2r[0].raw_buffer()->stride[2] = 0;

    t = 1e6f;
    for (int i = 0; i < samples; i++) {
        double t1 = current_time();
        bench_c2r.realize(R_c2r, target);
        double dt = (current_time() - t1)*1e3/reps;
        if (dt < t) t = dt;
    }
    printf("c2r time: %f us, %f MFLOP/s\n", t, 2.5*W*H*(log2(W) + log2(H))/t);

    twiddles.clear();

    return 0;
}
