#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>
#include <string>

// Dot products of 8-bit operands of opposite signedness, accumulated into
// int32. These map to dedicated instructions on some targets (e.g. usdot/sudot
// on ARM with FEAT_I8MM), whose operand order is fixed by the signedness, so
// check both orders, with and without one operand broadcast.

using namespace Halide;

namespace {

constexpr int K = 32;
constexpr int N = 64;

std::mt19937 rng(0);

template<typename T>
Buffer<T> make_input(int w, int h = 0) {
    Buffer<T> buf = h ? Buffer<T>(w, h) : Buffer<T>(w);
    std::uniform_int_distribution<int> dist(std::numeric_limits<T>::min(), std::numeric_limits<T>::max());
    buf.for_each_value([&](T &v) { v = (T)dist(rng); });
    // Make sure the extremes of the product appear.
    buf.data()[0] = std::numeric_limits<T>::min();
    buf.data()[1] = std::numeric_limits<T>::max();
    return buf;
}

std::string compile_to_asm(Func f, const std::vector<Argument> &args, const std::string &name,
                           const Target &target = get_jit_target_from_environment()) {
    std::string file = Internal::get_test_tmp_dir() + "mixed_sign_dot_product_" + name + ".s";
    f.compile_to_assembly(file, args, target);
    std::ifstream in(file);
    std::stringstream contents;
    contents << in.rdbuf();
    return contents.str();
}

bool expect_instructions() {
    Target t = get_jit_target_from_environment();
    return t.arch == Target::ARM && t.bits == 64 &&
           t.with_implied_features().has_feature(Target::ARMI8MM) &&
           !t.has_feature(Target::NoNEON) && !t.has_feature(Target::SVE2);
}

// out(x) = 7 + sum_r a(K * x + r) * b(K * x + r), with each lane of the output
// reducing a contiguous run of K elements of each input.
template<typename A, typename B>
int test_vector_form(const std::string &name, int lanes) {
    Buffer<A> a_buf = make_input<A>(N * K);
    Buffer<B> b_buf = make_input<B>(N * K);
    ImageParam a(type_of<A>(), 1, "a"), b(type_of<B>(), 1, "b");
    a.set(a_buf);
    b.set(b_buf);

    Var x("x"), xo("xo"), xi("xi");
    RDom r(0, K, "r");
    RVar ro("ro"), ri("ri");
    Func out("out_" + name);
    out(x) = 7;
    out(x) += cast<int32_t>(a(K * x + r)) * cast<int32_t>(b(K * x + r));
    out.bound(x, 0, N).vectorize(x, lanes);
    out.update()
        .split(r, ro, ri, 8)
        .split(x, xo, xi, lanes)
        .reorder(ri, xi, ro, xo)
        .atomic()
        .vectorize(ri)
        .vectorize(xi);

    Buffer<int32_t> result = out.realize({N});
    for (int i = 0; i < N; i++) {
        int32_t correct = 7;
        for (int k = 0; k < K; k++) {
            correct += (int32_t)a_buf(K * i + k) * (int32_t)b_buf(K * i + k);
        }
        if (result(i) != correct) {
            printf("%s: out(%d) = %d instead of %d\n", name.c_str(), i, result(i), correct);
            return 1;
        }
    }

    if (expect_instructions() &&
        compile_to_asm(out, {a, b}, name).find("usdot") == std::string::npos) {
        printf("%s: expected usdot in the generated assembly\n", name.c_str());
        return 1;
    }
    return 0;
}

// out(y) = sum_k m(k, y) * v(k), with the same four coefficients from v in
// every lane. On ARM, the unsigned operand of usdot must come first, so which
// operand is broadcast depends on the signedness. (LLVM may then use the
// indexed form of usdot or sudot, or broadcast with ld1r and use the vector
// form of usdot.)
template<typename M, typename V>
int test_broadcast_form(const std::string &name) {
    Buffer<M> m_buf = make_input<M>(K, N);
    Buffer<V> v_buf = make_input<V>(K);
    ImageParam m(type_of<M>(), 2, "m"), v(type_of<V>(), 1, "v");
    m.set(m_buf);
    v.set(v_buf);

    Var y("y"), yo("yo"), yi("yi");
    RDom k(0, K, "k");
    RVar ko("ko"), ki("ki");
    Func out("out_" + name);
    out(y) = 0;
    out(y) += cast<int32_t>(m(k, y)) * cast<int32_t>(v(k));
    out.bound(y, 0, N).vectorize(y, 4);
    out.update()
        .split(k, ko, ki, 4)
        .split(y, yo, yi, 4)
        .reorder(ki, yi, ko, yo)
        .atomic()
        .vectorize(ki)
        .vectorize(yi);

    Buffer<int32_t> result = out.realize({N});
    for (int j = 0; j < N; j++) {
        int32_t correct = 0;
        for (int i = 0; i < K; i++) {
            correct += (int32_t)m_buf(i, j) * (int32_t)v_buf(i);
        }
        if (result(j) != correct) {
            printf("%s: out(%d) = %d instead of %d\n", name.c_str(), j, result(j), correct);
            return 1;
        }
    }

    if (expect_instructions()) {
        std::string assembly = compile_to_asm(out, {m, v}, name);
        if (assembly.find("usdot") == std::string::npos &&
            assembly.find("sudot") == std::string::npos) {
            printf("%s: expected usdot or sudot in the generated assembly\n", name.c_str());
            return 1;
        }
    }
    return 0;
}

// The instructions need arm_i8mm, which Armv8.6-A implies. This only compiles,
// so it runs on any host.
int test_target_gating() {
    ImageParam a(UInt(8), 1, "a"), b(Int(8), 1, "b");
    Var x("x"), xo("xo"), xi("xi");
    RDom r(0, K, "r");
    RVar ro("ro"), ri("ri");
    Func out("gating");
    out(x) = 0;
    out(x) += cast<int32_t>(a(K * x + r)) * cast<int32_t>(b(K * x + r));
    out.update()
        .split(r, ro, ri, 4)
        .split(x, xo, xi, 4)
        .reorder(ri, xi, ro, xo)
        .atomic()
        .vectorize(ri)
        .vectorize(xi);

    const std::vector<std::pair<std::string, bool>> cases = {
        {"arm-64-linux-arm_dot_prod", false},
        {"arm-64-linux-arm_dot_prod-arm_i8mm", true},
        {"arm-64-linux-armv86a", true},
    };
    for (size_t i = 0; i < cases.size(); i++) {
        const auto &[target, expected] = cases[i];
        std::string assembly = compile_to_asm(out, {a, b}, "gating_" + std::to_string(i), Target(target));
        bool found = assembly.find("usdot") != std::string::npos;
        if (found != expected) {
            printf("%s: expected usdot to be %s\n", target.c_str(), expected ? "used" : "unused");
            return 1;
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (test_target_gating()) {
        return 1;
    }

    for (int lanes : {2, 4, 8}) {
        std::string suffix = "_x" + std::to_string(lanes);
        if (test_vector_form<uint8_t, int8_t>("u8_i8" + suffix, lanes) ||
            test_vector_form<int8_t, uint8_t>("i8_u8" + suffix, lanes)) {
            return 1;
        }
    }

    if (test_broadcast_form<uint8_t, int8_t>("broadcast_u8_i8") ||
        test_broadcast_form<int8_t, uint8_t>("broadcast_i8_u8")) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
