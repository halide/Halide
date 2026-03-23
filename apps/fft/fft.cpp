// This FFT is an implementation of the algorithm described in
// http://research.microsoft.com/pubs/131400/fftgpusc08.pdf. This
// algorithm is more well suited to Halide than in-place algorithms.

#include "fft.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <ostream>
#include <string>

#include "funct.h"

using std::string;
using std::vector;

using namespace Halide;
using namespace Halide::BoundaryConditions;

namespace {

#ifndef M_PI
#define M_PI 3.14159265358979310000
#endif
const float kPi = static_cast<float>(M_PI);

// This variable is used throughout the FFT code. It represents groups of
// columns which are being transformed.
Var group("g");

// Some useful constant complex numbers. Note this is defined as an integer, but
// can be transparently used with float ComplexExprs.
const ComplexExpr j(Expr(0), Expr(1));

// Make an undef ComplexExpr of the specified type.
ComplexExpr undef_z(Type t = Float(32)) {
    return ComplexExpr(undef(t), undef(t));
}

int gcd(int x, int y) {
    while (y != 0) {
        int r = x % y;
        x = y;
        y = r;
    }
    return x;
}

int lcm(int x, int y) {
    return std::min(x, y) * (std::max(x, y) / gcd(x, y));
}

// Compute the product of the integers in R.
int product(const vector<int> &R) {
    int p = 1;
    for (size_t i = 0; i < R.size(); i++) {
        p *= R[i];
    }
    return p;
}

// These tersely named functions concatenate vectors of Var/Expr for use
// in generating argument lists to Halide functions. They are named to avoid
// bloating the code, since these are used extremely frequently, and often many
// times within one line.
vector<Var> A(vector<Var> l, const vector<Var> &r) {
    for (const Var &i : r) {
        l.push_back(i);
    }
    return l;
}

template<typename T>
vector<Expr> A(vector<Expr> l, const vector<T> &r) {
    for (const Var &i : r) {
        l.push_back(i);
    }
    return l;
}

// Get call references to the first N elements of dimension dim of x. If temps
// is set, grab references to elements [-N, -1] instead.
typedef FuncRefT<ComplexExpr> ComplexFuncRef;
vector<ComplexFuncRef> get_func_refs(ComplexFunc x, int N, bool temps = false) {
    vector<Var> args(x.args());
    args.erase(args.begin());

    vector<ComplexFuncRef> refs;
    for (int i = 0; i < N; i++) {
        if (temps) {
            refs.push_back(x(A({Expr(-i - 1)}, args)));
        } else {
            refs.push_back(x(A({Expr(i)}, args)));
        }
    }
    return refs;
}

// Evaluate a complex multiplication where b = re_b + j*im_b
ComplexExpr mul(ComplexExpr a, float re_b, float im_b) {
    return a * ComplexExpr(re_b, im_b);
}

// Specializations for some small DFTs of the first dimension of a
// Func f.
ComplexFunc dft2(ComplexFunc f, const string &prefix) {
    Type type = f.types()[0];

    ComplexFunc F(prefix + "X2");
    F(f.args()) = undef_z(type);

    vector<ComplexFuncRef> x = get_func_refs(f, 2);
    vector<ComplexFuncRef> X = get_func_refs(F, 2);

    X[0] = x[0] + x[1];
    X[1] = x[0] - x[1];

    return F;
}

ComplexFunc dft4(ComplexFunc f, int sign, const string &prefix) {
    Type type = f.types()[0];

    ComplexFunc F(prefix + "X4");
    F(f.args()) = undef_z(type);

    vector<ComplexFuncRef> x = get_func_refs(f, 4);
    vector<ComplexFuncRef> X = get_func_refs(F, 4);
    vector<ComplexFuncRef> T = get_func_refs(F, 2, true);
    // We can re-use these two temps. T[0], T[2] and T[1], T[3] do not have
    // overlapping lifetime.
    T.push_back(T[1]);
    T.push_back(T[0]);

    T[0] = (x[0] + x[2]);
    T[2] = (x[1] + x[3]);
    X[0] = (T[0] + T[2]);
    X[2] = (T[0] - T[2]);

    T[1] = (x[0] - x[2]);
    T[3] = (x[1] - x[3]) * j * sign;
    X[1] = (T[1] + T[3]);
    X[3] = (T[1] - T[3]);

    return F;
}

ComplexFunc dft6(ComplexFunc f, int sign, const string &prefix) {
    const float re_W1_3 = -0.5f;
    const float im_W1_3 = sign * 0.866025404f;

    ComplexExpr W1_3(re_W1_3, im_W1_3);
    ComplexExpr W2_3(re_W1_3, -im_W1_3);
    ComplexExpr W4_3 = W1_3;

    Type type = f.types()[0];

    ComplexFunc F(prefix + "X8");
    F(f.args()) = undef_z(type);

    vector<ComplexFuncRef> x = get_func_refs(f, 6);
    vector<ComplexFuncRef> X = get_func_refs(F, 6);
    vector<ComplexFuncRef> T = get_func_refs(F, 6, true);

    // Prime factor FFT, N=2*3, no twiddle factors!
    T[0] = (x[0] + x[3]);
    T[3] = (x[0] - x[3]);
    T[1] = (x[1] + x[4]);
    T[4] = (x[1] - x[4]);
    T[2] = (x[2] + x[5]);
    T[5] = (x[2] - x[5]);

    X[0] = T[0] + T[2] + T[1];
    X[4] = T[0] + T[2] * W1_3 + T[1] * W2_3;
    X[2] = T[0] + T[2] * W2_3 + T[1] * W4_3;

    X[3] = T[3] + T[5] - T[4];
    X[1] = T[3] + T[5] * W1_3 - T[4] * W2_3;
    X[5] = T[3] + T[5] * W2_3 - T[4] * W4_3;

    return F;
}

ComplexFunc dft8(ComplexFunc f, int sign, const string &prefix) {
    const float sqrt2_2 = 0.70710678f;

    Type type = f.types()[0];

    ComplexFunc F(prefix + "X8");
    F(f.args()) = undef_z(type);

    vector<ComplexFuncRef> x = get_func_refs(f, 8);
    vector<ComplexFuncRef> X = get_func_refs(F, 8);
    vector<ComplexFuncRef> T = get_func_refs(F, 8, true);

    X[0] = (x[0] + x[4]);
    X[2] = (x[2] + x[6]);
    T[0] = (X[0] + X[2]);
    T[2] = (X[0] - X[2]);

    X[1] = (x[0] - x[4]);
    X[3] = (x[2] - x[6]) * j * sign;
    T[1] = (X[1] + X[3]);
    T[3] = (X[1] - X[3]);

    X[4] = (x[1] + x[5]);
    X[6] = (x[3] + x[7]);
    T[4] = (X[4] + X[6]);
    T[6] = (X[4] - X[6]) * j * sign;

    X[5] = (x[1] - x[5]);
    X[7] = (x[3] - x[7]) * j * sign;
    T[5] = mul(X[5] + X[7], sqrt2_2, sign * sqrt2_2);
    T[7] = mul(X[5] - X[7], -sqrt2_2, sign * sqrt2_2);

    X[0] = (T[0] + T[4]);
    X[1] = (T[1] + T[5]);
    X[2] = (T[2] + T[6]);
    X[3] = (T[3] + T[7]);
    X[4] = (T[0] - T[4]);
    X[5] = (T[1] - T[5]);
    X[6] = (T[2] - T[6]);
    X[7] = (T[3] - T[7]);

    return F;
}

// Compute the complex DFT of size N on dimension 0 of x.
ComplexFunc dftN(ComplexFunc x, int N, int sign, const string &prefix) {
    vector<Var> args(x.args());
    args.erase(args.begin());

    Var n("n");
    ComplexFunc X(prefix + "XN");
    if (N < 10) {
        // If N is small, unroll the loop.
        ComplexExpr dft = x(A({Expr(0)}, args));
        for (int k = 1; k < N; k++) {
            dft += expj((sign * 2 * kPi * k * n) / N) * x(A({Expr(k)}, args));
        }
        X(A({n}, args)) = dft;
    } else {
        // If N is larger, we really shouldn't be using this algorithm for the DFT anyways.
        RDom k(0, N);
        X(A({n}, args)) = sum(expj((sign * 2 * kPi * k * n) / N) * x(A({k}, args)));
    }
    X.unroll(n);
    return X;
}

ComplexFunc dft1d_c2c(ComplexFunc x, int N, int sign,
                      const string &prefix) {
    switch (N) {
    case 2:
        return dft2(x, prefix);
    case 4:
        return dft4(x, sign, prefix);
    case 6:
        return dft6(x, sign, prefix);
    case 8:
        return dft8(x, sign, prefix);
    default:
        return dftN(x, N, sign, prefix);
    }
}

// Map to remember previously computed twiddle factors.
typedef std::map<int, ComplexFunc> TwiddleFactorSet;

// Return a function defining the twiddle factors.
ComplexFunc twiddle_factors(int N, Expr gain, int sign,
                            const string &prefix,
                            TwiddleFactorSet *cache) {
    // If the gain is one, we can use the cache. Otherwise, always define a new
    // function. Generally, any given FFT will only have one set of twiddle
    // factors where gain != 1.
    ComplexFunc W(prefix + "W");
    if (is_const_one(gain)) {
        W = (*cache)[N];
    }
    if (!W.defined()) {
        Var n("n");
        W(n) = expj((sign * 2 * kPi * n) / N) * gain;
        W.compute_root();
    }

    return W;
}

// Compute the N point DFT of dimension 1 (columns) of x using
// radix R.
ComplexFunc fft_dim1(ComplexFunc x,
                     const vector<int> &NR,
                     int sign,
                     int extent_0,
                     Expr gain,
                     bool parallel,
                     const string &prefix,
                     const Target &target,
                     TwiddleFactorSet *twiddle_cache) {
    int N = product(NR);

    vector<Var> args = x.args();
    Var n0(args[0]), n1(args[1]);
    args.erase(args.begin());
    args.erase(args.begin());

    vector<std::pair<Func, RDom>> stages;

    RVar r_, s_;
    int S = 1;
    int vector_width = 1;
    for (size_t i = 0; i < NR.size(); i++) {
        int R = NR[i];
        assert(R != 1);

        std::stringstream stage_id;
        stage_id << prefix;
        if (S == N / R) {
            stage_id << "fft1";
        } else {
            stage_id << "x";
        }
        stage_id << "_S" << S << "_R" << R << "_" << n1.name();

        ComplexFunc exchange(stage_id.str());
        Var r("r"), s("s");

        // Load the points from each subtransform and apply the
        // twiddle factors. Twiddle factors for S = 1 are all expj(0) = 1.
        ComplexFunc v("v_" + stage_id.str());
        ComplexExpr x_rs = x(A({n0, s + r * (N / R)}, args));
        if (S > 1) {
            x_rs = cast<float>(x_rs);
            ComplexFunc W = twiddle_factors(R * S, gain, sign, prefix, twiddle_cache);
            v(A({r, s, n0}, args)) = select(r > 0, likely(x_rs * W(r * (s % S))), x_rs * gain);

            // Set the gain to 1 so it is only applied once.
            gain = 1.0f;
        } else {
            v(A({r, s, n0}, args)) = x_rs;
        }

        // The vector width is the least common multiple of the previous vector
        // width and the natural vector size for this stage.
        vector_width = lcm(vector_width, target.natural_vector_size(v.types()[0]));

        // Compute the R point DFT of the subtransform.
        ComplexFunc V = dft1d_c2c(v, R, sign, prefix);

        // Write the subtransform and use it as input to the next
        // pass. Since the pure stage is undef, we explicitly generate the
        // arg list (because we can't use placeholders in an undef
        // definition).
        exchange(A({n0, n1}, args)) = undef_z(V.types()[0]);

        RDom rs(0, R, 0, N / R);
        r_ = rs.x;
        s_ = rs.y;
        ComplexExpr V_rs = V(A({r_, s_, n0}, args));
        if (S == N / R) {
            // In case we haven't yet applied the requested gain (i.e. there were no
            // twiddle factor steps), do so now. If gain is one, this will be a no-op.
            V_rs = V_rs * gain;
            gain = 1.0f;
        }
        exchange(A({n0, ((s_ / S) * R * S) + (s_ % S) + (r_ * S)}, args)) = V_rs;
        exchange.bound(n1, 0, N);

        if (S > 1) {
            v.compute_at(exchange, s_).unroll(r);
            v.reorder_storage(n0, r, s);
        } else {
            // On the first stage, the twiddle factors are 1, so we can inline this (no-op).
        }

        V.compute_at(exchange, s_);
        V.reorder_storage(V.args()[2], V.args()[0], V.args()[1]);

        // The last stage needs explicit vectorization, because it doesn't get computed
        // at the vectorized context exchange (below).
        if (S == N / R) {
            if (S > 1) {
                v.vectorize(n0);
            }
            V.vectorize(V.args()[2]);
            for (int i = 0; i < V.num_update_definitions(); i++) {
                V.update(i).vectorize(V.args()[2]);
            }
        }

        exchange.update().unroll(r_);
        // Remember this stage for scheduling later.
        stages.push_back({exchange, rs});

        x = exchange;
        S *= R;
    }

    // Ensure that the vector width divides the vectorization dimension extent.
    vector_width = gcd(vector_width, extent_0);

    // Split the tile into groups of DFTs, and vectorize within the
    // group.
    x.update()
        .split(n0, group, n0, vector_width)
        .reorder(n0, r_, s_, group)
        .vectorize(n0);
    if (parallel) {
        x.update().parallel(group);
    }
    for (size_t i = 0; i + 1 < stages.size(); i++) {
        Func stage = stages[i].first;
        stage.compute_at(x, group).update().vectorize(n0);
    }

    return x;
}

// transpose the first two dimensions of x.
template<typename FuncType>
FuncType transpose(FuncType f) {
    vector<Halide::Var> argsT(f.args());
    std::swap(argsT[0], argsT[1]);
    FuncType fT;
    fT(argsT) = f(f.args());
    return fT;
}

template<typename FuncType>
std::pair<FuncType, FuncType> tiled_transpose(FuncType f, int max_tile_size,
                                              const Target &target,
                                              const string &prefix,
                                              bool always_tile = false) {
    // ARM can do loads of up to stride 4. We can use these loads to write a more
    // efficient transpose. The strategy is to break the transpose into 4x4 tiles,
    // transpose the tiles themselves (dense vector load/stores), then transpose
    // the data within each tile (stride 4 loads).
    if (target.arch != Target::ARM && !always_tile) {
        return {transpose(f), FuncType()};
    }

    const int tile_size =
        std::min(max_tile_size, target.natural_vector_size(f.types()[0]));

    vector<Var> args = f.args();
    Var x(args[0]), y(args[1]);
    args.erase(args.begin());
    args.erase(args.begin());

    Var xo(x.name() + "o");
    Var yo(y.name() + "o");

    // Break the transposed DFT into 4x4 tiles.
    FuncType f_tiled(prefix + "tiled");
    f_tiled(A({x, y, xo, yo}, args)) = f(A({xo * tile_size + x, yo * tile_size + y}, args));

    // transpose the values within each tile.
    FuncType f_tiledT(prefix + "tiledT");
    f_tiledT(A({y, x, xo, yo}, args)) = f_tiled(A({x, y, xo, yo}, args));

    FuncType fT_tiled(prefix + "T_tiled");
    fT_tiled(A({y, x, yo, xo}, args)) = f_tiledT(A({y, x, xo, yo}, args));

    // Produce the untiled result.
    FuncType fT(prefix + "T");
    fT(A({y, x}, args)) = fT_tiled(A({y % tile_size, x % tile_size, y / tile_size, x / tile_size}, args));

    f_tiledT
        .vectorize(x, tile_size)
        .unroll(y, tile_size);

    return {fT, f_tiledT};
}

}  // namespace

