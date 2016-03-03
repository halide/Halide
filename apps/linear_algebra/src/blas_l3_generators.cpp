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

    GeneratorParam<bool> transpose_A_ = {"transpose_A", false};
    GeneratorParam<bool> transpose_B_ = {"transpose_B", false};

    // Standard ordering of parameters in GEMM functions.
    Param<T>   a_ = {"a", 1.0};
    ImageParam A_ = {type_of<T>(), 2, "A"};
    ImageParam B_ = {type_of<T>(), 2, "B"};
    Param<T>   b_ = {"b", 1.0};
    ImageParam C_ = {type_of<T>(), 2, "C"};

    Var i, j, ii, ji, jii, io, jo, ti, tj, t;

    Func build() {
        const Expr num_rows = (A_.width()/32)*32;
        const Expr num_cols = (B_.height()/32)*32;
        const Expr sum_size = (A_.height()/32)*32;

        const int vec = natural_vector_size(a_.type());
        const int s = vec * 2;

        // Instead of transposing B, swap A and B, transpose A, and
        // then transpose AB.
        bool transpose_AB = false;
        if (transpose_B_) {
            std::swap(A_, B_);
            transpose_A_.set(!transpose_A_);
            transpose_B_.set(false);
            transpose_AB = true;
        }

        Var ti[3], tj[3];
        Func result("result");

        // Swizzle A for better memory order in the inner loop.
        Func A("A"), B("B"), As("As"), Atmp("Atmp");
        Atmp(i, j) = A_(i, j);
        if (transpose_A_) {
            As(i, j, io) = Atmp(j, io*s + i);
        } else {
            As(i, j, io) = Atmp(io*s + i, j);
        }

        A(i, j) = As(i % s, j, i / s);
        B(i, j) = B_(i, j);

        Var k("k");
        Func prod;
        // Express all the products we need to do a matrix multiply as a 3D Func.
        prod(k, i, j) = A(i, k) * B(k, j);

        // Reduce the products along k.
        Func AB("AB");
        RDom rv(0, sum_size);
        AB(i, j) += prod(rv, i, j);

        Func ABt("ABt");
        if (transpose_AB) {
            // Transpose A*B if necessary.
            ABt(i, j) = AB(j, i);
        } else {
            ABt(i, j) = AB(i, j);
        }

        // Do the part that makes it a 'general' matrix multiply.
        result(i, j) = (a_ * ABt(i, j) + b_ * C_(i, j));

        if (transpose_AB) {
            result
                .tile(i, j, ii, ji, 4, s).vectorize(ii).unroll(ji)
                .tile(i, j, ti[0], tj[0], i, j, s/4, 1);
        } else {
            result
                .tile(i, j, ii, ji, s, 4).vectorize(ii).unroll(ji)
                .tile(i, j, ti[0], tj[0], i, j, 1, s/4);
        }
        result.tile(ti[0], tj[0], ti[0], tj[0], ti[1], tj[1], 2, 2);

        // If we have enough work per task, parallelize over these tiles.
        result.specialize(num_rows >= 256 && num_cols >= 256)
            .fuse(tj[0], ti[0], t).parallel(t);

        // Otherwise tile one more time before parallelizing, or don't
        // parallelize at all.
        result.specialize(num_rows >= 128 && num_cols >= 128)
            .tile(ti[0], tj[0], ti[0], tj[0], ti[2], tj[2], 2, 2)
            .fuse(tj[0], ti[0], t).parallel(t);

        result.bound(i, 0, num_rows).bound(j, 0, num_cols);

        As.compute_root()
            .split(j, jo, ji, s).reorder(i, ji, io, jo)
            .unroll(i).vectorize(ji)
            .specialize(A_.width() >= 256 && A_.height() >= 256).parallel(jo, 4);

        Atmp.compute_at(As, io)
            .vectorize(i).unroll(j);

        AB.compute_at(result, i)
            .unroll(j).vectorize(i)
            .update()
            .reorder(i, j, rv).unroll(j).unroll(rv, 2).vectorize(i);

        if (transpose_AB) {
            ABt.compute_at(result, i).unroll(i).vectorize(j);
        }

        A_.dim(0).set_min(0).dim(1).set_min(0);
        B_.dim(0).set_bounds(0, sum_size).dim(1).set_min(0);
        C_.dim(0).set_bounds(0, num_rows).dim(1).set_bounds(0, num_cols);
        result.output_buffer().dim(0).set_bounds(0, num_rows).dim(1).set_bounds(0, num_cols);

        return result;
    }
};

RegisterGenerator<GEMMGenerator<float>>    register_sgemm("sgemm");
RegisterGenerator<GEMMGenerator<double>>   register_dgemm("dgemm");

}  // namespace
