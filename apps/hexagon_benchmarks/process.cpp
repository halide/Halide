#include <assert.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef TARGET_HAS_HVX
#include "HalideRuntimeHexagonHost.h"
#endif

#include "halide_benchmark.h"
#include "process.h"

void usage(char *prg_name) {
    const char usage_string[] = " Run a bunch of small filters\n\n"
                                "\t -n -> number of iterations\n"
                                "\t -h -> print this help message\n";
    printf("%s - %s", prg_name, usage_string);
}

int main(int argc, char **argv) {
    // Set some defaults first.
    const int W = 1024;
    const int H = 1024;
    int iterations = 10;

    // Process command line args.
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'h':
                usage(argv[0]);
                return 0;
                break;
            case 'n':
                iterations = atoi(argv[i + 1]);
                i++;
                break;
            }
        }
    }

    Conv3x3a16Descriptor conv3x3a16_pipeline(W, H);
    Dilate3x3Descriptor dilate3x3_pipeine(W, H);
    Median3x3Descriptor median3x3_pipeline(W, H);
    Gaussian5x5Descriptor gaussian5x5_pipeline(W, H);
    Conv3x3a32Descriptor conv3x3a32_pipeline(W, H);

    std::vector<PipelineDescriptorBase *> pipelines = {&conv3x3a16_pipeline, &dilate3x3_pipeine, &median3x3_pipeline,
                                                       &gaussian5x5_pipeline, &conv3x3a32_pipeline};

    for (PipelineDescriptorBase *p : pipelines) {
        if (!p->defined()) {
            continue;
        }
        p->init();
        printf("Running %s...\n", p->name());

#ifdef HALIDE_RUNTIME_HEXAGON
        // To avoid the cost of powering HVX on in each call of the
        // pipeline, power it on once now. Also, set Hexagon performance to turbo.
        halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_turbo);
        halide_hexagon_power_hvx_on(NULL);
#endif

        double time = Halide::Tools::benchmark(iterations, 10, [&]() {
            int result = p->run();
            if (result != 0) {
                printf("pipeline failed! %d\n", result);
            }
        });
        printf("Done, time (%s): %g s\n", p->name(), time);

#ifdef HALIDE_RUNTIME_HEXAGON
        // We're done with HVX, power it off, and reset the performance mode
        // to default to save power.
        halide_hexagon_power_hvx_off(NULL);
        halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_default);
#endif

        if (!p->verify(W, H)) {
            abort();
        }
        p->finalize();
    }

    printf("Success!\n");
    return 0;
}
