#include "Halide.h"

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// The masks of f's definition (0: pure, i: update i - 1), sorted.
std::vector<uint64_t> masks(const Func &f, int stage, const std::string &var, int bits) {
    const Definition &d = stage ? f.function().update(stage - 1) : f.function().definition();
    std::vector<uint64_t> m = load_invariant_bits(d, var, bits);
    std::sort(m.begin(), m.end());
    return m;
}

bool check(const char *name, const std::vector<uint64_t> &got, std::vector<uint64_t> want) {
    std::sort(want.begin(), want.end());
    if (got == want) {
        return true;
    }
    printf("%s: got", name);
    for (uint64_t m : got) {
        printf(" %llx", (unsigned long long)m);
    }
    printf(", want");
    for (uint64_t m : want) {
        printf(" %llx", (unsigned long long)m);
    }
    printf("\n");
    return false;
}

}  // namespace

int main(int argc, char **argv) {
    ImageParam qs(UInt(8), 1, "qs"), qh(UInt(8), 1, "qh"), a(Int(8), 1, "a");
    bool ok = true;

    // Two nibbles per byte, the byte's two values 16 apart (q4_0): bit 4
    // leaves the code's load in place, every bit moves the act's.
    {
        Func f("f");
        RDom r(0, 32);
        f() = 0;
        f() += cast<int>((qs(r % 16) >> (r / 16 * 4)) & 15) * a(r);
        ok &= check("nibbles", masks(f, 1, r.x.name(), 5), {0x1f, 0x10, 0});
    }

    // A 6-bit code from two fields (q6_K): its low nibble's byte is shared by
    // values 64 apart, the high bits' by values 32 and 64 apart. The fields
    // are read through an inlined Func, under a let, a likely and a promise.
    {
        Var x("x");
        Func code("code"), f("f");
        Expr lo = Let::make("i", 64 * (x / 128) + x % 64, qs(likely(Variable::make(Int(32), "i"))));
        Expr hi = qh(unsafe_promise_clamped(32 * (x / 128) + x % 32, 0, 63));
        code(x) = ((lo >> (x / 64 % 2 * 4)) & 15) | (((hi >> (x / 32 % 4 * 2)) & 3) << 4);
        RDom r(0, 256);
        f() = 0;
        f() += cast<int>(code(r)) * a(r);
        ok &= check("fields", masks(f, 1, r.x.name(), 8), {0xff, 0x40, 0x60, 0});
    }

    // A Func with an update is a load; a pure Var's bits.
    {
        Var x("x"), v("v");
        Func g("g"), f("f");
        g(x) = cast<int>(qs(x));
        g(x) += 1;
        f(v) = g(v / 4) + qh(v % 4) + qh(0);
        ok &= check("pure var", masks(f, 0, v.name(), 3), {0x3, 0x4, 0x7});
    }

    // A struct's array field: the record's arguments, then the field's and
    // the element's index (two codes per byte, 32 values per record).
    {
        Type record = Type::Struct({{"d", Float(16)}, {"q", UInt(8), 16}});
        ImageParam w(record, 1, "w");
        Func f("f");
        RDom r(0, 32, 0, 8);
        Expr rec = w(r.y);
        f() = 0;
        f() += cast<int>((field(rec, "q")[r.x % 16] >> (r.x / 16 * 4)) & 15) * a(r.y * 32 + r.x) + cast<int>(field(rec, "d"));
        ok &= check("struct", masks(f, 1, r.x.name(), 5), {0x1f, 0x10, 0, 0x1f});
    }

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
