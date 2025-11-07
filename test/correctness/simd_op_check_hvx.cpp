#include "Halide.h"
#include "simd_op_check.h"
// simd_op_check is different from all/most other tests in the testsuite because
// simd_op_check is not an 'offload' test. In other words, it runs SIMD tests
// for the architecture that is the host architecture in HL_TARGET.
// However, the buildbots are configured to test for HVX as an offload device
// i.e HL_TARGET and HL_JIT_TARGET, for instance, are host-hvx. This works
// fine for all the tests except simd_op_check because with HL_TARGET=host-hvx
// we end up running host tests and not HVX tests.
//
// One way of fixing this is to change the buildbot recipe. However, this would
// mean one exception for one test for one architecture. Instead, we refactor
// simd_op_check into two tests, simd_op_check.cpp and simd_op_check_hvx.cpp
// so that the latter is free to do its own thing - for simd_op_check_hvx.cpp
// to run any tests, all that is needed is that HL_TARGET have a HVX related
// target feature, i.e. one of HVX, HVX_v62, HVX_v65, HVX_v66 and HVX_v68.

using namespace Halide;
using namespace Halide::ConciseCasts;

class SimdOpCheckHVX : public SimdOpCheckTest {
public:
    SimdOpCheckHVX(Target t, int w = 768 /*256*3*/, int h = 128)
        : SimdOpCheckTest(t, w, h) {
    }
    int image_param_alignment() override {
        return 128;
    }