ComplexFunc fft2d_c2c(ComplexFunc x,
                      vector<int> R0,
                      vector<int> R1,
                      int sign,
                      const Target &target,
                      const Fft2dDesc &desc) {
    string prefix = desc.name.empty() ? "c2c_" : desc.name + "_";

    int N0 = product(R0);
    int N1 = product(R1);

    // Get the innermost variable outside the FFT.
    Var outer = Var::outermost();
    if (x.dimensions() > 2) {
        outer = x.args()[2];
    }
    Var n0 = x.args()[0];
    Var n1 = x.args()[1];

    // Cache of twiddle factors for this FFT.
    TwiddleFactorSet twiddle_cache;

    // transpose the input to the FFT.
    auto [xT, x_tiled] = tiled_transpose(x, N1, target, prefix);

    // Compute the DFT of dimension 1 (originally dimension 0).
    ComplexFunc dft1T = fft_dim1(xT,
                                 R0,
                                 sign,
                                 N1,  // extent of dim 0.
                                 1.0f,
                                 desc.parallel,
                                 prefix,
                                 target,
                                 &twiddle_cache);

    // transpose back.
    auto [dft1, dft1_tiled] = tiled_transpose(dft1T, N0, target, prefix);

    // Compute the DFT of dimension 1.
    ComplexFunc dft = fft_dim1(dft1,
                               R1,
                               sign,
                               N0,  // extent of dim 0
                               desc.gain,
                               desc.parallel,
                               prefix,
                               target,
                               &twiddle_cache);

    // Schedule the tiled transposes at each group.
    if (dft1_tiled.defined()) {
        dft1_tiled.compute_at(dft, group);
    } else {
        xT.compute_at(dft, outer).vectorize(n0).unroll(n1);
    }
    if (x_tiled.defined()) {
        x_tiled.compute_at(dft1T, group);
    }

    // Schedule the input, if requested.
    if (desc.schedule_input) {
        x.compute_at(dft1T, group);
    }

    dft1T.compute_at(dft, outer);

    dft.bound(dft.args()[0], 0, N0);
    dft.bound(dft.args()[1], 0, N1);

    return dft;
}

