#ifndef DAG_H
#define DAG_H

#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdlib.h>
#include <vector>

#include "Halide.h"

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
        std::cout << "avg_" << (round == Round::Down ? "d" : "u") << "(v" << i << ", v" << j << ")";
    }
};

void avg_down(Runtime::Buffer<uint8_t> a, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_a(UInt(8), 1), in_b(UInt(8), 1);
    static Func f = [&]() {
        Func f;
        Var x;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    in_a.set(Buffer<uint8_t>(std::move(a)));
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_down(int start, uint8_t m, uint8_t s, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask, shift;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>(((x + start) >> cast<int>(shift))) & mask;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    in_b.set(Buffer<uint8_t>(std::move(b)));
    mask.set(m);
    shift.set(s);
    f.realize(out);
}

void avg_down(int start, uint8_t m_a, uint8_t s_a, uint8_t m_b, uint8_t s_b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask_a, shift_a, mask_b, shift_b;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>(((x + start) >> cast<int>(shift_a))) & mask_a;
        Func in_b;
        in_b(x) = cast<uint8_t>(((x + start) >> cast<int>(shift_b))) & mask_b;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
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
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    in_a.set(Buffer<uint8_t>(std::move(a)));
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_up(int start, uint8_t m, uint8_t s, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask, shift;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((x + start) >> cast<int>(shift)) & mask;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask.set(m);
    shift.set(s);
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_up(int start, uint8_t m_a, uint8_t s_a, uint8_t m_b, uint8_t s_b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask_a, shift_a, mask_b, shift_b;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>(((x + start) >> cast<int>(shift_a))) & mask_a;
        Func in_b;
        in_b(x) = cast<uint8_t>(((x + start) >> cast<int>(shift_b))) & mask_b;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
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

void avg_up(Runtime::Buffer<int32_t> inputs, uint8_t m, uint8_t s, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_inputs(Int(32), 1);
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask, shift;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((in_inputs(x) >> cast<int>(shift))) & mask;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask.set(m);
    shift.set(s);
    in_inputs.set(Buffer<int32_t>(std::move(inputs)));
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_up(Runtime::Buffer<int32_t> inputs, uint8_t m_a, uint8_t s_a, uint8_t m_b, uint8_t s_b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_inputs(Int(32), 1);
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask_a, shift_a, mask_b, shift_b;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((in_inputs(x) >> cast<int>(shift_a))) & mask_a;
        Func in_b;
        in_b(x) = cast<uint8_t>((in_inputs(x) >> cast<int>(shift_b))) & mask_b;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x) + 1) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask_a.set(m_a);
    shift_a.set(s_a);
    mask_b.set(m_b);
    shift_b.set(s_b);
    in_inputs.set(Buffer<int32_t>(std::move(inputs)));
    f.realize(out);
}

void avg_down(Runtime::Buffer<int32_t> inputs, uint8_t m, uint8_t s, Runtime::Buffer<uint8_t> b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_inputs(Int(32), 1);
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask, shift;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((in_inputs(x) >> cast<int>(shift))) & mask;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask.set(m);
    shift.set(s);
    in_inputs.set(Buffer<int32_t>(std::move(inputs)));
    in_b.set(Buffer<uint8_t>(std::move(b)));
    f.realize(out);
}

void avg_down(Runtime::Buffer<int32_t> inputs, uint8_t m_a, uint8_t s_a, uint8_t m_b, uint8_t s_b, Runtime::Buffer<uint8_t> out) {
    static ImageParam in_inputs(Int(32), 1);
    static ImageParam in_b(UInt(8), 1);
    static Param<uint8_t> mask_a, shift_a, mask_b, shift_b;
    static Func f = [&]() {
        Var x;
        Func in_a;
        in_a(x) = cast<uint8_t>((in_inputs(x) >> cast<int>(shift_a))) & mask_a;
        Func in_b;
        in_b(x) = cast<uint8_t>((in_inputs(x) >> cast<int>(shift_b))) & mask_b;

        Func f;
        f(x) = cast<uint8_t>((cast<uint16_t>(in_a(x)) + in_b(x)) / 2);
        f.vectorize(x, 32, TailStrategy::RoundUp);
        // f.parallel(x, 4096, TailStrategy::GuardWithIf);
        f.align_bounds(x, 32);
        f.compile_jit();
        return f;
    }();
    mask_a.set(m_a);
    shift_a.set(s_a);
    mask_b.set(m_b);
    shift_b.set(s_b);
    in_inputs.set(Buffer<int32_t>(std::move(inputs)));
    f.realize(out);
}

struct Result {
    float bias, error, min_error, max_error;
    int32_t worst_input;
};

template<int N, bool exhaustive>
Result compute_bias_and_error(Runtime::Buffer<int32_t> inputs,
                              vector<uint8_t> &s,
                              vector<uint8_t> &m,
                              vector<int> &c,
                              Runtime::Buffer<uint8_t> buf) {
    static ImageParam inputs_buf(Int(32), 1);
    static ImageParam in_buf(UInt(8), 1), shifts(UInt(8), 1), masks(UInt(8), 1), coeffs(Int(32), 1);
    static Param<int> denom;
    static Func f = [&]() {
        Var x;
        RDom r(0, in_buf.dim(0).extent() / 32);
        Expr correct16 = cast<uint16_t>(0);
        Expr idx = r * 32 + x;

        Expr input_val = exhaustive ? idx : inputs_buf(idx);

        for (int i = 0; i < N; i++) {
            Expr e = cast<uint16_t>((input_val >> cast<int>(shifts(i)))) & masks(i);
            correct16 += e * cast<uint16_t>(coeffs(i));
        }
        Expr correct = cast<float>(correct16) * (1.0f / denom);

        Func f;
        f(x) = Tuple{0.f, 1e10f, -1e10f, 0};
        Expr actual = cast<float>(in_buf(idx));
        Expr error = actual - correct;
        f(x) = Tuple{f(x)[0] + error,
                     min(f(x)[1], error),
                     max(f(x)[2], error),
                     select(abs(error) > max(-f(x)[1], f(x)[2]), input_val, f(x)[3])};

        f.compute_root().vectorize(x).update().vectorize(x);

        Func h;
        RDom r2(0, 32);
        h() = Tuple{0.f, 1e10f, -1e10f, 0};

        h() = Tuple{
            h()[0] + f(r2)[0],
            min(h()[1], f(r2)[1]),
            max(h()[2], f(r2)[2]),
            select(max(-f(r2)[1], f(r2)[2]) > max(-h()[1], h()[2]),
                   f(r2)[3], h()[3])};

        h.compile_jit();
        return h;
    }();

    assert(s.size() == N);
    assert(m.size() == N);
    assert(c.size() == N);
    const int size = buf.number_of_elements();
    if (!exhaustive) {
        inputs_buf.set(Buffer<int32_t>(std::move(inputs)));
    }
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
    auto min_error_out = Buffer<float>::make_scalar();
    auto max_error_out = Buffer<float>::make_scalar();
    auto worst_index_out = Buffer<int32_t>::make_scalar();
    f.realize({bias_out, min_error_out, max_error_out, worst_index_out});
    assert(min_error_out() <= 0.f);
    assert(max_error_out() >= 0.f);
    return {bias_out() / size,
            std::max(std::abs(min_error_out()), std::abs(max_error_out())),
            min_error_out(),
            max_error_out(),
            worst_index_out()};
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

    void dump_path(uint32_t path, int depth, int &plusses, int &minuses) const {
        int idx = (int)ops.size() + num_inputs - 1;
        while (depth >= 0) {
            if (idx < num_inputs) {
                std::cout << idx;
            } else {
                bool up = ops[idx - num_inputs].round == Round::Up;
                bool down = ops[idx - num_inputs].round == Round::Down;
                plusses += up;
                minuses += down;
                std::cout << (up ? "+" : "-");
                if ((path >> (depth - 1)) & 1) {
                    idx = ops[idx - num_inputs].j;
                } else {
                    idx = ops[idx - num_inputs].i;
                }
            }
            depth--;
        }
        std::cout << "\n";
    }

    float estimated_bias() const {
        auto kernel = effective_kernel();
        int kernel_sum = 0;
        for (int k : kernel) {
            kernel_sum += k;
        }
        int log_kernel_sum = 1;
        while ((1 << log_kernel_sum) < kernel_sum) {
            log_kernel_sum++;
        }
        int plusses = 0, minuses = 0;
        for (int path = 0; path < kernel_sum; path++) {
            int idx = (int)ops.size() + num_inputs - 1;
            int depth = log_kernel_sum;
            while (idx >= num_inputs) {
                bool up = ops[idx - num_inputs].round == Round::Up;
                bool down = ops[idx - num_inputs].round == Round::Down;
                plusses += up;
                minuses += down;
                if ((path >> (depth - 1)) & 1) {
                    idx = ops[idx - num_inputs].j;
                } else {
                    idx = ops[idx - num_inputs].i;
                }
                depth--;
            }
        }
        return (plusses - minuses) / (float)kernel_sum;
    }

    void dump_paths() const {
        int plusses = 0, minuses = 0;
        auto kernel = effective_kernel();
        int kernel_sum = 0;
        for (int k : kernel) {
            kernel_sum += k;
        }
        int log_kernel_sum = 1;
        while ((1 << log_kernel_sum) < kernel_sum) {
            log_kernel_sum++;
        }
        for (int i = 0; i < kernel_sum; i++) {
            dump_path(i, log_kernel_sum, plusses, minuses);
        }
    }

    void dump() const {
        std::cout << "\nDag with " << num_inputs << " inputs:\n";
        int i = num_inputs;
        for (const auto &op : ops) {
            std::cout << "v" << i << " = ";
            op.dump();
            std::cout << "; //  Kernel: ";
            auto subdag = *this;
            while ((int)subdag.ops.size() > (i - num_inputs + 1)) {
                subdag.ops.pop_back();
            }
            auto k = subdag.effective_kernel();
            for (int j : k) {
                std::cout << j << " ";
            }
            auto p = subdag.bias();
            assert(p.min_error <= 0);
            assert(p.max_error >= 0);
            std::cout << " : " << p.bias << " " << p.min_error << " " << p.max_error << "\n";
            i++;
        }
        dump_paths();
    }

    Result bias_on(const set<int32_t> &inputs) {
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

        Runtime::Buffer<int32_t> inputs_buf(((std::max((int)inputs.size(), 1) + 31) / 32) * 32);
        inputs_buf.fill(0);
        int idx = 0;
        for (auto i : inputs) {
            inputs_buf(idx) = i;
            idx++;
        }

        const int N =
            inputs.empty() ? std::max(32, 1 << (total_bits)) : inputs_buf.dim(0).extent();

        const int slice = std::min(N, 1024 * 128);
        const int num_slices = N / slice;
        assert(slice * num_slices == N);
        Runtime::Buffer<uint8_t> buf(slice, ops.size());

        assert(num_slices == 1 || inputs.empty());

        Result result;

        for (int s = 0; s < N; s += slice) {
            int i = 0;
            for (const auto &op : ops) {
                // TODO: Add an outer loop to reduce peak memory usage
                // TODO: Add an extra dimension for all possible rounding modes
                if (op.round == Round::Down) {
                    if (inputs.empty() && op.j < num_inputs) {
                        assert(op.i < num_inputs);
                        avg_down(s, mask[op.i], shift[op.i], mask[op.j], shift[op.j], buf.sliced(1, i));
                    } else if (inputs.empty() && op.i < num_inputs) {
                        avg_down(s, mask[op.i], shift[op.i], buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                    } else if (op.j < num_inputs) {
                        assert(op.i < num_inputs);
                        avg_down(inputs_buf, mask[op.i], shift[op.i], mask[op.j], shift[op.j], buf.sliced(1, i));
                    } else if (op.i < num_inputs) {
                        avg_down(inputs_buf, mask[op.i], shift[op.i], buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                    } else {
                        assert(op.j >= num_inputs && op.i >= num_inputs);
                        avg_down(buf.sliced(1, op.i - num_inputs), buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                    }
                } else {
                    if (inputs.empty() && op.j < num_inputs) {
                        assert(op.i < num_inputs);
                        avg_up(s, mask[op.i], shift[op.i], mask[op.j], shift[op.j], buf.sliced(1, i));
                    } else if (inputs.empty() && op.i < num_inputs) {
                        avg_up(s, mask[op.i], shift[op.i], buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                    } else if (op.j < num_inputs) {
                        assert(op.i < num_inputs);
                        avg_up(inputs_buf, mask[op.i], shift[op.i], mask[op.j], shift[op.j], buf.sliced(1, i));
                    } else if (op.i < num_inputs) {
                        avg_up(inputs_buf, mask[op.i], shift[op.i], buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                    } else {
                        assert(op.j >= num_inputs && op.i >= num_inputs);
                        avg_up(buf.sliced(1, op.i - num_inputs), buf.sliced(1, op.j - num_inputs), buf.sliced(1, i));
                    }
                }
                i++;
            }

            Result r{0};

            if (inputs.empty()) {
                switch (num_inputs) {
                case 2:
                    r = compute_bias_and_error<2, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 3:
                    r = compute_bias_and_error<3, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 4:
                    r = compute_bias_and_error<4, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 5:
                    r = compute_bias_and_error<5, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 6:
                    r = compute_bias_and_error<6, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 7:
                    r = compute_bias_and_error<7, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 8:
                    r = compute_bias_and_error<8, true>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                default:
                    assert(false);
                }
            } else {
                switch (num_inputs) {
                case 2:
                    r = compute_bias_and_error<2, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 3:
                    r = compute_bias_and_error<3, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 4:
                    r = compute_bias_and_error<4, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 5:
                    r = compute_bias_and_error<5, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 6:
                    r = compute_bias_and_error<6, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 7:
                    r = compute_bias_and_error<7, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                case 8:
                    r = compute_bias_and_error<8, false>(inputs_buf, shift, mask, k, buf.sliced(1, buf.dim(1).max()));
                    break;
                default:
                    assert(false);
                }
            }

            if (s == 0) {
                result = r;
            } else {
                result.bias += r.bias;
                result.worst_input = result.error > r.error ? result.worst_input : r.worst_input;
                result.error = std::max(result.error, r.error);
                result.min_error = std::min(result.error, r.min_error);
                result.max_error = std::max(result.error, r.max_error);
            }
        }
        result.bias /= num_slices;

        return result;
    }

    Result bias() {
        std::set<int32_t> i;
        return bias_on(i);
    }

    void simplify(bool rounding_known) {

        // global value numbering
        map<int, int> replacements;
        for (size_t i = 0; i < ops.size(); i++) {
            int idx = (int)i + num_inputs;

            auto it = replacements.find(ops[i].i);
            if (it != replacements.end()) {
                ops[i].i = it->second;
            }

            it = replacements.find(ops[i].j);
            if (it != replacements.end()) {
                ops[i].j = it->second;
            }

            if (ops[i].j < ops[i].i) {
                std::swap(ops[i].i, ops[i].j);
            }

            if (ops[i].i == ops[i].j) {
                replacements[idx] = ops[i].i;
            } else if (rounding_known) {
                for (size_t j = 0; j < i; j++) {
                    if (ops[i].i == ops[j].i &&
                        ops[i].j == ops[j].j &&
                        ops[i].round == ops[j].round) {
                        replacements[idx] = (int)j + num_inputs;
                        break;
                    }
                }
            }
        }

        // Trim the end to the last used op
        while (1) {
            auto it = replacements.find((int)ops.size() + num_inputs - 1);
            if (it == replacements.end()) {
                break;
            }
            while ((int)ops.size() > it->second - num_inputs + 1) {
                ops.pop_back();
            }
        }

        // Delete dead ops
        vector<bool> used(ops.size(), false);
        used.back() = true;
        for (int i = (int)ops.size() - 1; i > 0; i--) {
            if (used[i]) {
                if (ops[i].i >= num_inputs) {
                    used[ops[i].i - num_inputs] = true;
                }
                if (ops[i].j >= num_inputs) {
                    used[ops[i].j - num_inputs] = true;
                }
            }
        }

        vector<Avg> new_ops;
        int deleted = 0;
        for (int i = 0; i < (int)ops.size(); i++) {
            int idx = num_inputs + i;
            if (used[i]) {
                new_ops.push_back(ops[i]);
            } else {
                for (int j = i + 1; j < (int)ops.size(); j++) {
                    if (ops[j].i > idx - deleted) {
                        ops[j].i--;
                    }
                    if (ops[j].j > idx - deleted) {
                        ops[j].j--;
                    }
                }
                deleted++;
            }
        }

        ops.swap(new_ops);
    }

    // To help with deduping
    bool operator<(const Dag &other) const {
        assert(num_inputs == other.num_inputs);
        if (ops.size() < other.ops.size()) {
            return true;
        }
        if (ops.size() > other.ops.size()) {
            return false;
        }
        for (size_t i = 0; i < ops.size(); i++) {
            if (ops[i].i < other.ops[i].i) {
                return true;
            }
            if (ops[i].i > other.ops[i].i) {
                return false;
            }
            if (ops[i].j < other.ops[i].j) {
                return true;
            }
            if (ops[i].j > other.ops[i].j) {
                return false;
            }
            if (ops[i].round < other.ops[i].round) {
                return true;
            }
            if (ops[i].round > other.ops[i].round) {
                return false;
            }
        }
        return false;
    }
};

#endif
