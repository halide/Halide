#include <vector>
#include "Halide.h"

using namespace Halide;

namespace {

// Generator class for BLAS axpy operations.
template<class T>
class AXPYGenerator :
        public Generator<AXPYGenerator<T>> {
  public:
    typedef Generator<AXPYGenerator<T>> Base;
    using Base::target;
    using Base::get_target;
    using Base::natural_vector_size;

    GeneratorParam<bool> assertions_enabled_ = {"assertions_enabled", false};
    GeneratorParam<bool> use_fma_ = {"use_fma", false};
    GeneratorParam<bool> vectorize_ = {"vectorize", true};
    GeneratorParam<int>  block_size_ = {"block_size", 1024};
    GeneratorParam<bool> scale_x_ = {"scale_x", true};
    GeneratorParam<bool> add_to_y_ = {"add_to_y", true};

    // Standard ordering of parameters in AXPY functions.
    Param<T>   a_ = {"a", 1.0};
    ImageParam x_ = {type_of<T>(), 1, "x"};
    ImageParam y_ = {type_of<T>(), 1, "y"};

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

    void Schedule(Func result, Expr width) {
        Var i("i"), o("o");
    }

    template<class Arg>
    Expr calc(Arg i) {
        if (static_cast<bool>(scale_x_) && static_cast<bool>(add_to_y_)) {
            return a_ * x_(i) + y_(i);
        } else if (static_cast<bool>(scale_x_)) {
            return a_ * x_(i);
        } else {
            return x_(i);
        }
    }

    Func build() {
        SetupTarget();

        const int vec_size = vectorize_? natural_vector_size(type_of<T>()): 1;
        Expr size = x_.width();
        Expr size_vecs = (size / vec_size) * vec_size;
        Expr size_tail = size - size_vecs;

        Var  i("i");
        Func result("result");
        RDom vecs(0, size_vecs, "vec");
        RDom tail(size_vecs, size_tail, "tail");
        result(i) = undef(type_of<T>());
        result(vecs) = calc(vecs);
        result(tail) = calc(tail);

        if (vectorize_) {
            Var ii("ii");
            result.update().vectorize(vecs, vec_size);
        }

        result.bound(i, 0, x_.width());
        result.output_buffer().set_bounds(0, 0, x_.width());

        x_.set_min(0, 0);
        y_.set_bounds(0, 0, x_.width());

        return result;
    }
};

// Generator class for BLAS dot operations.
template<class T>
class DotGenerator :
        public Generator<DotGenerator<T>> {
  public:
    typedef Generator<DotGenerator<T>> Base;
    using Base::target;
    using Base::get_target;
    using Base::natural_vector_size;

    GeneratorParam<bool> assertions_enabled_ = {"assertions_enabled", false};
    GeneratorParam<bool> use_fma_ = {"use_fma", false};
    GeneratorParam<bool> vectorize_ = {"vectorize", true};
    GeneratorParam<bool> parallel_ = {"parallel", true};
    GeneratorParam<int>  block_size_ = {"block_size", 1024};

    ImageParam x_ = {type_of<T>(), 1, "x"};
    ImageParam y_ = {type_of<T>(), 1, "y"};

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

    Func build() {
        SetupTarget();

        const int vec_size = vectorize_? natural_vector_size(type_of<T>()): 1;
        Expr size = x_.width();
        Expr size_vecs = size / vec_size;
        Expr size_tail = size - size_vecs * vec_size;

        Var i("i");
        Func result;
        if (vectorize_) {
            Func dot;

            RDom k(0, size_vecs);
            dot(i) += x_(k*vec_size + i) * y_(k*vec_size + i);

            RDom lanes(0, vec_size);
            RDom tail(size_vecs * vec_size, size_tail);
            result(i) = undef<T>();
            result(0) = sum(dot(lanes));
            result(0) += sum(x_(tail) * y_(tail));

            dot.compute_root().vectorize(i);
            dot.update(0).vectorize(i);
        } else {
            RDom k(0, size);
            result(i) = undef<T>();
            result(0) = sum(x_(k) * y_(k));
        }

        x_.set_min(0, 0);
        y_.set_bounds(0, 0, size);

        return result;
    }
};

// Generator class for BLAS dot operations.
template<class T>
class AbsSumGenerator :
        public Generator<AbsSumGenerator<T>> {
  public:
    typedef Generator<AbsSumGenerator<T>> Base;
    using Base::target;
    using Base::get_target;
    using Base::natural_vector_size;

    GeneratorParam<bool> assertions_enabled_ = {"assertions_enabled", false};
    GeneratorParam<bool> use_fma_ = {"use_fma", false};
    GeneratorParam<bool> vectorize_ = {"vectorize", true};
    GeneratorParam<bool> parallel_ = {"parallel", true};
    GeneratorParam<int>  block_size_ = {"block_size", 1024};

    ImageParam x_ = {type_of<T>(), 1, "x"};

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

    Func build() {
        SetupTarget();

        const int vec_size = vectorize_? natural_vector_size(type_of<T>()): 1;
        Expr size = x_.width();
        Expr size_vecs = size / vec_size;
        Expr size_tail = size - size_vecs * vec_size;

        Var i("i");
        Func result;
        if (vectorize_) {
            Func norm;

            RDom k(0, size_vecs);
            norm(i) += abs(x_(k*vec_size + i));

            RDom lanes(0, vec_size);
            RDom tail(size_vecs * vec_size, size_tail);
            result(i) = undef<T>();
            result(0) = sum(norm(lanes));
            result(0) += sum(abs(x_(tail)));

            norm.compute_root().vectorize(i);
            norm.update(0).vectorize(i);
        } else {
            RDom k(0, x_.width());
            result(i) = undef<T>();
            result(0) = sum(abs(x_(k)));
        }

        x_.set_min(0, 0);

        return result;
    }
};

RegisterGenerator<AXPYGenerator<float>>    register_saxpy("saxpy");
RegisterGenerator<AXPYGenerator<double>>   register_daxpy("daxpy");
RegisterGenerator<DotGenerator<float>>     register_sdot("sdot");
RegisterGenerator<DotGenerator<double>>    register_ddot("ddot");
RegisterGenerator<AbsSumGenerator<float>>  register_sasum("sasum");
RegisterGenerator<AbsSumGenerator<double>> register_dasum("dasum");

}  // namespace