// The next two functions implement real to complex or complex to real FFTs. To
// understand the real to complex FFT, we need some background on the properties
// of FFTs of real data. If X = DFT[x] for a real sequence x of length N, then
// the following relationship holds:
//
//    X_n = (X_(N-n))*                         (1)
//
// This means that for N even, N/2 - 1 of the elements of X are redundant with
// another element of X. This property allows us to store only roughly half of
// a DFT of a real sequence, because the remaining half is fully determined by
// the first.
//
// Also note that for any DFT (not just real):
//
//   Z*_n = sum[ (z_n*) e^(-2*pi*i*n/N) ]
//        = sum[ z_n (e^(-2*pi*i*n/N))* ]*
//        = sum[ z_n e^(-2*pi*i*(N - n)/N) ]*
//   Z*_n = (Z_(N-n))*                         (2)
//
// Using these relationships, we can more efficiently compute two real FFTs by
// using one complex FFT. Let x and y be two real sequences of length N, and
// let z = x + j*y. We can compute the FFT of x and y using one complex FFT of
// z; let X + j*Y = Z = DFT[z] = DFT[x + j*y], then by the linearity of the DFT
// and equations (1) and (2):
//
//    x_n = (z_n + (z_n)*)/2
// -> X_n = (Z_n + Z*_n)/2
//        = (Z_n + (Z_(N-n))*)/2               (3)
//
// and
//
//    y_n = (z_n - (z_n)*)/(2*j)
// -> Y_n = (Z_n - Z*_n)/(2*j)
//        = (Z_n + (Z_(N-n))*)/(2*j)           (4)
//
// This gives 2 real DFTs for the cost of computing 1 complex FFT.
//
// As a side note, a consequence of (1) is that Z_0 and Z_(N/2) must be real.
// Note that Z_N = Z_0 by periodicity of the DFT:
//
//   Z_0 = (Z_N)* = (Z_0)*
//   Z_(N/2) = (Z_(N/2))*
//
// The only way z = z* can be true is if im(z) = 0, i.e. z is real.
//
// We want an efficient 2D FFT. Applying the above tools to a 2D DFT leads to
// some interesting results. First, note that the FFT of a 2D sequence x_(m, n)
// with extents MxN (rows x cols) is a 1D FFT of the rows, followed by a 1D FFT
// of the columns. Suppose x is real. One way to use the above tools is to
// combine pairs of columns into a set of half as many complex columns, and
// compute the FFT of these complex columns. This gives a result laid out like
// so:
//
//       N/2
//   +---------+
//   |    a    | m = 0
//   +---------+
//   |         |
//   |    b    |
//   |         |
//   +---------+
//   |    c    | m = M/2
//   +---------+
//   |         |
//   |    d    |
//   |         |
//   +---------+
//
// When we unzip the columns using (3) and (4) from above, we get data with the
// following layout.
//
//             N
//   +-------------------+
//   |         a'        | m = 0
//   +-------------------+
//   |                   |
//   |         b'        |
//   |                   |
//   +-------------------+
//   |         c'        | m = M/2
//   +-------------------+
//   |                   |
//   |         b'*       |
//   |                   |
//   +-------------------+
//
// Because b'* is redundant with b, we don't need to compute or store it,
// leaving M/2 + 1 rows.
//
// Now, we want to compute the DFT of the rows of this data. Because there are
// M/2 + 1 of them, and we are going to compute the FFTs using SIMD
// instructions, the extra 1 row can be quite expensive. If M is 16, then we
// have 9 rows. If the SIMD width is 4, we will compute 3 SIMD vectors worth of
// FFTs instead of 2, 33% more work than necessary. This is even worse if the
// SIMD width is 8 (AVX).
//
// We can fix this by recognizing that a' and c' are both real, and combining
// them together into one row in the same manner we did for the columns. This
// gives a block of data that looks like:
//
//             N
//   +-------------------+
//   |      a' + j c'    |
//   +-------------------+
//   |                   |  M/2
//   |         b'        |
//   |                   |
//   +-------------------+
//
// This way, we have M/2 rows to compute the FFT of, which is likely to be
// efficient to compute without wasted SIMD work. The DFTs of the rows a and c
// can be recovered using (3) and (4) again.

