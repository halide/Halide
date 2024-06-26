#include "Halide.h"
#include <vector>

using namespace Halide;

namespace {

// Generator class for BLAS gemv (GEneralized Matrix-Vector product) operations.
template<class T>
class GEMVGenerator : public Generator<GEMVGenerator<T>> {
public:
    typedef Generator<GEMVGenerator<T>> Base;
    using Base::get_target;
    using Base::natural_vector_size;
    using Base::target;
    template<typename T2>
    using Input = typename Base::template Input<T2>;
    template<typename T2>
    using Output = typename Base::template Output<T2>;

    GeneratorParam<bool> vectorize_{"vectorize", true};
    GeneratorParam<bool> parallel_{"parallel", true};
    GeneratorParam<int> block_size_{"block_size", 1 << 8};
    GeneratorParam<bool> transpose_{"transpose", false};

    // Standard ordering of parameters in GEMV functions.
    Input<T> a_{"a", 1};
    Input<Buffer<T, 2>> A_{"A"};
    Input<Buffer<T, 1>> x_{"x"};
    Input<T> b_{"b", 1};
    Input<Buffer<T, 1>> y_{"y"};

    Output<Buffer<T, 1>> output_{"output"};

    void generate() {
        assert(get_target().has_feature(Target::NoBoundsQuery));

        const int vec_size = vectorize_ ? natural_vector_size(type_of<T>()) : 1;
        const int unroll_size = std::min(vec_size, 4);

        Var i("i"), j("j");
        Func result("result");

        if (transpose_) {
            const Expr size = A_.height();
            const Expr sum_size = A_.width();
            const Expr sum_size_vecs = sum_size / vec_size;

            Func prod("prod");
            prod(j, i) = A_(j, i) * x_(j);

            RDom k(0, sum_size_vecs, "k");
            Func accum_vecs("accum_vecs");
            accum_vecs(j, i) += prod(k * vec_size + j, i);

            Func accum_vecs_transpose("accum_vecs_transpose");
            accum_vecs_transpose(i, j) = accum_vecs(j, i);

            RDom lanes(0, vec_size);
            Func sum_lanes("sum_lanes");
            sum_lanes(i) += accum_vecs_transpose(i, lanes);

            RDom tail(sum_size_vecs * vec_size, sum_size - sum_size_vecs * vec_size);
            Func sum_tail("sum_tail");
            sum_tail(i) = sum_lanes(i);
            sum_tail(i) += prod(tail, i);

            Func Ax("Ax");
            Ax(i) = sum_tail(i);
            result(i) = b_ * y_(i) + a_ * Ax(i);

            Var ii("ii"), t("t");
            result.specialize((sum_size / vec_size) * vec_size == sum_size)
                .specialize(size >= unroll_size)
                .vectorize(i, unroll_size)
                .specialize(size >= block_size_)
                .split(i, t, i, block_size_ / unroll_size)
                .parallel(t);

            result
                .specialize(size >= unroll_size)
                .vectorize(i, unroll_size)
                .specialize(size >= block_size_)
                .split(i, t, i, block_size_ / unroll_size)
                .parallel(t);

            accum_vecs
                .compute_at(result, i)
                .unroll(i)
                .unroll(j)
                .update()
                .reorder(i, j, k)
                .unroll(i)
                .unroll(j);
            accum_vecs_transpose
                .compute_at(result, i)
                .unroll(i)
                .unroll(j);
            sum_lanes
                .compute_at(result, i)
                .update()
                .unroll(lanes);
            sum_tail
                .compute_at(result, i)
                .update()
                .reorder(i, tail);  //.unroll(i);

            if (vectorize_) {
                accum_vecs.vectorize(j)
                    .update()
                    .vectorize(j);
                accum_vecs_transpose.vectorize(j);

                sum_lanes.specialize(size >= vec_size).vectorize(i, vec_size);
                sum_lanes.update().specialize(size >= vec_size).vectorize(i, vec_size);

                sum_tail.specialize(size >= vec_size).vectorize(i, vec_size);
                sum_tail.update().specialize(size >= vec_size).vectorize(i, vec_size);
            }

            A_.dim(0).set_min(0).dim(1).set_min(0);
            x_.dim(0).set_bounds(0, A_.width());
            y_.dim(0).set_bounds(0, A_.height());
        } else {
            const Expr size = A_.width();
            const Expr sum_size = A_.height();
            const Expr sum_size_cols = (sum_size / unroll_size) * unroll_size;
            const Expr tail_size = sum_size - sum_size_cols;

            RDom k(0, sum_size_cols, "k");
            RDom tail(sum_size_cols, tail_size, "tail");
            Func block("block");
            block(i) = b_ * y_(i);
            block(i) += a_ * A_(i, k) * x_(k);
            block(i) += a_ * A_(i, tail) * x_(tail);
            result(i) = block(i);

            RVar ki("ki");
            Var ii("ii");
            result.specialize(tail_size == 0)
                .specialize(size >= vec_size)
                .vectorize(i, vec_size)
                .specialize(size >= unroll_size * vec_size)
                .unroll(i, unroll_size)
                .specialize(size >= block_size_)
                .split(i, i, ii, block_size_ / (unroll_size * vec_size))
                .parallel(i);

            result.specialize(size >= vec_size)
                .vectorize(i, vec_size)
                .specialize(size >= unroll_size * vec_size)
                .unroll(i, unroll_size)
                .specialize(size >= block_size_)
                .split(i, i, ii, block_size_ / (unroll_size * vec_size))
                .parallel(i);

            block.compute_at(result, i);
            block.specialize(size >= vec_size)
                .vectorize(i, vec_size);
            block.update()
                .specialize(size >= vec_size && sum_size >= unroll_size)
                .split(i, i, ii, vec_size)
                .split(k, k, ki, unroll_size)
                .reorder(ii, ki, i, k)
                .vectorize(ii)
                .unroll(ki);
            block.update()
                .specialize(size >= vec_size)
                .vectorize(i, vec_size);
            block.update(1)
                .reorder(i, tail)
                .specialize(size >= vec_size)
                .vectorize(i, vec_size)
                .specialize(sum_size >= unroll_size)
                .unroll(i, unroll_size);

            A_.dim(0).set_min(0).dim(1).set_min(0);
            x_.dim(0).set_bounds(0, A_.height());
            y_.dim(0).set_bounds(0, A_.width());
        }

        // TODO: delete this pointless memcpy, as we probably have the tools to deal with this now.
        // see https://github.com/halide/Halide/commit/cf999bf71939261bdcbb92d87fc4d07db5770732
        output_(i) = result(i);
        result.compute_root();

        const Expr size = x_.width();
        Var ii("ii");
        output_.specialize(size >= vec_size)
            .vectorize(i, vec_size)
            .specialize(size >= unroll_size * vec_size)
            .unroll(i, unroll_size)
            .specialize(size >= block_size_)
            .split(i, i, ii, block_size_ / (unroll_size * vec_size))
            .parallel(i);
    }
};

// Generator class for BLAS ger (GEneralized Rank-1 update) operations.
template<class T>
class GERGenerator : public Generator<GERGenerator<T>> {
public:
    typedef Generator<GERGenerator<T>> Base;
    using Base::get_target;
    using Base::natural_vector_size;
    using Base::target;
    template<typename T2>
    using Input = typename Base::template Input<T2>;
    template<typename T2>
    using Output = typename Base::template Output<T2>;

