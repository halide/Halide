#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Tools;
using namespace Halide::ConciseCasts;

// const int SIZE = 100000;

Expr int8(const Expr &expr) {
    return cast<int8_t>(expr);
}

Expr uint8(const Expr &expr) {
    return cast<uint8_t>(expr);
}

Expr int16(const Expr &expr) {
    return cast<int16_t>(expr);
}

Expr uint16(const Expr &expr) {
    return cast<uint16_t>(expr);
}

Expr int32(const Expr &expr) {
    return cast<int32_t>(expr);
}

Expr uint32(const Expr &expr) {
    return cast<uint32_t>(expr);
}

Expr reinterpret(const Expr &expr) {
    halide_type_code_t code = expr.type().is_int() ? halide_type_uint : halide_type_int;
    Type t = expr.type().with_code(code);
    return reinterpret(t, expr);
}

Expr saturating_narrow(const Expr &expr) {
    Type narrow = expr.type().narrow();
    return saturating_cast(narrow, expr);
}

Expr bitwise_and(const Expr &a, const Expr &b) {
    return a & b;
}

Expr shift_right(const Expr &a, const Expr &b) {
    return a >> b;
}

Expr widen(const Expr &expr) {
    return cast(expr.type().widen(), expr);
}

void test_simd_op_check() {
    ImageParam i16_1(Int(16), 1);
    ImageParam i16_2(Int(16), 1);
    ImageParam u16_1(UInt(16), 1);
    ImageParam u16_2(UInt(16), 1);
    ImageParam u8_1(UInt(8), 1);
    ImageParam i8_1(Int(8), 1);
    ImageParam i32_1(Int(32), 1);
    ImageParam i32_2(Int(32), 1);
    ImageParam u32_1(UInt(32), 1);
    ImageParam u32_2(UInt(32), 1);
    Var x("x");

    Target target("x86-64-linux-sse41-avx-avx2");

    {
        // i8_sat((i32(i16_1) + 8) / 16)
        Func f("f");

        Expr v_i16 = Variable::make(Int(16, 16), "v_i16");

        f(x) = i8_sat((i32(i16_1(x)) + 8) / 16);

        std::cout << i8_sat((i32(v_i16) + 8) / 16) << "\n";
        std::cout << lower_intrinsics(i8_sat((i32(v_i16) + 8) / 16)) << "\n";
        std::cout << find_intrinsics(i8_sat((i32(v_i16) + 8) / 16)) << "\n";
        std::cout << find_intrinsics(lower_intrinsics(i8_sat((i32(v_i16) + 8) / 16))) << "\n";

        // f.vectorize(x, 32);

        // std::string test_name = "sqrshrn_bad";
        // f.compile_to_assembly(test_name + ".asm", f.infer_arguments());
        // f.compile_to_lowered_stmt(test_name + ".stmt", f.infer_arguments(), Text);
    }
}

void test_unsigned_saturating_add(const Target &t, const std::string &name) {
    ImageParam u32_1(UInt(32), 1);
    ImageParam u32_2(UInt(32), 1);
    Var x("x");
    Func f("f"), g("g");

    f(x) = saturating_add(u32_1(x), u32_2(x));
    g(x) = u32_1(x) + min(u32_2(x), ~u32_1(x));

    const int vector_width = t.natural_vector_size<uint16_t>();

    f.vectorize(x, vector_width, TailStrategy::GuardWithIf);
    g.vectorize(x, vector_width, TailStrategy::GuardWithIf);

    f.compile_to_assembly(name + "_f.asm", f.infer_arguments(), t);
    f.compile_to_lowered_stmt(name + "_f.stmt", f.infer_arguments(), Text, t);
    // f.compile_to_llvm_assembly(name + "_f.ll", f.infer_arguments(), t);

    g.compile_to_assembly(name + "_g.asm", g.infer_arguments(), t);
    g.compile_to_lowered_stmt(name + "_g.stmt", g.infer_arguments(), Text, t);
    // g.compile_to_llvm_assembly(name + "_g.ll", g.infer_arguments(), t);
}

void test_unsigned_saturating_add() {
    Target x86("x86-64-linux-sse41-avx-avx2");
    Target hvx("hexagon-32-noos-no_bounds_query-no_asserts-hvx_128-hvx_v66");
    Target arm = get_host_target();

    test_unsigned_saturating_add(x86, "usadd_x86");
    // test_unsigned_saturating_add(hvx, "usadd_hvx");
    test_unsigned_saturating_add(arm, "usadd_arm");
}

void test_lifting_sobel() {
    
    const int lanes = 16;
    Expr x = Variable::make(UInt(8, lanes), "x");
    Expr y = Variable::make(UInt(8, lanes), "y");
    Expr z = Variable::make(UInt(8, lanes), "z");

    Expr expr = cast(UInt(16, lanes), x) + 2 * cast(UInt(16, lanes), y) + cast(UInt(16, lanes), z);
    expr = simplify(expr);

    std::cout << expr << "\n";
    std::cout << find_intrinsics(expr) << "\n";

    // (((uint16((uint8)x)*(uint16)2) + uint16((uint8)y)) + uint16((uint8)z))
    expr = 2 * cast(UInt(16, lanes), x) + cast(UInt(16, lanes), y) + cast(UInt(16, lanes), z);

    expr = simplify(expr);

    std::cout << expr << "\n";
    std::cout << find_intrinsics(expr) << "\n";

    // depthwise_conv
    // (int32((int16)x)*int32((uint8)y)) -> widening_mul((int16)x, int16((uint8)y))
    {
        Expr x_i16 = Variable::make(Int(16, lanes), "x");
        Expr y_u8 = Variable::make(UInt(8, lanes), "y");

        Expr expr = cast(Int(32, lanes), x_i16) * cast(Int(32, lanes), y_u8);

        expr = simplify(expr);

        std::cout << expr << "\n";
        std::cout << find_intrinsics(expr) << "\n";

    }

    {
        // (int16((uint8)x)*(int16)2) -> (int16)reinterpret((uint16)widening_shift_left((uint8)x, (uint8)1))
        // gaussian3x3
        Expr expr = cast(Int(16, lanes), x) * 2;

        expr = simplify(expr);

        std::cout << "\n" << expr << "\n";
        std::cout << find_intrinsics(expr) << "\n";
    }
}