ComplexFunc fft2d_r2c(Func r,
                      const vector<int> &R0,
                      const vector<int> &R1,
                      const Target &target,
                      const Fft2dDesc &desc) {
    string prefix = desc.name.empty() ? "r2c_" : desc.name + "_";

    vector<Var> args(r.args());
    Var n0(args[0]), n1(args[1]);
    args.erase(args.begin());
    args.erase(args.begin());

    // Get the innermost variable outside the FFT.
    Var outer = Var::outermost();
    if (!args.empty()) {
        outer = args.front();
    }

    int N0 = product(R0);
    int N1 = product(R1);

    const int natural_vector_size = target.natural_vector_size(r.types()[0]);

    // If this FFT is small, the logic related to zipping and unzipping
    // the FFT may be expensive compared to just brute forcing with a complex
    // FFT.
    bool skip_zip = N0 < natural_vector_size * 2;
    // We also are bad at handling zipping when the zip size is a small non-integer
    // factor of the vector size.
    skip_zip = skip_zip || (N0 < natural_vector_size * 4 && (N0 % (natural_vector_size * 2) != 0));
    if (skip_zip) {
        ComplexFunc r_complex("r_complex");
        r_complex(A({n0, n1}, args)) = ComplexExpr(r(A({n0, n1}, args)), 0.0f);
        ComplexFunc dft = fft2d_c2c(r_complex, R0, R1, -1, target, desc);

        // fft2d_c2c produces a N0 x N1 buffer, but the caller of this probably only expects
        // an N0 x N1 / 2 + 1 buffer.
        ComplexFunc result(prefix + "r2c");
        result(A({n0, n1}, args)) = dft(A({n0, n1}, args));
        result.bound(n0, 0, N0);
        result.bound(n1, 0, (N1 + 1) / 2 + 1);
        result.vectorize(n0, std::min(N0, target.natural_vector_size(result.types()[0])));
        dft.compute_at(result, outer);
        return result;
    }

    // Cache of twiddle factors for this FFT.
    TwiddleFactorSet twiddle_cache;

    // The gain requested of the FFT.
    Expr gain = desc.gain;

    // Combine pairs of real columns x, y into complex columns z = x + j y. This
    // allows us to compute two real DFTs using one complex FFT. See the large
    // comment above this function for more background.
    //
    // An implementation detail is that we zip the columns in groups from the
    // input data to enable the loads to be dense vectors. x is taken from the
    // even indexed groups columns, y is taken from the odd indexed groups of
    // columns.
    //
    // Changing the group size can (insignificantly) numerically change the result
    // due to regrouping floating point operations. To avoid this, if the FFT
    // description specified a vector width, use it as the group size.
    ComplexFunc zipped(prefix + "zipped");
    int zip_width = desc.vector_width;
    if (zip_width <= 0) {
        zip_width = target.natural_vector_size(r.types()[0]);
    }
    // Ensure the zip width divides the zipped extent.
    zip_width = gcd(zip_width, N0 / 2);
    Expr zip_n0 = (n0 / zip_width) * zip_width * 2 + (n0 % zip_width);
    zipped(A({n0, n1}, args)) =
        ComplexExpr(r(A({zip_n0, n1}, args)),
                    r(A({zip_n0 + zip_width, n1}, args)));

    // DFT down the columns first.
    ComplexFunc dft1 = fft_dim1(zipped,
                                R1,
                                -1,      // sign
                                N0 / 2,  // extent of dim 0
                                1.0f,
                                false,  // We parallelize unzipped below instead.
                                prefix,
                                target,
                                &twiddle_cache);

    // Unzip the two groups of real DFTs we zipped together above. For more
    // information about the unzipping operation, see the large comment above this
    // function.
    ComplexFunc unzipped(prefix + "unzipped");
    {
        Expr unzip_n0 = (n0 / (zip_width * 2)) * zip_width + (n0 % zip_width);
        ComplexExpr Z = dft1(A({unzip_n0, n1}, args));
        ComplexExpr conjsymZ = conj(dft1(A({unzip_n0, (N1 - n1) % N1}, args)));

        ComplexExpr X = Z + conjsymZ;
        ComplexExpr Y = -j * (Z - conjsymZ);
        // Rather than divide the above expressions by 2 here, adjust the gain
        // instead.
        gain /= 2;

        unzipped(A({n0, n1}, args)) =
            select(n0 % (zip_width * 2) < zip_width, X, Y);
    }

    // Zip the DC and Nyquist DFT bin rows, which should be real.
    ComplexFunc zipped_0(prefix + "zipped_0");
    zipped_0(A({n0, n1}, args)) =
        select(n1 > 0, likely(unzipped(A({n0, n1}, args))),
               ComplexExpr(re(unzipped(A({n0, 0}, args))),
                           re(unzipped(A({n0, N1 / 2}, args)))));

    // The vectorization of the columns must not exceed this value.
    int zipped_extent0 = std::min((N1 + 1) / 2, zip_width);

    // transpose so we can FFT dimension 0 (by making it dimension 1).
    auto [unzippedT, unzippedT_tiled] = tiled_transpose(zipped_0, zipped_extent0, target, prefix);

    // DFT down the columns again (the rows of the original).
    ComplexFunc dftT = fft_dim1(unzippedT,
                                R0,
                                -1,  // sign
                                zipped_extent0,
                                gain,
                                desc.parallel,
                                prefix,
                                target,
                                &twiddle_cache);

    // transpose the result back to the original orientation, unless the caller
    // requested a transposed DFT.
    ComplexFunc dft = transpose(dftT);

    // We are going to add a row to the result (with update steps) by unzipping
    // the DC and Nyquist bin rows. To avoid unnecessarily computing some junk for
    // this row before we overwrite it, pad the pure definition with undef.
    dft = ComplexFunc(constant_exterior((Func)dft, Tuple(undef_z()), Expr(), Expr(), Expr(0), Expr(N1 / 2)));

    // Unzip the DFTs of the DC and Nyquist bin DFTs. Unzip the Nyquist DFT first,
    // because the DC bin DFT is updated in-place. For more information about
    // this, see the large comment above this function.
    RDom n0z1(1, N0 / 2);
    RDom n0z2(N0 / 2, N0 / 2);
    // Update 0: Unzip the DC bin of the DFT of the Nyquist bin row.
    dft(A({0, N1 / 2}, args)) = im(dft(A({0, 0}, args)));
    // Update 1: Unzip the rest of the DFT of the Nyquist bin row.
    dft(A({n0z1, N1 / 2}, args)) =
        0.5f * -j * (dft(A({n0z1, 0}, args)) - conj(dft(A({N0 - n0z1, 0}, args))));
    // Update 2: Compute the rest of the Nyquist bin row via conjugate symmetry.
    // Note that this redundantly computes n0 = N0/2, but that's faster and easier
    // than trying to deal with N0/2 - 1 bins.
    dft(A({n0z2, N1 / 2}, args)) = conj(dft(A({N0 - n0z2, N1 / 2}, args)));

    // Update 3: Unzip the DC bin of the DFT of the DC bin row.
    dft(A({0, 0}, args)) = re(dft(A({0, 0}, args)));
    // Update 4: Unzip the rest of the DFT of the DC bin row.
    dft(A({n0z1, 0}, args)) =
        0.5f * (dft(A({n0z1, 0}, args)) + conj(dft(A({N0 - n0z1, 0}, args))));
    // Update 5: Compute the rest of the DC bin row via conjugate symmetry.
    // Note that this redundantly computes n0 = N0/2, but that's faster and easier
    // than trying to deal with N0/2 - 1 bins.
    dft(A({n0z2, 0}, args)) = conj(dft(A({N0 - n0z2, 0}, args)));

    // Schedule.
    dftT.compute_at(dft, outer);

    // Schedule the tiled transposes.
    if (unzippedT_tiled.defined()) {
        unzippedT_tiled.compute_at(dftT, group);
    }

    // Schedule the input, if requested.
    if (desc.schedule_input) {
        r.compute_at(dft1, group);
    }

    // Vectorize the zip groups, and unroll by a factor of 2 to simplify the
    // even/odd selection.
    Var n0o("n0o"), n0i("n0i");
    unzipped.compute_at(dft, outer)
        .split(n0, n0o, n0i, zip_width * 2)
        .reorder(n0i, n1, n0o)
        .vectorize(n0i, zip_width)
        .unroll(n0i);
    dft1.compute_at(unzipped, n0o);
    if (desc.parallel) {
        // Note that this also parallelizes dft1, which is computed inside this loop
        // of unzipped.
        unzipped.parallel(n0o);
    }

    // Schedule the final DFT transpose and unzipping updates.
    int vector_size = gcd(target.natural_vector_size<float>(), N0);
    dft.vectorize(n0, vector_size)
        .unroll(n0, gcd(N0 / vector_size, 4));

    // The Nyquist bin at n0z = N0/2 looks like a race condition because it
    // simplifies to an expression similar to the DC bin. However, we include it
    // in the reduction because it makes the reduction have length N/2, which is
    // convenient for vectorization, and just ignore the resulting appearance of
    // a race condition.
    dft.update(1).allow_race_conditions().vectorize(n0z1, vector_size);
    dft.update(2).allow_race_conditions().vectorize(n0z2, vector_size);
    dft.update(4).allow_race_conditions().vectorize(n0z1, vector_size);
    dft.update(5).allow_race_conditions().vectorize(n0z2, vector_size);

    // Intentionally serial
    dft.update(0).unscheduled();
    dft.update(3).unscheduled();

    // Our result is undefined outside these bounds.
    dft.bound(n0, 0, N0);
    dft.bound(n1, 0, (N1 + 1) / 2 + 1);

    return dft;
}

