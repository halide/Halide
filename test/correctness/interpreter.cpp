#include "Halide.h"

using namespace Halide;

int main(int argc, char **argv) {

    // TODO(#5738): remove after winbots are upgraded
    Target target = get_jit_target_from_environment();
    if (target.os == Target::Windows &&
        (target.has_feature(Target::OpenCL) ||
         target.has_feature(Target::D3D12Compute))) {
        printf("[SKIP] workaround for issue #5738\n");
        return 0;
    }

    // Workaround for https://github.com/halide/Halide/issues/7420
    if (target.has_feature(Target::WebGPU)) {
        printf("[SKIP] workaround for issue #7420\n");
        return 0;
    }

    // This test demonstrates a trick for writing interpreters in
    // Halide, and as a side-effect tests our ability to correctly
    // emit switch statements.

    // We'll define a mini arithmetic language to evaluate the same
    // arbitrary expression at every pixel, with the expression
    // provided by a sort of bytecode input to the pipeline. The
    // expression can include transcendentals, which would be
    // expensive if evaluated, so a big select tree is a bad idea.

    // We'll use SSA form. Every op in the expression language will
    // have two integer args indicating which prior values serve as
    // inputs, and one immediate arg. The single output of each op
    // just gets appended to the end of working memory. Working memory
    // is initialized to a 3x3 stencil footprint pulled from the
    // input. The amount of working memory required is thus just the
    // number of ops in the program plus 9, and the output of the
    // program is whatever gets left at the end of working memory.

    ImageParam program(Int(32), 2);

    ImageParam input(UInt(8), 2);

    Var x, y, u;

    // Working memory is initially undefined. We'll use int32 for working values.
    Func scratch;
    scratch(x, y, u) = undef<int32_t>();

    // Populate the start of working memory with a 3x3 stencil.
    RDom load_input(0, 3, 0, 3);
    scratch(x, y, load_input.x + load_input.y * 3) =
        cast<int>(input(x + load_input.x - 1, y + load_input.y - 1));

    // Then perform the ops specified by the program. This will be a
    // 2D RDom over the program. At every program instruction (the
    // outer loop) we'll evaluate every possible op (the inner loop),
    // but skip all but the correct one using a where clause. This
    // compiles to a switch statement.
    const int num_ops = 6;
    RDom r(0, num_ops, 0, program.dim(1).extent());

    Expr op = program(0, r.y);
    Expr arg1 = program(1, r.y);  // refers to an existing value
    Expr arg2 = program(2, r.y);  // refers to an existing value
    Expr arg3 = program(3, r.y);  // An immediate constant

    // Load the two inputs. If you trust the input program, replace
    // clamp with unsafe_promise_clamped. The range of valid inputs
    // locations is [0...8] when r.y is zero (the input 3x3 stencil),
    // and increases by one each iteration thereafter.

    Expr input1 = scratch(x, y, clamp(arg1, 0, r.y + 8));
    Expr input2 = scratch(x, y, clamp(arg2, 0, r.y + 8));

    std::vector<Expr> possible_results{
        arg3,  // Push a constant
        input1 + input2,
        input1 - input2,
        input1 * input2,
        input1 / input2,
        cast<int>(floor(sqrt(input1)))};

    // Give ourselves convenient names for these ops in the list to
    // use in the tests below.
    enum Op {
        Const = 0,
        Add,
        Sub,
        Mul,
        Div,
        Sqrt,
    };

    assert(num_ops == (int)possible_results.size());

    r.where(r.x == op);
    scratch(x, y, r.y + 9) = mux(r.x, possible_results);

    Func output;
    output(x, y) = cast<uint8_t>(scratch(x, y, 8 + program.dim(1).extent()));

    Target t = get_jit_target_from_environment();

    // Unroll the loading of the input stencil
    scratch
        .update(0)
        .unroll(load_input.x)
        .unroll(load_input.y);

    // The loop over possible ops must be fully unrolled to turn into
    // a switch statement.
    scratch
        .update(1)
        .unroll(r.x);

    if (t.has_gpu_feature()) {
        // Compile to GPU, storing working memory in shared.
        Var xi, yi;
        output
            .gpu_tile(x, y, xi, yi, 16, 16);
        scratch
            .compute_at(output, x)
            .gpu_threads(x, y);
    } else {
        // Compute to CPU, vectorizing the entire interpreter.
        output.vectorize(x, 8).parallel(y);
    }

    output.compile_jit(t);

    // Run some sample programs on a noise input

    const int W = 128, H = 128;
    Buffer<uint8_t> in_buf(W + 2, H + 2);
    in_buf.set_min(-1, -1);
    std::mt19937 rng{0};
    in_buf.for_each_value([&](uint8_t &val) { val = (uint8_t)rng(); });
    in_buf.set_host_dirty();
    input.set(in_buf);

    Buffer<uint8_t> out_buf(W, H);

    {
        // (in(x + 1, y) - in(x - 1, y)) / 2;
        int program_src[3][4] = {{Sub, 5, 3, 0},
                                 {Const, 0, 0, 2},
                                 {Div, 9, 10, 0}};

        Buffer<int> program_buf(&program_src[0][0], 4, 3);
        program_buf.set_host_dirty();
        program.set(program_buf);

        output.realize(out_buf);
        out_buf.copy_to_host();

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint8_t correct = (uint8_t)(((int)in_buf(x + 1, y) - in_buf(x - 1, y)) >> 1);
                if (out_buf(x, y) != correct) {
                    printf("out_buf(%d, %d) = %d instead of %d\n", x, y, out_buf(x, y), correct);
                    return 1;
                }
            }
        }
    }

    {
        // sqrt(in(x - 1, y - 1) ^ 2 + in(x + 1, y + 1) ^ 2)
        int program_src[4][4] = {{Mul, 0, 0, 0},
                                 {Mul, 8, 8, 0},
                                 {Add, 9, 10, 0},
                                 {Sqrt, 11, 0, 0}};

        const int W = 128, H = 128;

        Buffer<int> program_buf(&program_src[0][0], 4, 4);
        program_buf.set_host_dirty();
        program.set(program_buf);

        output.realize(out_buf);
        out_buf.copy_to_host();

        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                int a = in_buf(x - 1, y - 1);
                int b = in_buf(x + 1, y + 1);
                uint8_t correct = (uint8_t)((int)std::floor(std::sqrt(a * a + b * b)));
                if (out_buf(x, y) != correct) {
                    printf("out_buf(%d, %d) = %d instead of %d\n", x, y, out_buf(x, y), correct);
                    return 1;
                }
            }
        }
    }
    printf("Success!\n");
    return 0;
}
