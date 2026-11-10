#include "Halide.h"

// Check that a dense vector load from a GPU kernel's input, at an index known
// to be a multiple of the vector's width, is loaded 8 or 16 bytes at a time
// rather than one element at a time, where the input is known to be aligned
// to 16 bytes; and that it isn't where the input may not be (it may be a
// crop of another buffer). This cross-compiles, so it doesn't need a GPU.

using namespace Halide;
using namespace Halide::Internal;

// Find the GPU kernel source embedded in a compiled module.
class FindGPUSource : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (op->image.defined() &&
            ends_with(op->image.name(), "_gpu_source_kernels")) {
            const char *data = (const char *)op->image.data();
            source += std::string(data, data + op->image.number_of_elements());
        }
    }

public:
    std::string source;
};

// Stage rows of the input in shared memory, each thread loading `lanes`
// elements at once, and return the PTX.
std::string gpu_source(Type t, int lanes, bool aligned) {
    ImageParam in(t, 2, "in");
    if (aligned) {
        in.set_host_alignment(16);
    }
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
    Func f("f"), out("out");
    f(x, y) = cast<float>(in(x, y));
    out(x, y) = f(x, y) + f(x + 16, y);

    // Rows are 64 elements apart and tiles start at multiples of 64, so each
    // thread's load starts at a multiple of its lanes.
    in.dim(0).set_min(0);
    in.dim(1).set_min(0).set_stride(64);
    out.output_buffer().dim(0).set_min(0);
    out.gpu_tile(x, y, xo, yo, xi, yi, 64, 4, TailStrategy::RoundUp);
    f.compute_at(out, xo)
        .store_in(MemoryType::GPUShared)
        .split(x, xo, xi, lanes)
        .vectorize(xi)
        .gpu_threads(xo, y);

    Module m = out.compile_to_module(out.infer_arguments(), out.name(),
                                     Target("x86-64-linux-cuda-cuda_capability_70"));
    FindGPUSource finder;
    for (const auto &lf : m.functions()) {
        lf.body.accept(&finder);
    }
    return finder.source;
}

int count(const std::string &src, const std::string &pattern) {
    int n = 0;
    for (size_t i = src.find(pattern); i != std::string::npos; i = src.find(pattern, i + 1)) {
        n++;
    }
    return n;
}

int main(int argc, char **argv) {
    struct Case {
        Type type;
        int lanes;
        // The global loads of one thread's vector.
        int loads;
    };
    for (const Case &c : {Case{Float(16), 4, 1}, Case{Float(16), 8, 1}, Case{Float(32), 4, 1}, Case{Float(32), 8, 2}, Case{UInt(8), 16, 1}}) {
        for (bool aligned : {true, false}) {
            std::string ptx = gpu_source(c.type, c.lanes, aligned);
            int n = count(ptx, "ld.global");
            // Otherwise the lanes are loaded one at a time.
            const int expected = aligned ? c.loads : c.lanes;
            if (n != expected) {
                std::cout << "Expected " << expected << " global loads of " << c.lanes << " x " << c.type
                          << (aligned ? " from an aligned input" : " from an input of unknown alignment")
                          << " but found " << n << " in:\n"
                          << ptx << "\n";
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
