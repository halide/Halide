#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (target.arch == Target::WebAssembly) {
        printf("[SKIP] Performance tests are meaningless and/or misleading under WebAssembly interpreter.\n");
        return 0;
    }

    double times[3] = {0.f, 0.f, 0.f};

    for (int sz = 1; sz < 32; sz = sz * 2 + 1) {

        for (int c = 0; c < 3; c++) {
            MemoryType mem_type;
            bool use_bound;
            // Check three cases:

            if (c == 0) {
                // Allocation on the stack where size is known.
                mem_type = MemoryType::Stack;
                use_bound = true;
            } else if (c == 1) {
                // Allocation on the stack where the size is dynamic
                mem_type = MemoryType::Stack;
                use_bound = false;
            } else {
                // Allocation on the heap where the size is dynamic
                mem_type = MemoryType::Heap;
                use_bound = false;
            }

            Var x, y;

            std::vector<Func> fs;
            Expr e = 0.0f;
            for (int j = 0; j < 10; j++) {
                Func f;
                f(x, y) = x * j + y;
                e += f(x, y);
                fs.push_back(f);
            }

            Func g;
            g(x, y) = e;

            Var yo, yi;
            // Place the y loop body in its own function with its own
            // stack frame by making a parallel loop of some size
            // which will be 1 in practice.
            Param<int> task_size;
            g.split(y, yo, yi, task_size).parallel(yi);
            for (auto f : fs) {
                f.compute_at(g, yi).store_in(mem_type);
                if (use_bound) {
                    f.bound_extent(x, sz);
                }
            }

            Buffer<float> out(sz, 1024);
            task_size.set(1);
            double t = 1e3 * Tools::benchmark(10, 1 + 100 / sz, [&]() {
                           g.realize(out);
                       });
            times[c] += t;
        }
    }

    printf("Constant-sized stack allocation: %f\n"
           "Use alloca: %f\n"
           "Use malloc: %f\n",
           times[0], times[1], times[2]);

    if (times[0] > times[2] || times[1] > times[2]) {
        printf("Stack allocations should be cheaper than heap allocations\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
