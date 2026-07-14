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

    struct Variant {
        Func g;
        Param<int> task_size;
        Buffer<float> out;
    };

    for (int sz = 1; sz < 32; sz = sz * 2 + 1) {

        // Check three cases: allocation on the stack where the size is
        // known, allocation on the stack where the size is dynamic, and
        // allocation on the heap where the size is dynamic.
        auto build = [&](MemoryType mem_type, bool use_bound) {
            Var x, y;

            std::vector<Func> fs;
            Expr e = 0.0f;
            for (int j = 0; j < 10; j++) {
                Func f;
                f(x, y) = x * j + y;
                e += f(x, y);
                fs.push_back(f);
            }

            Variant v;
            v.g(x, y) = e;

            Var yo, yi;
            // Place the y loop body in its own function with its own
            // stack frame by making a parallel loop of some size
            // which will be 1 in practice.
            v.g.split(y, yo, yi, v.task_size).parallel(yi);
            for (auto f : fs) {
                f.compute_at(v.g, yi).store_in(mem_type);
                if (use_bound) {
                    f.bound_extent(x, sz);
                }
            }

            v.out = Buffer<float>(sz, 1024);
            v.task_size.set(1);
            return v;
        };

        Variant stack_bound = build(MemoryType::Stack, true);
        Variant stack_dynamic = build(MemoryType::Stack, false);
        Variant heap_dynamic = build(MemoryType::Heap, false);

        auto [r_stack_bound, r_stack_dynamic, r_heap_dynamic] = Tools::benchmark_comparison(
            Tools::BenchmarkConfig{},
            [&]() { stack_bound.g.realize(stack_bound.out); },
            [&]() { stack_dynamic.g.realize(stack_dynamic.out); },
            [&]() { heap_dynamic.g.realize(heap_dynamic.out); });

        times[0] += r_stack_bound.wall_time * 1e3;
        times[1] += r_stack_dynamic.wall_time * 1e3;
        times[2] += r_heap_dynamic.wall_time * 1e3;
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
