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

// Halide complex number class.
class ComplexExpr {
public:
    Expr x, y;

    ComplexExpr(Tuple z) : x(z[0]), y(z[1]) {}
    ComplexExpr(Expr x = 0.0f, Expr y = 0.0f) : x(x), y(y) {}
    ComplexExpr(float x, float y = 0.0f) : x(x), y(y) {}

    Expr re() { return x; }
    Expr im() { return y; }

    operator Tuple() const { return Tuple(x, y); }
};

Expr re(ComplexExpr z) { return z.re(); }
Expr im(ComplexExpr z) { return z.im(); }
Expr re(Expr x) { return x; }
Expr im(Expr x) { return 0.0f; }

ComplexExpr j(0.0f, 1.0f);
ComplexExpr undef_z(undef<float>(), undef<float>());

// Unary negation.
ComplexExpr operator -(ComplexExpr z) { return ComplexExpr(-re(z), -im(z)); }
// ComplexExpr conjugate.
ComplexExpr operator ~(ComplexExpr z) { return ComplexExpr(re(z), -im(z)); }
// Complex arithmetic.
ComplexExpr operator + (ComplexExpr a, ComplexExpr b) { return ComplexExpr(re(a) + re(b), im(a) + im(b)); }
ComplexExpr operator - (ComplexExpr a, ComplexExpr b) { return ComplexExpr(re(a) - re(b), im(a) - im(b)); }
ComplexExpr operator * (ComplexExpr a, ComplexExpr b) { return ComplexExpr(re(a)*re(b) - im(a)*im(b),
                                                                           re(a)*im(b) + im(a)*re(b)); }

// Compute exp(j*x)
ComplexExpr expj(Expr x) {
    return ComplexExpr(cos(x), sin(x));
}

// Some helpers for doing basic Halide operations with complex numbers.
ComplexExpr sum(ComplexExpr z, const std::string &s = "sum") {
    return ComplexExpr(sum(re(z), s + "_re"), sum(im(z), s + "_im"));
}

ComplexExpr select(Expr c, ComplexExpr t, ComplexExpr f) {
    return ComplexExpr(select(c, re(t), re(f)), select(c, im(t), im(f)));
}

typedef FuncT<ComplexExpr> ComplexFunc;
typedef FuncRefExprT<ComplexExpr> ComplexFuncRefExpr;

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

std::vector<Var> add_implicit_args(Var x0, const Func &implicit) {
    std::vector<Var> ret;
    ret.push_back(x0);
    add_implicit_args(ret, implicit);
    return ret;
}

