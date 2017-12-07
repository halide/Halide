#include <vector>
#include "Halide.h"

using namespace Halide;

namespace {

// Generator class for BLAS gemm operations.
class GEMMGenerator :
        public Generator<GEMMGenerator> {
  public:
    typedef Generator<GEMMGenerator> Base;
    using Base::target;
    using Base::get_target;
    using Base::natural_vector_size;

    GeneratorParam<bool> transpose_A_ = {"transpose_A", false};
    GeneratorParam<bool> transpose_B_ = {"transpose_B", false};
    GeneratorParam<bool> transpose_C_ = {"transpose_C", false};

    // Standard ordering of parameters in GEMM functions.
    ImageParam A_ = {type_of<uint8_t>(), 2, "A"};
    Param<int32_t> a_offset = {"a_offset", 0};
    ImageParam B_ = {type_of<uint8_t>(), 2, "B"};
    Param<int32_t> b_offset = {"b_offset", 0};
    ImageParam C_ = {type_of<uint8_t>(), 2, "C"};
    Param<int32_t> c_offset = {"c_offset", 0};
    Param<int32_t> c_mult_int = {"c_mult_int", 0};
    Param<int32_t> c_shift = {"c_shift", 0};

    Func build() {
        Var i("i"), j("j"), ii("ii"), ji("ji"), io("io"), jo("jo"), t("t");

        // Matrices are interpreted as column-major by default. The
        // transpose GeneratorParams are used to handle cases where
        // one or both is actually row major.
        const Expr num_rows = (A_.width()/32)*32;
        const Expr num_cols = (B_.height()/32)*32;
        const Expr sum_size = (A_.height()/32)*32;

        const int vec = natural_vector_size(Int(32));
        const int s = vec * 2;

        // If they're both transposed, then reverse the order and transpose the result instead.
        bool transpose_AB = false;
        if ((bool)transpose_A_ && (bool)transpose_B_) {
            std::swap(A_, B_);
            std::swap(a_offset, b_offset);
            transpose_A_.set(false);
            transpose_B_.set(false);
            transpose_AB = true;
        }

        Var ti[3], tj[3];
        Func result("result");

        // Swizzle A for better memory order in the inner loop.
        Func A("A"), B("B"), A_upcast("A_upcast"), B_upcast("B_upcast"), Btmp("Btmp");
        Func As("As"), Atmp("Atmp"), result_tmp1("result_tmp1"), result_tmp2("result_tmp2");
        Atmp(i, j) = A_(i, j);

        if (transpose_A_) {
            As(i, j, io) = Atmp(j, io*s + i);
        } else {
            As(i, j, io) = Atmp(io*s + i, j);
        }

        A(i, j) = As(i % s, j, i / s);

        Btmp(i, j) = B_(i, j);
        if (transpose_B_) {
            B(i, j) = Btmp(j, i);
        } else {
            B(i, j) = Btmp(i, j);
        }

        A_upcast(i, j) = cast<int32_t>(A(i, j));
        B_upcast(i, j) = cast<int32_t>(B(i, j));

        // Compute term2 = a_offset * P * B (where P is identity matrix with dim equal to A)
        // P * B is equal to row vector where each element is the sum of the corresponding columns
        Func term2("term2");
        RDom r2(0, num_cols);
        if (transpose_B_) {
            term2(j) += a_offset * B_(j, r2);
        } else {
            term2(j) += a_offset * B_(r2, j);
        }

        // Compute term3 = b_offset * A * Q (where Q is identity matrix with dim equal to B)
        // A * Q is equal to column vector where each element is the sum of the corresponding rows
        Func term3("term3");
        RDom r3(0, num_rows);
        if (transpose_A_) {
            term3(i) += b_offset * A_(r3, i);
        } else {
            term3(i) += b_offset * A_(i, r3);
        }

        // Compute term4 = a_offset * b_offset * P * Q.
        // Each element in P * Q is equal to the depth of the matrix (column size of A or row size of
        // B)
        Expr term4 = a_offset * b_offset * sum_size;

        // Compute A*B
        Var k("k");
        Func prod;
        // Express all the products we need to do a matrix multiply as a 3D Func.
        prod(k, i, j) = (A_upcast(i, k)* B_upcast(k, j));

        // Reduce the products along k.
        Func AB("AB");
        RDom rv(0, sum_size);
        AB(i, j) += prod(rv, i, j);

        Func all_terms("all_terms");
        all_terms(i, j) = AB(i, j) + term2(j) + term3(i) + term4;

        Func ABt("ABt");
        if (transpose_AB) {
            // Transpose if necessary.
            ABt(i, j) = all_terms(j, i);
        } else {
            ABt(i, j) = all_terms(i, j);
        }

        // Do the part that makes it a 'general' matrix multiply.
        result_tmp1(i, j) = (ABt(i, j) + c_offset) * c_mult_int;
        Expr rounding_term = 1 << (c_shift - 1);
        result_tmp2(i, j) = select(c_shift < 1, result_tmp1(i, j), result_tmp1(i, j) + rounding_term) >> c_shift;

        if (transpose_C_) {
            result(i, j) = cast<uint8_t>(clamp(result_tmp2(j, i), 0, 255));
        } else {
            result(i, j) = cast<uint8_t>(clamp(result_tmp2(i, j), 0, 255));
        }

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

        result.rename(tj[0], t);

        result.bound(i, 0, num_rows).bound(j, 0, num_cols);

        term2.compute_root()
            .split(j, jo, ji, s)
            .vectorize(ji)
            .update()
            .split(j, jo, ji, s)
            .reorder(ji, jo, r2).unroll(jo, 2).vectorize(ji);
        term2.bound(j, 0, num_cols);

        term3.compute_root()
            .split(i, io, ii, s)
            .vectorize(ii)
            .update()
            .split(i, io, ii, s)
            .reorder(ii, io, r3).unroll(io, 2).vectorize(ii);
        term3.bound(i, 0, num_rows);

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
            .unroll(j).vectorize(i)
            .update()
            .reorder(i, j, rv).unroll(j).unroll(rv, 2).vectorize(i);

        if (transpose_AB) {
            ABt.compute_at(result, i).unroll(i).vectorize(j);
        }

        A_.set_min(0, 0).set_min(1, 0);
        B_.set_bounds(0, 0, sum_size).set_min(1, 0);
        C_.set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);
        result.output_buffer().set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);

        //result.print_loop_nest();

        return result;
    }
};

RegisterGenerator<GEMMGenerator>    register_igemm("igemm");

}  // namespace
