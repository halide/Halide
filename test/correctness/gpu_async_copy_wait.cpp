#include "Halide.h"

#include <set>

// Check where a CUDA kernel commits and waits for the asynchronous copies of a
// Func staged in GPUSharedAsync memory: committed as soon as they are issued,
// and awaited at the barrier that publishes them to the block where the block
// stages another input through registers after them, so that those loads
// overlap the copies; where it copies another one, each where it ends. This cross-compiles, so it doesn't need a
// GPU; gpu_async_copy checks the values on one.

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

// Two inputs staged per tile, one copied by the copy engine and one loaded and
// converted by the threads, the copied one first or second (sibling Funcs are
// staged in the order of their names); returns the PTX.
std::string gpu_source(bool copied_first, bool both_copied = false) {
    ImageParam a(Float(32), 2, "a"), b(both_copied ? Float(32) : UInt(8), 2, "b");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
    Func copied(copied_first ? "a_copied" : "b_copied"), loaded(copied_first ? "b_loaded" : "a_loaded"), out("out");
    copied(x, y) = a(x, y);
    loaded(x, y) = both_copied ? b(x, y) : cast<float>(b(x, y)) * 2.0f;
    out(x, y) = copied(x, y) + loaded(x, y);

    for (ImageParam p : {a, b}) {
        p.dim(0).set_min(0);
        p.dim(1).set_min(0).set_stride(256);
    }
    out.output_buffer().dim(0).set_min(0);
    out.gpu_tile(x, y, xo, yo, xi, yi, 64, 8, TailStrategy::RoundUp);
    copied.compute_at(out, xo)
        .store_in(MemoryType::GPUSharedAsync)
        .vectorize(x, 4)
        .gpu_threads(y);
    loaded.compute_at(out, xo)
        .store_in(both_copied ? MemoryType::GPUSharedAsync : MemoryType::GPUShared)
        .vectorize(x, 4)
        .gpu_threads(y);

    Module m = out.compile_to_module(out.infer_arguments(), out.name(),
                                     Target("x86-64-linux-cuda-cuda_capability_80"));
    FindGPUSource finder;
    for (const auto &lf : m.functions()) {
        lf.body.accept(&finder);
    }
    return finder.source;
}

int main(int argc, char **argv) {
    // Whether each case staged the copied input first.
    std::set<bool> copies_first;
    for (bool copied_first : {true, false}) {
        std::string ptx = gpu_source(copied_first);
        const size_t copy = ptx.rfind("cp.async.ca.shared.global");
        const size_t commit = ptx.find("cp.async.commit_group");
        const size_t wait = ptx.find("cp.async.wait_group");
        const size_t load = ptx.rfind("ld.global");
        const size_t barrier = ptx.find("bar.sync", copy);
        if (copy == std::string::npos || commit == std::string::npos || wait == std::string::npos ||
            load == std::string::npos || barrier == std::string::npos) {
            std::cout << "Expected asynchronous copies, a wait for them, global loads and a barrier in:\n"
                      << ptx << "\n";
            return 1;
        }
        // The copies committed as soon as they are issued (before the other
        // input's loads, where it is staged second), and awaited once, after
        // those loads, at the barrier.
        const bool copied_before = ptx.find("cp.async.ca.shared.global") < ptx.find("ld.global");
        if (ptx.find("cp.async.wait_group", wait + 1) != std::string::npos ||
            commit < copy || (copied_before && commit > load) ||
            wait < commit || wait < load || wait > barrier) {
            std::cout << "Expected the copies to be committed as they are issued and awaited once, after the other input's loads, at the barrier ("
                      << (copied_first ? "copied input first" : "copied input second") << "):\n"
                      << ptx << "\n";
            return 1;
        }
        copies_first.insert(copied_before);
    }
    if (copies_first.size() != 2) {
        std::cout << "Expected the two cases to stage the inputs in both orders\n";
        return 1;
    }
    // Two copied inputs: the first awaited before the second's copies.
    std::string ptx = gpu_source(true, true);
    const size_t first_wait = ptx.find("cp.async.wait_group");
    const size_t last_copy = ptx.rfind("cp.async.ca.shared.global");
    if (first_wait == std::string::npos || first_wait > last_copy ||
        ptx.find("cp.async.wait_group", first_wait + 1) == std::string::npos) {
        std::cout << "Expected each of two copied inputs to be awaited where its copies end:\n"
                  << ptx << "\n";
        return 1;
    }
    printf("Success!\n");
    return 0;
}
