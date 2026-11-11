#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

// 4-way dots of three or four narrow factors, one a per-group scale: x86
// pair-sums the 8-bit factors with pmaddubsw, then pmaddwd by the scale.
// Signed by signed uses |a| * sign(b, a); a [-1, 1] factor goes onto a
// signed one with psignb. Both are exact only away from -128, which the
// pattern must check.

int check(const char *name, const Target &t, bool want_maddubs,
          const std::function<Expr(Expr, Expr, Expr, Expr, Expr)> &term) {
    const int n = 256;
    ImageParam a(Int(8), 1), b(Int(8), 1), c(UInt(8), 1), s(Int(16), 1);
    Var x("x");
    RDom r(0, 4);
    Func f(std::string("f_") + name);
    Expr i = 4 * x + r;
    f(x) = 0;
    f(x) += term(cast<int>(a(i)), cast<int>(b(i)), cast<int>(c(i)), cast<int>(s(x)), r);
    f.update().atomic().vectorize(r).vectorize(x, 16);

    Buffer<int8_t> ab(4 * n), bb(4 * n);
    Buffer<uint8_t> cb(4 * n);
    Buffer<int16_t> sb(n);
    for (int k = 0; k < 4 * n; k++) {
        // (every corner, including -128, early)
        ab(k) = (int8_t)(k < 256 ? k : rand());
        bb(k) = (int8_t)(k < 256 ? 255 - k : rand());
        cb(k) = (uint8_t)rand();
    }
    for (int k = 0; k < n; k++) {
        sb(k) = (int16_t)((rand() % 1001) - 500);
    }
    a.set(ab);
    b.set(bb);
    c.set(cb);
    s.set(sb);

    Func ref;
    ref(x) = 0;
    ref(x) += term(cast<int>(a(i)), cast<int>(b(i)), cast<int>(c(i)), cast<int>(s(x)), r);
    Buffer<int> want = ref.realize({n}, Target(t.os, t.arch, t.bits));
    Buffer<int> got = f.realize({n}, t);
    for (int k = 0; k < n; k++) {
        if (got(k) != want(k)) {
            printf("%s: f(%d) = %d instead of %d\n", name, k, got(k), want(k));
            return 1;
        }
    }

    std::string asm_file = Internal::get_test_tmp_dir() + "x86_dot_three_factor_" + name + ".s";
    f.compile_to_assembly(asm_file, {a, b, c, s}, t);
    std::ifstream in(asm_file);
    std::stringstream ss;
    ss << in.rdbuf();
    bool has = ss.str().find("pmaddubsw") != std::string::npos;
    if (has != want_maddubs) {
        printf("%s: pmaddubsw %s\n", name, has ? "used where it is not exact" : "not used");
        return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (t.arch != Target::X86 || !t.has_feature(Target::AVX2)) {
        printf("[SKIP] Needs an x86 target with AVX2.\n");
        return 0;
    }
    auto sign_of = [](Expr c) { return select(c > 127, -1, select(c < 3, 0, 1)); };
    int failures = 0;
    // unsigned by signed, with a scale (the existing three-factor form)
    failures += check("u_s", t, true, [](Expr a, Expr b, Expr c, Expr s, Expr r) { return (c & 15) * a * s; });
    // signed by signed (one can't be -128): |a| * sign(b, a)
    failures += check("s_s", t, true, [](Expr a, Expr b, Expr c, Expr s, Expr r) { return a * clamp(b, -127, 127) * s; });
    // both can be -128: |a| * -b doesn't fit int8
    failures += check("s_s_min", t, false, [](Expr a, Expr b, Expr c, Expr s, Expr r) { return a * b * s; });
    // a sign factor onto the signed one beside an unsigned one
    failures += check("u_s_sign", t, true, [&](Expr a, Expr b, Expr c, Expr s, Expr r) { return (c & 15) * clamp(a, -127, 127) * sign_of(c) * s; });
    // a sign factor and two signed factors
    failures += check("s_s_sign", t, true, [&](Expr a, Expr b, Expr c, Expr s, Expr r) { return a * clamp(b, -127, 127) * sign_of(c) * s; });
    // signed codes from a table without -128 (a dynamic_shuffle), by
    // full-range signed values
    failures += check("lut_s", t, true, [](Expr a, Expr b, Expr c, Expr s, Expr r) {
        std::vector<Expr> table;
        for (int v : {-127, -104, -83, -65, -49, -35, -22, -10, 1, 13, 25, 38, 53, 69, 89, 113}) {
            table.push_back(cast<int8_t>(v));
        }
        return cast<int>(mux(c & 15, table)) * a * s;
    });
    // a scale of bit fields selected per pair of values (uniform over pairs
    // of lanes, through a shuffle of constants)
    failures += check("pair_fields", t, true, [](Expr a, Expr b, Expr c, Expr s, Expr r) {
        return (c & 15) * a * (((s >> (r / 2 * 4)) & 15) + 1);
    });
    // the same, with the lane pattern used twice (a let after CSE)
    failures += check("pair_fields_let", t, true, [](Expr a, Expr b, Expr c, Expr s, Expr r) {
        Expr sh = r / 2 * 4;
        return (c & 15) * a * (((s >> sh) & ((sh & 4) + 11)) + 1);
    });
    if (failures) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
