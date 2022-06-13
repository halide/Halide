#include "Halide.h"
#include "HalideRuntime.h"
#include <stdio.h>

using namespace Halide;

// An extern stage implemented by a Halide pipeline running
// either on host or device. The outer Halide filter must
// override the "device_api" parameter of Func::define_extern
// when using the extern_stage on device.
extern "C" HALIDE_EXPORT_SYMBOL int extern_stage(int extern_on_device,
                                                 int outer_filter_on_device,
                                                 halide_buffer_t *out) {
    if (!out->is_bounds_query()) {
        if (extern_on_device > 0 && outer_filter_on_device > 0) {
            // If both the extern and the outer filter are on running on
            // device, the host allocation shall be null and the device
            // allocation must exist before entering the extern stage.
            assert(out->host == nullptr);
            assert(out->device != 0);
        } else {
            // For other cases, the host allocation must exist.
            assert(out->host);
        }
        assert(out->type == halide_type_of<int32_t>());
        assert(out->dimensions == 2);
        printf("Generating data over [%d %d] x [%d %d]\n",
               out->dim[0].min, out->dim[0].min + out->dim[0].extent,
               out->dim[1].min, out->dim[1].min + out->dim[1].extent);
        Var x, y;
        Var xi, yi;
        Func f("f");
        f(x, y) = x + y;

        if (extern_on_device > 0) {
            f.gpu_tile(x, y, xi, yi, 16, 16);
        }
        f.realize(out);
    }
    return 0;
}

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();

    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }
    DeviceAPI device_api = get_default_device_api_for_target(target);

    Var x, y;
    Var xi, yi;

    for (int extern_on_device : {0, 1}) {
        for (int sink_on_device : {0, 1}) {
            Func source("source");
            std::vector<ExternFuncArgument> args;
            args.push_back(extern_on_device);
            args.push_back(sink_on_device);
            source.define_extern("extern_stage",
                                 args,
                                 Int(32),
                                 {x, y},
                                 NameMangling::Default,
                                 extern_on_device ? device_api : Halide::DeviceAPI::Host);

            Func sink("sink");
            sink(x, y) = source(x, y) - (x + y);

            source.compute_root();
            sink.compute_root();
            if (sink_on_device > 0) {
                sink.gpu_tile(x, y, xi, yi, 16, 16);
            }

            Buffer<int32_t> output = sink.realize({100, 100});

            // Should be all zeroes.
            RDom r(output);
            uint32_t error = evaluate_may_gpu<uint32_t>(sum(abs(output(r.x, r.y))));
            if (error != 0) {
                printf("Something went wrong when "
                       "extern_on_device=%d, sink_on_device=%d \n",
                       extern_on_device, sink_on_device);
                return -1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
