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

    Func build() {
        // Matrices are interpreted as column-major by default. The
        // transpose GeneratorParams are used to handle cases where
        // one or both is actually row major.
        const Expr num_rows = A_.width();
        const Expr num_cols = B_.height();
        const Expr sum_size = A_.height();

        const int vec = natural_vector_size(a_.type());
        const int s = vec * 2;

        ImageParam A_in, B_in;

        // If they're both transposed, then reverse the order and transpose the result instead.
        bool transpose_AB = false;
        if ((bool)transpose_A_ && (bool)transpose_B_) {
            A_in = B_;
            B_in = A_;
            transpose_A_.set(false);
            transpose_B_.set(false);
            transpose_AB = true;
        } else {
            A_in = A_;
            B_in = B_;
        }

        Var i, j, ii, ji, jii, iii, io, jo, t;
        Var ti[3], tj[3];
        Func result("result");

        // Swizzle A for better memory order in the inner loop.
        Func A("A"), B("B"), Btmp("Btmp"), As("As"), Atmp("Atmp");
        Atmp(i, j) = BoundaryConditions::constant_exterior(A_in, cast<T>(0))(i, j);

        if (transpose_A_) {
            As(i, j, io) = Atmp(j, io*s + i);
        } else {
            As(i, j, io) = Atmp(io*s + i, j);
        }

        A(i, j) = As(i % s, j, i / s);

        Btmp(i, j) = B_in(i, j);
        if (transpose_B_) {
            B(i, j) = Btmp(j, i);
        } else {
            B(i, j) = Btmp(i, j);
        }

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

        result.tile(i, j, ti[1], tj[1], i, j, 2*s, 2*s, TailStrategy::GuardWithIf);
        if (transpose_AB) {
            result
                .tile(i, j, ii, ji, 4, s)
                .tile(i, j, ti[0], tj[0], i, j, s/4, 1);

        } else {
            result
                .tile(i, j, ii, ji, s, 4)
                .tile(i, j, ti[0], tj[0], i, j, 1, s/4);
        }

        // If we have enough work per task, parallelize over these tiles.
        result.specialize(num_rows >= 512 && num_cols >= 512)
            .fuse(tj[1], ti[1], t).parallel(t);

        // Otherwise tile one more time before parallelizing, or don't
        // parallelize at all.
        result.specialize(num_rows >= 128 && num_cols >= 128)
            .tile(ti[1], tj[1], ti[2], tj[2], ti[1], tj[1], 2, 2)
            .fuse(tj[2], ti[2], t).parallel(t);

        result.rename(tj[0], t);

        result.bound(i, 0, num_rows).bound(j, 0, num_cols);

        As.compute_root()
            .split(j, jo, ji, s).reorder(i, ji, io, jo)
            .unroll(i).vectorize(ji)
            .specialize(A_.width() >= 256 && A_.height() >= 256).parallel(jo, 4);

        Atmp.compute_at(As, io)
            .vectorize(i).unroll(j);

        if (transpose_B_) {
            B.compute_at(result, t)
                .tile(i, j, ii, ji, 8, 8)
                .vectorize(ii).unroll(ji);
            Btmp.reorder_storage(j, i)
                .compute_at(B, i)
                .vectorize(i)
                .unroll(j);
        }


        AB.compute_at(result, i)
            .bound_extent(j, 4).unroll(j)
            .bound_extent(i, s).vectorize(i)
            .update()
            .reorder(i, j, rv).unroll(j).unroll(rv, 2).vectorize(i);
        if (transpose_AB) {
            ABt.compute_at(result, i)
                .bound_extent(i, 4).unroll(i)
                .bound_extent(j, s).vectorize(j);
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
