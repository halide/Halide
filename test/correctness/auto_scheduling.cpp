#include <stdio.h>
#include "Halide.h"

using namespace Halide;
using Halide::Internal::Dim;
using Halide::Internal::ForType;
using Halide::Internal::Schedule;

namespace {

Dim get_outer_dim(Func f) {
    Schedule &s = f.function().schedule();
    return s.dims()[s.dims().size() - 1];
}

Dim get_inner_dim(Func f) {
    return f.function().schedule().dims()[0];
}

} // end anonymous namespace

int main(int argc, char **argv) {
    const int w = 10, h = 10;
    Var x("x"), y("y");
    ImageParam im(Int(32), 2);
    Image<int> im_values = lambda(x, y, x+y).realize(w, h);
    im.set(im_values);
    Expr clamped_x = clamp(x, 0, w-1), clamped_y = clamp(y, 0, h-1);
    Func input("input");
    input(x, y) = im(clamped_x, clamped_y);
    
    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x, y);
        g.auto_schedule(ComputeRootAllStencils);
        // No stencils, so the schedule should be all inlined.
        assert(f.function().schedule().compute_level().is_inline());
        Image<float> result = g.realize(w, h);
        assert(result(5, 5) == (5 + 5) * 0.5f);
    }
    
    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(ComputeRootAllStencils);
        assert(f.function().schedule().compute_level().is_root());
        Image<float> result = g.realize(w, h);
        assert(result(5, 5) == ((4 + 5) * 0.5f + (5 + 5) * 0.5f + (6 + 5) * 0.5f));
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        f.auto_schedule(ComputeRootAllStencils);
        // f is the root of the auto schedule pipeline, so its schedule isn't modified.
        assert(f.function().schedule().compute_level().is_inline());
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(ComputeRootAllStencils)
            .auto_schedule(ParallelizeOuter);
        assert(f.function().schedule().compute_level().is_root());
        assert(get_outer_dim(f).for_type == ForType::Parallel);
        assert(get_outer_dim(g).for_type == ForType::Parallel);
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(ParallelizeOuter);
        assert(f.function().schedule().compute_level().is_inline());
        assert(get_outer_dim(f).for_type == ForType::Serial);
        assert(get_outer_dim(g).for_type == ForType::Parallel);
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(VectorizeInner);
        assert(f.function().schedule().compute_level().is_inline());
        assert(get_inner_dim(f).for_type == ForType::Serial);
        assert(get_inner_dim(g).for_type == ForType::Vectorized);
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(ComputeRootAllStencils)
            .auto_schedule(VectorizeInner);
        assert(f.function().schedule().compute_level().is_root());
        assert(get_inner_dim(f).for_type == ForType::Vectorized);
        assert(get_inner_dim(g).for_type == ForType::Vectorized);
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(ComputeRootAllStencils)
            .auto_schedule(ParallelizeOuter)
            .auto_schedule(VectorizeInner);
        assert(f.function().schedule().compute_level().is_root());
        assert(get_inner_dim(f).for_type == ForType::Vectorized);
        assert(get_inner_dim(g).for_type == ForType::Vectorized);
        assert(get_outer_dim(f).for_type == ForType::Parallel);
        assert(get_outer_dim(g).for_type == ForType::Parallel);
    }

    {
        Func f("f"), g("g");
        f(x, y) = input(x, y) * 0.5f;
        g(x, y) = f(x-1, y) + f(x, y) + f(x+1, y);
        g.auto_schedule(VectorizeInner)
            .auto_schedule(ParallelizeOuter)
            .auto_schedule(ComputeRootAllStencils);
        // Note order does matter: f is root, but not parallel or vectorized.
        assert(f.function().schedule().compute_level().is_root());
        assert(get_inner_dim(f).for_type == ForType::Serial);
        assert(get_inner_dim(g).for_type == ForType::Vectorized);
        assert(get_outer_dim(f).for_type == ForType::Serial);
        assert(get_outer_dim(g).for_type == ForType::Parallel);
    }
    
    printf("Success!\n");
    return 0;
}
