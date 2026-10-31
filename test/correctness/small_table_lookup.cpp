#include "Halide.h"

#include <cstdio>
#include <random>

using namespace Halide;
using namespace Halide::Internal;

// Vectorized lookups into small compile-time-constant tables (a mux of
// constants), small embedded Buffers, and fixed-size internal allocations
// may be turned into
// dynamic_shuffle calls, which are lowered to table lookup instructions on
// some targets (ARM tbl, x86 pshufb/vpermb, WebAssembly i8x16.swizzle). Check
// that all forms of such lookups give the right answer, including the ones
// that can't be transformed.

namespace {

std::mt19937 rng(0);

const int W = 263;

bool has_dynamic_shuffle(const Stmt &s) {
    bool found = false;
    visit_with(s, [&](auto *self, const Call *op) {
        found |= op->is_intrinsic(Call::dynamic_shuffle);
        self->visit_base(op);
    });
    return found;
}

bool test_lowering() {
    Target target("arm-64-linux");
    Expr index = clamp(Variable::make(Int(32, 16), "index"), 0, 12);
    auto allocation = [&](Expr extent, Expr idx, Expr new_expr = Expr()) {
        Stmt body = Evaluate::make(Load::make(UInt(8, 16), "lut", idx));
        return Allocate::make("lut", UInt(8), MemoryType::Stack, {extent},
                              const_true(), body, new_expr);
    };
    auto check_lowering = [&](const Stmt &s, const Target &t, bool expected) {
        Stmt lowered = optimize_small_table_lookups(s, t);
        if (has_dynamic_shuffle(lowered) != expected) {
            std::cerr << "Unexpected small-table lowering for " << t << ":\n"
                      << lowered << "\n";
            return false;
        }
        return true;
    };
    Stmt safe = allocation(13, index);
    if (!check_lowering(safe, target, true) ||
        !check_lowering(allocation(12, index), target, false) ||
        !check_lowering(allocation(13, index - 1), target, false) ||
        !check_lowering(allocation(Variable::make(Int(32), "size"), index), target, false) ||
        !check_lowering(allocation(13, index, Variable::make(Handle(), "ptr")), target, false) ||
        !check_lowering(Evaluate::make(Load::make(UInt(8, 16), "lut", index)), target, false) ||
        !check_lowering(safe, target.with_feature(Target::NoNEON), false) ||
        !check_lowering(safe, Target("arm-64-linux-sve2-vector_bits_256"), false) ||
        !check_lowering(safe, Target("x86-64-linux"), false) ||
        !check_lowering(safe, Target("x86-64-linux-sse41"), true) ||
        !check_lowering(safe, Target("wasm-32-wasmrt"), false) ||
        !check_lowering(safe, Target("wasm-32-wasmrt-wasm_simd128"), true)) {
        return false;
    }

    // Check that normal lowering, before LLVM codegen, introduces the intrinsic
    // and that the same lowered module can still be emitted as C++.
    Var x;
    Param<int> salt;
    Buffer<uint8_t> embedded(13);
    Func lut, out;
    lut(x) = cast<uint8_t>(x * 37 + salt);
    lut.compute_root();
    std::vector<Expr> table;
    for (int i = 0; i < 13; i++) {
        table.push_back(cast<uint8_t>(i * 53 + 7));
        embedded(i) = i * 29 + 3;
    }
    Expr idx = (x * x) % 13;
    out(x) = lut(idx) + mux(idx, table) + embedded(idx);
    out.vectorize(x, 16);
    Module module = out.compile_to_module(out.infer_arguments(), "small_table_lowering", target);
    if (!has_dynamic_shuffle(module.functions().back().body)) {
        std::cerr << "Small-table lookups were not optimized during lowering\n";
        return false;
    }
    std::ostringstream source;
    CodeGen_C cg(source, target, CodeGen_C::CImplementation);
    cg.compile(module);
    if (source.str().find("using int32_t =") != std::string::npos) {
        std::cerr << "C++ emission declared a scalar as a vector type\n";
        return false;
    }
    return true;
}

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
    Param<int> salt;
    salt.set(77);

    Var x;
    Func internal, internal_offset;
    internal(x) = cast<T>(x * 37 + salt - 100);
    internal_offset(x) = cast<T>((x + 2) * 37 + salt - 100);
    internal.compute_root();
    internal_offset.compute_root();

    Expr u8_x = in_u8(x), i32_x = in_i32(x);
    Expr in_range = u8_x % n;
    std::vector<Func> fs(10);
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
    // Fixed-size internal allocations, including a non-zero allocation min.
    fs[8](x) = internal(cast<int>(in_range));
    fs[9](x) = internal_offset(cast<int>(in_range) - 2);

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
        [&](int x) { return table_value<T>(in_u8(x) % n, 7); },
        [&](int x) { return table_value<T>(in_u8(x) % n, 7); },
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
    salt.set(88);
    refs[8] = refs[9] = [&](int x) { return table_value<T>(in_u8(x) % n, 8); };
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
    if (!test_lowering()) {
        return 1;
    }

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
        for (int vec : {3, 8, 16, 17, 64}) {
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
