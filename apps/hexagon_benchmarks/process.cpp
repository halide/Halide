#include <stdio.h>
#include <memory.h>
#include <assert.h>
#include <stdlib.h>

#include "halide_benchmark.h"
#include "process.h"

void usage(char *prg_name) {
    const char usage_string[] = " Run a bunch of small filters\n\n"
                                "\t -m -> hvx_mode - options are hvx64, hvx128. Default is to run hvx64, hvx128 and cpu\n"
                                "\t -n -> number of iterations\n"
                                "\t -h -> print this help message\n";
    printf ("%s - %s", prg_name, usage_string);

}

const char *to_string(bmark_run_mode_t mode) {
    if (mode == bmark_run_mode_t::hvx64) {
        return "(64 byte mode)";
    } else if (mode == bmark_run_mode_t::hvx128) {
        return "(128 byte mode)";
    } else {
        return "(cpu)";
    }
}

int main(int argc, char **argv) {
    // Set some defaults first.
    const int W = 1024;
    const int H = 1024;
    std::vector<bmark_run_mode_t> modes;
    int iterations = 10;

    // Process command line args.
    for (int i = 1; i < argc; ++i) {
        if (argv[i][0] == '-') {
            switch (argv[i][1]) {
            case 'm':
                {
                    std::string mode_to_run = argv[i+1];
                    if (mode_to_run == "hvx64") {
                        modes.push_back(bmark_run_mode_t::hvx64);
                    } else if (mode_to_run == "hvx128") {
                        modes.push_back(bmark_run_mode_t::hvx128);
                    } else if (mode_to_run == "cpu") {
                        modes.push_back(bmark_run_mode_t::cpu);
                    } else {
                        usage(argv[0]);
                        abort();
                    }
                    i++;
                }
                break;
            case 'h':
                usage(argv[0]);
                return 0;
                break;
            case 'n':
                iterations = atoi(argv[i+1]);
                i++;
                break;
            }
        }
    }
    if (modes.empty()) {
        modes.push_back(bmark_run_mode_t::hvx64);
        modes.push_back(bmark_run_mode_t::hvx128);
        modes.push_back(bmark_run_mode_t::cpu);
    }
    Conv3x3a16Descriptor conv3x3a16_pipeline(W, H);
    Dilate3x3Descriptor dilate3x3_pipeine(W, H);
    Median3x3Descriptor median3x3_pipeline(W, H);
    Gaussian5x5Descriptor gaussian5x5_pipeline(W, H);
    SobelDescriptor sobel_pipeline(W, H);
    Conv3x3a32Descriptor conv3x3a32_pipeline(W, H);


    std::vector<PipelineDescriptorBase *> pipelines = {&conv3x3a16_pipeline, &dilate3x3_pipeine, &median3x3_pipeline,
                                                       &gaussian5x5_pipeline, &sobel_pipeline, &conv3x3a32_pipeline};

    for (bmark_run_mode_t m : modes) {
        for (PipelineDescriptorBase *p : pipelines) {
            if (!p->defined()) {
                continue;
            }
            p->init();
            printf ("Running %s...\n", p->name());

            // To avoid the cost of powering HVX on in each call of the
            // pipeline, power it on once now. Also, set Hexagon performance to turbo.
            halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_turbo);
            halide_hexagon_power_hvx_on(NULL);

            double time = Halide::Tools::benchmark(iterations, 10, [&]() {
                    int result = p->run(m);
                    if (result != 0) {
                        printf("pipeline failed! %d\n", result);
                    }
                });
            printf("Done, time (%s): %g s %s\n", p->name(), time, to_string(m));

            // We're done with HVX, power it off, and reset the performance mode
            // to default to save power.
            halide_hexagon_power_hvx_off(NULL);
            halide_hexagon_set_performance_mode(NULL, halide_hexagon_power_default);

            if (!p->verify(W, H)) {
                abort();
            }
            p->finalize();
        }
    }

    printf("Success!\n");
    return 0;
}
