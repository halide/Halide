#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <vector>

#include <Halide.h>

using namespace Halide;

using std::pair;
using std::set;
using std::vector;

enum class Round {
    Up,
    Down
};

struct Avg {
    // Indices of the inputs within some list of ops.
    int i, j;

    // Round up or down
    Round round;

    void dump() const {
        std::cout << "avg_" << (round == Round::Down ? "d" : "u") << "(" << i << ", " << j << ")";
    }
};

void avg_down(Runtime::Buffer<uint8_t> a, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_a(UInt(8), 1), in_b(UInt(8), 1);
    static Func f = [&]() {
        Func f;
        Var x;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    in_a.set(Buffer<uint8_t>(std::move(a)));
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_down(uint8_t m, uint8_t s, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask, shift;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((x >> cast<int>(shift))) & mask;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    in_b.set(Buffer<uint8_t>(std::move(b)));
    mask.set(m);
    shift.set(s);
    f.realize(out);
}

void avg_down(uint8_t m_a, uint8_t s_a, uint8_t m_b, uint8_t s_b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask_a, shift_a, mask_b, shift_b;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((x >> cast<int>(shift_a))) & mask_a;
        Func in_b;
        in_b(x) = cast<uint8_t>((x >> cast<int>(shift_b))) & mask_b;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask_a.set(m_a);
    shift_a.set(s_a);
    mask_b.set(m_b);
    shift_b.set(s_b);
    f.realize(out);
}

void avg_up(Runtime::Buffer<uint8_t> a, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_a(UInt(8), 1), in_b(UInt(8), 1);
    static Func f = [&]() {
        Func f;
        Var x;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32);
        f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    in_a.set(Buffer<uint8_t>(std::move(a)));
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_up(uint8_t m, uint8_t s, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask, shift;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((x >> cast<int>(shift))) & mask;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask.set(m);
    shift.set(s);
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_up(uint8_t m_a, uint8_t s_a, uint8_t m_b, uint8_t s_b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask_a, shift_a, mask_b, shift_b;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((x >> cast<int>(shift_a))) & mask_a;
        Func in_b;
        in_b(x) = cast<uint8_t>((x >> cast<int>(shift_b))) & mask_b;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask_a.set(m_a);
    shift_a.set(s_a);
    mask_b.set(m_b);
    shift_b.set(s_b);
    f.realize(out);
}

uint64_t sum(Runtime::Buffer<uint8_t> a) {
    static ImageParam in(UInt(8), 1);
    static Buffer<uint32_t> out(1);
    static Param<int> size;
    static Func h = [&]() {
        Var x, y;

        Func g;
        RDom r2(0, size / 32);
        g(x) += cast<uint32_t>(in(r2 * 32 + x));

        Func h;
        RDom r3(0, 32);
        h(x) += g(r3);

        g.compute_root().vectorize(x);
        h.compute_root();
        h.compile_jit();
        return h;
    }();
    size.set(a.dim(0).extent());
    in.set(Buffer<uint8_t>(std::move(a)));
    h.realize(out);
    return out(0);
}

struct Dag {
    int num_inputs;
    vector<Avg> ops;

    vector<int> effective_kernel() const {
        vector<int> coefficients;
        for (int i = 0; i < num_inputs; i++) {
            // Get the coefficient by running the dag on a basis
            // vector where this input is one and all other inputs are
            // zero. The result will be fractional
            vector<pair<int, int>> stack;
            for (int j = 0; j < num_inputs; j++) {
                stack.emplace_back(0, 1);
            }
            stack[i].first = 1;

            for (auto op : ops) {
                auto a = stack[op.i];
                auto b = stack[op.j];
                while (a.second < b.second) {
                    a.first *= 2;
                    a.second *= 2;
                }
                while (b.second < a.second) {
                    b.first *= 2;
                    b.second *= 2;
                }
                stack.emplace_back(a.first + b.first, b.second * 2);
            }
            // All coefficients are going to end up with the same
            // denominator, so we can just return the numerator.
            coefficients.push_back(stack.back().first);
        }
        return coefficients;
    }

    int last_used_input() const {
        int i = -1;
        for (auto op : ops) {
            if (op.i < num_inputs) {
                i = std::max(i, op.i);
            }
            if (op.j < num_inputs) {
                i = std::max(i, op.j);
            }
        }
        return i;
    }

    void dump() const {
        std::cout << "Dag with " << num_inputs << " inputs:\n";
        int i = num_inputs;
        for (const auto &op : ops) {
            std::cout << i << ": ";
            op.dump();
            std::cout << "  Kernel: ";
            auto subdag = *this;
            while ((int)subdag.ops.size() > (i - num_inputs + 1))
                subdag.ops.pop_back();
            auto k = subdag.effective_kernel();
            for (int j : k) {
                std::cout << j << " ";
            }
            std::cout << "\n";
            i++;
        }
    }

    double bias() {
        int kernel_sum = 0;
        auto k = effective_kernel();
        for (int i : k) {
            kernel_sum += i;
        }
        int log_kernel_sum = 0;
        while ((1 << log_kernel_sum) < kernel_sum) {
            log_kernel_sum++;
        }
        vector<int> bits_per_input;
        vector<uint8_t> mask, shift;
        int total_bits = 0;
        for (int i = 0; i < num_inputs; i++) {
            const int bits = log_kernel_sum - __builtin_ctz(k[i]);
            bits_per_input.push_back(bits);
            mask.push_back((uint8_t)((1 << bits) - 1));
            shift.push_back(total_bits);
            total_bits += bits;
        }
        const int N = std::max(32, 1 << (total_bits));
        Runtime::Buffer<uint8_t> buf(N, ops.size());
        vector<Runtime::Buffer<uint8_t>> stack;
        int i = 0;
        for (const auto &op : ops) {
            if (op.round == Round::Down) {
                if (op.j < num_inputs) {
                    assert(op.i < num_inputs);
                    avg_down(mask[op.i], shift[op.i], mask[op.j], shift[op.j], buf.sliced(1, i));
                } else if (op.i < num_inputs) {
                    avg_down(mask[op.i], shift[op.i], buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                } else {
                    assert(op.j >= num_inputs && op.i >= num_inputs);
                    avg_down(buf.sliced(1, op.i - num_inputs), buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                }
            } else {
                if (op.j < num_inputs) {
                    assert(op.i < num_inputs);
                    avg_up(mask[op.i], shift[op.i], mask[op.j], shift[op.j], buf.sliced(1, i));
                } else if (op.i < num_inputs) {
                    avg_up(mask[op.i], shift[op.i], buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                } else {
                    assert(op.j >= num_inputs && op.i >= num_inputs);
                    avg_up(buf.sliced(1, op.i - num_inputs), buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                }
            }
            i++;
        }

        uint64_t s = sum(buf.sliced(1, buf.dim(1).max()));

        uint32_t correct_sum = 0;
        // Figure out the expected value of each input, and combine
        // them using the kernel, to get the correct expected value of
        // the output.
        const uint32_t scale = N / kernel_sum;
        for (int i = 0; i < num_inputs; i++) {
            const double mean_input = ((1 << bits_per_input[i]) - 1) / 2.0;
            correct_sum += (uint32_t)(scale * k[i] * mean_input);
        }

        // Note that we're letting things overflow on purpose, so
        // we're only getting the bottom 32 bits of the bias. I'm
        // hoping that's small enough that zero means zero.

        int32_t bias = (s - correct_sum);

        return ((double)bias / N);
    }
};

vector<Dag> enumerate_dags(int num_inputs, int num_ops) {
    if (num_ops == 0) {
        Dag do_nothing{num_inputs, vector<Avg>{}};
        return vector<Dag>{do_nothing};
    }

    vector<Dag> dags = enumerate_dags(num_inputs, num_ops - 1);
    vector<Dag> new_dags;
    for (const auto &dag : dags) {
        // Add one new op to this dag. Don't worry about rounding direction.
        new_dags.push_back(dag);
        int l = dag.last_used_input();
        for (int i = 0; i < num_inputs + (int)dag.ops.size(); i++) {
            // We're invariant to the order of the inputs, so force
            // the enumeration to consume them in-order
            if (i < num_inputs && i > l + 1) continue;
            for (int j = i + 1; j < num_inputs + (int)dag.ops.size(); j++) {
                if (j < num_inputs && j > std::max(i, l) + 1) continue;

                bool already_has_this_op = false;
                for (const auto &op : dag.ops) {
                    already_has_this_op |= (op.i == i && op.j == j);
                }

                if (!already_has_this_op) {
                    new_dags.push_back(dag);
                    new_dags.back().ops.push_back(Avg{i, j, Round::Down});
                }
            }
        }
    }

    return new_dags;
}

int main(int argc, const char **argv) {

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " num_inputs max_ops\n";
        return 0;
    }

    const int num_inputs = atoi(argv[1]);
    const int max_ops = atoi(argv[2]);

    auto dags = enumerate_dags(num_inputs, max_ops);

    set<vector<int>> seen_kernels;
    set<vector<int>> unbiased_kernels;

    for (auto &dag : dags) {
        if (dag.ops.empty()) {
            continue;
        }

        vector<int> kernel = dag.effective_kernel();
        std::sort(kernel.begin(), kernel.end());

        /*
        if (kernel[0] == 0) {
            // This kernel doesn't use one of its inputs
            continue;
        }
        */

        // Don't filter on seeing a kernel before, because maybe
        // there's a longer dag that gives us an unbiased version and
        // we don't want to miss it.
        // if (!seen_kernels.insert(kernel).second) continue;

        if (unbiased_kernels.count(kernel)) {
            // We already know how to do this one unbiased
            continue;
        }

        // Try all possible roundings and find the one with the least
        // bias.
        double best_bias = 0;
        size_t best_i = 0;
        for (size_t i = 0; i < ((size_t)1 << dag.ops.size()); i++) {
            for (size_t j = 0; j < dag.ops.size(); j++) {
                dag.ops[j].round = ((i >> j) & 1) ? Round::Up : Round::Down;
            }
            auto bias = dag.bias();
            if (i == 0 || std::abs(bias) < std::abs(best_bias)) {
                best_bias = bias;
                best_i = i;
            }
        }
        for (size_t j = 0; j < dag.ops.size(); j++) {
            dag.ops[j].round = ((best_i >> j) & 1) ? Round::Up : Round::Down;
        }

        if (std::abs(best_bias) > 1e-5) continue;

        // Shift all the kernel coefficients as rightwards as possible
        // to canonicalize the kernel.
        int mask = 0;
        for (int i : kernel) {
            mask |= i;
        }
        int shift = 0;
        while (!(mask & 1)) {
            mask >>= 1;
            shift++;
        }
        for (int &i : kernel) {
            i >>= shift;
        }

        if (!unbiased_kernels.insert(kernel).second) continue;

        dag.dump();
        std::cout << "Kernel: ";
        for (int c : kernel) {
            std::cout << c << " ";
        }
        std::cout << "\n"
                  << "Bias: " << best_bias << "\n";
    }

    return 0;
}