    void add_tests() override {
        Expr f32_1 = in_f32(x), f32_2 = in_f32(x + 16), f32_3 = in_f32(x + 32);
        Expr f64_1 = in_f64(x), f64_2 = in_f64(x + 16), f64_3 = in_f64(x + 32);
        Expr i8_1 = in_i8(x), i8_2 = in_i8(x + 16), i8_3 = in_i8(x + 32), i8_4 = in_i8(x + 48);
        Expr u8_1 = in_u8(x), u8_2 = in_u8(x + 16), u8_3 = in_u8(x + 32), u8_4 = in_u8(x + 48);
        Expr u8_even = in_u8(2 * x), u8_odd = in_u8(2 * x + 1);
        Expr i16_1 = in_i16(x), i16_2 = in_i16(x + 16), i16_3 = in_i16(x + 32);
        Expr u16_1 = in_u16(x), u16_2 = in_u16(x + 16), u16_3 = in_u16(x + 32);
        Expr i32_1 = in_i32(x), i32_2 = in_i32(x + 16), i32_3 = in_i32(x + 32);
        Expr u32_1 = in_u32(x), u32_2 = in_u32(x + 16), u32_3 = in_u32(x + 32);
        Expr i64_1 = in_i64(x), i64_2 = in_i64(x + 16), i64_3 = in_i64(x + 32);
        Expr u64_1 = in_u64(x), u64_2 = in_u64(x + 16), u64_3 = in_u64(x + 32);
        Expr bool_1 = (f32_1 > 0.3f), bool_2 = (f32_2 < -0.3f), bool_3 = (f32_3 != -0.34f);

        constexpr int hvx_width = 128;

        int isa_version;
        if (target.has_feature(Halide::Target::HVX_v68)) {
            isa_version = 68;
        } else if (target.has_feature(Halide::Target::HVX_v66)) {
            isa_version = 66;
        } else if (target.has_feature(Halide::Target::HVX_v65)) {
            isa_version = 65;
        } else {
            isa_version = 62;
        }

        // Verify that unaligned loads use the right instructions, and don't try to use
        // immediates of more than 3 bits.
        check("valign(v*,v*,#7)", hvx_width / 1, in_u8(x + 7));
        check("vlalign(v*,v*,#7)", hvx_width / 1, in_u8(x + hvx_width - 7));
        check("valign(v*,v*,r*)", hvx_width / 1, in_u8(x + 8));
        check("valign(v*,v*,r*)", hvx_width / 1, in_u8(x + hvx_width - 8));
        check("valign(v*,v*,#6)", hvx_width / 1, in_u16(x + 3));
        check("vlalign(v*,v*,#6)", hvx_width / 1, in_u16(x + hvx_width - 3));
        check("valign(v*,v*,r*)", hvx_width / 1, in_u16(x + 4));
        check("valign(v*,v*,r*)", hvx_width / 1, in_u16(x + hvx_width - 4));

        check("vunpack(v*.ub)", hvx_width / 1, u16(u8_1));
        check("vunpack(v*.ub)", hvx_width / 1, i16(u8_1));
        check("vunpack(v*.uh)", hvx_width / 2, u32(u16_1));
        check("vunpack(v*.uh)", hvx_width / 2, i32(u16_1));
        check("vunpack(v*.b)", hvx_width / 1, u16(i8_1));
        check("vunpack(v*.b)", hvx_width / 1, i16(i8_1));
        check("vunpack(v*.h)", hvx_width / 2, u32(i16_1));
        check("vunpack(v*.h)", hvx_width / 2, i32(i16_1));

        check("vunpack(v*.ub)", hvx_width / 1, u32(u8_1));
        check("vunpack(v*.ub)", hvx_width / 1, i32(u8_1));
        check("vunpack(v*.b)", hvx_width / 1, u32(i8_1));
        check("vunpack(v*.b)", hvx_width / 1, i32(i8_1));

#if 0
        // It's quite difficult to write a single expression that tests vzxt
        // and vsxt, because it gets rewritten as vpack/vunpack.
        check("vzxt(v*.ub)", hvx_width/1, u16(u8_1));
        check("vzxt(v*.ub)", hvx_width/1, i16(u8_1));
        check("vzxt(v*.uh)", hvx_width/2, u32(u16_1));
        check("vzxt(v*.uh)", hvx_width/2, i32(u16_1));
        check("vsxt(v*.b)", hvx_width/1, u16(i8_1));
        check("vsxt(v*.b)", hvx_width/1, i16(i8_1));
        check("vsxt(v*.h)", hvx_width/2, u32(i16_1));
        check("vsxt(v*.h)", hvx_width/2, i32(i16_1));

        check("vzxt(v*.ub)", hvx_width/1, u32(u8_1));
        check("vzxt(v*.ub)", hvx_width/1, i32(u8_1));
        check("vsxt(v*.b)", hvx_width/1, u32(i8_1));
        check("vsxt(v*.b)", hvx_width/1, i32(i8_1));
#endif
        check("vadd(v*.b,v*.b)", hvx_width / 1, u8_1 + u8_2);
        check("vadd(v*.h,v*.h)", hvx_width / 2, u16_1 + u16_2);
        check("vadd(v*.w,v*.w)", hvx_width / 4, u32_1 + u32_2);
        check("vadd(v*.b,v*.b)", hvx_width / 1, i8_1 + i8_2);
        check("vadd(v*.h,v*.h)", hvx_width / 2, i16_1 + i16_2);
        check("vadd(v*.w,v*.w)", hvx_width / 4, i32_1 + i32_2);
        check("v*.h = vadd(v*.ub,v*.ub)", hvx_width / 1, u16(u8_1) + u16(u8_2));
        check("v*.w = vadd(v*.uh,v*.uh)", hvx_width / 2, u32(u16_1) + u32(u16_2));
        check("v*.w = vadd(v*.h,v*.h)", hvx_width / 2, i32(i16_1) + i32(i16_2));
        check("vadd(v*.ub,v*.ub):sat", hvx_width / 1, u8_sat(u16(u8_1) + u16(u8_2)));
        check("vadd(v*.uh,v*.uh):sat", hvx_width / 2, u16_sat(u32(u16_1) + u32(u16_2)));
        check("vadd(v*.h,v*.h):sat", hvx_width / 2, i16_sat(i32(i16_1) + i32(i16_2)));
        check("vadd(v*.w,v*.w):sat", hvx_width / 4, i32_sat(i64(i32_1) + i64(i32_2)));
        check("vadd(v*.uw,v*.uw):sat", hvx_width / 4, u32_sat(u64(u32_1) + u64(u32_2)));

        check("vsub(v*.b,v*.b)", hvx_width / 1, u8_1 - u8_2);
        check("vsub(v*.h,v*.h)", hvx_width / 2, u16_1 - u16_2);
        check("vsub(v*.w,v*.w)", hvx_width / 4, u32_1 - u32_2);
        check("vsub(v*.b,v*.b)", hvx_width / 1, i8_1 - i8_2);
        check("vsub(v*.h,v*.h)", hvx_width / 2, i16_1 - i16_2);
        check("vsub(v*.w,v*.w)", hvx_width / 4, i32_1 - i32_2);
        check("v*.h = vsub(v*.ub,v*.ub)", hvx_width / 1, u16(u8_1) - u16(u8_2));
        check("v*:*.h = vsub(v*.ub,v*.ub)", hvx_width / 1, i16(u8_1) - i16(u8_2));
        check("v*.w = vsub(v*.uh,v*.uh)", hvx_width / 2, u32(u16_1) - u32(u16_2));
        check("v*:*.w = vsub(v*.uh,v*.uh)", hvx_width / 2, i32(u16_1) - i32(u16_2));
        check("v*.w = vsub(v*.h,v*.h)", hvx_width / 2, i32(i16_1) - i32(i16_2));
        check("vsub(v*.ub,v*.ub):sat", hvx_width / 1, u8_sat(i16(u8_1) - i16(u8_2)));
        check("vsub(v*.uh,v*.uh):sat", hvx_width / 2, u16_sat(i32(u16_1) - i32(u16_2)));
        check("vsub(v*.h,v*.h):sat", hvx_width / 2, i16_sat(i32(i16_1) - i32(i16_2)));
        check("vsub(v*.w,v*.w):sat", hvx_width / 4, i32_sat(i64(i32_1) - i64(i32_2)));

        // Double vector versions of the above
        check("vadd(v*:*.b,v*:*.b)", hvx_width * 2, u8_1 + u8_2);
        check("vadd(v*:*.h,v*:*.h)", hvx_width / 1, u16_1 + u16_2);
        check("vadd(v*:*.w,v*:*.w)", hvx_width / 2, u32_1 + u32_2);
        check("vadd(v*:*.b,v*:*.b)", hvx_width * 2, i8_1 + i8_2);
        check("vadd(v*:*.h,v*:*.h)", hvx_width / 1, i16_1 + i16_2);
        check("vadd(v*:*.w,v*:*.w)", hvx_width / 2, i32_1 + i32_2);
        check("vadd(v*:*.ub,v*:*.ub):sat", hvx_width * 2, u8_sat(u16(u8_1) + u16(u8_2)));
        check("vadd(v*:*.uh,v*:*.uh):sat", hvx_width / 1, u16_sat(u32(u16_1) + u32(u16_2)));
        check("vadd(v*:*.h,v*:*.h):sat", hvx_width / 1, i16_sat(i32(i16_1) + i32(i16_2)));
        check("vadd(v*:*.w,v*:*.w):sat", hvx_width / 2, i32_sat(i64(i32_1) + i64(i32_2)));
        check("vadd(v*:*.uw,v*:*.uw):sat", hvx_width / 2, u32_sat(u64(u32_1) + u64(u32_2)));

        check("vsub(v*:*.b,v*:*.b)", hvx_width * 2, u8_1 - u8_2);
        check("vsub(v*:*.h,v*:*.h)", hvx_width / 1, u16_1 - u16_2);
        check("vsub(v*:*.w,v*:*.w)", hvx_width / 2, u32_1 - u32_2);
        check("vsub(v*:*.b,v*:*.b)", hvx_width * 2, i8_1 - i8_2);
        check("vsub(v*:*.h,v*:*.h)", hvx_width / 1, i16_1 - i16_2);
        check("vsub(v*:*.w,v*:*.w)", hvx_width / 2, i32_1 - i32_2);
        check("vsub(v*:*.ub,v*:*.ub):sat", hvx_width * 2, u8_sat(i16(u8_1) - i16(u8_2)));
        check("vsub(v*:*.uh,v*:*.uh):sat", hvx_width / 1, u16_sat(i32(u16_1) - i32(u16_2)));
        check("vsub(v*:*.h,v*:*.h):sat", hvx_width / 1, i16_sat(i32(i16_1) - i32(i16_2)));
        check("vsub(v*:*.w,v*:*.w):sat", hvx_width / 2, i32_sat(i64(i32_1) - i64(i32_2)));

        check("vavg(v*.ub,v*.ub)", hvx_width / 1, u8((u16(u8_1) + u16(u8_2)) / 2));
        check("vavg(v*.ub,v*.ub):rnd", hvx_width / 1, u8((u16(u8_1) + u16(u8_2) + 1) / 2));
        check("vavg(v*.uh,v*.uh)", hvx_width / 2, u16((u32(u16_1) + u32(u16_2)) / 2));
        check("vavg(v*.uh,v*.uh):rnd", hvx_width / 2, u16((u32(u16_1) + u32(u16_2) + 1) / 2));
        check("vavg(v*.h,v*.h)", hvx_width / 2, i16((i32(i16_1) + i32(i16_2)) / 2));
        check("vavg(v*.h,v*.h):rnd", hvx_width / 2, i16((i32(i16_1) + i32(i16_2) + 1) / 2));
        check("vavg(v*.w,v*.w)", hvx_width / 4, i32((i64(i32_1) + i64(i32_2)) / 2));
        check("vavg(v*.w,v*.w):rnd", hvx_width / 4, i32((i64(i32_1) + i64(i32_2) + 1) / 2));
        check("vnavg(v*.ub,v*.ub)", hvx_width / 1, i8((i16(u8_1) - i16(u8_2)) / 2));
        check("vnavg(v*.h,v*.h)", hvx_width / 2, i16((i32(i16_1) - i32(i16_2)) / 2));
        check("vnavg(v*.w,v*.w)", hvx_width / 4, i32((i64(i32_1) - i64(i32_2)) / 2));
        if (isa_version >= 65) {
            check("vavg(v*.b,v*.b)", hvx_width / 1, i8((i16(i8_1) + i16(i8_2)) / 2));
            check("vavg(v*.b,v*.b):rnd", hvx_width / 1, i8((i16(i8_1) + i16(i8_2) + 1) / 2));
            check("vavg(v*.uw,v*.uw)", hvx_width / 4, u32((u64(u32_1) + u64(u32_2)) / 2));
            check("vavg(v*.uw,v*.uw):rnd", hvx_width / 4, u32((u64(u32_1) + u64(u32_2) + 1) / 2));
            check("vnavg(v*.b,v*.b)", hvx_width / 1, i8((i16(i8_1) - i16(i8_2)) / 2));
        }

        // The behavior of shifts larger than the type behave differently
        // on HVX vs. the scalar processor, so we clamp.
        // Unsigned RHS shifts.
        check("vlsr(v*.h,v*.h)", hvx_width / 1, u8_1 >> (u8_2 % 8));
        check("vlsr(v*.h,v*.h)", hvx_width / 2, u16_1 >> (u16_2 % 16));
        check("vlsr(v*.w,v*.w)", hvx_width / 4, u32_1 >> (u32_2 % 32));
        check("vasr(v*.h,v*.h)", hvx_width / 1, i8_1 >> (u8_2 % 8));
        check("vasr(v*.h,v*.h)", hvx_width / 2, i16_1 >> (u16_2 % 16));
        check("vasr(v*.w,v*.w)", hvx_width / 4, i32_1 >> (u32_2 % 32));
        check("vasr(v*.h,v*.h,r*):sat", hvx_width / 1, u8_sat(i16_1 >> 4));
        check("vasr(v*.w,v*.w,r*):sat", hvx_width / 2, u16_sat(i32_1 >> 8));
        check("vasr(v*.w,v*.w,r*):sat", hvx_width / 2, i16_sat(i32_1 >> 8));
        check("vasr(v*.w,v*.w,r*)", hvx_width / 2, i16(i32_1 >> 8));
        check("vasl(v*.h,v*.h)", hvx_width / 1, u8_1 << (u8_2 % 8));
        check("vasl(v*.h,v*.h)", hvx_width / 2, u16_1 << (u16_2 % 16));
        check("vasl(v*.w,v*.w)", hvx_width / 4, u32_1 << (u32_2 % 32));
        check("vasl(v*.h,v*.h)", hvx_width / 1, i8_1 << (u8_2 % 8));
        check("vasl(v*.h,v*.h)", hvx_width / 2, i16_1 << (u16_2 % 16));
        check("vasl(v*.w,v*.w)", hvx_width / 4, i32_1 << (u32_2 % 32));
        // Signed RHS shifts.
        check("vlsr(v*.h,v*.h)", hvx_width / 1, u8_1 >> (i8_2 % 16 - 8));
        check("vlsr(v*.h,v*.h)", hvx_width / 2, u16_1 >> (i16_2 % 32 - 16));
        check("vlsr(v*.w,v*.w)", hvx_width / 4, u32_1 >> (i32_2 % 64 - 32));
        check("vasr(v*.h,v*.h)", hvx_width / 1, i8_1 >> (i8_2 % 16 - 8));
        check("vasr(v*.h,v*.h)", hvx_width / 2, i16_1 >> (i16_2 % 32 - 16));
        check("vasr(v*.w,v*.w)", hvx_width / 4, i32_1 >> (i32_2 % 64 - 32));
        check("vasl(v*.h,v*.h)", hvx_width / 1, u8_1 << (i8_2 % 16 - 8));
        check("vasl(v*.h,v*.h)", hvx_width / 2, u16_1 << (i16_2 % 32 - 16));
        check("vasl(v*.w,v*.w)", hvx_width / 4, u32_1 << (i32_2 % 64 - 32));
        check("vasl(v*.h,v*.h)", hvx_width / 1, i8_1 << (i8_2 % 16 - 8));
        check("vasl(v*.h,v*.h)", hvx_width / 2, i16_1 << (i16_2 % 32 - 16));
        check("vasl(v*.w,v*.w)", hvx_width / 4, i32_1 << (i32_2 % 64 - 32));

        // The scalar lsr generates uh/uw arguments, while the vector
        // version just generates h/w.
        // Unsigned RHS shifts.
        check("vlsr(v*.uh,r*)", hvx_width / 1, u8_1 >> (u8(y) % 8));
        check("vlsr(v*.uh,r*)", hvx_width / 2, u16_1 >> (u16(y) % 16));
        check("vlsr(v*.uw,r*)", hvx_width / 4, u32_1 >> (u32(y) % 32));
        check("vasr(v*.h,r*)", hvx_width / 1, i8_1 >> (u8(y) % 8));
        check("vasr(v*.h,r*)", hvx_width / 2, i16_1 >> (u16(y) % 16));
        check("vasr(v*.w,r*)", hvx_width / 4, i32_1 >> (u32(y) % 32));
        check("vasl(v*.h,r*)", hvx_width / 1, u8_1 << (u8(y) % 8));
        check("vasl(v*.h,r*)", hvx_width / 2, u16_1 << (u16(y) % 16));
        check("vasl(v*.w,r*)", hvx_width / 4, u32_1 << (u32(y) % 32));
        check("vasl(v*.h,r*)", hvx_width / 1, i8_1 << (u8(y) % 8));
        check("vasl(v*.h,r*)", hvx_width / 2, i16_1 << (u16(y) % 16));
        check("vasl(v*.w,r*)", hvx_width / 4, i32_1 << (u32(y) % 32));
        // Signed RHS shifts.
        check("vlsr(v*.uh,r*)", hvx_width / 1, u8_1 >> (i8(y) % 16 - 8));
        check("vlsr(v*.uh,r*)", hvx_width / 2, u16_1 >> (i16(y) % 32 - 16));
        check("vlsr(v*.uw,r*)", hvx_width / 4, u32_1 >> (i32(y) % 64 - 32));
        check("vasr(v*.h,r*)", hvx_width / 1, i8_1 >> (i8(y) % 16 - 8));
        check("vasr(v*.h,r*)", hvx_width / 2, i16_1 >> (i16(y) % 32 - 16));
        check("vasr(v*.w,r*)", hvx_width / 4, i32_1 >> (i32(y) % 64 - 32));
        check("vasl(v*.h,r*)", hvx_width / 1, u8_1 << (i8(y) % 16 - 8));
        check("vasl(v*.h,r*)", hvx_width / 2, u16_1 << (i16(y) % 32 - 16));
        check("vasl(v*.w,r*)", hvx_width / 4, u32_1 << (i32(y) % 64 - 32));
        check("vasl(v*.h,r*)", hvx_width / 1, i8_1 << (i8(y) % 16 - 8));
        check("vasl(v*.h,r*)", hvx_width / 2, i16_1 << (i16(y) % 32 - 16));
        check("vasl(v*.w,r*)", hvx_width / 4, i32_1 << (i32(y) % 64 - 32));

        check("vpacke(v*.h,v*.h)", hvx_width / 1, u8(u16_1));
        check("vpacke(v*.h,v*.h)", hvx_width / 1, u8(i16_1));
        check("vpacke(v*.h,v*.h)", hvx_width / 1, i8(u16_1));
        check("vpacke(v*.h,v*.h)", hvx_width / 1, i8(i16_1));
        check("vpacke(v*.w,v*.w)", hvx_width / 2, u16(u32_1));
        check("vpacke(v*.w,v*.w)", hvx_width / 2, u16(i32_1));
        check("vpacke(v*.w,v*.w)", hvx_width / 2, i16(u32_1));
        check("vpacke(v*.w,v*.w)", hvx_width / 2, i16(i32_1));

        check("vpacko(v*.h,v*.h)", hvx_width / 1, u8(u16_1 >> 8));
        check("vpacko(v*.h,v*.h)", hvx_width / 1, u8(i16_1 >> 8));
        check("vpacko(v*.h,v*.h)", hvx_width / 1, i8(u16_1 >> 8));
        check("vpacko(v*.h,v*.h)", hvx_width / 1, i8(i16_1 >> 8));
        check("vpacko(v*.w,v*.w)", hvx_width / 2, u16(u32_1 >> 16));
        check("vpacko(v*.w,v*.w)", hvx_width / 2, u16(i32_1 >> 16));
        check("vpacko(v*.w,v*.w)", hvx_width / 2, i16(u32_1 >> 16));
        check("vpacko(v*.w,v*.w)", hvx_width / 2, i16(i32_1 >> 16));

        // vpack doesn't interleave its inputs, which means it doesn't
        // simplify with widening. This is preferable for when the
        // pipeline doesn't widen to begin with, as in the above
        // tests. However, if the pipeline does widen, we want to generate
        // different instructions that have a built in interleaving that
        // we can cancel with the deinterleaving from widening.
        check("vshuffe(v*.b,v*.b)", hvx_width / 1, u8(u16(u8_1) * 127));
        check("vshuffe(v*.b,v*.b)", hvx_width / 1, u8(i16(i8_1) * 63));
        check("vshuffe(v*.b,v*.b)", hvx_width / 1, i8(u16(u8_1) * 127));
        check("vshuffe(v*.b,v*.b)", hvx_width / 1, i8(i16(i8_1) * 63));
        check("vshuffe(v*.h,v*.h)", hvx_width / 2, u16(u32(u16_1) * 32767));
        check("vshuffe(v*.h,v*.h)", hvx_width / 2, u16(i32(i16_1) * 16383));
        check("vshuffe(v*.h,v*.h)", hvx_width / 2, i16(u32(u16_1) * 32767));
        check("vshuffe(v*.h,v*.h)", hvx_width / 2, i16(i32(i16_1) * 16383));

        check("vshuffo(v*.b,v*.b)", hvx_width / 1, u8((u16(u8_1) * 127) >> 8));
        check("vshuffo(v*.b,v*.b)", hvx_width / 1, u8((i16(i8_1) * 63) >> 8));
        check("vshuffo(v*.b,v*.b)", hvx_width / 1, i8((u16(u8_1) * 127) >> 8));
        check("vshuffo(v*.b,v*.b)", hvx_width / 1, i8((i16(i8_1) * 63) >> 8));
        check("vshuffo(v*.h,v*.h)", hvx_width / 2, u16((u32(u16_1) * 32767) >> 16));
        check("vshuffo(v*.h,v*.h)", hvx_width / 2, u16((i32(i16_1) * 16383) >> 16));
        check("vshuffo(v*.h,v*.h)", hvx_width / 2, i16((u32(u16_1) * 32767) >> 16));
        check("vshuffo(v*.h,v*.h)", hvx_width / 2, i16((i32(i16_1) * 16383) >> 16));

        check("vpacke(v*.h,v*.h)", hvx_width / 1, in_u8(2 * x));
        check("vpacke(v*.w,v*.w)", hvx_width / 2, in_u16(2 * x));
        check("vdeal(v*,v*,r*)", hvx_width / 4, in_u32(2 * x));
        check("vpacko(v*.h,v*.h)", hvx_width / 1, in_u8(2 * x + 1));
        check("vpacko(v*.w,v*.w)", hvx_width / 2, in_u16(2 * x + 1));
        check("vdeal(v*,v*,r*)", hvx_width / 4, in_u32(2 * x + 1));

        check("vdelta(v*,v*)", hvx_width / 1, in_u8(3 * x / 2));
        check("vdelta(v*,v*)", hvx_width / 2, in_u16(3 * x / 2));
        check("vdelta(v*,v*)", hvx_width / 2, in_u32(3 * x / 2));
        check("vdelta(v*,v*)", hvx_width * 3, in_u16(x * 3));
        check("vdelta(v*,v*)", hvx_width * 3, in_u8(x * 3));
        check("vdelta(v*,v*)", hvx_width * 4, in_u16(x * 4));
        check("vdelta(v*,v*)", hvx_width * 4, in_u8(x * 4));

        check("vlut32(v*.b,v*.b,r*)", hvx_width / 1, in_u8(u8_1));
        check("vlut32(v*.b,v*.b,r*)", hvx_width / 1, in_u8(clamp(u16_1, 0, 63)));
        check("vlut16(v*.b,v*.h,r*)", hvx_width / 2, in_u16(u8_1));
        check("vlut16(v*.b,v*.h,r*)", hvx_width / 2, in_u16(clamp(u16_1, 0, 15)));
        check("vlut16(v*.b,v*.h,r*)", hvx_width / 2, in_u32(u8_1));
        check("vlut16(v*.b,v*.h,r*)", hvx_width / 2, in_u32(clamp(u16_1, 0, 15)));

        check("v*.ub = vpack(v*.h,v*.h):sat", hvx_width / 1, u8_sat(i16_1));
        check("v*.b = vpacke(v*.h,v*.h)", hvx_width / 1, u8_sat(u16_1));
        check("v*.b = vpack(v*.h,v*.h):sat", hvx_width / 1, i8_sat(i16_1));
        check("v*.uh = vpack(v*.w,v*.w):sat", hvx_width / 2, u16_sat(i32_1));
        // Due to the unavailability of an unsigned word "min" operation in HVX,
        // we deinterlave a vector pair and then do a saturating downcast that interleaves
        // (intrinsic:vsatuwuh). See halide.hexagon.pack_satuh.vuw in hvx_128.ll
        // for a more detailed explanation.
        check("v*.uh = vsat(v*.uw,v*.uw)", hvx_width / 2, u16_sat(u32_1));
        check("v*.h = vpack(v*.w,v*.w):sat", hvx_width / 2, i16_sat(i32_1));
        // Test that bounds-inference instruction selection is working properly.
        check("v*.ub = vpack(v*.h,v*.h):sat", hvx_width / 1, u8_sat(u16_1 >> 1));
        check("v*.b = vpack(v*.h,v*.h):sat", hvx_width / 1, i8_sat(u16_1 >> 1));
        check("v*.uh = vpack(v*.w,v*.w):sat", hvx_width / 2, u16_sat(u32_1 >> 1));
        check("v*.h = vpack(v*.w,v*.w):sat", hvx_width / 2, i16_sat(u32_1 >> 1));

        // vpack doesn't interleave its inputs, which means it doesn't
        // simplify with widening. This is preferable for when the
        // pipeline doesn't widen to begin with, as in the above
        // tests. However, if the pipeline does widen, we want to generate
        // different instructions that have a built in interleaving that
        // we can cancel with the deinterleaving from widening.
        check("v*.ub = vsat(v*.h,v*.h)", hvx_width / 1, u8_sat(i16(i8_1) << 1));
        check("v*.uh = vasr(v*.w,v*.w,r*):sat", hvx_width / 2, u16_sat(i32(i16_1) << 1));
        check("v*.h = vsat(v*.w,v*.w)", hvx_width / 2, i16_sat(i32(i16_1) << 1));

        // Also check double saturating narrows.
        check("v*.ub = vpack(v*.h,v*.h):sat", hvx_width / 1, u8_sat(i32_1));
        check("v*.b = vpack(v*.h,v*.h):sat", hvx_width / 1, i8_sat(i32_1));
        check("v*.h = vsat(v*.w,v*.w)", hvx_width / 1, u8_sat(i32(i16_1) << 8));
        // Saturating narrowing cast
        check("v*.uh = vsat(v*.uw, v*.uw)", hvx_width / 2, u16_sat(u32_1));

        check("vround(v*.h,v*.h)", hvx_width / 1, u8_sat((i32(i16_1) + 128) / 256));
        check("vround(v*.h,v*.h)", hvx_width / 1, i8_sat((i32(i16_1) + 128) / 256));
        check("vround(v*.uh,v*.uh)", hvx_width / 1, u8_sat((u32(u16_1) + 128) / 256));
        // int32 is safe for overflow, allow non-widening rounding.
        check("vround(v*.w,v*.w)", hvx_width / 2, u16_sat((i32_1 + 32768) / 65536));
        check("vround(v*.w,v*.w)", hvx_width / 2, i16_sat((i32_1 + 32768) / 65536));
        check("vround(v*.w,v*.w)", hvx_width / 2, u16_sat((i64(i32_1) + 32768) / 65536));
        check("vround(v*.w,v*.w)", hvx_width / 2, i16_sat((i64(i32_1) + 32768) / 65536));
        check("vround(v*.uw,v*.uw)", hvx_width / 2, u16_sat((u64(u32_1) + 32768) / 65536));

        check("v*.ub = vasr(v*.h,v*.h,r*):rnd:sat", hvx_width / 1, u8_sat((i32(i16_1) + 8) / 16));
        check("v*.b = vasr(v*.h,v*.h,r*):rnd:sat", hvx_width / 1, i8_sat((i32(i16_1) + 16) / 32));
        if (isa_version >= 65) {
            check("v*.ub = vasr(v*.uh,v*.uh,r*):rnd:sat", hvx_width / 1, u8_sat((u32(u16_1) + 32) / 64));
        }
        // int32 is safe for overflow, allow non-widening rounding.
        check("v*.uh = vasr(v*.w,v*.w,r*):rnd:sat", hvx_width / 2, u16_sat((i32_1 + 64) / 128));
        check("v*.h = vasr(v*.w,v*.w,r*):rnd:sat", hvx_width / 2, i16_sat((i32_1 + 128) / 256));
        check("v*.uh = vasr(v*.w,v*.w,r*):rnd:sat", hvx_width / 2, u16_sat((i64(i32_1) + 256) / 512));
        check("v*.h = vasr(v*.w,v*.w,r*):rnd:sat", hvx_width / 2, i16_sat((i64(i32_1) + 512) / 1024));
        check("v*.uh = vasr(v*.uw,v*.uw,r*):rnd:sat", hvx_width / 2, u16_sat((u64(u32_1) + 1024) / 2048));

        check("vshuff(v*,v*,r*)", hvx_width * 2, select((x % 2) == 0, in_u8(x / 2), in_u8((x + 16) / 2)));
        check("vshuff(v*,v*,r*)", hvx_width * 2, select((x % 2) == 0, in_i8(x / 2), in_i8((x + 16) / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 2, select((x % 2) == 0, in_u16(x / 2), in_u16((x + 16) / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 2, select((x % 2) == 0, in_i16(x / 2), in_i16((x + 16) / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 4, select((x % 2) == 0, in_u32(x / 2), in_u32((x + 16) / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 4, select((x % 2) == 0, in_i32(x / 2), in_i32((x + 16) / 2)));

        check("vshuff(v*,v*,r*)", hvx_width * 2, select((x % 2) == 0, u8(x / 2), u8(x / 2)));
        check("vshuff(v*,v*,r*)", hvx_width * 2, select((x % 2) == 0, i8(x / 2), i8(x / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 2, select((x % 2) == 0, u16(x / 2), u16(x / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 2, select((x % 2) == 0, i16(x / 2), i16(x / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 4, select((x % 2) == 0, u32(x / 2), u32(x / 2)));
        check("vshuff(v*,v*,r*)", (hvx_width * 2) / 4, select((x % 2) == 0, i32(x / 2), i32(x / 2)));

        check("vmax(v*.ub,v*.ub)", hvx_width / 1, max(u8_1, u8_2));
        check("vmax(v*.uh,v*.uh)", hvx_width / 2, max(u16_1, u16_2));
        check("vmax(v*.h,v*.h)", hvx_width / 2, max(i16_1, i16_2));
        check("vmax(v*.w,v*.w)", hvx_width / 4, max(i32_1, i32_2));

        check("vmin(v*.ub,v*.ub)", hvx_width / 1, min(u8_1, u8_2));
        check("vmin(v*.uh,v*.uh)", hvx_width / 2, min(u16_1, u16_2));
        check("vmin(v*.h,v*.h)", hvx_width / 2, min(i16_1, i16_2));
        check("vmin(v*.w,v*.w)", hvx_width / 4, min(i32_1, i32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width / 1, select(i8_1 < i8_2, i8_3, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width / 1, select(u8_1 < u8_2, u8_3, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width / 2, select(i16_1 < i16_2, i16_3, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width / 2, select(u16_1 < u16_2, u16_3, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width / 4, select(i32_1 < i32_2, i32_3, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width / 4, select(u32_1 < u32_2, u32_3, u32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width / 1, select(i8_1 > i8_2, i8_3, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width / 1, select(u8_1 > u8_2, u8_3, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width / 2, select(i16_1 > i16_2, i16_3, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width / 2, select(u16_1 > u16_2, u16_3, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width / 4, select(i32_1 > i32_2, i32_3, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width / 4, select(u32_1 > u32_2, u32_3, u32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width / 1, select(i8_1 <= i8_2, i8_3, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width / 1, select(u8_1 <= u8_2, u8_3, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width / 2, select(i16_1 <= i16_2, i16_3, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width / 2, select(u16_1 <= u16_2, u16_3, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width / 4, select(i32_1 <= i32_2, i32_3, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width / 4, select(u32_1 <= u32_2, u32_3, u32_2));

        check("vcmp.gt(v*.b,v*.b)", hvx_width / 1, select(i8_1 >= i8_2, i8_3, i8_2));
        check("vcmp.gt(v*.ub,v*.ub)", hvx_width / 1, select(u8_1 >= u8_2, u8_3, u8_2));
        check("vcmp.gt(v*.h,v*.h)", hvx_width / 2, select(i16_1 >= i16_2, i16_3, i16_2));
        check("vcmp.gt(v*.uh,v*.uh)", hvx_width / 2, select(u16_1 >= u16_2, u16_3, u16_2));
        check("vcmp.gt(v*.w,v*.w)", hvx_width / 4, select(i32_1 >= i32_2, i32_3, i32_2));
        check("vcmp.gt(v*.uw,v*.uw)", hvx_width / 4, select(u32_1 >= u32_2, u32_3, u32_2));

        check("vcmp.eq(v*.b,v*.b)", hvx_width / 1, select(i8_1 == i8_2, i8_3, i8_2));
        check("vcmp.eq(v*.b,v*.b)", hvx_width / 1, select(u8_1 == u8_2, u8_3, u8_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width / 2, select(i16_1 == i16_2, i16_3, i16_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width / 2, select(u16_1 == u16_2, u16_3, u16_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width / 4, select(i32_1 == i32_2, i32_3, i32_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width / 4, select(u32_1 == u32_2, u32_3, u32_2));

        check("vcmp.eq(v*.b,v*.b)", hvx_width / 1, select(i8_1 != i8_2, i8_3, i8_2));
        check("vcmp.eq(v*.b,v*.b)", hvx_width / 1, select(u8_1 != u8_2, u8_3, u8_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width / 2, select(i16_1 != i16_2, i16_3, i16_2));
        check("vcmp.eq(v*.h,v*.h)", hvx_width / 2, select(u16_1 != u16_2, u16_3, u16_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width / 4, select(i32_1 != i32_2, i32_3, i32_2));
        check("vcmp.eq(v*.w,v*.w)", hvx_width / 4, select(u32_1 != u32_2, u32_3, u32_2));

        check("vabsdiff(v*.ub,v*.ub)", hvx_width / 1, absd(u8_1, u8_2));
        check("vabsdiff(v*.uh,v*.uh)", hvx_width / 2, absd(u16_1, u16_2));
        check("vabsdiff(v*.h,v*.h)", hvx_width / 2, absd(i16_1, i16_2));
        check("vabsdiff(v*.w,v*.w)", hvx_width / 4, absd(i32_1, i32_2));

        // Expression Rearrangements
        check("vmpa(v*.ub,r*.b)", hvx_width / 1, 2 * (i16(u8_1) + i16(u8_2)));
        check("vmpa(v*.ub,r*.b)", hvx_width / 1, 3 * (4 * i16(u8_1) + i16(u8_2)));
        check("vmpa(v*.h,r*.b)", hvx_width / 2, 5 * (i32(i16_1) + 7 * i32(i16_2)));
        check("vmpa(v*.ub,r*.b)", hvx_width / 1, 2 * (i16(u8_1) - i16(u8_2)));
        check("vmpa(v*.ub,r*.b)", hvx_width / 1, 3 * (4 * i16(u8_1) - i16(u8_2)));
        check("vmpa(v*.h,r*.b)", hvx_width / 2, 5 * (i32(i16_1) - 7 * i32(i16_2)));

        check("vand(v*,v*)", hvx_width / 1, u8_1 & u8_2);
        check("vand(v*,v*)", hvx_width / 2, u16_1 & u16_2);
        check("vand(v*,v*)", hvx_width / 4, u32_1 & u32_2);
        check("vor(v*,v*)", hvx_width / 1, u8_1 | u8_2);
        check("vor(v*,v*)", hvx_width / 2, u16_1 | u16_2);
        check("vor(v*,v*)", hvx_width / 4, u32_1 | u32_2);
        check("vxor(v*,v*)", hvx_width / 1, u8_1 ^ u8_2);
        check("vxor(v*,v*)", hvx_width / 2, u16_1 ^ u16_2);
        check("vxor(v*,v*)", hvx_width / 4, u32_1 ^ u32_2);
        check("vnot(v*)", hvx_width / 1, ~u8_1);
        check("vnot(v*)", hvx_width / 2, ~u16_1);
        check("vnot(v*)", hvx_width / 4, ~u32_1);

        // v62 - Broadcasting scalars
        check("vsplat(r*)", hvx_width / 1, in_u8(y));
        check("vsplat(r*)", hvx_width / 2, in_u16(y));
        check("vsplat(r*)", hvx_width / 4, in_u32(y));

        check("vmux(q*,v*,v*)", hvx_width / 1, select(i8_1 == i8_2, i8_3, i8_2));
        check("vmux(q*,v*,v*)", hvx_width / 2, select(i16_1 == i16_2, i16_3, i16_2));
        check("vmux(q*,v*,v*)", hvx_width / 4, select(i32_1 == i32_2, i32_3, i32_2));

        check("vabs(v*.h)", hvx_width / 2, abs(i16_1));
        check("vabs(v*.w)", hvx_width / 4, abs(i32_1));
        if (isa_version >= 65) {
            check("vabs(v*.b)", hvx_width / 1, abs(i8_1));
        }

        check("vmpy(v*.ub,v*.ub)", hvx_width / 1, u16(u8_1) * u16(u8_2));
        check("vmpy(v*.b,v*.b)", hvx_width / 1, i16(i8_1) * i16(i8_2));
        check("vmpy(v*.uh,v*.uh)", hvx_width / 2, u32(u16_1) * u32(u16_2));
        check("vmpy(v*.h,v*.h)", hvx_width / 2, i32(i16_1) * i32(i16_2));
        check("vmpyi(v*.h,v*.h)", hvx_width / 2, i16_1 * i16_2);
        check("vmpyio(v*.w,v*.h)", hvx_width / 2, i32_1 * i32(i16_1));
        check("vmpyie(v*.w,v*.uh)", hvx_width / 2, i32_1 * i32(u16_1));
        check("vmpyie(v*.w,v*.uh)", hvx_width / 2, u32_1 * u32(u16_1));
        check("vmpyieo(v*.h,v*.h)", hvx_width / 4, i32_1 * i32_2);
        // The inconsistency in the expected instructions here is
        // correct. For bytes, the unsigned value is first, for half
        // words, the signed value is first.
        check("vmpy(v*.ub,v*.b)", hvx_width / 1, i16(u8_1) * i16(i8_2));
        check("vmpy(v*.h,v*.uh)", hvx_width / 2, i32(u16_1) * i32(i16_2));
        check("vmpy(v*.ub,v*.b)", hvx_width / 1, i16(i8_1) * i16(u8_2));
        check("vmpy(v*.h,v*.uh)", hvx_width / 2, i32(i16_1) * i32(u16_2));

        check("vmpy(v*.ub,r*.b)", hvx_width / 1, i16(u8_1) * 3);
        check("vmpy(v*.h,r*.h)", hvx_width / 2, i32(i16_1) * 10);
        check("vmpy(v*.ub,r*.ub)", hvx_width / 1, u16(u8_1) * 3);
        check("vmpy(v*.uh,r*.uh)", hvx_width / 2, u32(u16_1) * 10);

        check("vmpy(v*.ub,r*.b)", hvx_width / 1, 3 * i16(u8_1));
        check("vmpy(v*.h,r*.h)", hvx_width / 2, 10 * i32(i16_1));
        check("vmpy(v*.ub,r*.ub)", hvx_width / 1, 3 * u16(u8_1));
        check("vmpy(v*.uh,r*.uh)", hvx_width / 2, 10 * u32(u16_1));

        check("vmpyi(v*.h,r*.b)", hvx_width / 2, i16_1 * 127);
        check("vmpyi(v*.h,r*.b)", hvx_width / 2, 127 * i16_1);
        check("vmpyi(v*.w,r*.h)", hvx_width / 4, i32_1 * 32767);
        check("vmpyi(v*.w,r*.h)", hvx_width / 4, 32767 * i32_1);

        check("v*.h += vmpyi(v*.h,v*.h)", hvx_width / 2, i16_1 + i16_2 * i16_3);

        check("v*.h += vmpyi(v*.h,r*.b)", hvx_width / 2, i16_1 + i16_2 * 127);
        check("v*.w += vmpyi(v*.w,r*.h)", hvx_width / 4, i32_1 + i32_2 * 32767);
        check("v*.h += vmpyi(v*.h,r*.b)", hvx_width / 2, i16_1 + 127 * i16_2);
        check("v*.w += vmpyi(v*.w,r*.h)", hvx_width / 4, i32_1 + 32767 * i32_2);

        check("v*.uh += vmpy(v*.ub,v*.ub)", hvx_width / 1, u16_1 + u16(u8_1) * u16(u8_2));
        check("v*.uw += vmpy(v*.uh,v*.uh)", hvx_width / 2, u32_1 + u32(u16_1) * u32(u16_2));
        check("v*.h += vmpy(v*.b,v*.b)", hvx_width / 1, i16_1 + i16(i8_1) * i16(i8_2));
        check("v*.w += vmpy(v*.h,v*.h)", hvx_width / 2, i32_1 + i32(i16_1) * i32(i16_2));

        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width / 1, i16_1 + i16(u8_1) * i16(i8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width / 2, i32_1 + i32(i16_1) * i32(u16_2));
        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width / 1, i16_1 + i16(u8_1) * i16(i8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width / 2, i32_1 + i32(i16_1) * i32(u16_2));

        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width / 1, i16_1 + i16(i8_1) * i16(u8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width / 2, i32_1 + i32(u16_1) * i32(i16_2));
        check("v*.h += vmpy(v*.ub,v*.b)", hvx_width / 1, i16_1 + i16(i8_1) * i16(u8_2));
        check("v*.w += vmpy(v*.h,v*.uh)", hvx_width / 2, i32_1 + i32(u16_1) * i32(i16_2));
        check("v*.w += vmpy(v*.h, r*.h):sat", hvx_width / 1, i32_1 + i32(i16_1) * 32767);
        check("v*.w += vmpy(v*.h, r*.h):sat", hvx_width / 1, i32_1 + 32767 * i32(i16_1));

        check("v*.uh += vmpy(v*.ub,r*.ub)", hvx_width / 1, u16_1 + u16(u8_1) * 255);
        check("v*.h += vmpy(v*.ub,r*.b)", hvx_width / 1, i16_1 + i16(u8_1) * 127);
        check("v*.uw += vmpy(v*.uh,r*.uh)", hvx_width / 2, u32_1 + u32(u16_1) * 65535);
        check("v*.uh += vmpy(v*.ub,r*.ub)", hvx_width / 1, u16_1 + 255 * u16(u8_1));
        check("v*.h += vmpy(v*.ub,r*.b)", hvx_width / 1, i16_1 + 127 * i16(u8_1));
        check("v*.uw += vmpy(v*.uh,r*.uh)", hvx_width / 2, u32_1 + 65535 * u32(u16_1));

        check("v*.h += vmpy(v*.ub,r*.b)", hvx_width / 1, i16_1 - i16(u8_1) * -127);
        check("v*.h += vmpyi(v*.h,r*.b)", hvx_width / 2, i16_1 - i16_2 * -127);

        check("v*.w += vmpy(v*.h,r*.h)", hvx_width / 1, i32_1 + i32(i16_1) * 32767);
        check("v*.w += vmpy(v*.h,r*.h)", hvx_width / 1, i32_1 + 32767 * i32(i16_1));

        for (int factor : {1, 2}) {
            check("vmpy(v*.h,v*.h):<<1:rnd:sat", hvx_width / 2, i16_sat((i32(i16_1) * i32(i16_2 * factor) + 16384) / 32768));

            check("vmpyo(v*.w,v*.h)", hvx_width / 4, i32((i64(i32_1) * i64(i32_2 * factor)) / (i64(1) << 32)));
            check("vmpyo(v*.w,v*.h):<<1:sat", hvx_width / 4, i32_sat((i64(i32_1 * factor) * i64(i32_2)) / (i64(1) << 31)));
            check("vmpyo(v*.w,v*.h):<<1:rnd:sat", hvx_width / 4, i32_sat((i64(i32_1) * i64(i32_2 * factor) + (1 << 30)) / (i64(1) << 31)));
        }

        for (int scalar : {32766, 32767}) {
            check("vmpy(v*.h,r*.h):<<1:sat", hvx_width / 2, i16_sat((i32(i16_1) * scalar) / 32768));
            check("vmpy(v*.h,r*.h):<<1:sat", hvx_width / 2, i16_sat((scalar * i32(i16_1)) / 32768));
            check("vmpy(v*.h,r*.h):<<1:rnd:sat", hvx_width / 2, i16_sat((i32(i16_1) * scalar + 16384) / 32768));
            check("vmpy(v*.h,r*.h):<<1:rnd:sat", hvx_width / 2, i16_sat((scalar * i32(i16_1) + 16384) / 32768));
        }

        for (int scalar : {std::numeric_limits<int>::max() - 1, std::numeric_limits<int>::max()}) {
            check("vmpyo(v*.w,v*.h)", hvx_width / 4, i32((i64(i32_1) * scalar) / (i64(1) << 32)));
            check("vmpyo(v*.w,v*.h)", hvx_width / 4, i32((scalar * i64(i32_2)) / (i64(1) << 32)));
            check("vmpyo(v*.w,v*.h):<<1:sat", hvx_width / 4, i32_sat((i64(i32_1) * scalar) / (i64(1) << 31)));
            check("vmpyo(v*.w,v*.h):<<1:sat", hvx_width / 4, i32_sat((scalar * i64(i32_2)) / (i64(1) << 31)));
            check("vmpyo(v*.w,v*.h):<<1:rnd:sat", hvx_width / 4, i32_sat((i64(i32_1) * scalar + (1 << 30)) / (i64(1) << 31)));
            check("vmpyo(v*.w,v*.h):<<1:rnd:sat", hvx_width / 4, i32_sat((scalar * i64(i32_2) + (1 << 30)) / (i64(1) << 31)));
        }

        check("vmpa(v*.ub,r*.b)", hvx_width / 1, i16(u8_1) * 127 + i16(u8_2) * -128);
        check("vmpa(v*.ub,r*.b)", hvx_width / 1, i16(u8_1) * 127 + 126 * i16(u8_2));
        check("vmpa(v*.ub,r*.b)", hvx_width / 1, -100 * i16(u8_1) + 40 * i16(u8_2));
        check("v*.h += vmpa(v*.ub,r*.b)", hvx_width / 1, 2 * i16(u8_1) + 3 * i16(u8_2) + i16_1);

        check("vmpa(v*.h,r*.b)", hvx_width / 2, i32(i16_1) * 2 + i32(i16_2) * 3);
        check("vmpa(v*.h,r*.b)", hvx_width / 2, i32(i16_1) * 2 + 3 * i32(i16_2));
        check("vmpa(v*.h,r*.b)", hvx_width / 2, 2 * i32(i16_1) + 3 * i32(i16_2));
        check("v*.w += vmpa(v*.h,r*.b)", hvx_width / 2, 2 * i32(i16_1) + 3 * i32(i16_2) + i32_1);

#if 0
        // TODO: Re-enable these when vtmpy codegen is re-enabled.
        check("v*:*.h = vtmpy(v*:*.ub, r*.b)", hvx_width/1, 2*i16(in_u8(x - 1)) + 3*i16(in_u8(x)) + i16(in_u8(x + 1)));
        check("v*:*.h = vtmpy(v*:*.ub, r*.b)", hvx_width/1, i16(in_u8(x - 1)) + 3*i16(in_u8(x)) + i16(in_u8(x + 1)));
        check("v*:*.h = vtmpy(v*:*.ub, r*.b)", hvx_width/1, i16(in_u8(x - 1))*2 + i16(in_u8(x)) + i16(in_u8(x + 1)));
        check("v*:*.h = vtmpy(v*:*.ub, r*.b)", hvx_width/1, i16(in_u8(x - 1)) + i16(in_u8(x)) + i16(in_u8(x + 1)));

        check("v*:*.h = vtmpy(v*:*.b, r*.b)", hvx_width/1, 2*i16(in_i8(x - 1)) + 3*i16(in_i8(x)) + i16(in_i8(x + 1)));
        check("v*:*.h = vtmpy(v*:*.b, r*.b)", hvx_width/1, i16(in_i8(x - 1)) + 3*i16(in_i8(x)) + i16(in_i8(x + 1)));
        check("v*:*.h = vtmpy(v*:*.b, r*.b)", hvx_width/1, i16(in_i8(x - 1))*2 + i16(in_i8(x)) + i16(in_i8(x + 1)));
        check("v*:*.h = vtmpy(v*:*.b, r*.b)", hvx_width/1, i16(in_i8(x - 1)) + i16(in_i8(x)) + i16(in_i8(x + 1)));

        check("v*:*.w = vtmpy(v*:*.h, r*.b)", hvx_width/2, 2*i32(in_i16(x - 1)) + 3*i32(in_i16(x)) + i32(in_i16(x + 1)));
        check("v*:*.w = vtmpy(v*:*.h, r*.b)", hvx_width/2, i32(in_i16(x - 1)) + 3*i32(in_i16(x)) + i32(in_i16(x + 1)));
        check("v*:*.w = vtmpy(v*:*.h, r*.b)", hvx_width/2, i32(in_i16(x - 1))*2 + i32(in_i16(x)) + i32(in_i16(x + 1)));
        check("v*:*.w = vtmpy(v*:*.h, r*.b)", hvx_width/2, i32(in_i16(x - 1)) + i32(in_i16(x)) + i32(in_i16(x + 1)));
#endif

        // We only generate vdmpy if the inputs are interleaved (otherwise we would use vmpa).
        check("vdmpy(v*.ub,r*.b)", hvx_width / 2, i16(in_u8(2 * x)) * 127 + i16(in_u8(2 * x + 1)) * -128);
        check("vdmpy(v*.h,r*.b)", hvx_width / 4, i32(in_i16(2 * x)) * 2 + i32(in_i16(2 * x + 1)) * 3);
        check("v*.h += vdmpy(v*.ub,r*.b)", hvx_width / 2, i16(in_u8(2 * x)) * 120 + i16(in_u8(2 * x + 1)) * -50 + i16_1);
        check("v*.w += vdmpy(v*.h,r*.b)", hvx_width / 4, i32(in_i16(2 * x)) * 80 + i32(in_i16(2 * x + 1)) * 33 + i32_1);

#if 0
        // These are incorrect because the two operands aren't
        // interleaved correctly.
        check("vdmpy(v*:*.ub,r*.b)", (hvx_width/2)*2, i16(in_u8(2*x))*2 + i16(in_u8(2*x + 1))*3);
        check("vdmpy(v*:*.h,r*.b)", (hvx_width/4)*2, i32(in_i16(2*x))*2 + i32(in_i16(2*x + 1))*3);
        check("v*:*.h += vdmpy(v*:*.ub,r*.b)", (hvx_width/2)*2, i16(in_u8(2*x))*2 + i16(in_u8(2*x + 1))*3 + i16_1);
        check("v*:*.w += vdmpy(v*:*.h,r*.b)", (hvx_width/4)*2, i32(in_i16(2*x))*2 + i32(in_i16(2*x + 1))*3 + i32_1);
#endif

        check("vrmpy(v*.ub,r*.ub)", hvx_width, u32(u8_1) * 255 + u32(u8_2) * 254 + u32(u8_3) * 253 + u32(u8_4) * 252);
        check("vrmpy(v*.ub,r*.b)", hvx_width, i32(u8_1) * 127 + i32(u8_2) * -128 + i32(u8_3) * 126 + i32(u8_4) * -127);
        check("v*.uw += vrmpy(v*.ub,r*.ub)", hvx_width, u32_1 + u32(u8_1) * 2 + u32(u8_2) * 3 + u32(u8_3) * 4 + u32(u8_4) * 5);
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width, i32_1 + i32(u8_1) * 2 + i32(u8_2) * -3 + i32(u8_3) * -4 + i32(u8_4) * 5);

        // Check a few of these with implicit ones.
        check("vrmpy(v*.ub,r*.b)", hvx_width, i32(u8_1) + i32(u8_2) * -2 + i32(u8_3) * 3 + i32(u8_4) * -4);
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width, i32_1 + i32(u8_1) + i32(u8_2) * 2 + i32(u8_3) * 3 + i32(u8_4) * 4);

        // We should also match this pattern.
        check("vrmpy(v*.ub,r*.ub)", hvx_width, u32(u16(u8_1) * 255) + u32(u16(u8_2) * 254) + u32(u16(u8_3) * 253) + u32(u16(u8_4) * 252));
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width, i32_1 + i32(i16(u8_1) * 2) + i32(i16(u8_2) * -3) + i32(i16(u8_3) * -4) + i32(i16(u8_4) * 5));

        check("vrmpy(v*.ub,v*.ub)", hvx_width, u32(u8_1) * u8_1 + u32(u8_2) * u8_2 + u32(u8_3) * u8_3 + u32(u8_4) * u8_4);
        check("vrmpy(v*.b,v*.b)", hvx_width, i32(i8_1) * i8_1 + i32(i8_2) * i8_2 + i32(i8_3) * i8_3 + i32(i8_4) * i8_4);
        check("v*.uw += vrmpy(v*.ub,v*.ub)", hvx_width, u32_1 + u32(u8_1) * u8_1 + u32(u8_2) * u8_2 + u32(u8_3) * u8_3 + u32(u8_4) * u8_4);
        check("v*.w += vrmpy(v*.b,v*.b)", hvx_width, i32_1 + i32(i8_1) * i8_1 + i32(i8_2) * i8_2 + i32(i8_3) * i8_3 + i32(i8_4) * i8_4);

#if 0
        // These don't generate yet because we don't support mixed signs yet.
        check("vrmpy(v*.ub,v*.b)", hvx_width, i32(u8_1)*i8_1 + i32(u8_2)*i8_2 + i32(u8_3)*i8_3 + i32(u8_4)*i8_4);
        check("v*.w += vrmpy(v*.ub,v*.b)", hvx_width, i32_1 + i32(u8_1)*i8_1 + i32(u8_2)*i8_2 + i32(u8_3)*i8_3 + i32(u8_4)*i8_4);
        check("vrmpy(v*.ub,v*.b)", hvx_width, i16(u8_1)*i8_1 + i16(u8_2)*i8_2 + i16(u8_3)*i8_3 + i16(u8_4)*i8_4);
#endif

        // These should also work with 16 bit results. However, it is
        // only profitable to do so if the interleave simplifies away.
        Expr u8_4x4[] = {
            in_u8(4 * x + 0),
            in_u8(4 * x + 1),
            in_u8(4 * x + 2),
            in_u8(4 * x + 3),
        };
        check("vrmpy(v*.ub,r*.b)", hvx_width / 2, i16(u8_4x4[0]) * 127 + i16(u8_4x4[1]) * 126 + i16(u8_4x4[2]) * -125 + i16(u8_4x4[3]) * 124);

        // Make sure it doesn't generate if the operands don't interleave.
        check("vmpa(v*.ub,r*.b)", hvx_width, i16(u8_1) * 127 + i16(u8_2) * -126 + i16(u8_3) * 125 + i16(u8_4) * 124);

        check("v*.w += vasl(v*.w,r*)", hvx_width / 4, u32_1 + (u32_2 * 8));
        check("v*.w += vasl(v*.w,r*)", hvx_width / 4, i32_1 + (i32_2 * 8));
        check("v*.w += vasr(v*.w,r*)", hvx_width / 4, i32_1 + (i32_2 / 8));

        check("v*.w += vasl(v*.w,r*)", hvx_width / 4, i32_1 + (i32_2 << u32(y % 32)));
        check("v*.w += vasr(v*.w,r*)", hvx_width / 4, i32_1 + (i32_2 >> u32(y % 32)));

        if (isa_version >= 65) {
            check("v*.h += vasl(v*.h,r*)", hvx_width / 2, i16_1 + (i16_2 << u16(y % 16)));
            check("v*.h += vasr(v*.h,r*)", hvx_width / 2, i16_1 + (i16_2 >> u16(y % 16)));
            check("v*.h += vasl(v*.h,r*)", hvx_width / 2, u16_1 + (u16_2 * 16));
            check("v*.h += vasl(v*.h,r*)", hvx_width / 2, i16_1 + (i16_2 * 16));
            check("v*.h += vasl(v*.h,r*)", hvx_width / 2, u16_1 + (16 * u16_2));
            check("v*.h += vasl(v*.h,r*)", hvx_width / 2, i16_1 + (16 * i16_2));
            check("v*.h += vasr(v*.h,r*)", hvx_width / 2, i16_1 + (i16_2 / 16));
        }

        check("vcl0(v*.uh)", hvx_width / 2, count_leading_zeros(u16_1));
        check("vcl0(v*.uw)", hvx_width / 4, count_leading_zeros(u32_1));
        check("vnormamt(v*.h)", hvx_width / 2, max(count_leading_zeros(i16_1), count_leading_zeros(~i16_1)));
        check("vnormamt(v*.w)", hvx_width / 4, max(count_leading_zeros(i32_1), count_leading_zeros(~i32_1)));
        check("vpopcount(v*.h)", hvx_width / 2, popcount(u16_1));

        check("v* = vdelta(v*, v*)", hvx_width, in_u8((x / 8) * 9 + x % 8));
        check("v* = vdelta(v*, v*)", hvx_width / 2, in_u16((x / 8) * 9 + x % 8));
        check("v* = vdelta(v*, v*)", hvx_width / 4, in_u32((x / 8) * 9 + x % 8));

        int rfac = 4;
        RDom r(0, rfac);
        check("v*.uw = vrmpy(v*.ub,r*.ub)", hvx_width / 4, sum(u16(in_u8(rfac * x + r))));
        check("v*.uw = vrmpy(v*.ub,r*.ub)", hvx_width / 4, sum(u16(in_u8(rfac * x + r)) * u8(r)));
        check("v*.w  = vrmpy(v*.ub,r*.b)", hvx_width / 4, sum(i16(in_u8(rfac * x + r)) * i8(r)));
        check("v*.uw = vrmpy(v*.ub,v*.ub)", hvx_width / 4, sum(u16(in_u8(rfac * x + r)) * in_u8(rfac * x + r + 32)));
        check("v*.w  = vrmpy(v*.ub,v*.b)", hvx_width / 4, sum(i16(in_u8(rfac * x + r)) * in_i8(rfac * x + r + 32)));
        check("v*.w  = vrmpy(v*.b,v*.b)", hvx_width / 4, sum(i16(in_i8(rfac * x + r)) * in_i8(rfac * x + r + 32)));
        check("v*.uw += vrmpy(v*.ub,r*.ub)", hvx_width / 4, sum(u32(in_u8(rfac * x + r))));
        check("v*.w += vrmpy(v*.ub,r*.b)", hvx_width / 4, sum(i32(in_u8(rfac * x + r))));
        check("v*.uw = vrmpy(v*.ub,r*.ub)", hvx_width / 4, sum(u32(in_u8(rfac * x + r)) * 34));
        check("v*.uw += vrmpy(v*.ub,r*.ub)", hvx_width / 4, sum(u32(in_u8(rfac * x + r)) * u8(r)));
        check("v*.uw += vrmpy(v*.ub,r*.ub)", hvx_width / 4, sum(i32(in_u8(rfac * x + r)) * u8(r)));
        check("v*.w  += vrmpy(v*.ub,r*.b)", hvx_width / 4, sum(i32(in_u8(rfac * x + r)) * i8(r)));
        check("v*.uw += vrmpy(v*.ub,v*.ub)", hvx_width / 4, sum(u32(in_u8(rfac * x + r)) * in_u8(rfac * x + r + 32)));
        check("v*.uw += vrmpy(v*.ub,v*.ub)", hvx_width / 4, sum(i32(in_u8(rfac * x + r)) * in_u8(rfac * x + r + 32)));
        check("v*.w  += vrmpy(v*.ub,v*.b)", hvx_width / 4, sum(i32(in_u8(rfac * x + r)) * in_i8(rfac * x + r + 32)));
        check("v*.w  += vrmpy(v*.b,v*.b)", hvx_width / 4, sum(i32(in_i8(rfac * x + r)) * in_i8(rfac * x + r + 32)));
        // Sliding window
        // TODO: We can generate accumulative versions of below instructions.
        check("v*:*.uw = vrmpy(v*:*.ub, r*.ub, #*)", hvx_width, sum(u32(in_u8(x + r))));
        check("v*:*.uw = vrmpy(v*:*.ub, r*.ub, #*)", hvx_width, sum(u32(in_u8(x + r)) * 34));
        check("v*:*.w = vrmpy(v*:*.ub, r*.b, #*)", hvx_width, sum(u32(in_u8(x + r)) * i8(r)));
        check("v*:*.w = vrmpy(v*:*.ub, r*.b, #*)", hvx_width, sum(i32(in_u8(x + r)) * i8(-r)));

        rfac = 2;
        RDom r2(0, rfac);
        check("v*.h += vdmpy(v*.ub, r*.b)", hvx_width / 2, sum(i16(in_u8(rfac * x + r2))));
        check("v*.h = vdmpy(v*.ub, r*.b)", hvx_width / 2, sum(i16(in_u8(rfac * x + r2)) * 34));
        check("v*.w += vdmpy(v*.h, r*.b)", hvx_width / 4, sum(i32(in_i16(rfac * x + r2)) * i8(r2)));
        check("v*.w += vdmpy(v*.h, r*.b)", hvx_width / 4, sum(i32(in_i16(rfac * x + r2)) * i8(r2)));
        check("v*.w = vdmpy(v*.h, r*.b)", hvx_width / 4, sum(i32(in_i16(rfac * x + r2)) * 15246));
        check("v*.w = vdmpy(v*.h, r*.b)", hvx_width / 4, sum(i32(in_i16(rfac * x + r2)) * (-1246)));
        // Sliding window
        // TODO: Check for the crash
        // check("v*:*.h = vdmpy(v*:*.ub, r*.b)", hvx_width, sum(i16(in_u8(x + r2)) * i16(r2)));

        rfac = 3;
        RDom r3(0, rfac);
        check("v*:*.h += vtmpy(v*:*.b, r*.b)", hvx_width, sum(i16(in_i8(x + r3))));
        check("v*:*.h += vtmpy(v*:*.ub, r*.b)", hvx_width, sum(i16(in_u8(x + r3))));
        check("v*:*.w += vtmpy(v*:*.h, r*.b)", hvx_width, sum(i32(in_i16(x + r3))));
        // TODO: This should work, a common stencil
        // check("v*:*.h += vtmpy(v*:*.b, r*.b)", hvx_width, sum(i16(in_i8(x + r3)) * mux(r3, {1, 2, 1})));
        // check("v*:*.h += vtmpy(v*:*.ub, r*.b)", hvx_width, sum(i16(in_u8(x + r3)) * mux(r3, {1, 2, 1})));
        // check("v*:*.w += vtmpy(v*:*.h, r*.b)", hvx_width, sum(i32(in_i16(x + r3)) * mux(r3, {1, 2, 1})));
    }

private:
    const Var x{"x"}, y{"y"};
};

int main(int argc, char **argv) {
    return SimdOpCheckTest::main<SimdOpCheckHVX>(
        argc, argv,
        {
            // IMPORTANT:
            // When adding new targets here, make sure to also update
            // can_run_code in simd_op_check.h to include any new features used.

            Target("hexagon-32-noos-hvx"),
            Target("hexagon-32-noos-hvx-hvx_128"),
            Target("hexagon-32-noos-hvx-hvx_128-hvx_v62"),
            Target("hexagon-32-noos-hvx-hvx_128-hvx_v65"),
            Target("hexagon-32-noos-hvx-hvx_128-hvx_v66"),
            Target("hexagon-32-noos-hvx-hvx_128-hvx_v68"),
        });
}
