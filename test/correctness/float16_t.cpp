#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {
    Var x;

    Buffer<float16_t> in1 = lambda(x, cast<float16_t>(-0.5f) + cast<float16_t>(x) / (128)).realize(128);
    Buffer<bfloat16_t> in2 = lambda(x, cast<bfloat16_t>(-0.5f) + cast<bfloat16_t>(x) / (128)).realize(128);

    // Check the Halide-side float 16 conversion math matches the C++-side math.
    in1.for_each_element([&](int i) {
        float16_t correct = Halide::float16_t(-0.5f) + Halide::float16_t(i) / Halide::float16_t(128.0f);
        if (in1(i) != correct) {
            printf("in1(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
            abort();
        }
    });

    in2.for_each_element([&](int i) {
        bfloat16_t correct = Halide::bfloat16_t(-0.5f) + Halide::bfloat16_t(i) / Halide::bfloat16_t(128.0f);
        if (in2(i) != correct) {
            printf("in2(%d) = %f instead of %f\n", i, float(in2(i)), float(correct));
            abort();
        }
    });

    // Check some basic math works on float16. More math is tested in
    // correctness_vector_math.
    Func wrap1, wrap2;
    wrap1(x) = in1(x);
    wrap2(x) = in2(x);

    Func f;
    f(x) = abs(sqrt(abs(wrap1(x) * 4.0f)) - sqrt(abs(wrap2(x))) * 2.0f);

    f.compute_root().vectorize(x, 16);
    wrap1.compute_at(f, x).vectorize(x);
    wrap2.compute_at(f, x).vectorize(x);

    RDom r(0, 128);
    Func g;
    g() = maximum(cast<double>(f(r)));

    double d = evaluate<double>(g());
    if (d != 0) {
        printf("Should be zero: %f\n", d);
        return -1;
    }

    // Check scalar parameters
    {
        Param<float16_t> a;
        Param<bfloat16_t> b;
        a.set(float16_t(1.5f));
        b.set(bfloat16_t(2.75f));
        float result = evaluate<float>(cast<float>(a) + cast<float>(b));
        if (result != 4.25f) {
            printf("Incorrect result: %f != 4.25f\n", result);
            return 1;
        }
    }

    // Check scalar parameters work using a problematic case
    {
        Param<float16_t> a, b, c;
        a.set(float16_t(24.062500f));
        b.set(float16_t(30.187500f));
        c.set(float16_t(0));
        float16_t result = evaluate<float16_t>(lerp(a, b, c));
        if (float(result) != 24.062500f) {
            printf("Incorrect result: %f != 24.0625f\n", (float)result);
            return 1;
        }
    }

    {
        Param<bfloat16_t> a, b, c;
        a.set(bfloat16_t(24.5f));
        b.set(bfloat16_t(30.5f));
        c.set(bfloat16_t(0));
        bfloat16_t result = evaluate<bfloat16_t>(lerp(a, b, c));
        if (float(result) != 24.5f) {
            printf("Incorrect result: %f != 24.5f\n", (float)result);
            return 1;
        }
    }

    // Check that ties round towards a zero last bit on narrowing conversions
    {
        bfloat16_t start = bfloat16_t(37.2789f);
        for (uint16_t x = 0; x < 8; x++) {
            bfloat16_t a = bfloat16_t::make_from_bits(start.to_bits() + x);
            bfloat16_t b = bfloat16_t::make_from_bits(start.to_bits() + x + 1);
            bfloat16_t ab = bfloat16_t(((float)a + (float)b) / 2);

            if (a > ab || ab > b) {
                printf("Misordered: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
            }

            bool ok = (((a.to_bits() & 1) && (ab == b)) ||
                       ((b.to_bits() & 1) && (ab == a)));

            if (!ok) {
                printf("Incorrect rounding: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
                return -1;
            }
        }
    }

    // Check that ties round towards a zero last bit on narrowing conversions
    {
        float16_t start = float16_t(37.2789f);
        for (uint16_t x = 0; x < 8; x++) {
            float16_t a = float16_t::make_from_bits(start.to_bits() + x);
            float16_t b = float16_t::make_from_bits(start.to_bits() + x + 1);
            float16_t ab = float16_t(((float)a + (float)b) / 2);

            if (a > ab || ab > b) {
                printf("Misordered: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
            }

            bool ok = (((a.to_bits() & 1) && (ab == b)) ||
                       ((b.to_bits() & 1) && (ab == a)));

            if (!ok) {
                printf("Incorrect rounding: %x %x %x\n", a.to_bits(), ab.to_bits(), b.to_bits());
                return -1;
            }
        }
    }

    // Check rounding intrinsics
    {
        Func noise;
        Var x;
        noise(x) = (random_int() % 256) * 0.1f;
        noise.compute_root();
        Func trunc_f32 = lambda(x, trunc(noise(x)));
        Func round_f32 = lambda(x, round(noise(x)));
        Func ceil_f32 = lambda(x, ceil(noise(x)));
        Func floor_f32 = lambda(x, floor(noise(x)));
        Func trunc_f16 = lambda(x, trunc(cast<float16_t>(noise(x))));
        Func round_f16 = lambda(x, round(cast<float16_t>(noise(x))));
        Func ceil_f16 = lambda(x, ceil(cast<float16_t>(noise(x))));
        Func floor_f16 = lambda(x, floor(cast<float16_t>(noise(x))));

        std::vector<Func> funcs = {trunc_f32, round_f32, ceil_f32, floor_f32,
                                   trunc_f16, round_f16, ceil_f16, floor_f16};

        for (auto f : funcs) {
            f.compute_root().vectorize(x, 16);
        }

        const char *names[] = {"trunc", "round", "ceil", "floor"};

        Pipeline p(funcs);
        Realization r = p.realize(1024);
        for (int i = 0; i < 1024; i++) {
            for (int j = 0; j < 4; j++) {
                float f32 = Buffer<float>(r[j])(i);
                float f16 = float(Buffer<float16_t>(r[j + 4])(i));
                if (f32 != f16) {
                    printf("%s outputs do not match: %f %f\n",
                           names[j], f32, f16);
                    return -1;
                }
            }
        }
    }

    Target target = get_jit_target_from_environment();
    if (target.has_feature(Target::CUDA) ||
        target.has_feature(Target::Metal)) {
        // Check we can pass a float16 to a GPU kernel. Skip OpenCL
        // because support is spotty.
        Var x, y;
        ImageParam input(Float(16), 2);
        Param<float16_t> mul("mul");

        Func output;
        output(x, y) = x * y * (input(x, y) * mul);

        Var xi, yi;
        output.gpu_tile(x, y, xi, yi, 8, 8);

        mul.set(float16_t(2.0f));
        Buffer<float16_t> in(8, 8);
        in.fill(float16_t(0.25f));
        input.set(in);
        Buffer<float16_t> buf = output.realize(8, 8);
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                float16_t correct = float16_t((x * y) / 2.0f);
                if (buf(x, y).to_bits() != correct.to_bits()) {
                    printf("buf(%d, %d) = 0x%x instead of 0x%x\n",
                           x, y, buf(x, y).to_bits(), correct.to_bits());
                    return -1;
                }
            }
        }
    }

    {
        // Check constants are emitted correctly
        Func out;
        float16_t constant(100.0f);
        out() = constant;
        Buffer<float16_t> buf = out.realize();
        if (buf(0) != constant) {
            printf("buf(0) = %f instead of %f\n", float(buf(0)), float(constant));
            return -1;
        }
    }

    // Enable to read assembly generated by the conversion routines
    if ((false)) {  // Intentional dead code. Extra parens to pacify clang-tidy.
        Func src, to_f16, from_f16;

        src(x) = cast<float>(x);
        to_f16(x) = cast<float16_t>(src(x));
        from_f16(x) = cast<float>(to_f16(x));

        src.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
        to_f16.compute_root().vectorize(x, 8, TailStrategy::RoundUp);
        from_f16.compute_root().vectorize(x, 8, TailStrategy::RoundUp);

        from_f16.compile_to_assembly("/dev/stdout", {}, Target("host-no_asserts-no_bounds_query-no_runtime-disable_llvm_loop_unroll-disable_llvm_loop_vectorize"));
    }

    printf("Success!\n");
    return 0;
}