    GeneratorParam<bool> vectorize_{"vectorize", true};
    GeneratorParam<bool> parallel_{"parallel", true};
    GeneratorParam<int> block_size_{"block_size", 1 << 5};

    // Standard ordering of parameters in GEMV functions.
    Input<T> a_{"a", 1};
    Input<Buffer<T, 1>> x_{"x"};
    Input<Buffer<T, 1>> y_{"y"};

    Output<Buffer<T, 2>> result_{"result"};

    void generate() {
        const int vec_size = vectorize_ ? natural_vector_size(type_of<T>()) : 1;

        Var i("i"), j("j");
        result_(i, j) = undef<T>();  // in-place operation on the output

        result_(i, j) += (a_ * y_(j)) * x_(i);

        if (vectorize_) {
            result_.update().vectorize(i, vec_size * 4, TailStrategy::GuardWithIf);
        }
        if (parallel_) {
            result_.update().parallel(j, 8, TailStrategy::GuardWithIf);
        }

        x_.dim(0).set_min(0);
        y_.dim(0).set_min(0);
        result_.dim(0).set_bounds(0, x_.dim(0).extent());
        result_.dim(1).set_bounds(0, y_.dim(0).extent());
    }
};

}  // namespace

HALIDE_REGISTER_GENERATOR(GEMVGenerator<float>, sgemv)
HALIDE_REGISTER_GENERATOR(GEMVGenerator<double>, dgemv)
HALIDE_REGISTER_GENERATOR(GERGenerator<float>, sger)
HALIDE_REGISTER_GENERATOR(GERGenerator<double>, dger)
