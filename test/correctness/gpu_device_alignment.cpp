#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>
#include <sstream>
#include <stdio.h>

using namespace Halide;

// f(x) = in(x + offset) * 2, with vectors of four floats per GPU thread.
Func make_pipeline(ImageParam in, int offset, DeviceAPI api) {
    Var x("x"), xo("xo"), xi("xi"), b("b"), t("t");
    Func f("f");
    f(x) = in(x + offset) * 2.0f;
    f.split(x, xo, xi, 4)
        .gpu_tile(xo, b, t, 16, TailStrategy::Auto, api)
        .vectorize(xi);
    return f;
}

// Only the device alignment says anything about the loads in GPU code.
bool check_ptx_uses_device_alignment() {
    Target t = get_host_target()
                   .with_feature(Target::CUDA)
                   .with_feature(Target::CUDACapability80);
    auto ptx_for = [&](bool host_aligned, bool device_aligned) {
        ImageParam in(Float(32), 1, "in");
        if (host_aligned) {
            in.set_host_alignment(16);
        }
        if (device_aligned) {
            in.set_device_alignment(16);
        }
        in.dim(0).set_min(0);
        Func f = make_pipeline(in, 0, DeviceAPI::CUDA);
        f.output_buffer().dim(0).set_min(0);
        std::string path = Internal::get_test_tmp_dir() + "gpu_device_alignment.s";
        f.compile_to_assembly(path, {in}, "f", t);
        std::ifstream file(path);
        std::stringstream ss;
        ss << file.rdbuf();
        return ss.str();
    };
    const char *vector_load = "ld.global.nc.v4";
    if (ptx_for(false, false).find(vector_load) != std::string::npos) {
        printf("FAIL: PTX assumed an aligned device pointer\n");
        return false;
    }
    if (ptx_for(true, false).find(vector_load) != std::string::npos) {
        printf("FAIL: host_alignment made PTX assume an aligned device pointer\n");
        return false;
    }
    if (ptx_for(false, true).find(vector_load) == std::string::npos) {
        printf("FAIL: device_alignment did not give PTX aligned vector loads\n");
        return false;
    }
    return true;
}

int count_checks(const Internal::Stmt &s) {
    int count = 0;
    Internal::visit_with(s, [&](auto *self, const Internal::Call *op) {
        if (op->name == "halide_device_check_alignment") {
            count++;
        }
        self->visit_base(op);
    });
    return count;
}

// A check is only emitted for buffers that asked for one.
bool check_checks_are_emitted() {
    Target t = get_host_target().with_feature(Target::CUDA);
    for (bool aligned : {false, true}) {
        ImageParam in(Float(32), 1, "in");
        if (aligned) {
            in.set_device_alignment(64);
        }
        Func f = make_pipeline(in, 0, DeviceAPI::CUDA);
        int count = 0;
        struct Counter : public Internal::IRMutator {
            int &count;
            Counter(int &c)
                : count(c) {
            }
            using IRMutator::mutate;
            Internal::Stmt mutate(const Internal::Stmt &s) override {
                count += count_checks(s);
                return s;
            }
        };
        f.add_custom_lowering_pass(new Counter(count));
        f.compile_to_module({in}, "f", t);
        if (count != (aligned ? 1 : 0)) {
            printf("FAIL: expected %d device alignment checks, got %d\n", aligned ? 1 : 0, count);
            return false;
        }
    }
    return true;
}

bool error_occurred = false;
std::string error_message;
void my_error_handler(JITUserContext *, const char *msg) {
    error_occurred = true;
    error_message = msg;
}

bool check_runtime() {
    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] Not checking at runtime: target has no CUDA feature.\n");
        return true;
    }

    ImageParam in(Float(32), 1, "in");
    in.set_device_alignment(64);
    Func f = make_pipeline(in, 1, DeviceAPI::CUDA);
    f.jit_handlers().custom_error = my_error_handler;

    Buffer<float> data(129);
    data.for_each_element([&](int x) { data(x) = (float)x; });

    // The pipeline gives a host-only buffer a device allocation of its own,
    // which CUDA aligns.
    in.set(data);
    error_occurred = false;
    Buffer<float> out = f.realize({128}, t);
    if (error_occurred) {
        printf("FAIL: unexpected error: %s\n", error_message.c_str());
        return false;
    }
    out.copy_to_host();
    for (int x = 0; x < 128; x++) {
        if (out(x) != (x + 1) * 2.0f) {
            printf("FAIL: out(%d) = %f\n", x, out(x));
            return false;
        }
    }

    // A crop of a device buffer one element in is misaligned.
    data.set_host_dirty(false);
    Buffer<float> cropped = data.cropped(0, 1, 128);
    in.set(cropped);
    error_occurred = false;
    f.realize({127}, t);
    if (!error_occurred || error_message.find("device pointer of in") == std::string::npos) {
        printf("FAIL: misaligned device pointer was not rejected\n");
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    if (!check_ptx_uses_device_alignment() ||
        !check_checks_are_emitted() ||
        !check_runtime()) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
