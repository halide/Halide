#include "Halide.h"

#include <cstdio>
#include <random>

using namespace Halide;
using namespace Halide::Internal;

// Vectorized lookups into small compile-time-constant tables (a mux of
// constants) and into small embedded Buffers may be turned into
// dynamic_shuffle calls, which are lowered to table lookup instructions on
// some targets (ARM tbl, x86 pshufb/vpermb, WebAssembly i8x16.swizzle). Check
// that all forms of such lookups give the right answer, including the ones
// that can't be transformed.

namespace {

std::mt19937 rng(0);

const int W = 263;

template<typename T>
T table_value(int i, int salt) {
    return (T)(i * 37 + salt * 11 - 100);
}

template<typename T>
bool check(const Buffer<T> &out, const std::function<T(int)> &ref,
           const std::string &what) {
    for (int x = 0; x < out.width(); x++) {
        T correct = ref(x);
        if (out(x) != correct) {
            std::cerr << what << ": out(" << x << ") = " << (double)out(x)
                      << " instead of " << (double)correct << "\n";
            return false;
        }
    }
    return true;
}

template<typename T>
bool test(int n, int vec, const Target &target) {
    std::ostringstream ctx;
    ctx << type_of<T>() << " n=" << n << " vec=" << vec << " target=" << target;

    Buffer<uint8_t> in_u8(W);
    Buffer<int32_t> in_i32(W);
    in_u8.for_each_value([](uint8_t &v) { v = (uint8_t)rng(); });
    // Include negative and large indices to exercise the out-of-range
    // behavior of mux.
    in_i32.for_each_value([](int32_t &v) { v = (int32_t)(rng() % 600) - 300; });

    std::vector<Expr> table;
    for (int i = 0; i < n; i++) {
        table.push_back(cast<T>(table_value<T>(i, 0)));
    }

    // An embedded buffer of exactly n elements.
    Buffer<T> lut(n);
    lut.for_each_element([&](int i) { lut(i) = table_value<T>(i, 1); });

    // An embedded buffer with a non-zero min, of which only a part is used.
    Buffer<T> lut_offset(n + 10);
    lut_offset.set_min(-5);
    lut_offset.for_each_element([&](int i) { lut_offset(i) = table_value<T>(i, 2); });

    // A two-dimensional embedded buffer.
    Buffer<T> lut_2d(4, (n + 3) / 4);
    lut_2d.for_each_element([&](int i, int j) { lut_2d(i, j) = table_value<T>(i + 4 * j, 5); });

    Param<int> p;
    p.set(n - 1);

    Var x;
    Expr u8_x = in_u8(x), i32_x = in_i32(x);
    Expr in_range = u8_x % n;
    std::vector<Func> fs(8);
    // mux with an index proven to be in range.
    fs[0](x) = mux(in_range, table);
    // mux with an index of unknown range.
    fs[1](x) = mux(u8_x, table);
    // mux with a signed index that may be negative.
    fs[2](x) = mux(i32_x, table);
    // mux with a let-bound index proven in range.
    fs[3](x) = mux(Let::make("idx", cast<int>(in_range), Variable::make(Int(32), "idx")), table);
    // Embedded buffer, clamped index.
    fs[4](x) = lut(clamp(cast<int>(u8_x), 0, n - 1));
    // Embedded buffer with a min, using a sub-range.
    fs[5](x) = lut_offset(cast<int>(in_range) - 2);
    // Embedded buffer, index range not provable at compile time.
    fs[6](x) = lut(clamp(cast<int>(u8_x), 0, p));
    // Two-dimensional embedded buffer.
    fs[7](x) = lut_2d(cast<int>(in_range) % 4, cast<int>(in_range) / 4);

    for (Func &f : fs) {
        f.vectorize(x, vec);
    }

    auto mux_ref = [&](int64_t i) {
        return table_value<T>((i < 0 || i >= n) ? n - 1 : (int)i, 0);
    };
    std::vector<std::function<T(int)>> refs = {
        [&](int x) { return mux_ref(in_u8(x) % n); },
        [&](int x) { return mux_ref(in_u8(x)); },
        [&](int x) { return mux_ref(in_i32(x)); },
        [&](int x) { return mux_ref(in_u8(x) % n); },
        [&](int x) { return lut(std::min((int)in_u8(x), n - 1)); },
        [&](int x) { return lut_offset(in_u8(x) % n - 2); },
        [&](int x) { return lut(std::min((int)in_u8(x), n - 1)); },
        [&](int x) { return lut_2d((in_u8(x) % n) % 4, (in_u8(x) % n) / 4); },
    };

    Pipeline pipeline(fs);
    Realization r = pipeline.realize({W}, target);
    for (size_t i = 0; i < fs.size(); i++) {
        if (!check<T>(r[i], refs[i], ctx.str() + " case " + std::to_string(i))) {
            return false;
        }
    }

#ifndef WITH_SERIALIZATION_JIT_ROUNDTRIP_TESTING
    // Embedded buffers are passed by reference to JIT-compiled pipelines, so
    // changes to their contents must be visible on the next run. (Not under
    // serialization round-trip testing, which JIT-compiles a deserialized
    // copy of the pipeline, and so of its embedded buffers.)
    lut.for_each_element([&](int i) { lut(i) = table_value<T>(i, 3); });
    lut_offset.for_each_element([&](int i) { lut_offset(i) = table_value<T>(i, 4); });
    lut_2d.for_each_element([&](int i, int j) { lut_2d(i, j) = table_value<T>(i + 4 * j, 6); });
    r = pipeline.realize({W}, target);
    for (size_t i = 4; i < fs.size(); i++) {
        if (!check<T>(r[i], refs[i], ctx.str() + " case " + std::to_string(i) + " after update")) {
            return false;
        }
    }
#endif

    return true;
}

// The generic (non-target-specific) lowering of dynamic_shuffle.
bool test_generic_dynamic_shuffle(const Target &target) {
    const int n = 13;
    std::vector<Expr> table;
    for (int i = 0; i < n; i++) {
        table.push_back(cast<int16_t>(i * 1000 - 5000));
    }
    Buffer<uint8_t> in(W);
    in.for_each_value([](uint8_t &v) { v = (uint8_t)rng(); });
    Var x;
    Func f;
    Expr idx = cast<uint8_t>(in(x) % n);
    f(x) = Call::make(Int(16), Call::dynamic_shuffle,
                      {Shuffle::make_concat(table), idx, 0, n - 1},
                      Call::PureIntrinsic);
    Buffer<int16_t> out = f.realize({W}, target);
    return check<int16_t>(out, [&](int x) { return (int16_t)((in(x) % n) * 1000 - 5000); }, "generic dynamic_shuffle");
}

}  // namespace