std::vector<Var> add_implicit_args(Var x0, Var x1, const Func &implicit) {
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

// Compute the complex DFT of size N on dimension 0 of x. This is slow,
// if this is being used, consider implementing a specialization for N.
ComplexFunc dft_dim0(ComplexFunc x, int N, float sign) {
    Var n("n");
    ComplexFunc X("X");
    RDom k(0, N);
    X(n, _) = sum(x(k, _)*expj((sign*2*pi*k*n)/N));
    X.unroll(n);
    return X;
}

// Specializations for some small DFTs.
ComplexFunc dft2_dim0(ComplexFunc x, float sign) {
    Var n("n");
    ComplexFunc X("X2_dim0");
    X(add_implicit_args(n, x)) = undef_z;

    ComplexExpr x0 = x(0, _), x1 = x(1, _);
    ComplexFuncRefExpr X0 = X(0, _), X1 = X(1, _);

    X0 = x0 + x1;
    X1 = x0 - x1;

    return X;
}

ComplexFunc dft4_dim0(ComplexFunc x, float sign) {
    Var n("n");
    ComplexFunc X("X");
    X(add_implicit_args(n, x)) = undef_z;

    ComplexExpr x0 = x(0, _), x1 = x(1, _), x2 = x(2, _), x3 = x(3, _);
    ComplexFuncRefExpr X0 = X(0, _), X1 = X(1, _), X2 = X(2, _), X3 = X(3, _);

    ComplexFuncRefExpr T0 = X(-1, _);
    ComplexFuncRefExpr T2 = X(-2, _);
    T0 = x0 + x2;
    T2 = x1 + x3;
    X0 = T0 + T2;
    X2 = T0 - T2;

    ComplexFuncRefExpr T1 = T0;
    ComplexFuncRefExpr T3 = T2;
    T1 = x0 - x2;
    T3 = (x1 - x3)*j*sign;
    X1 = T1 + T3;
    X3 = T1 - T3;

    return X;
}

ComplexFunc dft8_dim0(ComplexFunc x, float sign) {
    const float sqrt2_2 = 0.70710678f;

    Var n("n");
    ComplexFunc X("X");
    X(add_implicit_args(n, x)) = undef_z;

    ComplexExpr x0 = x(0, _), x1 = x(1, _), x2 = x(2, _), x3 = x(3, _);
    ComplexExpr x4 = x(4, _), x5 = x(5, _), x6 = x(6, _), x7 = x(7, _);
    ComplexFuncRefExpr X0 = X(0, _), X1 = X(1, _), X2 = X(2, _), X3 = X(3, _);
    ComplexFuncRefExpr X4 = X(4, _), X5 = X(5, _), X6 = X(6, _), X7 = X(7, _);

    ComplexFuncRefExpr T0 = X(-1, _), T1 = X(-2, _), T2 = X(-3, _), T3 = X(-4, _);
    ComplexFuncRefExpr T4 = X(-5, _), T5 = X(-6, _), T6 = X(-7, _), T7 = X(-8, _);

    X0 = x0 + x4;
    X2 = x2 + x6;
    T0 = X0 + X2;
    T2 = X0 - X2;

    X1 = x0 - x4;
    X3 = (x2 - x6)*j*sign;
    T1 = X1 + X3;
    T3 = X1 - X3;

    X4 = x1 + x5;
    X6 = x3 + x7;
    T4 = X4 + X6;
    T6 = (X4 - X6)*j*sign;

    X5 = x1 - x5;
    X7 = (x3 - x7)*j*sign;
    T5 = (X5 + X7)*ComplexExpr(sqrt2_2, sign*sqrt2_2);
    T7 = (X5 - X7)*ComplexExpr(-sqrt2_2, sign*sqrt2_2);

    X0 = T0 + T4;
    X1 = T1 + T5;
    X2 = T2 + T6;
    X3 = T3 + T7;
    X4 = T0 - T4;
    X5 = T1 - T5;
    X6 = T2 - T6;
    X7 = T3 - T7;

    return X;
}

std::map<int, ComplexFunc> twiddles;

// Return a function computing the twiddle factors.
ComplexFunc W(int N, float sign) {
    // Check to see if this set of twiddle factors is already computed.
    ComplexFunc &w = twiddles[N*(int)sign];

    Var n("n");
    if (!w.defined()) {
        ComplexFunc W("W");
        W(n) = expj((sign*2*pi*n)/N);
        Realization compute_static = W.realize(N);
        Image<float> reW = compute_static[0];
        Image<float> imW = compute_static[1];
        w(n) = ComplexExpr(reW(n), imW(n));
    }

    return w;
}

// Compute the N point DFT of dimension 1 (columns) of x using
// radix R.
ComplexFunc fft_dim1(ComplexFunc x, const std::vector<int> &NR, float sign, int group_size = 8) {
    int N = product(NR);
    Var n0("n0"), n1("n1");

    std::vector<ComplexFunc> stages;

    RVar r_, s_;
    int S = 1;
    for (size_t i = 0; i < NR.size(); i++) {
        int R = NR[i];

        std::stringstream stage_id;
        stage_id << "S" << S << "_R" << R;

        ComplexFunc exchange("x_" + stage_id.str());
        Var r("r"), s("s");

        // Load the points from each subtransform and apply the
        // twiddle factors. Twiddle factors for S = 1 are all expj(0) = 1.
        ComplexFunc v("v_" + stage_id.str());
        ComplexExpr x_rs = x(n0, s + r*(N/R), _);
        if (S > 1) {
            ComplexFunc W_RS = W(R*S, sign);
            v(r, s, n0, _) = x_rs*select(r > 0, W_RS(r*(s%S)), 1.0f);
        } else {
            v(r, s, n0, _) = x_rs;
        }

        // Compute the R point DFT of the subtransform.
        ComplexFunc V;
        switch (R) {
        case 2: V = dft2_dim0(v, sign); break;
        case 4: V = dft4_dim0(v, sign); break;
        case 8: V = dft8_dim0(v, sign); break;
        default: V = dft_dim0(v, R, sign); break;
        }

        // Write the subtransform and use it as input to the next
        // pass.
        exchange(add_implicit_args(n0, n1, x)) = undef_z;
        exchange.bound(n1, 0, N);

        RDom rs(0, R, 0, N/R);
        r_ = rs.x;
        s_ = rs.y;
        exchange(n0, (s_/S)*R*S + s_%S + r_*S, _) = V(r_, s_, n0, _);

        v.compute_at(exchange, s_).unroll(r);
        v.reorder_storage(n0, r, s);

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

// Transpose the first two dimensions of f.
template <typename T>
T transpose(T f) {
    std::vector<Var> argsT(f.args());
    std::swap(argsT[0], argsT[1]);
    T fT;
    fT(argsT) = f(f.args());
    return fT;
}

// Compute the N0 x N1 2D complex DFT of x using radixes R0, R1.
// sign = -1 indicates a forward DFT, sign = 1 indicates an inverse
// DFT.
ComplexFunc fft2d_c2c(ComplexFunc x, const std::vector<int> &R0, const std::vector<int> &R1, float sign) {
    // Transpose the input to the FFT.
    ComplexFunc xT = transpose(x);

    // Compute the DFT of dimension 1 (originally dimension 0).
    ComplexFunc dft1T = fft_dim1(xT, R0, sign);

    // Transpose back.
    ComplexFunc dft1 = transpose(dft1T);

    // Compute the DFT of dimension 1.
    ComplexFunc dft = fft_dim1(dft1, R1, sign);
    dft.bound(dft.args()[0], 0, product(R0));
    dft.bound(dft.args()[1], 0, product(R1));

    dft1T.compute_at(dft, outermost(dft));
    dft.compute_root();
    return dft;
}

// Compute the N0 x N1 2D real DFT of x using radixes R0, R1.
// Note that the transform domain is transposed and has dimensions
// N1/2+1 x N0 due to the conjugate symmetry of real DFTs.
ComplexFunc fft2d_r2cT(Func r, const std::vector<int> &R0, const std::vector<int> &R1) {
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
    ComplexFunc zipped("zipped");
    zipped(n0, n1, _) = ComplexExpr(r(n0, n1, _),
                                    r(clamp(n0 + zip_n, 0, N0 - 1), n1, _));

    // DFT down the columns first.
    ComplexFunc dft1 = fft_dim1(zipped, R1, -1.0f, group);

    // Unzip the DFTs of the columns.
    ComplexFunc unzipped("unzipped");
    // By linearity of the DFT, Z = X + j*Y, where X, Y, and Z are the
    // DFTs of x, y and z.

    // By the conjugate symmetry of real DFTs, computing Z_n +
    // conj(Z_(N-n)) and Z_n - conj(Z_(N-n)) gives 2*X_n and 2*j*Y_n,
    // respectively.
    ComplexExpr Z = dft1(n0%zip_n, n1, _);
    ComplexExpr symZ = dft1(n0%zip_n, (N1 - n1)%N1, _);
    ComplexExpr X = Z + ~symZ;
    ComplexExpr Y = -j*(Z - ~symZ);
    unzipped(n0, n1, _) = 0.5f*select(n0 < zip_n, X, Y);

    // Transpose so we can FFT dimension 0 (by making it dimension 1).
    ComplexFunc unzippedT = transpose(unzipped);

    // DFT down the columns again (the rows of the original).
    ComplexFunc dft = fft_dim1(unzippedT, R0, -1.0f, group);
    dft.bound(dft.args()[0], 0, (N1 + 1)/2 + 1);
    dft.bound(dft.args()[1], 0, N0);

    unzipped.compute_at(dft, Var("g")).vectorize(n0, 8).unroll(n0);
    dft1.compute_at(dft, outermost(dft));
    dft.compute_root();

    return dft;
}

// Compute the N0 x N1 2D inverse DFT of x using radixes R0, R1.
// Note that the input domain is transposed and should have dimensions
// N1/2+1 x N0 due to the conjugate symmetry of real FFTs.
Func fft2d_cT2r(ComplexFunc cT,  const std::vector<int> &R0, const std::vector<int> &R1) {
    // How many columns to group together in one FFT. This is the
    // vectorization width.
    const int group = 8;

    int N0 = product(R0);
    int N1 = product(R1);

    Var n0("n0"), n1("n1");

    // Take the inverse DFT of the columns (rows in the final result).
    ComplexFunc dft0T = fft_dim1(cT, R0, 1.0f, group);

    // Transpose so we can take the DFT of the columns again.
    ComplexFunc dft0 = transpose(dft0T);

    // Zip two real DFTs X and Y into one complex DFT Z = X + j*Y
    ComplexFunc zipped("zipped");
    // Construct the whole DFT domain of X and Y via conjugate
    // symmetry. At n1 = N1/2, both branches are equal (the dft is
    // real, so the conjugate is a no-op), so the slightly less
    // intuitive form of this expression still works, but vectorizes
    // more cleanly than n1 <= N1/2.
    ComplexExpr X = select(n1 < N1/2,
                           dft0(n0, clamp(n1, 0, N1/2), _),
                           ~dft0(n0, clamp((N1 - n1)%N1, 0, N1/2), _));

    // The zip point is roughly half of the domain, aligned up to the
    // nearest group.
    int zip_n = ((N0/2 - 1) | (group - 1)) + 1;

    Expr n0_Y = n0 + zip_n;
    if (zip_n*2 != N0) {
        // When the group-aligned zip location isn't exactly half of
        // the domain, we need to clamp excess accesses.
        n0_Y = clamp(n0_Y, 0, N0 - 1);
    }
    ComplexExpr Y = select(n1 < N1/2,
                           dft0(n0_Y, clamp(n1, 0, N1/2), _),
                           ~dft0(n0_Y, clamp((N1 - n1)%N1, 0, N1/2), _));
    zipped(n0, n1, _) = X + j*Y;

    // Take the inverse DFT of the columns again.
    ComplexFunc dft = fft_dim1(zipped, R1, 1.0f, group);

    // Extract the real inverse DFTs.
    Func unzipped("unzipped");
    unzipped(n0, n1, _) = select(n0 < zip_n,
                                 re(dft(n0%zip_n, n1, _)),
                                 im(dft(n0%zip_n, n1, _)));
    unzipped.bound(n0, 0, N0);
    unzipped.bound(n1, 0, N1);

    dft0.compute_at(dft, outermost(dft)).vectorize(dft0.args()[0], 8).unroll(dft0.args()[0]);
    dft0T.compute_at(dft, outermost(dft));
    dft.compute_at(unzipped, outermost(unzipped));

    unzipped.compute_root().vectorize(n0, 8).unroll(n0);
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
ComplexFunc fft2d_c2c(ComplexFunc c, int N0, int N1, float sign) {
    return fft2d_c2c(c, radix_factor(N0), radix_factor(N1), sign);
}

// Compute N0 x N1 real DFTs. The DFT is transposed.
ComplexFunc fft2d_r2cT(Func r, int N0, int N1) {
    return fft2d_r2cT(r, radix_factor(N0), radix_factor(N1));
}
Func fft2d_cT2r(ComplexFunc cT, int N0, int N1) {
    return fft2d_cT2r(cT, radix_factor(N0), radix_factor(N1));
}


template <typename T>
Func make_real(Image<T> &re) {
    Var x, y;
    Func ret;
    ret(x, y) = re(x, y);
    return ret;
}

template <typename T>
ComplexFunc make_complex(Image<T> &re) {
    Var x, y;
    ComplexFunc ret;
    ret(x, y) = re(x, y);
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
        ComplexFunc dft_in = fft2d_c2c(make_complex(in), W, H, -1);
        ComplexFunc dft_kernel = fft2d_c2c(make_complex(kernel), W, H, -1);

        // Compute the convolution.
        ComplexFunc dft_filtered("dft_filtered");
        dft_filtered(x, y) = dft_in(x, y)*dft_kernel(x, y);

        // Compute the inverse DFT to get the result.
        ComplexFunc dft_out = fft2d_c2c(dft_filtered, W, H, 1);

        // Extract the real component and normalize.
        filtered_c2c(x, y) = re(dft_out(x, y))/cast<float>(W*H);
    }

    Func filtered_r2c;
    {
        // Compute the DFT of the input and the kernel.
        ComplexFunc dft_in = fft2d_r2cT(make_real(in), W, H);
        ComplexFunc dft_kernel = fft2d_r2cT(make_real(kernel), W, H);

        // Compute the convolution.
        ComplexFunc dft_filtered("dft_filtered");
        dft_filtered(x, y) = dft_in(x, y)*dft_kernel(x, y);

        // Compute the inverse DFT to get the result.
        filtered_r2c = fft2d_cT2r(dft_filtered, W, H);

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

    ComplexFunc c2c_in;
    c2c_in(x, y, rep) = ComplexExpr(in(x, y), 0.0f);
    ComplexFunc bench_c2c = fft2d_c2c(c2c_in, W, H, -1);
    Realization R_c2c = bench_c2c.realize(W, H, reps, target);

    for (int i = 0; i < samples; i++) {
        double t1 = current_time();
        bench_c2c.realize(R_c2c, target);
        double dt = (current_time() - t1)*1e3/reps;
        if (dt < t) t = dt;
    }
    printf("c2c  time: %f us, %f MFLOP/s\n", t, 5*W*H*(log2(W) + log2(H))/t);

    Func r2cT_in;
    r2cT_in(x, y, rep) = in(x, y);
    ComplexFunc bench_r2cT = fft2d_r2cT(r2cT_in, W, H);
    // Due to padding for vectorization, this has asserts that fail,
    // but are harmless (padding is overwritten, but no more).
    if (!target.has_feature(Target::NoAsserts)) {
        ComplexFunc clamp_r2cT;
        clamp_r2cT(x, y, rep) = bench_r2cT(clamp(x, 0, H/2), y, rep);
        clamp_r2cT.compute_root().vectorize(x, 8);
        bench_r2cT = clamp_r2cT;
    }
    Realization R_r2cT = bench_r2cT.realize(H/2 + 1, W, reps, target);

    t = 1e6f;
    for (int i = 0; i < samples; i++) {
        double t1 = current_time();
        bench_r2cT.realize(R_r2cT, target);
        double dt = (current_time() - t1)*1e3/reps;
        if (dt < t) t = dt;
    }
    printf("r2cT time: %f us, %f MFLOP/s\n", t, 2.5*W*H*(log2(W) + log2(H))/t);

    Image<float> cT(H/2 + 1, W);
    memset(cT.data(), 0, cT.width()*cT.height()*sizeof(float));
    ComplexFunc cT2r_in;
    cT2r_in(x, y, rep) = ComplexExpr(cT(x, y), cT(x, y));
    Func bench_cT2r = fft2d_cT2r(cT2r_in, W, H);
    Realization R_cT2r = bench_cT2r.realize(W, H, reps, target);

    t = 1e6f;
    for (int i = 0; i < samples; i++) {
        double t1 = current_time();
        bench_cT2r.realize(R_cT2r, target);
        double dt = (current_time() - t1)*1e3/reps;
        if (dt < t) t = dt;
    }
    printf("cT2r time: %f us, %f MFLOP/s\n", t, 2.5*W*H*(log2(W) + log2(H))/t);

    twiddles.clear();

    return 0;
}
