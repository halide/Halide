#include <Halide.h>
#include <stdio.h>

using namespace Halide;

Var x, y;

Func blur(Func in) {
    Func blurry;
    blurry(x, y) = (in(x-1, y-1) + in(x+1, y+1) + in(x+1, y-1) + in(x-1, y+1)) / 4;
    return blurry;
}

int main(int argc, char **argv) {
    Image<float> input = lambda(x, y, sin(x + y)).realize(100, 100);

    std::vector<Func> stages;
    stages.push_back(lambda(x, y, input(x, y)));
    for (int i = 0; i < 50; i++) {
        stages.push_back(blur(stages.back()));
    }

    for (size_t i = 0; i < stages.size()-1; i++) {
        stages[i].store_root().compute_at(stages.back(), y).vectorize(x, 4);
    }

    // We just want to ensure this compiles before the heat death of the universe
    stages.back().compile_jit();

    return 0;
}
