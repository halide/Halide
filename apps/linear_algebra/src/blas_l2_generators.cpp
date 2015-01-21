#include <vector>
#include "Halide.h"

using namespace Halide;

namespace {

// Generator class for BLAS gemv operations.
template<class T>
class GEMVGenerator :
      public Generator<GEMVGenerator<T> > {
 public:
  typedef Generator<GEMVGenerator<T> > Base;
  using Base::target;
  using Base::get_target;
  using Base::natural_vector_size;

  GeneratorParam<bool> assertions_enabled_ = {"assertions_enabled", false};
  GeneratorParam<bool> use_fma_ = {"use_fma", false};
  GeneratorParam<bool> vectorize_ = {"vectorize", true};
  GeneratorParam<bool> parallel_ = {"parallel", true};
  GeneratorParam<int>  block_size_ = {"block_size", 1 << 5};
  GeneratorParam<bool> transpose_ = {"transpose", false};

  Param<T>   a_ = {"a", 1.0};
  ImageParam A_ = {type_of<T>(), 2, "A"};
  ImageParam x_ = {type_of<T>(), 1, "x"};
  Param<T>   b_ = {"b", 1.0};
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
    const Expr num_rows = A_.width();
    const Expr num_cols = A_.height();

    Var i("i"), j("j");
    Func result("result");

    Func A;
    A(i, j) = select(j < num_cols, A_(i, clamp(j, 0, num_cols)), cast<T>(0));

    if (transpose_) {
      const Expr proxy_size = (num_cols + vec_size - 1) / vec_size;

      RDom k(0, proxy_size, "k");
      Func Ax("Ax");
      Ax(j, i) += A_(k*vec_size + j, i) * x_(k*vec_size + j);

      Func At("At");
      At(i, j) = Ax(j, i);

      RDom sum_lanes(0, vec_size);
      Func prod("prod");
      prod(i)  += At(i, sum_lanes);
      result(i) = b_ * y_(i) + a_ * prod(i);

      if (vectorize_) {
        result.specialize(num_rows >= vec_size).vectorize(i, vec_size);

        prod.compute_at(result, i);
        prod.vectorize(i, vec_size).unroll(i);
        prod.update(0).specialize(num_rows >= vec_size)
            .reorder(i, sum_lanes).unroll(sum_lanes)
            .vectorize(i, vec_size).unroll(i);

        Ax.compute_at(result, i).vectorize(j).unroll(i);
        Ax.update(0).specialize(num_rows >= vec_size)
            .reorder(i, j, k)
            .vectorize(j).unroll(i);;

        At.compute_at(result, i).vectorize(i, vec_size).unroll(j);
      }

      A_.set_min(0, 0).set_min(1, 0);
      x_.set_bounds(0, 0, A_.width());
      y_.set_bounds(0, 0, A_.height());
      result.output_buffer().set_bounds(0, 0, A_.height());
    } else {
      const int block_size = 4;
      const Expr proxy_size = ((num_cols + block_size - 1) / block_size) * block_size;

      RDom k(0, proxy_size, "k");
      result(i)  = b_ * y_(i);
      result(i) += a_ * A_(i, k) * x_(k);

      if (vectorize_) {

        RVar ki("ki");
        Var ii("ii");
        result.update(0).specialize(num_rows >= vec_size && num_cols >= block_size)
            .split(i, i, ii, vec_size)
            .split(k, k, ki, block_size)
            .reorder(ii, ki, i, k)
            .vectorize(ii).unroll(ki);

        result.specialize(num_rows >= vec_size).vectorize(i, vec_size);
        result.update(0).specialize(num_rows >= vec_size).vectorize(i, vec_size);
      }


      A_.set_min(0, 0).set_min(1, 0);
      x_.set_bounds(0, 0, A_.height());
      y_.set_bounds(0, 0, A_.width());
      result.output_buffer().set_bounds(0, 0, A_.width());
    }

    return result;
  }
};

RegisterGenerator<GEMVGenerator<float> >    register_sgemv("sgemv");
RegisterGenerator<GEMVGenerator<double> >   register_dgemv("dgemv");

}  // namespace
