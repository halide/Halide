#include "Halide.h"
#include <stdio.h>

using namespace Halide;

Var x, y;
Param<int> divisor;

Func blur(Func in, std::string n) {
    Func blurry(n);
    blurry(x) = (in(x) + in(x + 1)) / divisor;
    return blurry;
}

int main(int argc, char **argv) {
    Buffer<float> input = lambda(x, sin(10 * x) + 1.0f).realize({1000});

    std::vector<Func> stages;
    Func first("S0");
    first(x) = input(x);

    stages.push_back(first);
    for (size_t i = 0; i < 30; i++) {
        stages.push_back(blur(stages.back(), "S" + std::to_string(i + 1)));
    }

    for (size_t i = 0; i < stages.size() - 1; i++) {
        stages[i].store_root().compute_at(stages.back(), x);
    }

    // Add an unreasonable number of specialize() calls, to ensure
    // that various parts of the pipeline don't blow up
    for (int i = 1; i <= 10; i++) {
        stages.back().specialize(divisor == i);
    }

    divisor.set(2);
    Buffer<float> result = stages.back().realize({10});

    // After all the averaging, the result should be a flat 1.0f
    float err = evaluate_may_gpu<float>(sum(abs(result(RDom(result)) - 1.0f)));

    if (err > 0.01f) {
        printf("Error too large: %f!\n", err);
        return 1;
    }

    printf("Success!\n");
    return 0;
}
