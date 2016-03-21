#include <string>

#include "Halide.h"

namespace {

class MemoryProfiler : public Halide::Generator<MemoryProfiler> {
public:
    GeneratorParam<int> index{ "index", 0 };

    ImageParam input{ Float(32), 2, "input" };

    Param<int> wrap_x{ "wrap_x", 64 };
    Param<int> wrap_y{ "wrap_y", 64 };

    Func build() {
        Var x("x"), y("y");

        std::string f_name = "f_" + index.to_string();
        std::string g_name = "g_" + index.to_string();

        Func f(f_name), g(g_name);
        g(x, y) = x;
        f(x, y) = g(x%64, y%64);
        g.compute_root();

        return f;
    }
};

Halide::RegisterGenerator<MemoryProfiler> register_mem_profiler_gen{"memory_profiler"};

}  // namespace
