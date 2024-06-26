#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

class CountForLoops : public IRMutator {
    using IRMutator::visit;

    Stmt visit(const For *op) override {
        count++;
        return IRMutator::visit(op);
    }

public:
    int count = 0;
};

int main(int argc, char **argv) {
    // See https://github.com/halide/Halide/issues/5353

    const int width = 1280, height = 1024;
    Buffer<uint8_t> input(width, height);
    input.fill(0);

    Var x, y;

    Func clamped;
    clamped = BoundaryConditions::repeat_edge(input);

    Func max_x;
    max_x(x, y) = max(clamped(x - 1, y), clamped(x, y), clamped(x + 1, y));

    Func max_y;
    max_y(x, y) = max(max_x(x, y - 1), max_x(x, y), max_x(x, y + 1));

    CountForLoops counter;
    max_y.add_custom_lowering_pass(&counter, nullptr);

    Buffer<uint8_t> out = max_y.realize({width, height});

    // We expect a loop structure like:
    // Top of the image
    // for y:
    //  for x:
    // Middle of the image
    // for y:
    //  Left edge
    //  for x:
    //  Center
    //  for x:
    //  Right edge
    //  for x:
    // Bottom of the image
    // for y:
    //  for x:

    const int expected_loops = 8;

    if (counter.count != expected_loops) {
        printf("Loop was not partitioned into the expected number of cases\n");
        return 1;
    }

    printf("Success!\n");
    return 0;
}