Func fft2d_c2r(ComplexFunc c,
               vector<int> R0,
               vector<int> R1,
               const Target &target,
               const Fft2dDesc &desc) {
    string prefix = desc.name.empty() ? "c2r_" : desc.name + "_";

    vector<Var> args = c.args();
    Var n0(args[0]), n1(args[1]);
    args.erase(args.begin());
    args.erase(args.begin());

    // Get the innermost variable outside the FFT.
    Var outer = Var::outermost();
    if (!args.empty()) {
        outer = args.front();
    }

    int N0 = product(R0);
    int N1 = product(R1);

    // Add a boundary condition to prevent scheduling from causing the
    // algorithms below to reach out of the bounds we promise to define in
    // forward FFTs.
    c = ComplexFunc(repeat_edge((Func)c, {{Expr(0), Expr(N0)}, {Expr(0), Expr((N1 + 1) / 2 + 1)}}));

    // If this FFT is small, the logic related to zipping and unzipping
    // the FFT may be expensive compared to just brute forcing with a complex
    // FFT.
    const int natural_vector_size = target.natural_vector_size(c.types()[0]);

    bool skip_zip = N0 < natural_vector_size * 2;

    ComplexFunc dft;
    Func unzipped(prefix + "unzipped");
    if (skip_zip) {
        // Because fft2d_c2c expects the full complex domain, we need to reconstruct
        // it via conjugate symmetry.
        ComplexFunc c_extended(prefix + "c_extended");
        c_extended(A({n0, n1}, args)) =
            select(n1 <= (N1 + 1) / 2, c(A({n0, n1}, args)), conj(c(A({(N0 - n0) % N0, (N1 - n1) % N1}, args))));
        dft = fft2d_c2c(c_extended, R0, R1, 1, target, desc);
        unzipped(A({n0, n1}, args)) = re(dft(A({n0, n1}, args)));

        dft.compute_at(unzipped, outer);

        // We want to unroll by at least two zip_widths to simplify the zip group
        // logic.
        int vector_size = std::min(N0, natural_vector_size);
        unzipped.vectorize(n0, vector_size);
    } else {
        // Cache of twiddle factors for this FFT.
        TwiddleFactorSet twiddle_cache;

        int zipped_extent0 = (N1 + 1) / 2;

        // The DC and Nyquist bins must be real, so we zip those two DFTs together
        // into one complex DFT. Note that this select gets eliminated due to the
        // scheduling done by tiled_transpose below.
        ComplexFunc c_zipped(prefix + "c_zipped");
        {
            // Stuff the Nyquist bin DFT into the imaginary part of the DC bin DFT.
            ComplexExpr X = c(A({n0, 0}, args));
            ComplexExpr Y = c(A({n0, N1 / 2}, args));
            c_zipped(A({n0, n1}, args)) = select(n1 > 0, likely(c(A({n0, n1}, args))), X + j * Y);
        }

        // transpose the input.
        auto [cT, cT_tiled] =
            tiled_transpose(c_zipped, zipped_extent0, target, prefix);

        // Take the inverse DFT of the columns (rows in the final result).
        ComplexFunc dft0T = fft_dim1(cT,
                                     R0,
                                     1,  // sign
                                     zipped_extent0,
                                     1.0f,
                                     desc.parallel,
                                     prefix,
                                     target,
                                     &twiddle_cache);

        // The vector width of the zipping performed below.
        int zip_width = desc.vector_width;
        if (zip_width <= 0) {
            zip_width = gcd(target.natural_vector_size(dft0T.types()[0]), N1 / 2);
        }

        // transpose so we can take the DFT of the columns again.
        auto [dft0, dft0_tiled] = tiled_transpose(dft0T, zip_width, target, prefix, true);

        // Unzip the DC and Nyquist DFTs.
        ComplexFunc dft0_unzipped("dft0_unzipped");
        {
            dft0_unzipped(A({n0, n1}, args)) =
                select(n1 <= 0, re(dft0(A({n0, 0}, args))),
                       n1 >= N1 / 2, im(dft0(A({n0, 0}, args))),
                       likely(dft0(A({n0, min(n1, (N1 / 2) - 1)}, args))));
        }

        // Zip two real DFTs X and Y into one complex DFT Z = X + j Y. For more
        // information, see the large comment above fft2d_r2c.
        //
        // As an implementation detail, this zip operation is done in groups of
        // columns to enable dense vector loads. X is taken from the even indexed
        // groups of columns, Y is taken from the odd indexed groups of columns.
        //
        // Ensure the zip width divides the zipped extent.
        zip_width = gcd(zip_width, N0 / 2);

        ComplexFunc zipped(prefix + "zipped");
        {
            // Construct the whole DFT domain of X and Y via conjugate symmetry.
            Expr n0_X = (n0 / zip_width) * zip_width * 2 + (n0 % zip_width);
            Expr n1_sym = (N1 - n1) % N1;
            ComplexExpr X = select(n1 < N1 / 2,
                                   dft0_unzipped(A({n0_X, n1}, args)),
                                   conj(dft0_unzipped(A({n0_X, n1_sym}, args))));

            Expr n0_Y = n0_X + zip_width;
            ComplexExpr Y = select(n1 < N1 / 2,
                                   dft0_unzipped(A({n0_Y, n1}, args)),
                                   conj(dft0_unzipped(A({n0_Y, n1_sym}, args))));
            zipped(A({n0, n1}, args)) = X + j * Y;
        }

        // Take the inverse DFT of the columns again.
        dft = fft_dim1(zipped,
                       R1,
                       1,                            // sign
                       std::min(zip_width, N0 / 2),  // extent of dim 0
                       desc.gain,
                       desc.parallel,
                       prefix,
                       target,
                       &twiddle_cache);

        ComplexFunc dft_padded = ComplexFunc(repeat_edge((Func)dft, {{Expr(), Expr()}, {Expr(0), Expr(N1)}}));

        // Extract the real inverse DFTs.
        Expr unzip_n0 = (n0 / (zip_width * 2)) * zip_width + (n0 % zip_width);
        unzipped(A({n0, n1}, args)) =
            select(n0 % (zip_width * 2) < zip_width,
                   re(dft_padded(A({unzip_n0, n1}, args))),
                   im(dft_padded(A({unzip_n0, n1}, args))));

        // Schedule.

        // Schedule the transpose step.
        if (cT_tiled.defined()) {
            cT_tiled.compute_at(dft0T, group);
        }
        dft0_tiled.compute_at(dft, outer);

        // Schedule the input, if requested.
        if (desc.schedule_input) {
            // We should want to compute this at dft0T, group. However, due to the zip
            // operation, the bounds are bigger than we'd like (we need the last row for
            // the first group).
            c.compute_at(dft, outer);
        }

        dft0T.compute_at(dft, outer);

        // We want to unroll by at least two zip_widths to simplify the zip group
        // logic.
        unzipped
            .vectorize(n0, zip_width)
            .unroll(n0, gcd(N0 / zip_width, 4));
    }
    dft.compute_at(unzipped, outer);

    unzipped.bound(n0, 0, N0);
    unzipped.bound(n1, 0, N1);

    return unzipped;
}

