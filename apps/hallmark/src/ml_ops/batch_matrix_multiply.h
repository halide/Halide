#ifndef HALIDE_APPS_HALLMARK_BATCH_MATRIX_MULTIPLY_H_
#define HALIDE_APPS_HALLMARK_BATCH_MATRIX_MULTIPLY_H_

#include <string>

#include "Halide.h"
#include "absl/log/check.h"

namespace hallmark {

// Multiply all the 2D matrices defined by the initial dimensions of two
// Funcs, iterating across the higher dimensions in correspondence fashion.
// (Should implement the standard ML op, though transposition/adjoint is pushed
//  outside this interface.)

struct BatchMatrixMultiply : public Halide::NamesInterface {
    BatchMatrixMultiply(const std::string &base_name)
        : base_name(base_name), result(base_name + "_batch_matrix_multiply") {
    }

    std::string base_name;
    Func result;

    RDom r;
    Var in1_0, in1_1;

    // TODO: better API needed
    // TODO: Likely can infer the processing type here and make this not just float32.
    void float32_layer(Func in1, Func in2, Expr shared_dim_size,
                       Expr in1_dim1_size, Expr in2_dim0_size) {
        std::vector<Var> in1_args = in1.args();
        std::vector<Var> in2_args = in2.args();

        CHECK(in1_args.size() == in2_args.size());
        CHECK(in1_args.size() > 2);

        r = RDom(0, shared_dim_size, base_name + "_rdom");

        std::vector<Expr> result_reduction_args(in1_args.begin(), in1_args.end());
        std::vector<Expr> in1_reduction_args = result_reduction_args;
        std::vector<Expr> in2_reduction_args = result_reduction_args;

        in1_reduction_args[0] = r;
        in1_reduction_args[1] = in1_args[1];
        in2_reduction_args[0] = in1_args[0];
        in2_reduction_args[1] = r;

        result(in1_args) = 0.0f;
        result(in1_args) += in1(in1_reduction_args) * in2(in2_reduction_args);

        in1_0 = in1_args[0];
        in1_1 = in1_args[1];
    }

    void default_schedule(LoopLevel result_loop_level, const Target &t,
                          int parallel_split) {
        result.compute_at(result_loop_level);
        // Don't vectorize here the pure-init case: it will expand the boundaries
        // (which will cause OOB for some use cases), and more importantly, LLVM is
        // apparently smart enough to just use memset(0) to clear this anyway.

        RVar ro("ro"), ri("ri");
        Var fo("fo"), fi("fi");

        result.update()
            .split(r, ro, ri, t.natural_vector_size<float>() * 4)
            .atomic()
            .vectorize(ri);

        if (parallel_split != 0) {
            result.update().split(in1_1, fo, fi, parallel_split).parallel(fo);
        }
    }
};

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_BATCH_MATRIX_MULTIPLY_H_
