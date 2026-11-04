#include "Halide.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <type_traits>

using namespace Halide;

// Atomic updates in GPU kernels, beyond the plain histograms of
// correctness_atomics: the read-modify-write operations with a native atomic
// (min, max, and, or, xor), the ones without (compare-and-swap loops), and
// atomics on shared memory.

namespace {

constexpr int img_size = 10000;
constexpr int hist_size = 13;

Expr bin_of(const Expr &x) {
    return (x * x + 3) % hist_size;
}

template<typename T>
Expr const_of(T v) {
    if (std::is_floating_point<T>::value) {
        return Internal::make_const(type_of<T>(), (double)v);
    } else if (std::is_signed<T>::value) {
        return Internal::make_const(type_of<T>(), (int64_t)v);
    } else {
        return Internal::make_const(type_of<T>(), (uint64_t)v);
    }
}

template<typename T>
std::string type_name() {
    std::ostringstream os;
    os << type_of<T>();
    return os.str();
}

int ref_bin_of(int x) {
    return (x * x + 3) % hist_size;
}

template<typename T>
Expr value_of(const Expr &x) {
    if (std::is_floating_point<T>::value) {
        return cast<T>((x * 37) % 1001) / 7;
    }
    return cast<T>((x * 7919) % 100003 - (std::is_signed<T>::value ? 50000 : 0));
}

template<typename T>
T ref_value_of(int x) {
    if (std::is_floating_point<T>::value) {
        return (T)((x * 37) % 1001) / 7;
    }
    return (T)((x * 7919) % 100003 - (std::is_signed<T>::value ? 50000 : 0));
}

enum class Op {
    Add,
    Min,
    Max,
    And,
    Or,
    Xor,
    Mul,  // no native atomic: compare-and-swap loop
};

const char *op_name(Op op) {
    switch (op) {
    case Op::Add:
        return "add";
    case Op::Min:
        return "min";
    case Op::Max:
        return "max";
    case Op::And:
        return "and";
    case Op::Or:
        return "or";
    case Op::Xor:
        return "xor";
    case Op::Mul:
        return "mul";
    }
    return "";
}

template<typename T>
int test_scatter(Op op, const Target &target) {
    const bool is_float = std::is_floating_point<T>::value;
    if (is_float && (op == Op::And || op == Op::Or || op == Op::Xor)) {
        return 0;
    }

    Var x;
    RDom r(0, img_size);
    Func f;

    T init = T(0);
    if (op == Op::Min) {
        init = std::numeric_limits<T>::max();
    } else if (op == Op::Max) {
        init = std::numeric_limits<T>::lowest();
    } else if (op == Op::And) {
        init = T(~0);
    } else if (op == Op::Mul) {
        init = T(1);
    }

    // Keep the products small (and exact for floats).
    Expr v = (op == Op::Mul) ? cast<T>(select(r % 1000 == 0, 3, 1)) : value_of<T>(r);
    Expr b = bin_of(r);
    f(x) = const_of<T>(init);
    switch (op) {
    case Op::Add:
        f(b) += v;
        break;
    case Op::Min:
        f(b) = min(f(b), v);
        break;
    case Op::Max:
        f(b) = max(v, f(b));
        break;
    case Op::And:
        f(b) = f(b) & v;
        break;
    case Op::Or:
        f(b) = v | f(b);
        break;
    case Op::Xor:
        f(b) = f(b) ^ v;
        break;
    case Op::Mul:
        f(b) *= v;
        break;
    }

    RVar ro, ri;
    f.update()
        .atomic(true /*override_associativity_test*/)
        .split(r, ro, ri, 64)
        .gpu_blocks(ro)
        .gpu_threads(ri);

    std::vector<T> ref(hist_size, init);
    for (int i = 0; i < img_size; i++) {
        T val = (op == Op::Mul) ? T(i % 1000 == 0 ? 3 : 1) : ref_value_of<T>(i);
        T &o = ref[ref_bin_of(i)];
        switch (op) {
        case Op::Add:
            o = o + val;
            break;
        case Op::Min:
            o = std::min(o, val);
            break;
        case Op::Max:
            o = std::max(o, val);
            break;
        case Op::And:
            o = (T)((uint64_t)o & (uint64_t)val);
            break;
        case Op::Or:
            o = (T)((uint64_t)o | (uint64_t)val);
            break;
        case Op::Xor:
            o = (T)((uint64_t)o ^ (uint64_t)val);
            break;
        case Op::Mul:
            o = o * val;
            break;
        }
    }

    for (int iter = 0; iter < 5; iter++) {
        Buffer<T> out = f.realize({hist_size}, target);
        for (int i = 0; i < hist_size; i++) {
            bool ok = is_float ? std::abs((double)out(i) - (double)ref[i]) <= 1e-3 * std::max(1.0, std::abs((double)ref[i])) : out(i) == ref[i];
            if (!ok) {
                printf("atomic %s on %s: out(%d) = %f instead of %f\n",
                       op_name(op), type_name<T>().c_str(), i, (double)out(i), (double)ref[i]);
                return 1;
            }
        }
    }
    return 0;
}

// A histogram like a real-world one: each GPU block accumulates a partial
// histogram in shared memory with atomics, which is then atomically added to
// the output.
template<typename T>
int test_shared_histogram(const Target &target) {
    const int w = 256, h = 128;
    Var x, y, bin, bx, by;
    RDom r(0, w, 0, h);

    Func im;
    im(x, y) = (x * 3 + y * y) % hist_size;

    Func hist;
    hist(bin) = cast<T>(0);
    hist(im(r.x, r.y)) += cast<T>(1);

    RVar rxo, ryo, rxi, ryi;
    Func block = hist.update()
                     .tile(r.x, r.y, rxo, ryo, rxi, ryi, 16, 16)
                     .rfactor({{rxo, bx}, {ryo, by}});

    hist.compute_root()
        .gpu_single_thread()
        .update()
        .atomic()
        .reorder(bin, rxo, ryo)
        .gpu_blocks(rxo, ryo)
        .gpu_threads(bin);
    block.compute_at(hist, rxo)
        .gpu_threads(bin)
        .update()
        .atomic()
        .gpu_threads(rxi, ryi);

    std::vector<T> ref(hist_size, T(0));
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            ref[(i * 3 + j * j) % hist_size] += T(1);
        }
    }

    for (int iter = 0; iter < 5; iter++) {
        Buffer<T> out = hist.realize({hist_size}, target);
        for (int i = 0; i < hist_size; i++) {
            if (out(i) != ref[i]) {
                printf("shared histogram of %s: out(%d) = %f instead of %f\n",
                       type_name<T>().c_str(), i, (double)out(i), (double)ref[i]);
                return 1;
            }
        }
    }
    return 0;
}

template<typename T>
int test_all(const Target &target) {
    for (Op op : {Op::Add, Op::Min, Op::Max, Op::And, Op::Or, Op::Xor, Op::Mul}) {
        if (test_scatter<T>(op, target)) {
            return 1;
        }
    }
    return test_shared_histogram<T>(target);
}

}  // namespace

int main(int argc, char **argv) {
    Target target = get_jit_target_from_environment();
    if (!target.has_gpu_feature()) {
        printf("[SKIP] No GPU target enabled.\n");
        return 0;
    }
    if (target.has_feature(Target::Metal) ||
        target.has_feature(Target::WebGPU) ||
        target.has_feature(Target::D3D12Compute)) {
        printf("[SKIP] These atomic updates are not supported in this GPU backend.\n");
        return 0;
    }

    if (test_all<uint32_t>(target) ||
        test_all<int32_t>(target) ||
        test_all<float>(target)) {
        return 1;
    }

    const bool has_64_bit_atomics =
        target.has_feature(Target::CUDA) ||
        target.has_feature(Target::CLAtomics64) ||
        target.has_feature(Target::VulkanInt64);
    if (has_64_bit_atomics) {
        if (test_all<uint64_t>(target) ||
            test_all<int64_t>(target)) {
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
