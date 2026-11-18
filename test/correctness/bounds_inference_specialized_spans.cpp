// Public Halide API reproducer for specialization-dependent reduction domains.
#include "Halide.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace Halide;

int main(int argc, char **argv) {
    int partitions = argc == 2 ? std::atoi(argv[1]) : 8;
    if (argc > 2 || partitions < 1 || partitions > 64) {
        std::cerr << "Usage: specialized_spans_repro [partitions: 1..64]\n";
        return 2;
    }
    Var x("x"), y("y"), c("c"), yo("yo"), yi("yi");
    Param<bool> geometry("geometry");
    Param<int> layout("layout");
    Func spans("spans"), output("output");
    spans(y, c) = Tuple((y + c) % 4, (y + c) % 4 + 8);
    output(x, y, c) = cast<uint8_t>(0);
    RDom r({{0, 1}, {0, 16}, {0, 2}, {0, select(geometry, 16, 8)}}, "span");
    r.where(r[1] <= spans(r[3], r[2])[1] - spans(r[3], r[2])[0]);
    output(spans(r[3], r[2])[0] + r[1], r[3], c) = cast<uint8_t>(42);
    Stage stage = output.update();
    stage.reorder(c, r[0], r[1], r[2], r[3])
        .split(r[3], yo, yi, 4, TailStrategy::GuardWithIf);
    auto specialize = [&](Stage s) {
        for (int i = 0; i < partitions; ++i) {
            s.specialize(layout == i);
        }
    };
    specialize(stage.specialize(geometry));
    specialize(stage);
    spans.compute_at(output, yo);

    auto start = std::chrono::steady_clock::now();
    output.compile_jit(get_host_target());
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    for (bool g : {false, true}) {
        for (int choice : {0, partitions / 2, partitions + 1}) {
            geometry.set(g);
            layout.set(choice);
            Buffer<uint8_t> image = output.realize({32, 24, 3});
            for (int channel = 0; channel < 3; ++channel) {
                for (int row = 0; row < 24; ++row) {
                    for (int col = 0; col < 32; ++col) {
                        bool covered = false;
                        if (row < (g ? 16 : 8)) {
                            for (int piece = 0; piece < 2; ++piece) {
                                int lo = (row + piece) % 4;
                                covered = covered || (col >= lo && col <= lo + 8);
                            }
                        }
                        if (image(col, row, channel) != (covered ? 42 : 0)) {
                            std::cerr << "Incorrect pixel at " << col << "," << row << "," << channel << "\n";
                            return 1;
                        }
                    }
                }
            }
        }
    }
    std::cout << "{\"partitions\":" << partitions << ",\"compile_seconds\":" << seconds
              << ",\"checked_pixels\":13824}\n";
    std::cout << "Success!\n";
}
