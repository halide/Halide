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
  GeneratorParam<int>  block_size_ = {"block_size", 1 << 14};
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

    Var i("i"), j("j");
    Func result("result");
    if (transpose_) {
      const Expr proxy_size = (A_.width() + vec_size - 1) / vec_size;
      RDom k(0, proxy_size, "k");
      Func Ax("Ax");
      Ax(j, i) += a_ * A_(k*vec_size + j, i) * x_(k*vec_size + j);

      RDom sum_lanes(0, vec_size);
      result(i)  = b_ * y_(i);
      result(i) += Ax(sum_lanes, i);

      if (vectorize_) {
        Var ii("ii");
        result.vectorize(i, vec_size);
        result.update(0).vectorize(i, vec_size).unroll(sum_lanes);

        Ax.compute_at(result, i).vectorize(j).unroll(i);
        Ax.update(0)
            .reorder(i, j, k)
            .unroll(i)
            .vectorize(j);
      }

      A_.set_min(0, 0).set_min(1, 0);
      x_.set_bounds(0, 0, A_.width());
      y_.set_bounds(0, 0, A_.height());
      result.output_buffer().set_bounds(0, 0, A_.height());
    } else {
      const Expr proxy_size = ((A_.height() + 3) / 4) * 4;
      RDom k(0, proxy_size);
      result(i)  = b_ * y_(i);
      result(i) += select(k < A_.height(), a_ * A_(i, k) * x_(k), cast<T>(0));

      if (parallel_) {
        Var ii("ii");
        const Expr M = y_.width();
        const Expr N = x_.width();
        const Expr work_size = max(block_size_ / N, 1);
        if (vectorize_) {
          result.vectorize(i, vec_size);
          result.update(1).specialize(M / work_size >= 4)
              .split(i, ii, i, work_size)
              .reorder(i, k , ii)
              .parallel(ii)
              .vectorize(i, vec_size);
        } else {
          result.update(1).specialize(M / work_size >= 4)
              .split(i, ii, i, work_size)
              .reorder(i, k , ii)
              .parallel(ii);
        }
      } else {
        if (vectorize_) {
          Var ii("ii");
          RVar ki("ki");
          result.vectorize(i, vec_size);
          result.update(0)
              .split(i, i, ii, vec_size)
              .split(k, k, ki, 4)
              .reorder(ii, ki, i, k)
              .unroll(ki).vectorize(ii);
        }
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
