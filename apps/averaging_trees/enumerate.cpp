#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <vector>

#include <Halide.h>

using namespace Halide;

using std::map;
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

    Avg(int i, int j, Round round)
        : i(i), j(j), round(round) {
    }

    bool operator==(const Avg &other) const {
        return i == other.i && j == other.j && round == other.round;
    }

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

template<int N>
std::pair<float, float> compute_bias_and_error(vector<uint8_t> &s,
                                               vector<uint8_t> &m,
                                               vector<int> &c,
                                               Runtime::Buffer<uint8_t> buf) {
    static ImageParam in_buf(UInt(8), 1), shifts(UInt(8), 1), masks(UInt(8), 1), coeffs(Int(32), 1);
    static Param<int> denom;
    static Func f = [&]() {
        Var x;
        RDom r(0, in_buf.dim(0).extent() / 32);
        Expr correct16 = cast<uint16_t>(0);
        Expr idx = r * 32 + x;
        for (int i = 0; i < N; i++) {
            Expr e = cast<uint16_t>((idx >> cast<int>(shifts(i)))) & masks(i);
            correct16 += e * cast<uint16_t>(coeffs(i));
        }
        Expr correct = cast<float>(correct16) * (1.0f / denom);

        Func f;
        f(x) = Tuple{0.f, 0.f};
        Expr actual = cast<float>(in_buf(idx));
        Expr error = actual - correct;
        f(x) = Tuple{f(x)[0] + error, max(f(x)[1], abs(error))};
        f.compute_root().vectorize(x).update().vectorize(x);

        Func h;
        RDom r2(0, 32);
        h() = Tuple{0.f, 0.f};
        h() = Tuple{h()[0] + f(r2)[0], max(h()[1], f(r2)[1])};

        h.compile_jit();
        return h;
    }();

    assert(s.size() == N);
    assert(m.size() == N);
    assert(c.size() == N);
    const int size = buf.number_of_elements();
    shifts.set(Buffer<uint8_t>(s.data(), N));
    masks.set(Buffer<uint8_t>(m.data(), N));
    coeffs.set(Buffer<int>(c.data(), N));
    in_buf.set(Buffer<uint8_t>(std::move(buf)));
    int d = 0;
    for (int i : c) {
        d += i;
    }
    denom.set(d);
    auto bias_out = Buffer<float>::make_scalar();
    auto error_out = Buffer<float>::make_scalar();
    f.realize({bias_out, error_out});
    return {bias_out() / size, error_out()};
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

    int unused_values() const {
        vector<bool> used(num_inputs + ops.size(), false);
        for (size_t i = ops.size(); i > 0; i--) {
            const auto &op = ops[i - 1];
            used[op.i] = true;
            used[op.j] = true;
        }
        int unused = 0;
        for (bool u : used) {
            unused += !u;
        }
        return unused;
    }

    void dump() const {
        std::cout << "\nDag with " << num_inputs << " inputs:\n";
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

    pair<float, float> bias() {
        int kernel_sum = 0;
        auto k = effective_kernel();
        for (int i : k) {
            kernel_sum += i;
        }
        int log_kernel_sum = 0;
        while ((1 << log_kernel_sum) < kernel_sum) {
            log_kernel_sum++;
        }

        // Figure out the max depth of each value from the root of the tree (the output)
        vector<int> depth(num_inputs + ops.size(), 0);
        for (size_t j = ops.size() + num_inputs - 1; j >= (size_t)num_inputs; j--) {
            const auto &op = ops[j - num_inputs];
            int d = depth[j];
            depth[op.i] = std::max(depth[op.i], d + 1);
            depth[op.j] = std::max(depth[op.j], d + 1);
        }

        vector<int> bits_per_input;
        vector<uint8_t> mask, shift;
        int total_bits = 0;
        for (int i = 0; i < num_inputs; i++) {
            const int bits = depth[i];
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

        switch (num_inputs) {
        case 2:
            return compute_bias_and_error<2>(shift, mask, k, buf.sliced(1, buf.dim(1).max()));
        case 3:
            return compute_bias_and_error<3>(shift, mask, k, buf.sliced(1, buf.dim(1).max()));
        case 4:
            return compute_bias_and_error<4>(shift, mask, k, buf.sliced(1, buf.dim(1).max()));
        case 5:
            return compute_bias_and_error<5>(shift, mask, k, buf.sliced(1, buf.dim(1).max()));
        case 6:
            return compute_bias_and_error<6>(shift, mask, k, buf.sliced(1, buf.dim(1).max()));
        case 7:
            return compute_bias_and_error<7>(shift, mask, k, buf.sliced(1, buf.dim(1).max()));
        default:
            assert(false);
            return std::pair<float, float>{};
        }
    }
};

vector<Dag> enumerate_dags(int num_inputs, int num_ops, int max_unused_values = 1) {
    if (num_ops == 0) {
        Dag do_nothing{num_inputs, vector<Avg>{}};
        return vector<Dag>{do_nothing};
    }

    vector<Dag> dags = enumerate_dags(num_inputs, num_ops - 1, max_unused_values + 1);
    vector<Dag> new_dags;
    for (const auto &dag : dags) {
        // Add one new op to this dag. Don't worry about rounding direction.
        if (dag.unused_values() <= max_unused_values) {
            new_dags.push_back(dag);
        }
        int l = dag.last_used_input();
        for (int i = 0; i < num_inputs + (int)dag.ops.size(); i++) {
            // We're invariant to the order of the inputs, so force
            // the enumeration to consume them in-order
            if (i < num_inputs && i > l + 1) continue;
            for (int j = i + 1; j < num_inputs + (int)dag.ops.size(); j++) {
                if (j < num_inputs && j > std::max(i, l) + 1) continue;

                int instances_of_this_op = 0;
                for (const auto &op : dag.ops) {
                    instances_of_this_op += (op.i == i && op.j == j) ? 1 : 0;
                }
                // We're allowed two instances of each op: one rounding up and another rounding down

                if (instances_of_this_op < 2) {
                    new_dags.push_back(dag);
                    new_dags.back().ops.push_back(Avg{i, j, Round::Down});
                    if (new_dags.back().unused_values() > max_unused_values) {
                        new_dags.pop_back();
                    }
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

    map<vector<int>, pair<float, float>> best_error_map, best_bias_map;

    std::cout << "Enumerating " << dags.size() << " dags\n";
    size_t counter = 0;

    for (auto &dag : dags) {
        counter++;

        if (counter % 10000 == 0) {
            std::cout << counter << "/" << dags.size() << "\n";
        }

        if (dag.ops.empty()) {
            continue;
        }

        vector<int> kernel = dag.effective_kernel();
        std::sort(kernel.begin(), kernel.end());

        // Skip dags that compute something then discard it
        vector<bool> used(dag.ops.size() + num_inputs, false);
        for (auto op : dag.ops) {
            used[op.i] = used[op.j] = true;
        }
        bool unused_intermediate = false;
        for (int i = 0; i < (int)dag.ops.size() - 1; i++) {
            unused_intermediate |= (!used[num_inputs + i]);
        }
        if (unused_intermediate) {
            continue;
        }

        // Shift all the kernel coefficients as rightwards as possible
        // to canonicalize the kernel.
        vector<int> normalized_kernel;
        normalized_kernel.reserve(kernel.size());
        int mask = 0;
        for (int i : kernel) {
            mask |= i;
        }
        int shift = 0;
        while (!(mask & 1)) {
            mask >>= 1;
            shift++;
        }
        for (int i : kernel) {
            normalized_kernel.push_back(i >> shift);
        }

        // Try all possible roundings and find the one with the least
        // bias and the one with the least error
        float best_bias = 0, best_error = 0, error_of_best_bias = 0, bias_of_best_error = 0;
        size_t best_bias_idx = 0, best_error_idx = 0;
        for (size_t i = 0; i < ((size_t)1 << dag.ops.size()); i++) {
            for (size_t j = 0; j < dag.ops.size(); j++) {
                dag.ops[j].round = ((i >> j) & 1) ? Round::Up : Round::Down;
            }
            // Skip dags that duplicate an op
            bool bad = false;
            for (size_t j = 0; j < dag.ops.size(); j++) {
                for (size_t k = 0; k < j; k++) {
                    bad |= (dag.ops[j] == dag.ops[k]);
                }
            }
            if (bad) continue;

            auto bias_and_error = dag.bias();
            auto bias = bias_and_error.first;
            auto error = bias_and_error.second;
            if (best_error == 0 || std::abs(bias) < std::abs(best_bias) ||
                (std::abs(bias) == std::abs(best_bias) &&
                 error < error_of_best_bias)) {
                best_bias = bias;
                error_of_best_bias = error;
                best_bias_idx = i;
            }
            if (best_error == 0 || error < best_error ||
                (error == best_error &&
                 std::abs(bias) < std::abs(bias_of_best_error))) {
                best_error = error;
                bias_of_best_error = bias;
                best_error_idx = i;
            }
        }

        auto bias_dag = dag;
        auto error_dag = dag;

        for (size_t j = 0; j < dag.ops.size(); j++) {
            bias_dag.ops[j].round = ((best_bias_idx >> j) & 1) ? Round::Up : Round::Down;
            error_dag.ops[j].round = ((best_error_idx >> j) & 1) ? Round::Up : Round::Down;
        }

        auto error_it = best_error_map.find(normalized_kernel);
        auto bias_it = best_bias_map.find(normalized_kernel);
        bool better_bias =
            error_it == best_error_map.end() ||
            std::abs(best_bias) < std::abs(bias_it->second.first) ||
            (std::abs(best_bias) == std::abs(bias_it->second.first) &&
             error_of_best_bias < bias_it->second.second);

        bool better_error =
            error_it == best_error_map.end() ||
            best_error < error_it->second.second ||
            (best_error == error_it->second.second &&
             std::abs(bias_of_best_error) < std::abs(bias_it->second.first));

        if (better_bias) {
            // This breaks a record for the best bias seen so far
            best_bias_map[normalized_kernel] = std::make_pair(best_bias, error_of_best_bias);
            bias_dag.dump();
            std::cout << "Kernel: ";
            for (int c : normalized_kernel) {
                std::cout << c << " ";
            }
            std::cout << "\n"
                      << "Bias: " << best_bias << "\n"
                      << "Error: " << error_of_best_bias << "\n";
        }

        if (better_error) {
            best_error_map[normalized_kernel] = std::make_pair(bias_of_best_error, best_error);

            if (!better_bias || best_error_idx != best_bias_idx) {
                // Only print it if we didn't just print it above
                error_dag.dump();
                std::cout << "Kernel: ";
                for (int c : normalized_kernel) {
                    std::cout << c << " ";
                }
                std::cout << "\n"
                          << "Bias: " << bias_of_best_error << "\n"
                          << "Error: " << best_error << "\n";
            }
        }
    }

    return 0;
}