int main(int argc, char **argv) {
    Target host = get_jit_target_from_environment();

    std::vector<Target> targets = {host};
    if (host.arch == Target::ARM && host.bits == 64) {
        // Without NEON, these lookups are left alone.
        targets.push_back(host.with_feature(Target::NoNEON));
    } else if (host.arch == Target::WebAssembly && host.has_feature(Target::WasmSimd128)) {
        targets.push_back(host.without_feature(Target::WasmSimd128));
    } else if (host.arch == Target::X86) {
        // Variants that use smaller vector registers, or no pshufb at all.
        if (host.has_feature(Target::AVX2)) {
            targets.emplace_back(host.os, host.arch, host.bits, std::vector<Target::Feature>{Target::SSE41});
        }
        targets.emplace_back(host.os, host.arch, host.bits);
    }

    for (const Target &t : targets) {
        for (int vec : {8, 16, 64}) {
            for (int n : {2, 13, 16, 17, 48, 64, 65}) {
                if (!test<uint8_t>(n, vec, t) || !test<int8_t>(n, vec, t)) {
                    return 1;
                }
            }
            // Wider types are not currently transformed, but check them too.
            if (!test<uint16_t>(16, vec, t) || !test<float>(16, vec, t)) {
                return 1;
            }
        }
        if (!test_generic_dynamic_shuffle(t)) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
