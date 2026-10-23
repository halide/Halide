#include "Halide.h"

// Check the Metal and PTX source generated for reductions across gpu
// lanes. This cross-compiles, so it doesn't need a GPU.
// gpu_lanes_reduction.cpp runs the same schedules.

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

const Target metal_target("macos-arm-64-metal");
const Target cuda_target("x86-64-linux-cuda-cuda_capability_70");

std::string gpu_source(Func f, const Target &t) {
    Module m = f.compile_to_module(f.infer_arguments(), f.name(), t);
    FindGPUSource finder;
    for (const auto &lf : m.functions()) {
        lf.body.accept(&finder);
    }
    return finder.source;
}

// A butterfly reduction of values held in registers across the lanes.
// Each stage reads the value from another lane, so the stages are
// register shuffles. The last stage is only needed for lane zero.
Func butterfly_reduction(const Buffer<float> &in, int lanes) {
    Var u("u"), m("m"), mo("mo"), mi("mi");
    RDom r(0, in.width() / lanes);
    Func p("p");
    p(u, m) = 0.f;
    p(u, m) += in(r * lanes + u, m);
    std::vector<Func> stages{p};
    for (int w = lanes / 2; w >= 1; w /= 2) {
        Func s("s" + std::to_string(w));
        s(u, m) = stages.back()(u, m) + stages.back()(u + w, m);
        stages.push_back(s);
    }
    Func out("out");
    out(m) = stages.back()(0, m);
    out.split(m, mo, mi, 4).gpu_blocks(mo).gpu_threads(mi);
    for (Func s : stages) {
        s.compute_at(out, mi).gpu_lanes(u);
    }
    p.update().gpu_lanes(u);
    return out;
}

enum class Op {
    Add,
    Mul,
    Min,
    Max,
};

// Reduce each row of a matrix across a group of gpu lanes. Each lane
// computes a partial reduction over a strided subset of the row, and
// then the partials are reduced across the lanes using an atomic
// update scheduled with gpu_lanes.
template<typename T>
Func lane_reduction(const Buffer<T> &in, Op op, int lanes, TailStrategy tail) {
    Var m("m"), u("u"), mo("mo"), mi("mi"), ml("ml");
    RDom r(0, in.width());

    Func f("f");
    Expr v = in(r, m);
    switch (op) {
    case Op::Add:
        f(m) = cast<T>(0);
        f(m) += v;
        break;
    case Op::Mul:
        f(m) = cast<T>(1);
        f(m) *= v;
        break;
    case Op::Min:
        f(m) = v.type().max();
        f(m) = min(f(m), v);
        break;
    case Op::Max:
        f(m) = v.type().min();
        f(m) = max(f(m), v);
        break;
    }

    Func out("out");
    out(m) = f(m);

    RVar ro("ro"), ri("ri");
    out.split(m, mo, mi, 4, tail)
        .split(mi, mi, ml, 1)
        .gpu_blocks(mo)
        .gpu_threads(ml, mi);
    Func intm = f.update().split(r, ro, ri, lanes).rfactor(ri, u);
    f.compute_at(out, ml);
    f.update().atomic().gpu_lanes(ri);
    intm.compute_at(out, mi).gpu_lanes(u);
    intm.update().gpu_lanes(u);
    return out;
}

bool expect(const std::string &src, const std::string &pattern, const std::string &what) {
    if (src.find(pattern) == std::string::npos) {
        std::cout << "Expected " << pattern << " in the source for " << what << ":\n"
                  << src << "\n";
        return false;
    }
    return true;
}

template<typename T>
bool check_lane_reduction(Op op, int lanes, int width, int height) {
    // Guard the rows with an if if they don't divide evenly into groups.
    TailStrategy tail = height % 4 ? TailStrategy::GuardWithIf : TailStrategy::Auto;
    Buffer<T> in(width, height);
    std::ostringstream what;
    what << "a reduction over " << lanes << " lanes of type " << type_of<T>();

    std::string metal = gpu_source(lane_reduction(in, op, lanes, tail), metal_target);
    std::string ptx = gpu_source(lane_reduction(in, op, lanes, tail), cuda_target);
    if (!expect(metal, "simd_shuffle_xor", what.str()) ||
        !expect(ptx, "shfl.sync.bfly", what.str())) {
        return false;
    }
    // Groups of lanes narrower than a warp may take different paths
    // from the other groups in the same warp, so they must only
    // synchronize with the lanes in their own group.
    if (lanes < 32 && !expect(ptx, "%laneid", what.str())) {
        return false;
    }
    return true;
}

int main(int argc, char **argv) {
    {
        const int lanes = 32;
        Buffer<float> in(lanes * 8, 64);
        if (!expect(gpu_source(butterfly_reduction(in, lanes), metal_target),
                    "simd_shuffle_down", "a butterfly reduction") ||
            !expect(gpu_source(butterfly_reduction(in, lanes), cuda_target),
                    "shfl.sync.down", "a butterfly reduction")) {
            return 1;
        }
    }

    // Reductions across lanes expressed as an atomic update become
    // butterfly reductions using xor shuffles.
    for (int lanes : {32, 16, 8}) {
        // Also check sizes that don't divide evenly, so that there are
        // conditions inside and outside of the loop over lanes.
        for (auto [width, height] : {std::pair{lanes * 8, 64}, std::pair{lanes * 8 + 5, 63}}) {
            if (!check_lane_reduction<float>(Op::Add, lanes, width, height) ||
                !check_lane_reduction<int32_t>(Op::Add, lanes, width, height) ||
                !check_lane_reduction<uint8_t>(Op::Add, lanes, width, height) ||
                !check_lane_reduction<int32_t>(Op::Mul, lanes, width, height) ||
                !check_lane_reduction<uint16_t>(Op::Min, lanes, width, height) ||
                !check_lane_reduction<float>(Op::Max, lanes, width, height)) {
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
