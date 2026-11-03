#include "Halide.h"

// Check that vector loads and stores of GPU shared memory stay dense once the
// allocations sharing a block's shared memory are flattened into one. This
// cross-compiles, so it doesn't need a GPU.

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

std::string gpu_source(const Target &t) {
    ImageParam in(Float(32), 2, "in");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
    Func f("f"), g("g"), out("out");
    f(x, y) = in(x, y) * 2.f;
    g(x, y) = in(x, y) + 1.f;
    out(x, y) = f(x, y) + f(x + 4, y) + g(x, y) + g(x + 4, y);

    // f and g share the block's shared memory, at different offsets.
    out.gpu_tile(x, y, xo, yo, xi, yi, 16, 4).vectorize(xi, 4);
    for (Func s : {f, g}) {
        s.compute_at(out, xo)
            .store_in(MemoryType::GPUShared)
            .split(x, xo, xi, 4)
            .vectorize(xi)
            .gpu_threads(xo, y);
    }

    Module m = out.compile_to_module(out.infer_arguments(), out.name(), t);
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

bool expect(const std::string &src, const std::string &pattern, int expected) {
    int n = count(src, pattern);
    if (n != expected) {
        std::cout << "Expected " << expected << " occurrences of " << pattern
                  << " but found " << n << " in:\n"
                  << src << "\n";
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    // One store to each of f and g, and two loads of each.
    std::string metal = gpu_source(Target("macos-arm-64-metal"));
    if (!expect(metal, "threadgroup packed_float4 *", 6)) {
        return 1;
    }

    std::string ptx = gpu_source(Target("x86-64-linux-cuda-cuda_capability_70"));
    if (!expect(ptx, "st.shared.v4", 2) ||
        !expect(ptx, "ld.shared.v4", 4)) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
