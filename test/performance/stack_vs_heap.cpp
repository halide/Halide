#include "Halide.h"
#include "halide_benchmark.h"

using namespace Halide;

int main(int argc, char **argv) {

    for (int i = 0; i < 10; i++) {
        for (auto mem_type : {MemoryType::Stack, MemoryType::Heap}) {
            Func f;
            Var x, y;
            f(x, y) = x / 18.3f + y;

            Func g;
            g(x, y) = f(x, y) + f(x, y + 1);

            g.parallel(y);
            f.compute_at(g, y).store_in(mem_type);

            Buffer<float> out(32, 1024);
            double t = 1e3 * Tools::benchmark(10, 100, [&]() {
                           g.realize(out);
                       });

            std::cout << mem_type << ": " << t << "\n";
        }
    }

    return 0;
}