void run_sobel(const std::string &name, const Target &target) {
    ImageParam input(UInt(8), 2);
    Var x("x"), y("y");

    Func sobel_x_avg{ "sobel_x_avg" }, sobel_y_avg{ "sobel_y_avg" };
    Func sobel_x{ "sobel_x" }, sobel_y{ "sobel_y" };
    Func bounded_input{ "bounded_input" };

    Func output{ "output" };

    bounded_input(x, y) = BoundaryConditions::repeat_edge(input)(x, y);

    Func input_16{ "input_16" };
    input_16(x, y) = cast<uint16_t>(bounded_input(x, y));


    sobel_x_avg(x, y) = input_16(x - 1, y) + 2 * input_16(x, y) + input_16(x + 1, y);
    // ARM
    // sobel_x_avg(x, y) = widening_add(bounded_input(x - 1, y), bounded_input(x + 1, y))  + widening_mul(bounded_input(x, y), cast<uint8_t>(2));
    // HVX
    // sobel_x_avg(x, y) = reinterpret(UInt(16), (cast<int16_t>(bounded_input(x - 1, y)) + (widening_mul(bounded_input(x, y), cast<int8_t>(2)) + widening_mul(bounded_input(x + 1, y), cast<int8_t>(1)))));
    
    sobel_x(x, y) = absd(sobel_x_avg(x, y - 1), sobel_x_avg(x, y + 1));


    sobel_y_avg(x, y) = input_16(x, y - 1) + 2 * input_16(x, y) + input_16(x, y + 1);
    // ARM
    // sobel_y_avg(x, y) = widening_add(bounded_input(x, y - 1), bounded_input(x, y + 1)) + widening_mul(bounded_input(x, y), cast<uint8_t>(2));
    // HVX
    // sobel_y_avg(x, y) = reinterpret(UInt(16), (cast<int16_t>(bounded_input(x, y - 1)) + (widening_mul(bounded_input(x, y), cast<int8_t>(2)) + widening_mul(bounded_input(x, y + 1), cast<int8_t>(1)))));

    sobel_y(x, y) = absd(sobel_y_avg(x - 1, y), sobel_y_avg(x + 1, y));


    // This sobel implementation is non-standard in that it doesn't take the square root
    // of the gradient.
    output(x, y) = cast<uint8_t>(clamp(sobel_x(x, y) + sobel_y(x, y), 0, 255));

    input.dim(0).set_min(0);
    input.dim(1).set_min(0);

    Var xi{"xi"}, yi{"yi"};

    input.dim(0).set_min(0);
    input.dim(1).set_min(0);

    if (target.arch == Target::Hexagon) {
        const int vector_size = 128;
        // Expr input_stride = input.width();
        // input.dim(1).set_stride((input_stride / vector_size) * vector_size);

        // Expr output_stride = output.width();
        // output.dim(1).set_stride((output_stride / vector_size) * vector_size);
        bounded_input
            .compute_at(Func(output), y)
            .align_storage(x, 128)
            .vectorize(x, vector_size, TailStrategy::RoundUp);
        output
            .hexagon()
            .tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
            .vectorize(xi);
    } else {
        const int vector_size = (target.arch == Target::Hexagon) ? (4 * target.natural_vector_size<uint16_t>()) : target.natural_vector_size<uint8_t>();
        bounded_input
            .compute_at(output, y)
            .align_storage(x, 128)
            .vectorize(x, vector_size, TailStrategy::RoundUp);
        output
            .tile(x, y, xi, yi, vector_size, 4, TailStrategy::RoundUp)
            .vectorize(xi)
            .unroll(yi);
    }

    // const std::string test_name = "sobel_test_saturate_" + name;
    const std::string test_name = "sobel_test_" + name;
    output.compile_to_assembly(test_name + ".asm", output.infer_arguments(), target);
    output.compile_to_lowered_stmt(test_name + ".stmt", output.infer_arguments(), Text, target);
    output.compile_to_llvm_assembly(test_name + ".ll", output.infer_arguments(), target);

    if (get_host_target().arch == target.arch) {
        Buffer<uint8_t> input(1536, 2560);

        Buffer<uint8_t> output_(input.width(), input.height());

        output.realize(output_);

        const int timing_iterations = 100;

        double min_t_manual = benchmark(timing_iterations, 10, [&]() {
            output.realize(output_);
            output_.device_sync();
        });
        printf("Runtime time: %gms\n", min_t_manual * 1e3);
    }
}

int main(int argc, char **argv) {

    // test_simd_op_check();

    // Target hl_target = get_target_from_environment();

    // std::cout << hl_target << "\n";

    // test_unsigned_saturating_add();

    // test_lifting_sobel();

    Target x86("x86-64-noos-no_bounds_query-no_asserts-sse41-avx-avx2");

    run_sobel("x86_opt", x86);



    return 0;
}