namespace {

// Compute a factorization of N suitable for use in the FFT.
vector<int> radix_factor(int N) {
    // Some special cases to optimize.
    switch (N) {
    case 16:
        return {4, 4};
    case 32:
        return {8, 4};
    case 64:
        return {8, 8};
    case 128:
        return {8, 4, 4};
    case 256:
        return {8, 8, 4};
    }

    // Factor N into factors found in the 'radices' set.
    static const int radices[] = {8, 6, 4, 2};
    vector<int> R;
    for (int r : radices) {
        while (N % r == 0) {
            R.push_back(r);
            N /= r;
        }
    }

    // If there are still factors left over, just include them as a radix.
    if (N != 1 || R.empty()) {
        R.push_back(N);
    }

    return R;
}

}  // namespace

ComplexFunc fft2d_c2c(ComplexFunc x,
                      int N0, int N1,
                      int sign,
                      const Target &target,
                      const Fft2dDesc &desc) {
    return fft2d_c2c(x, radix_factor(N0), radix_factor(N1), sign, target, desc);
}

ComplexFunc fft2d_r2c(Func r,
                      int N0, int N1,
                      const Target &target,
                      const Fft2dDesc &desc) {
    return fft2d_r2c(r, radix_factor(N0), radix_factor(N1), target, desc);
}

Func fft2d_c2r(ComplexFunc c,
               int N0, int N1,
               const Target &target,
               const Fft2dDesc &desc) {
    return fft2d_c2r(c, radix_factor(N0), radix_factor(N1), target, desc);
}
