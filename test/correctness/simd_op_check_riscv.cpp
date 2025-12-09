#include "simd_op_check.h"

#include "Halide.h"

#include <iostream>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

using namespace Halide;
using namespace Halide::ConciseCasts;

namespace {

class SimdOpCheckRISCV : public SimdOpCheckTest {
public:
    SimdOpCheckRISCV(Target t, int w = 768, int h = 128)
        : SimdOpCheckTest(t, w, h) {
    }

    void add_tests() override {
        if (target.arch == Target::RISCV &&
            target.has_feature(Target::RVV)) {
            check_rvv_all();
        }
    }

    Expr uint_image_param(int bit_width, Expr index) {
        switch (bit_width) {
        case 8:
            return in_u8(index);
            break;
        case 16:
            return in_u16(index);
            break;
        case 32:
            return in_u32(index);
            break;
        case 64:
            return in_u64(index);
            break;
        }
        return Expr();
    }

    Expr int_image_param(int bit_width, Expr index) {
        switch (bit_width) {
        case 8:
            return in_i8(index);
            break;
        case 16:
            return in_i16(index);
            break;
        case 32:
            return in_i32(index);
            break;
        case 64:
            return in_i64(index);
            break;
        }
        return Expr();
    }

    void check_rvv_integer_bits(int base_bit_width, int lanes, std::string /* lmul_check */) {
        Expr i_1 = int_image_param(base_bit_width, x);
        Expr i_2 = int_image_param(base_bit_width, x + 16);
        Expr u_1 = uint_image_param(base_bit_width, x);
        Expr u_2 = uint_image_param(base_bit_width, x + 16);

        // Basic math operations.
        check("vadd.vv", lanes, i_1 + i_2);
        check("vadd.vv", lanes, u_1 + u_2);

        // Vector + immediate / scalar form. Disabled because LLVM 18 broadcasts
        // scalars to vectors registers outside the loop.
        // check("vadd.vi", lanes, i_1 + 2);
        // check("vadd.vi", lanes, u_1 + 2);
        // check("vadd.vx", lanes, i_1 + 42);
        // check("vadd.vx", lanes, u_1 + 42);

        check("vsub.vv", lanes, i_1 - i_2);
        check("vsub.vv", lanes, u_1 - u_2);
        // TODO: these seem to compile to a vector add
        // for some lanes/sizes.
        // check("vsub.v", lanes, i_1 - 42);
        // check("vsub.v", lanes, u_1 - 42);
        check("vmul.vv", lanes, i_1 * i_2);
        check("vmul.vv", lanes, u_1 * u_2);
        check("vmul.v", lanes, i_1 * 42);
        check("vmul.v", lanes, u_1 * 42);

        // Intrinsics mapping.
        check("vmseq.vv", lanes, select(i_1 == i_2, cast(Int(base_bit_width), 1), cast(Int(base_bit_width), 2)));
        check("vmseq.vv", lanes, select(i_1 == i_2, cast(UInt(base_bit_width), 1), cast(UInt(base_bit_width), 2)));
        check("vaadd.vv", lanes, halving_add(i_1, i_2));
        check("vaaddu.vv", lanes, halving_add(u_1, u_2));
        check("vaadd.vv", lanes, rounding_halving_add(i_1, i_2));
        check("vaaddu.vv", lanes, rounding_halving_add(u_1, u_2));

        // Widening intrinsics
        if (base_bit_width < 64) {
            Expr i_2xbits_1 = int_image_param(base_bit_width * 2, x);
            Expr i_2xbits_2 = int_image_param(base_bit_width * 2, x + 16);
            Expr u_2xbits_1 = uint_image_param(base_bit_width * 2, x);
            Expr u_2xbits_2 = uint_image_param(base_bit_width * 2, x + 16);

            check("vwadd.vv", lanes, widening_add(i_1, i_2));
            check("vwaddu.vv", lanes, widening_add(u_1, u_2));
            check("vwsub.vv", lanes, widening_sub(i_1, i_2));
            check("vwsubu.vv", lanes, widening_sub(u_1, u_2));
            check("vwmul.vv", lanes, widening_mul(i_1, i_2));
            check("vwmulu.vv", lanes, widening_mul(u_1, u_2));
        }
    }

    void check_rvv_all() {
        for (int i = 3; i < 7; i++) {
            int bit_width = (1 << i);
            int natural_lanes = target.vector_bits / bit_width;
            // TODO: This should work for all lanes from 2 to 8 * natural_lanes
            // but the vector predication paths require using vscale multiples.
            // This is using powers of two rather than vscale multiples for
            // some other issue which needs to be fixed.
            for (int lanes = std::max(2, 64 / bit_width);
                 lanes < (natural_lanes * 8);
                 lanes *= 2) {
                check_rvv_integer_bits(bit_width, lanes, "");
            }
        }
    }

private:
    const Var x{"x"}, y{"y"};
};
}  // namespace

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckRISCV>(
        argc, argv,
        {
            // IMPORTANT:
            // When adding new targets here, make sure to also update
            // can_run_code in simd_op_check.h to include any new features used.

            Target("riscv-64-linux-rvv-vector_bits_128"),
            Target("riscv-64-linux-rvv-vector_bits_512"),
        });
}
