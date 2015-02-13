#include <vector>
#include "Halide.h"

using namespace Halide;

namespace {

// Generator class for BLAS gemm operations.
template<class T>
class GEMMGenerator :
        public Generator<GEMMGenerator<T>> {
  public:
    typedef Generator<GEMMGenerator<T>> Base;
    using Base::target;
    using Base::get_target;
    using Base::natural_vector_size;

    GeneratorParam<bool> assertions_enabled_ = {"assertions_enabled", false};
    GeneratorParam<bool> use_fma_ = {"use_fma", false};
    GeneratorParam<bool> vectorize_ = {"vectorize", true};
    GeneratorParam<bool> parallel_ = {"parallel", true};
    GeneratorParam<int>  block_size_ = {"block_size", 1 << 5};
    GeneratorParam<bool> transpose_A_ = {"transpose_A", false};
    GeneratorParam<bool> transpose_B_ = {"transpose_B", false};

    // Standard ordering of parameters in GEMM functions.
    Param<T>   a_ = {"a", 1.0};
    ImageParam A_ = {type_of<T>(), 2, "A"};
    ImageParam B_ = {type_of<T>(), 2, "B"};
    Param<T>   b_ = {"b", 1.0};
    ImageParam C_ = {type_of<T>(), 2, "C"};

    void SetupTarget() {
        if (!assertions_enabled_) {
            target.set(get_target()
                       .with_feature(Target::NoAsserts)
                       .with_feature(Target::NoBoundsQuery));
        }

        if (use_fma_) {
            target.set(get_target().with_feature(Target::FMA));
        }
    }

    Func transpose(ImageParam im) {
        Func transpose_tmp("transpose_tmp"), im_t("im_t");
        Var i("i"), j("j"), ii("ii"), ji("ji"),
            ti("ti"), tj("tj"), t("t");

        transpose_tmp(i, j) = im(j, i);
        im_t(i, j) = transpose_tmp(i, j);

        Expr rows = im.width(), cols = im.height();

        im_t.compute_root()
            .specialize(rows >= 4 && cols >= 4)
            .tile(i, j, ii, ji, 4, 4).vectorize(ii).unroll(ji)
            .specialize(rows >= 128 && cols >= 128)
            .tile(i, j, ti, tj, i, j, 16, 16)
            .fuse(ti, tj, t).parallel(t);

        transpose_tmp.compute_at(im_t, i)
            .specialize(rows >= 4 && cols >= 4).vectorize(j).unroll(i);

        return im_t;
    }

    Func build() {
        SetupTarget();

        const int vec_size = vectorize_? natural_vector_size(type_of<T>()): 1;

        Var i("i"), j("j");
        Var ii("ii"), ji("ji");
        Var ti[3], tj[3], t;
        Func result("result");

        const Expr num_rows = A_.width();
        const Expr num_cols = B_.height();
        const Expr sum_size = A_.height();

        const Expr sum_size_vec = sum_size / vec_size;

        // Pretranspose A and/or B as necessary
        Func At, B;
        if (transpose_A_) {
            At(i, j) = A_(i, j);
        } else {
            At = transpose(A_);
        }

        if (transpose_B_) {
            B = transpose(B_);
        } else {
            B(i, j) = B_(i, j);
        }

        Var k("k");
        Func prod;
        // Express all the products we need to do a matrix multiply as a 3D Func.
        prod(k, i, j) = At(k, i) * B(k, j);

        // Reduce the products along k using whole vectors.
        Func dot_vecs;
        RDom rv(0, sum_size_vec);
        dot_vecs(k, i, j) += prod(rv * vec_size + k, i, j);

        // Transpose the result to make summing the lanes vectorizable
        Func dot_vecs_transpose;
        dot_vecs_transpose(i, j, k) = dot_vecs(k, i, j);

        Func sum_lanes;
        RDom lanes(0, vec_size);
        sum_lanes(i, j) += dot_vecs_transpose(i, j, lanes);

        // Add up any leftover elements when the sum size is not a
        // multiple of the vector size.
        Func sum_tail;
        RDom tail(sum_size_vec * vec_size, sum_size - sum_size_vec * vec_size);
        sum_tail(i, j) += prod(tail, i, j);

        // Add the two.
        Func AB;
        AB(i, j) = sum_lanes(i, j) + sum_tail(i, j);

        // Do the part that makes it a 'general' matrix multiply.
        result(i, j) = a_ * AB(i, j) + b_ * C_(i, j);

        // There's a mild benefit in specializing the case with no
        // tail (the sum size is a whole number of vectors).  We do a
        // z-order traversal of each block expressed using nested
        // tiling.

        result
            .specialize(sum_size == (sum_size / 8) * 8)
            .specialize(num_rows >= 4 && num_cols >= 2)
            .tile(i, j, ii, ji, 4, 2).vectorize(ii).unroll(ji)
            .specialize(num_rows >= 8 && num_cols >= 8)
            .tile(i, j, ti[0], tj[0], i, j, 2, 4)
            .specialize(num_rows >= 16 && num_cols >= 16)
            .tile(ti[0], tj[0], ti[1], tj[1], 2, 2)
            .specialize(num_rows >= 32 && num_cols >= 32)
            .tile(ti[0], tj[0], ti[2], tj[2], 2, 2)
            .specialize(num_rows >= 64 && num_cols >= 64)
            .fuse(tj[0], ti[0], t).parallel(t);

        // The general case with a tail (sum_size is not a multiple of
        // vec_size). The same z-order traversal of blocks of the
        // output.
        result
            .specialize(num_rows >= 4 && num_cols >= 2)
            .tile(i, j, ii, ji, 4, 2).vectorize(ii).unroll(ji)
            .specialize(num_rows >= 8 && num_cols >= 8)
            .tile(i, j, ti[0], tj[0], i, j, 2, 4)
            .specialize(num_rows >= 16 && num_cols >= 16)
            .tile(ti[0], tj[0], ti[1], tj[1], 2, 2)
            .specialize(num_rows >= 32 && num_cols >= 32)
            .tile(ti[0], tj[0], ti[2], tj[2], 2, 2)
            .specialize(num_rows >= 64 && num_cols >= 64)
            .fuse(tj[0], ti[0], t).parallel(t);

        dot_vecs
            .compute_at(result, i).unroll(i).unroll(j)
            .update().reorder(i, j, rv).unroll(i).unroll(j);
        dot_vecs_transpose
            .compute_at(result, i).unroll(i).unroll(j);
        sum_lanes
            .compute_at(result, i).update().unroll(lanes);
        sum_tail
            .compute_at(result, i)
            .update().reorder(i, j, tail).unroll(i).unroll(j);

        if (vectorize_) {
            dot_vecs.vectorize(k).update().vectorize(k);
            dot_vecs_transpose.vectorize(k);

            // The following stages are only vectorizable when we're
            // computing multiple dot products unrolled.
            Expr can_vectorize = num_rows >= 4 && num_cols >= 2;
            sum_tail.specialize(can_vectorize).fuse(i, j, t).vectorize(t);
            sum_lanes.specialize(can_vectorize).fuse(i, j, t).vectorize(t);
            sum_lanes.update().specialize(can_vectorize).fuse(i, j, t).vectorize(t);

        }

        A_.set_min(0, 0).set_min(1, 0);
        B_.set_bounds(0, 0, sum_size).set_min(1, 0);
        C_.set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);
        result.output_buffer().set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);

        return result;
    }
};

RegisterGenerator<GEMMGenerator<float>>    register_sgemm("sgemm");
RegisterGenerator<GEMMGenerator<double>>   register_dgemm("dgemm");

}  // namespace
