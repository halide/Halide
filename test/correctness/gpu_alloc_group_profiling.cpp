#include "Halide.h"

using namespace Halide;

int main(int argc, char *argv[]) {

    Target t = get_jit_target_from_environment();
    if (!t.has_gpu_feature()) {
        printf("[SKIP] GPU not enabled\n");
        return 0;
    }

    // There was a bug that causes the inject profiling logic to try to
    // lookup a Func from the environment, by the buffer name of an allocation group.
    // Of course there is no Func for that name.
    // This happens when the buffer originally was intended for GPUShared, but got somehow
    // lifted to Heap (which I ran into before, without doing it explicitly like this below).
    //  --mcourteaux

    Var x{"x"}, y{"y"};

    Func f1{"f1"}, f2{"f2"};
    f1(x, y) = cast<float>(x + y);
    f2(x, y) = f1(x, y) * 2;

    Func result{"result"};
    result(x, y) = f2(x, y);

    Var xo{"xo"}, yo{"yo"}, xi{"xi"}, yi{"yi"};
    result
        .compute_root()
        .gpu_tile(x, y, xo, yo, xi, yi, 16, 16)
        .reorder(xi, yi, xo, yo);

    f2.compute_at(result, xo)
        .gpu_threads(x, y)
        .store_in(MemoryType::Heap);

    f1.compute_at(result, xo)
        .gpu_threads(x, y)
        .store_in(MemoryType::Heap);

    result.print_loop_nest();

    t.set_feature(Target::Profile);  // Make sure profiling is enabled!
    result.compile_jit(t);
    result.realize({64, 64}, t);
    // result.compile_to_conceptual_stmt("gpu_alloc_group_profiling.stmt.html", {}, Halide::HTML, t);

    printf("Success!\n");
    return 0;
}
