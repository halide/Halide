#ifndef HALIDE_APPS_HALLMARK_ROPE_WEIGHTS_H_
#define HALIDE_APPS_HALLMARK_ROPE_WEIGHTS_H_

#include <string>

#include "Halide.h"

namespace hallmark {

// Produce cos/sin phased sinusoids at different frequencies to provide a
// positioning signal on input.
// TODO: Is rank 1 correct for this?
struct RoPEWeights : public Halide::NamesInterface {
    RoPEWeights(const std::string &base_name)
        : base_name(base_name), result(base_name + "_rope_weights") {
    }
    std::string base_name;
    Func result;
    RDom r;
    int num_channels;

    void apply(int32_t num_channels, const Type &generating_type) {
        r = RDom(0, num_channels, base_name + "rope_weights_r");

        Expr e = 2.f / num_channels;
        Expr time_scale = pow(1e-4f, e * (r.x % (num_channels / 2)));

        Var h("h"), t("t");
        result(h, t) = undef(generating_type);
        result(r.x, t) = select(r.x >= num_channels / 2, sin(t * time_scale),
                                cos(t * time_scale));
        this->num_channels = num_channels;
    }

    void default_schedule(LoopLevel result_loop_level, const Target &t) {
        RVar ro("ro"), ri("ri");
        result.compute_at(result_loop_level)
            .update()
            .split(r.x, ro, ri, num_channels / 2)
            .unroll(ro)
            .unroll(ri, 4)
            .vectorize(ri, t.natural_vector_size<float>());
    }
};

// Implemented per https://arxiv.org/pdf/2104.09864v5.pdf, bottom of page 5.
// Effectively treat each pair of features in the input and weights as a
// complex number and multiply them
//
// Complex representation places real values contiguous and imaginary values
// contifuous immediately after the real ones.
//
// TODO: Rewrite to take arguments from embedding rather than
// hardcodeing vars.
struct RoPE : public Halide::NamesInterface {
    RoPE(const std::string &base_name)
        : base_name(base_name),
          result(base_name + "_rotated"),
          inner(base_name + "_rotated_inner") {
    }
    std::string base_name;
    Func result;
    Func inner;
    Var inner_var{"inner_var"};
    Var is_imaginary{"is_imaginary"};
    int d;

    void apply(Func embedding, Func rope_weights, int d) {
        std::vector<Var> a = embedding.args();
        CHECK(a.size() == 4);

        std::vector<Expr> args(a.begin(), a.end());

        Expr real_h_index = inner_var;
        Expr imaginary_h_index = d / 2 + inner_var;

        auto e_args_r = args, e_args_i = args;
        e_args_r[0] = real_h_index;
        e_args_i[0] = imaginary_h_index;

        Expr e_r = embedding(e_args_r);
        Expr e_i = embedding(e_args_i);

        std::vector<Expr> rw_args_r = {real_h_index, args[2]};
        std::vector<Expr> rw_args_i = {imaginary_h_index, args[2]};

        Expr rw_r = rope_weights(rw_args_r);
        Expr rw_i = rope_weights(rw_args_i);

        auto ri_args = args;
        ri_args[0] = inner_var;
        ri_args.insert(ri_args.begin(), is_imaginary);

        // ri(is_imaginary, inner_var, ...) = select(...);
        inner(ri_args) = select(is_imaginary == 0, e_r * rw_r - e_i * rw_i,
                                e_r * rw_i + e_i * rw_r);

        auto r_args_lhs = args;
        r_args_lhs[0] = inner_var;

        auto r_args_rhs = args;
        r_args_rhs[0] = inner_var % (d / 2);
        r_args_rhs.insert(r_args_rhs.begin(), inner_var >= (d / 2));

        result(r_args_lhs) = inner(r_args_rhs);

        this->d = d;
    }

    void default_schedule(LoopLevel result_loop_level, const Target &t) {
        Var io("io"), ii("ii");
        inner.compute_at(result, io)
            .bound(is_imaginary, 0, 2)
            .unroll(is_imaginary)
            .unroll(inner_var, 4)
            .vectorize(inner_var, t.natural_vector_size<float>());
        result.compute_at(result_loop_level)
            .split(inner_var, io, ii, d / 2)
            .unroll(io, 2)
            .unroll(ii, 4)
            .vectorize(ii, t.natural_vector_size<float>());
    }
};

}  // namespace hallmark

#endif  // HALIDE_APPS_HALLMARK_ROPE_WEIGHTS_H_
