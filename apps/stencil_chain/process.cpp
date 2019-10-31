#include <cstdio>
#include <chrono>

#include "stencil_chain.h"
#include "stencil_chain_auto_schedule.h"
#include "stencil_chain_simple_auto_schedule.h"

#include "benchmark_util.h"
#include "HalideBuffer.h"
#include "halide_image_io.h"

using namespace Halide::Runtime;
using namespace Halide::Tools;

int main(int argc, char **argv) {
    if (argc < 4) {
        printf("Usage: ./process input.png timing_iterations output.png\n"
               "e.g.: ./process input.png 10 output.png\n");
        return 0;
    }

    // Input may be a PNG8
    Buffer<uint16_t> input = load_and_convert_image(argv[1]);
    // Just take the red channel
    input.slice(2, 0);

    Buffer<uint16_t> output(input.width(), input.height());

    multi_way_bench({
        {"Manual", [&]() { stencil_chain(input, output); output.device_sync(); }},
        {"Auto-scheduled", [&]() { stencil_chain_auto_schedule(input, output); output.device_sync(); }},
        {"Simple auto-scheduled", [&]() { stencil_chain_simple_auto_schedule(input, output); output.device_sync();}}
        }
    );

    stencil_chain(input, output);
    convert_and_save_image(output, argv[3]);

    return 0;
}
