#include "Halide.h"
#include <cstdio>
#include <regex>
#include <type_traits>

using namespace Halide;

// A dp4a's operand (here: of a vectorized sum of its elements widened to 32
// bits) that is a sum, difference or shift of vectors of 8- or 16-bit
// integers filling a 32- or 64-bit register is computed in that register,
// also where the arithmetic is done wider and narrowed (4-bit codes offset by
// 8, as int8): PTX has no vector registers, and the backend otherwise
// extracts each element (prmt), operates on it and packs the results again.
// Likewise the 16-bit operand of a dp2a, here of a dot product with 8-bit
// weights.
enum Op { Add,
          Sub,
          Nibbles,
          ShiftLeft,
          DivPow2,
          Bitwise,
          ModWidth,
          SignedShift };

const char *name(Op op) {
    const char *names[] = {"add", "sub", "nibbles", "shift left", "divide by 4", "bitwise", "mod by the field's width", "signed shift right"};
    return names[op];
}

// A shift right of signed fields has to fill each with its own sign bit,
// which the register's shift does not do: it is computed element by element.
bool in_register(Op op) {
    return op != SignedShift;
}

template<typename A>
A apply(Op op, A a, A b) {
    using U = std::make_unsigned_t<A>;
    using S = std::make_signed_t<A>;
    const int bits = sizeof(A) * 8;
    switch (op) {
    case Add:
        return (A)(a + b);
    case Sub:
        return (A)(a - b);
    case Nibbles:
        return (A)((int8_t)((int16_t)((uint8_t)a >> 4) - 8) + (int8_t)((int16_t)((uint8_t)b % 16) - 8));
    case ShiftLeft:
        return (A)((U)((U)a << 3) - b);
    case DivPow2:
        return (A)((U)a / 4 + b);
    case Bitwise:
        return (A)(((a ^ b) | (a & (A)0x35)) + ~b);
    case ModWidth:
        return (A)(((uint32_t)(U)a + (uint32_t)(U)b + 300) % ((uint32_t)1 << bits));
    case SignedShift:
        return (A)((S)((S)a >> 2) + (S)b);
    }
    return 0;
}

template<typename A>
Expr apply(Op op, const Expr &a, const Expr &b) {
    const Type t = type_of<A>(), u = t.with_code(Type::UInt), s = t.with_code(Type::Int);
    switch (op) {
    case Add:
        return a + b;
    case Sub:
        return a - b;
    case Nibbles:
        return cast<A>(cast<int8_t>(cast<int16_t>(cast<uint8_t>(a) >> 4) - 8) + cast<int8_t>(cast<int16_t>(cast<uint8_t>(b) % 16) - 8));
    case ShiftLeft:
        return cast<A>(cast(u, a) << 3) - b;
    case DivPow2:
        return cast<A>(cast(u, a) / 4) + b;
    case Bitwise:
        return ((a ^ b) | (a & cast<A>(0x35))) + ~b;
    case ModWidth:
        return cast<A>((cast(u.widen(), cast(u, a)) + cast(u.widen(), cast(u, b)) + 300) % (1 << t.bits()));
    case SignedShift:
        return cast<A>((cast(s, a) >> 2) + cast(s, b));
    }
    return Expr();
}

// `weighted`: each element times an 8-bit weight, which is what a dp2a takes
// its 16-bit operand with.
template<typename A>
int test(const Target &t, int vec, Op op, bool weighted = false) {
    Func a, b, w, g, h;
    Var x, y, xi, yi;
    a(x, y) = cast<A>(x * 37 - y * 11);
    b(x, y) = cast<A>(x * 101 + y * 29 + 3);
    w(x, y) = cast<int8_t>(x * 13 + y * 7 - 5);
    a.compute_root();
    b.compute_root();
    w.compute_root();
    RDom r(0, 64);
    Expr e = cast<int32_t>(apply<A>(op, a(r, x + y), b(r, x + y)));
    g(x, y) += weighted ? e * w(r, x + y) : e;
    h(x, y) = g(x, y);
    g.update().atomic().vectorize(r, vec).unroll(r);
    h.gpu_tile(x, y, xi, yi, 32, 8, TailStrategy::RoundUp);
    Buffer<int32_t> out = h.realize({64, 64}, t);
    out.copy_to_host();
    const int bits = sizeof(A) * 8;
    for (int y = 0; y < out.height(); y++) {
        for (int x = 0; x < out.width(); x++) {
            int32_t correct = 0;
            for (int k = 0; k < 64; k++) {
                A ea = (A)(k * 37 - (x + y) * 11), eb = (A)(k * 101 + (x + y) * 29 + 3);
                const int32_t weight = weighted ? (int8_t)(k * 13 + (x + y) * 7 - 5) : 1;
                correct += (int32_t)apply(op, ea, eb) * weight;
            }
            if (out(x, y) != correct) {
                printf("%d x %d bits, %s: out(%d, %d) = %d instead of %d\n", vec, bits, name(op), x, y, out(x, y), correct);
                return 1;
            }
        }
    }
    if ((bits == 8 || weighted) && t.get_cuda_capability_lower_bound() >= 61) {
        Buffer<uint8_t> buf = h.compile_to_module(std::vector<Argument>(), "h", t).compile_to_buffer();
        std::regex per_element("prmt[.]b32|(add|sub|shr|shl)[.][us]16"), dot(bits == 8 ? "dp4a[.][us]32[.][us]32" : "dp2a[.](lo|hi)[.][us]32[.][us]32");
        const bool found_per_element = std::regex_search((const char *)buf.begin(), (const char *)buf.end(), per_element);
        if (!std::regex_search((const char *)buf.begin(), (const char *)buf.end(), dot)) {
            printf("%d x %d bits, %s: the PTX has no dot product instruction. Rerun with HL_DEBUG_CODEGEN=1 to debug\n", vec, bits, name(op));
            return 1;
        }
        if (found_per_element == in_register(op)) {
            printf("%d x %d bits, %s: the PTX %s element by element. Rerun with HL_DEBUG_CODEGEN=1 to debug\n", vec, bits, name(op),
                   found_per_element ? "computes" : "does not compute");
            return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] Cuda is not enabled in target: %s\n", t.to_string().c_str());
        return 0;
    }
    for (Op op : {Add, Sub, ShiftLeft, DivPow2, Bitwise, ModWidth, SignedShift}) {
        if (test<int8_t>(t, 4, op) || test<uint8_t>(t, 4, op) || test<int8_t>(t, 8, op) || test<uint8_t>(t, 8, op) ||
            test<int16_t>(t, 2, op) || test<uint16_t>(t, 4, op) ||
            test<int16_t>(t, 4, op, true) || test<uint16_t>(t, 4, op, true)) {
            return 1;
        }
    }
    if (test<int8_t>(t, 4, Nibbles) || test<int8_t>(t, 8, Nibbles)) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
