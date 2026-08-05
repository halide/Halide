// Exercises the user-facing error paths for MemoryType::GPUSharedAsync. Asking
// for that memory type is a promise that every store to the allocation is a
// copy the copy engine can make, so each scenario below breaks one of those
// requirements and should be told exactly which one.
//
// The test verifies that each scenario produces a Halide::CompileError (a user
// error) rather than crashing, hitting an internal assert, or quietly falling
// back to a load and a store.

#include "Halide.h"
#include <stdio.h>

#if HALIDE_WITH_EXCEPTIONS

using namespace Halide;

namespace {

// Compiling is enough to reach the error - no device is needed.
Target async_target() {
    return get_host_target()
        .with_feature(Target::CUDA)
        .with_feature(Target::CUDACapability80);
}

// Run `body` and assert it produces a Halide user error mentioning `substring`.
template<typename F>
bool expect_user_error(const char *name, const char *substring, F body) {
    try {
        body();
    } catch (const CompileError &e) {
        std::string msg = e.what();
        if (msg.find(substring) == std::string::npos) {
            printf("[%s] FAIL: error did not mention \"%s\":\n%s\n",
                   name, substring, msg.c_str());
            return false;
        }
        printf("[%s] OK\n", name);
        return true;
    } catch (...) {
        printf("[%s] FAIL: expected a CompileError but got a different exception\n", name);
        return false;
    }
    printf("[%s] FAIL: expected a user error but none was raised\n", name);
    return false;
}

Buffer<float> input_f32() {
    Buffer<float> in(256, 64);
    in.fill(0.f);
    return in;
}

// The copy engine moves bytes untouched, so the staged Func has to be a plain
// copy. Doing arithmetic in it means the value stored isn't a load at all.
void scenario_not_a_copy() {
    Buffer<float> in = input_f32();
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(x, y) * 2.f;
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);
    out.compile_jit(async_target());
}

// The source has to live outside the kernel. Staging something already
// computed into registers or shared memory isn't a global-to-shared copy.
void scenario_source_inside_kernel() {
    Buffer<float> in = input_f32();
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func producer("producer"), stage("stage"), out("out");
    producer(x, y) = in(x, y) + 1.f;
    stage(x, y) = producer(x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 64, 8);
    producer.compute_at(out, x).gpu_threads(y);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);
    out.compile_jit(async_target());
}

// The hardware copies 4, 8 or 16 bytes per thread. Three floats is 12.
void scenario_bad_vector_width() {
    Buffer<float> in = input_f32();
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 48, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 3);
    out.compile_jit(async_target());
}

// A single byte per thread is below the minimum too.
void scenario_too_narrow() {
    Buffer<uint8_t> in(256, 64);
    in.fill(0);
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(x, y);
    out.compile_jit(async_target());
}

// Each copy has to be contiguous in both the source and the destination, so a
// strided read of the input can't be done this way.
void scenario_strided_source() {
    Buffer<float> in(512, 64);
    in.fill(0.f);
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(2 * x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .gpu_threads(y)
        .vectorize(x, 4);
    out.compile_jit(async_target());
}

// A predicated tail means some lanes are masked off, which the copy engine
// can't express.
void scenario_predicated() {
    Buffer<float> in(256, 64);
    in.fill(0.f);
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 66, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .align_storage(x, 4)
        .gpu_threads(y)
        .vectorize(x, 4, TailStrategy::Predicate);
    out.compile_jit(async_target());
}

// Each copy has to be dense at both ends. Storing the staged Func in the
// opposite order to the one it is read in leaves the source dense but the
// destination strided.
void scenario_strided_destination() {
    Buffer<float> in = input_f32();
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .reorder_storage(y, x)
        .gpu_threads(y)
        .vectorize(x, 4);
    out.compile_jit(async_target());
}

// The destination address of each copy has to be aligned to its width. Padding
// the rows to a stride that isn't a multiple of the vector width breaks that
// for every row after the first.
void scenario_misaligned_destination() {
    Buffer<float> in = input_f32();
    Var x("x"), y("y"), xi("xi"), yi("yi");
    Func stage("stage"), out("out");
    stage(x, y) = in(x, y);
    out(x, y) = stage(x, y);
    out.gpu_tile(x, y, xi, yi, 64, 8);
    stage.compute_at(out, x)
        .store_in(MemoryType::GPUSharedAsync)
        .align_storage(x, 6)
        .gpu_threads(y)
        .vectorize(x, 4);
    out.compile_jit(async_target());
}

}  // namespace

int main(int argc, char **argv) {
    if (!Halide::exceptions_enabled()) {
        printf("[SKIP] Halide was compiled without exceptions.\n");
        return 0;
    }

    int failures = 0;

    failures += !expect_user_error("not_a_copy", "not a load", scenario_not_a_copy);
    failures += !expect_user_error("source_inside_kernel", "another allocation inside the", scenario_source_inside_kernel);
    failures += !expect_user_error("bad_vector_width", "4, 8 or 16 bytes", scenario_bad_vector_width);
    failures += !expect_user_error("too_narrow", "4, 8 or 16 bytes", scenario_too_narrow);
    failures += !expect_user_error("strided_source", "not read densely", scenario_strided_source);
    failures += !expect_user_error("predicated", "predicated", scenario_predicated);
    failures += !expect_user_error("strided_destination", "densely", scenario_strided_destination);
    failures += !expect_user_error("misaligned_destination", "aligned", scenario_misaligned_destination);

    if (failures != 0) {
        printf("%d scenario(s) did not produce the expected user error\n", failures);
        return 1;
    }
    printf("Success!\n");
    return 0;
}

#else  // HALIDE_WITH_EXCEPTIONS

int main(int argc, char **argv) {
    printf("[SKIP] Halide was compiled without exceptions.\n");
    return 0;
}

#endif  // HALIDE_WITH_EXCEPTIONS
