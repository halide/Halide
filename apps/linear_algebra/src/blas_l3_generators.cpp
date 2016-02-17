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
                       //.with_feature(Target::NoAsserts)
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

        transpose_tmp.compute_root()
            .specialize(rows >= 4 && cols >= 4).vectorize(j).unroll(i);

        return im_t;
    }

    Func build() {
        SetupTarget();

        // Thoughts on writing a fast matrix multiply:
        //
        // 1) There are three loops - rows of the output (i), columns of
        // the output (j), and the direction you're summing over (k). You
        // should do a 3D (possibly nested) tiling of these three loops.
        //
        // 2) It's worth copying pieces of A and B into temporary
        // buffers at some loop level. You use them lots of times. The
        // order of the temporary buffers should be setup so that the
        // innermost loop walks them in increasing order, so that the
        // prefetchers can do their thing, and so that you don't need
        // addressing math in the inner loop. This may require tiled
        // storage. For a given tile of i, j, k, you need a i*k piece
        // of A, and a k*j piece of B. So you should size your loops
        // such that these fit into some cache level.
        //
        // 3) The number of multiply-adds is fixed. You want to do the
        // minimum amount of memory traffic per math op. The best way
        // to do this is to make the inner loop do long dot products
        // between rows of A and columns of B, so that there are loads
        // but no stores. You don't do single dot products though, you
        // do groups of them that share inputs together - i.e. some
        // small tile of the output space. There are 16 vector
        // registers. Use 12 of them as accumulators and 4 as
        // temporaries. You want to compute a squarish block of dot
        // products in the output to maximize the number of times you
        // can reuse each loaded value. If the vector width is v, good
        // options are (3xv)x4, (2xv)x6, etc.
        //
        // 4) You want tiles of i, j, k to be small so that the staged
        // portions of A and B can fit into cache, but you want i and
        // j to be large - you reuse each value of A j times, and you
        // reuse each value of B i times. You also want k to be large,
        // because that's the length of your inner loop. The answer is
        // to just try lots of tilings.
        
        Var i("i"), j("j"), k("k");
        Var ii("ii"), ji("ji");
        Var io("io"), jo("jo"), iio("iio"), iii("iii");
        Var t("t");
        Func result("result");

        const Expr num_rows = A_.width();
        const Expr num_cols = B_.height();
        const Expr sum_size = A_.height();
        
        // The vector width
        int v = natural_vector_size(a_.type());

        // The size of the tiles of output we accumulate at a
        // time. Always 4 high and either 3 vectors wide for large
        // matrices (using 12 accumulators), or 2 vectors wide for
        // smaller ones (using 8 accumulators).
        Expr larger_than_64x64 = num_cols > 64;

        Expr c2 = select(larger_than_64x64, 3 * v, 2 * v);
        int c3 = 4;
        
        // some number less than 512 that divides up the rows nicely. Make it a multiple of c3.
        Expr c0 = c3 * cast<int>(ceil((1.0f / c3) * num_rows / ceil(num_rows / 512.0f)));
        // Once the sum over k exceeds this size it's worth breaking
        // and moving to the next row/col instead to keep A and B in
        // cache.
        Expr c1 = 192; 

        Expr has_k_tail = sum_size == (sum_size / c1) * c1;
        

        // Stage A and B, transposing if necessary.        
        
        Func A("A"), A_swizzled("As"), B("B");
        
        // A is going to be staged in swizzled order, so that the
        // inner loop walks through it in order. While we can reorder
        // storage dimensions in Halide, we can't split them, so I'll
        // have to make it an explicit 3D Func for scheduling.
        if (transpose_A_) {                
            A_swizzled(ii, j, io) = BoundaryConditions::constant_exterior(A_, 0)(io*c3 + ii, j);
        } else {
            A_swizzled(ii, j, io) = BoundaryConditions::constant_exterior(A_, 0)(j, io*c3 + ii);
        }
        // Change indexing back into 2D to use it.
        A(j, i) = A_swizzled(i % c3, j, i / c3);
        
        // No such fanciness is required for B
        if (transpose_B_) {
            B(j, i) = BoundaryConditions::constant_exterior(B_, 0)(i, j);
        } else {
            B(j, i) = BoundaryConditions::constant_exterior(B_, 0)(j, i);
        }
        
        // We're going to factor the summation into two levels. It
        // won't evenly divide the matrix size, so we'll need a tail
        // case version too.
        
        RDom ki(0, c1);        
        Func AB("AB");
        AB(j, i, k) += A(k*c1 + ki, i) * B(j, k*c1 + ki);
                                           
        Expr tail_size = (((sum_size % c1) + 3) / 4) * 4; 
        RDom ktail(sum_size - tail_size, tail_size);
        Func AB_tail("AB_tail");
        AB_tail(j, i) += A(ktail, i) * B(j, ktail);    
        
        // Sum across ko, and do the part that makes it a 'general' matrix multiply.
        RDom ko(0, sum_size / c1);
        result(j, i) = b_ * C_(j, i);
        result(j, i) += a_ * AB(j, i, ko);
        result(j, i) += select(has_k_tail, a_ * AB_tail(j, i), 0);

        // Copy from the computed result (which may have been padded for tiling)
        // into the actual output buffer.                
        Func output("output");
        output(j, i) = result(j, i);
        
        result.compute_at(output, Var::outermost())
            .vectorize(j, v).specialize(larger_than_64x64).parallel(i, 8);

        result.update(0)
            .reorder(j, i, ko)
            .tile(j, i, jo, io, ji, ii, c2, c0)
            .vectorize(ji, v).unroll(ji).unroll(ii, c3).specialize(larger_than_64x64).parallel(jo);

        result.update(1)
            .specialize(has_k_tail)
            .tile(j, i, jo, io, ji, ii, c2, c0)
            .vectorize(ji, v).unroll(ji).unroll(ii, c3).specialize(larger_than_64x64).parallel(jo);

        // If there's no tail, just write some schedule that makes the
        // schedule for A, AB_tail, etc valid. It'll get dead-code
        // eliminated.
        result.update(1)
            .tile(j, i, jo, io, ji, ii, 1, 1);
        
        // AB is one output tile. It'll be stored in registers to accumulate it.
        AB.compute_at(result, ii).vectorize(j, v).unroll(j).unroll(i)
            .update().reorder(j, i, ki).vectorize(j, v).unroll(j).unroll(i).unroll(ki, 8);
        
        AB_tail.compute_at(result, ii).vectorize(j, v).unroll(j).unroll(i)
            .update().reorder(j, i, ktail).vectorize(j, v).unroll(j).unroll(i).unroll(ktail, 4);       
        
        // Compute A swizzled at the loop over large strips of the output.
        A_swizzled.compute_at(result, io).vectorize(j, v).unroll(ii);

        // Stage B per at the loop over columns of output tiles
        B.compute_at(result, jo).vectorize(j, v).unroll(j); 

        output.compute_root().vectorize(j, 8).specialize(larger_than_64x64).parallel(i, 8);
        
        // We expect indices to start at zero, and the sizes should
        // all make sense for a matrix multiply.
        A_.set_min(0, 0).set_min(1, 0);
        B_.set_bounds(0, 0, sum_size).set_min(1, 0);
        C_.set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);
        output.output_buffer().set_bounds(0, 0, num_rows).set_bounds(1, 0, num_cols);

        if (Expr(a_).type().bits() == 32) {
            Target t;
            t.from_string("host-no_runtime-no_asserts-no_bounds_query");
            output.compile_to_assembly("/dev/stdout", {a_, b_, A_, B_, C_}, t);
        }

        return output;
    }
};

RegisterGenerator<GEMMGenerator<float>>    register_sgemm("sgemm");
RegisterGenerator<GEMMGenerator<double>>   register_dgemm("dgemm");

}  // namespace
