// Integer division and modulus of 32-bit values by small constants is lowered
// to a multiply by a magic number, keeping the high half of the 64-bit
// product (mul_shift_right). The Vulkan backend must emit that as a 32-bit
// OpUMulExtended/OpSMulExtended instead of widening to 64-bit integers, so
// such pipelines compile for Vulkan targets without vk_int64.
//
// Division never produces a signed mul_shift_right, nor one with q == 32, so
// those are checked by calling mul_shift_right directly. A shift right of an
// unsigned value by a signed amount is checked too, as negative amounts must
// shift left.
//
// The first part only compiles (forcing a Vulkan target, as
// vulkan_bool_scalar_abi does), the second part runs on a Vulkan device if
// the JIT target has one, and compares against the CPU.

#include "Halide.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Halide;

namespace {

// A spread of numerators around zero and near the extremes of the type,
// without signed integer overflow.
Expr numerator(Type t, const Expr &x, const Expr &y) {
    Expr v = cast(t, (x - 512) * 7919 + (y - 4) * 0x10001 * 997);
    Expr low, high;
    if (t.is_int()) {
        low = -0x7ff00000 - x * 977;
        high = 0x7ff00000 + x * 977;
    } else {
        low = cast(t, x * 977);
        high = Expr((uint32_t)0xfff00000u) + cast(t, x * 977);
    }
    return select(y == 0, low, y == 1, high, v);
}

struct Case {
    Type t;
    // A divisor, or a shift for mul_shift_right if negative.
    int divisor_or_shift;
    // Shift right an unsigned value by a signed amount instead.
    bool signed_shift = false;

    std::string name() const {
        std::string type = t.is_int() ? "int32" : "uint32";
        if (signed_shift) {
            return type + " >> int32";
        } else if (divisor_or_shift < 0) {
            return "mul_shift_right(" + type + ", " + type + ", " + std::to_string(-divisor_or_shift) + ")";
        } else {
            return type + " / " + std::to_string(divisor_or_shift);
        }
    }
};

Func make_pipeline(const Case &c, int vector_width, bool gpu) {
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func in("in"), f("f");
    Type t = c.t;
    in(x, y) = numerator(t, x, y);
    if (c.signed_shift) {
        f(x, y) = in(x, y) >> (y - 8);
    } else if (c.divisor_or_shift < 0) {
        Expr b = numerator(t, 1023 - x, y ^ 1);
        f(x, y) = mul_shift_right(in(x, y), b, -c.divisor_or_shift);
    } else {
        Expr d = cast(t, c.divisor_or_shift);
        f(x, y) = Tuple(in(x, y) / d, in(x, y) % d);
    }
    if (gpu) {
        f.gpu_tile(x, y, xi, yi, 16, 4);
        if (vector_width > 1) {
            f.vectorize(xi, vector_width);
        }
    }
    return f;
}

}  // namespace

int main(int argc, char **argv) {
    const std::vector<Type> types = {Int(32), UInt(32)};
    const std::vector<int> divisors = {3, 5, 6, 7, 10, 12, 25, 100, 127, 255};
    const std::vector<int> shifts = {32, 33, 35, 40, 47};
    const std::vector<int> vector_widths = {1, 4};

    std::vector<Case> cases;
    for (Type t : types) {
        for (int d : divisors) {
            cases.push_back({t, d});
        }
        for (int q : shifts) {
            cases.push_back({t, -q});
        }
    }
    cases.push_back({UInt(32), 0, true});

    // Part 1: compiling must not require 64-bit integer support.
    Target vk = get_host_target()
                    .with_feature(Target::Vulkan)
                    .without_feature(Target::VulkanInt64);
    for (const Case &c : cases) {
        for (int w : vector_widths) {
            Func f = make_pipeline(c, w, true);
            try {
                f.compile_to_module({}, "vulkan_div_mod_no_int64", vk);
            } catch (const Halide::Error &e) {
                printf("Compiling %s (vector width %d) for %s failed:\n%s\n",
                       c.name().c_str(), w, vk.to_string().c_str(), e.what());
                return 1;
            }
        }
    }

    // Part 2: run it, if we have a Vulkan device.
    Target jit = get_jit_target_from_environment();
    if (!jit.has_feature(Target::Vulkan)) {
        printf("[SKIP] No Vulkan target enabled; only checked compilation.\n");
        printf("Success!\n");
        return 0;
    }
    jit = jit.without_feature(Target::VulkanInt64);

    const int W = 1024, H = 16;
    for (const Case &c : cases) {
        for (int w : vector_widths) {
            Func ref = make_pipeline(c, w, false);
            Func gpu = make_pipeline(c, w, true);
            Realization r_ref = ref.realize({W, H}, get_host_target());
            Realization r_gpu = gpu.realize({W, H}, jit);
            for (size_t k = 0; k < r_ref.size(); k++) {
                Buffer<> b_ref = r_ref[k];
                Buffer<> b_gpu = r_gpu[k];
                b_gpu.copy_to_host();
                for (int y = 0; y < H; y++) {
                    for (int x = 0; x < W; x++) {
                        int64_t e, a;
                        if (c.t.is_int()) {
                            e = Buffer<int32_t>(b_ref)(x, y);
                            a = Buffer<int32_t>(b_gpu)(x, y);
                        } else {
                            e = Buffer<uint32_t>(b_ref)(x, y);
                            a = Buffer<uint32_t>(b_gpu)(x, y);
                        }
                        if (e != a) {
                            printf("Mismatch for %s, vector width %d, output %d at (%d, %d): "
                                   "expected %lld, got %lld\n",
                                   c.name().c_str(), w, (int)k, x, y,
                                   (long long)e, (long long)a);
                            return 1;
                        }
                    }
                }
            }
        }
    }

    printf("Success!\n");
    return 0;
}
