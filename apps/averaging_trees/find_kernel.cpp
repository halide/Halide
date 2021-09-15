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

    void simplify() {

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
            } else {
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

vector<Dag> enumerate_dags(const vector<int> &ids, const vector<int> &kernel, int num_inputs, Round round = Round::Down) {

    vector<Dag> result;
    // For all possible partitions of ids into two equal sets,
    // generate a dag for each and combine.

    if (ids.size() == 2) {
        result.emplace_back();
        result.back().num_inputs = num_inputs;
        result.back().ops.emplace_back(ids[0], ids[1], round);
        return result;
    }

    // To generate all partitions, we'll iterate up to 1 << ids.size()
    // and treat that as a bit-mask telling us which side each id goes
    // to.

    for (size_t i = 0; i < (1ULL << (int)ids.size()); i++) {
        if (__builtin_popcount(i) != ids.size() / 2) {
            // Not a balanced partition
            continue;
        }

        vector<int> left_ids, right_ids;
        left_ids.reserve(ids.size() / 2);
        right_ids.reserve(ids.size() / 2);
        for (int j = 0; j < (int)ids.size(); j++) {
            if (i & (1 << j)) {
                left_ids.push_back(ids[j]);
            } else {
                right_ids.push_back(ids[j]);
            }
        }

        // avg is commutative, so to break symmetry we require that
        // the set that goes left is lexicographically before the set
        // that goes right.
        bool before = true;
        for (int j = 0; j < (int)left_ids.size(); j++) {
            if (left_ids[j] < right_ids[j]) {
                break;
            } else if (left_ids[j] > right_ids[j]) {
                before = false;
                break;
            }
        }
        if (!before) continue;

        // Each instance of each id is the same, so again to break
        // symmetries we require than for each id, they go to the left
        // before going to the right.
        vector<bool> id_has_gone_right(kernel.size(), false);
        bool bad = false;
        for (int j = 0; j < (int)ids.size(); j++) {
            if (i & (1 << j)) {
                bad |= id_has_gone_right[ids[j]];
            } else {
                id_has_gone_right[ids[j]] = true;
            }
        }
        if (bad) continue;

        Round subround = round == Round::Down ? Round::Up : Round::Down;
        vector<Dag> left = enumerate_dags(left_ids, kernel, num_inputs, subround);
        vector<Dag> right = enumerate_dags(right_ids, kernel, num_inputs, subround);

        for (const auto &l : left) {
            for (const auto &r : right) {
                Dag combined = l;
                int left_output_id = num_inputs + (int)l.ops.size() - 1;
                for (const auto &op : r.ops) {
                    auto adjust_id = [&](int i) {
                        if (i < num_inputs) {
                            return i;
                        } else {
                            return i + (int)l.ops.size();
                        }
                    };
                    combined.ops.emplace_back(adjust_id(op.i), adjust_id(op.j), op.round);
                }
                int right_output_id = num_inputs + (int)combined.ops.size() - 1;
                combined.ops.emplace_back(left_output_id, right_output_id, round);

                // Any ids that share a coefficient could be swapped
                // in the program, so break the symmetry by rejecting
                // anything that uses an large id with the same
                // coefficient as a small id before the small one.
                map<int, int> first_use_of_coefficient;
                for (auto k : kernel) {
                    first_use_of_coefficient[k] = -1;
                }
                bool bad = false;
                for (const auto &op : combined.ops) {
                    if (op.i < num_inputs) {
                        int coeff = kernel[op.i];
                        if (first_use_of_coefficient[coeff] < 0) {
                            first_use_of_coefficient[coeff] = op.i;
                        } else if (first_use_of_coefficient[coeff] > op.i) {
                            bad = true;
                        }
                    }
                    if (op.j < num_inputs) {
                        int coeff = kernel[op.j];
                        if (first_use_of_coefficient[coeff] < 0) {
                            first_use_of_coefficient[coeff] = op.j;
                        } else if (first_use_of_coefficient[coeff] > op.j) {
                            bad = true;
                        }
                    }
                }
                if (!bad) {
                    result.push_back(combined);
                }
            }
        }
    }

    return result;
}

int main(int argc, const char **argv) {

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " 1 4 6 4 1\n";
        return 0;
    }

    const int num_inputs = argc - 1;
    vector<int> kernel;
    int kernel_sum = 0;
    for (int i = 1; i < argc; i++) {
        kernel.push_back(std::atoi(argv[i]));
        kernel_sum += kernel.back();
    }
    assert(((kernel_sum & (kernel_sum - 1)) == 0) && "Kernel must sum to a power of two");

    // Place the inputs at the leaves of a balanced binary tree
    vector<int> ids;
    int id = 0;
    for (auto k : kernel) {
        for (int i = 0; i < k; i++) {
            ids.push_back(id);
        }
        id++;
    }

    vector<Dag> dags = enumerate_dags(ids, kernel, num_inputs);
    auto start = std::chrono::high_resolution_clock::now();
    std::cout << "Dags: " << dags.size() << "\n";
    int counter = 0;
    double best_bias = 1e100, error_of_best_bias = 1e100;
    double best_error = 1e100, bias_of_best_error = 1e100;

    for (auto &dag : dags) {
        dag.simplify();

        if (counter % 10 == 0) {
            auto t = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(t - start).count();
            auto total_time = (elapsed / counter) * dags.size();
            auto minutes_remaining = (total_time - elapsed) / 60;
            std::cout << counter << " / " << dags.size() << " (" << minutes_remaining << " minutes remaining)\n";
        }
        counter++;
        // Try all rounding options for this dag
        std::set<size_t> positive_bias, negative_bias;
        for (size_t i = 0; i < ((size_t)1 << dag.ops.size()); i++) {

            for (size_t j = 0; j < dag.ops.size(); j++) {
                dag.ops[j].round = ((i >> j) & 1) ? Round::Up : Round::Down;
            }

            // Before we do an expensive bias computation, see if we
            // already know this bias will be worse than a similar
            // tree
            bool skip_it = false;
            for (size_t j : positive_bias) {
                if ((i & j) == j) {
                    // We round up everywhere this other tree does,
                    // and more, and it has positive bias, so we're
                    // screwed.
                    skip_it = true;
                    break;
                }
            }
            for (size_t j : negative_bias) {
                if ((i & j) == i) {
                    // We round down everywhere this other tree does,
                    // and more, and it has negative bias, so we're
                    // screwed.
                    skip_it = true;
                    break;
                }
            }
            if (skip_it) continue;

            auto p = dag.bias();
            double bias = p.first;
            double error = p.second;

            if (bias > 0) {
                positive_bias.insert(i);
            } else if (bias < 0) {
                negative_bias.insert(i);
            }

            bool better_bias =
                (std::abs(bias) < std::abs(best_bias) ||
                 (std::abs(bias) == std::abs(best_bias) && error < error_of_best_bias));
            bool better_error =
                (error < best_error ||
                 (error == best_error && std::abs(bias) < std::abs(bias_of_best_error)));
            if (better_bias) {
                best_bias = bias;
                error_of_best_bias = error;
            }
            if (better_error) {
                best_error = error;
                bias_of_best_error = bias;
            }
            if (better_bias || better_error) {
                dag.dump();
                std::cout << "Bias: " << bias << " Error: " << error << "\n";
            }
        }
    }

    return 0;
}
