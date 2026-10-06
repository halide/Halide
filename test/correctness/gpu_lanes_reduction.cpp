#include "Halide.h"

// Run reductions across gpu lanes on a GPU and check the results.
// gpu_lanes_reduction_metal_codegen.cpp checks the Metal source
// generated for the same schedules.

using namespace Halide;

namespace {

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

template<typename T>
bool check_lane_reduction(Op op, int lanes, int width, int height) {
    // Guard the rows with an if if they don't divide evenly into groups.
    TailStrategy tail = height % 4 ? TailStrategy::GuardWithIf : TailStrategy::Auto;
    Buffer<T> in(width, height);
    in.for_each_element([&](int x, int y) {
        if (op == Op::Mul) {
            in(x, y) = (T)((x * 3 + y * 7) % 11 == 0 ? -1 : 1);
        } else {
            in(x, y) = (T)((x * 37 + y * 101) % 61);
        }
    });

    Buffer<T> result = lane_reduction(in, op, lanes, tail).realize({height});
    for (int y = 0; y < height; y++) {
        T correct = op == Op::Add ? (T)0 : op == Op::Mul ? (T)1 :
                                                           in(0, y);
        for (int x = 0; x < width; x++) {
            T val = in(x, y);
            switch (op) {
            case Op::Add:
                correct = (T)(correct + val);
                break;
            case Op::Mul:
                correct = (T)(correct * val);
                break;
            case Op::Min:
                correct = std::min(correct, val);
                break;
            case Op::Max:
                correct = std::max(correct, val);
                break;
            }
        }
        if (result(y) != correct) {
            std::cout << "Reduction over " << lanes << " lanes of type " << type_of<T>()
                      << ": result(" << y << ") = " << (double)result(y)
                      << " instead of " << (double)correct << "\n";
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    Target t = get_jit_target_from_environment();
    bool has_metal = t.has_feature(Target::Metal);
    bool has_cuda = t.has_feature(Target::CUDA) && t.get_cuda_capability_lower_bound() >= 50;
    if (!has_metal && !has_cuda) {
        printf("[SKIP] Metal, or CUDA with capability greater than or equal to 5.0, required.\n");
        return 0;
    }

    {
        const int lanes = 32, width = lanes * 8, height = 64;
        Buffer<float> in(width, height);
        in.for_each_element([&](int x, int y) {
            in(x, y) = (float)((x * 37 + y * 101) % 61);
        });

        Buffer<float> result = butterfly_reduction(in, lanes).realize({height});
        for (int y = 0; y < height; y++) {
            float correct = 0;
            for (int x = 0; x < width; x++) {
                correct += in(x, y);
            }
            if (result(y) != correct) {
                printf("Butterfly reduction: result(%d) = %f instead of %f\n",
                       y, result(y), correct);
                return 1;
            }
        }
    }

    // Reductions across lanes expressed as an atomic update are only
    // supported on Metal, so on CUDA only the butterfly above is tested.
    if (!has_metal) {
        printf("Atomic reductions across gpu lanes are only supported on Metal; not testing them.\n");
        printf("Success!\n");
        return 0;
    }

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
