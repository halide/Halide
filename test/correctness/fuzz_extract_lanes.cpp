#include "Halide.h"
#include <array>
#include <functional>
#include <random>

// Fuzz test for deinterleave / extract_lane operations in Deinterleave.cpp.
// Constructs random vector expressions covering the IR node types that
// the Deinterleaver has visit methods for, evaluates them by JIT-compiling
// with a custom lowering pass, then checks that deinterleave() produces
// results consistent with the original expression.

namespace {

using std::string;
using std::vector;
using namespace Halide;
using namespace Halide::Internal;

using RandomEngine = std::mt19937_64;

constexpr int fuzz_var_count = 3;
std::vector<Param<int>> fuzz_vars(fuzz_var_count);

template<typename T>
decltype(auto) random_choice(RandomEngine &rng, T &&choices) {
    std::uniform_int_distribution<size_t> dist(0, std::size(choices) - 1);
    return choices[dist(rng)];
}

Type fuzz_types[] = {UInt(8), UInt(16), UInt(32), UInt(64), Int(8), Int(16), Int(32), Int(64)};

Type random_scalar_type(RandomEngine &rng) {
    return random_choice(rng, fuzz_types);
}

int random_factor(RandomEngine &rng, int x) {
    vector<int> factors;
    factors.reserve(x);
    for (int i = 1; i < x; i++) {
        if (x % i == 0) {
            factors.push_back(i);
        }
    }
    return random_choice(rng, factors);
}

Expr random_const(RandomEngine &rng, Type t) {
    int val = (int)((int8_t)(rng() & 0x0f));
    if (t.is_vector()) {
        return Broadcast::make(cast(t.element_of(), val), t.lanes());
    } else {
        return cast(t, val);
    }
}

Expr random_leaf(RandomEngine &rng, Type t) {
    if (t.is_scalar()) {
        if (rng() & 1) {
            // Variable
            std::uniform_int_distribution dist(0, fuzz_var_count - 1);
            return cast(t, fuzz_vars[dist(rng)]);
        } else {
            return random_const(rng, t);
        }
    }
    // For vector types, build from Ramp or Broadcast
    int lanes = t.lanes();
    if (rng() & 1) {
        Expr base = random_leaf(rng, t.element_of());
        Expr stride = random_const(rng, t.element_of());
        return Ramp::make(base, stride, lanes);
    } else {
        Expr val = random_leaf(rng, t.element_of());
        return Broadcast::make(val, lanes);
    }
}

Expr random_vector_expr(RandomEngine &rng, Type t, int depth) {
    if (depth <= 0 || t.lanes() == 1) {
        return random_leaf(rng, t);
    }

    // Weight the choices to cover all Deinterleaver visit methods:
    // Broadcast, Ramp, Cast, Reinterpret, Call (via abs), Shuffle,
    // VectorReduce, Add/Sub/Min/Max (handled by default IRMutator)
    std::function<Expr()> ops[] = {
        // Leaf
        [&]() -> Expr {
            return random_leaf(rng, t);
        },
        // Add
        [&]() -> Expr {
            Expr a = random_vector_expr(rng, t, depth - 1);
            Expr b = random_vector_expr(rng, t, depth - 1);
            return a + b;
        },
        // Sub (only for signed types to avoid unsigned underflow coercion errors)
        [&]() -> Expr {
            if (t.is_uint()) {
                // Fall back to Add for unsigned types
                Expr a = random_vector_expr(rng, t, depth - 1);
                Expr b = random_vector_expr(rng, t, depth - 1);
                return a + b;
            }
            Expr a = random_vector_expr(rng, t, depth - 1);
            Expr b = random_vector_expr(rng, t, depth - 1);
            return a - b;
        },
        // Min
        [&]() -> Expr {
            Expr a = random_vector_expr(rng, t, depth - 1);
            Expr b = random_vector_expr(rng, t, depth - 1);
            return min(a, b);
        },
        // Max
        [&]() -> Expr {
            Expr a = random_vector_expr(rng, t, depth - 1);
            Expr b = random_vector_expr(rng, t, depth - 1);
            internal_assert(a.type() == b.type()) << a << " " << b;
            return max(a, b);
        },
        // Select
        [&]() -> Expr {
            Expr a = random_vector_expr(rng, t, depth - 1);
            Expr b = random_vector_expr(rng, t, depth - 1);
            Expr c = random_vector_expr(rng, t, depth - 1);
            Expr cond = (a > b);
            return select(cond, a, c);
        },
        // Cast
        [&]() -> Expr {
            // Cast from a different type
            Type other = random_scalar_type(rng).with_lanes(t.lanes());
            while (other == t) {
                other = random_scalar_type(rng).with_lanes(t.lanes());
            }
            Expr e = random_vector_expr(rng, other, depth - 1);
            return Cast::make(t, e);
        },
        // Reinterpret (different bit width, changes lane count)
        [&]() -> Expr {
            int total_bits = t.bits() * t.lanes();
            // Pick a different bit width that divides the total bits evenly
            int bit_widths[] = {8, 16, 32, 64};
            vector<int> valid_widths;
            for (int bw : bit_widths) {
                if (total_bits % bw == 0) {
                    valid_widths.push_back(bw);
                }
            }
            // Should at least be able to preserve the existing bit width and change signedness.
            internal_assert(!valid_widths.empty());
            int other_bits = random_choice(rng, valid_widths);
            int other_lanes = total_bits / other_bits;
            Type other = ((rng() & 1) ? Int(other_bits) : UInt(other_bits)).with_lanes(other_lanes);
            Expr e = random_vector_expr(rng, other, depth - 1);
            return Reinterpret::make(t, e);
        },
        // Broadcast of sub-expression
        [&]() -> Expr {
            int f = random_factor(rng, t.lanes());
            Expr val = random_vector_expr(rng, t.with_lanes(f), depth - 1);
            return Broadcast::make(val, t.lanes() / f);
        },
        // Ramp
        [&]() -> Expr {
            int f = random_factor(rng, t.lanes());
            Type sub_t = t.with_lanes(f);
            Expr base = random_vector_expr(rng, sub_t, depth - 1);
            Expr stride = random_const(rng, sub_t);
            return Ramp::make(base, stride, t.lanes() / f);
        },
        // Shuffle (interleave)
        [&]() -> Expr {
            if (t.lanes() >= 4 && t.lanes() % 2 == 0) {
                int half = t.lanes() / 2;
                Expr a = random_vector_expr(rng, t.with_lanes(half), depth - 1);
                Expr b = random_vector_expr(rng, t.with_lanes(half), depth - 1);
                return Shuffle::make_interleave({a, b});
            }
            // Fall back to a simple expression
            return random_vector_expr(rng, t, depth - 1);
        },
        // Shuffle (concat)
        [&]() -> Expr {
            if (t.lanes() >= 4 && t.lanes() % 2 == 0) {
                int half = t.lanes() / 2;
                Expr a = random_vector_expr(rng, t.with_lanes(half), depth - 1);
                Expr b = random_vector_expr(rng, t.with_lanes(half), depth - 1);
                return Shuffle::make_concat({a, b});
            }
            return random_vector_expr(rng, t, depth - 1);
        },
        // Shuffle (slice)
        [&]() -> Expr {
            // Make a wider vector and slice it
            if (t.lanes() <= 8) {
                int wider = t.lanes() * 2;
                Expr e = random_vector_expr(rng, t.with_lanes(wider), depth - 1);
                // Slice: take every other element starting at 0 or 1
                int start = rng() & 1;
                return Shuffle::make_slice(e, start, 2, t.lanes());
            }
            return random_vector_expr(rng, t, depth - 1);
        },
        // VectorReduce (only when we can make it work with lane counts)
        [&]() -> Expr {
            // Input has more lanes, output has t.lanes() lanes
            // factor must divide input lanes, and input lanes = t.lanes() * factor
            int factor = (rng() % 3) + 2;
            int input_lanes = t.lanes() * factor;
            if (input_lanes <= 32) {
                VectorReduce::Operator ops[] = {
                    VectorReduce::Add,
                    VectorReduce::Min,
                    VectorReduce::Max,
                };
                auto op = random_choice(rng, ops);
                Expr val = random_vector_expr(rng, t.with_lanes(input_lanes), depth - 1);
                internal_assert(val.type().lanes() == input_lanes) << val;
                return VectorReduce::make(op, val, t.lanes());
            }
            return random_vector_expr(rng, t, depth - 1);
        },
        // Call node (using a pure intrinsic like absd)
        [&]() -> Expr {
            Expr a = random_vector_expr(rng, t, depth - 1);
            Expr b = random_vector_expr(rng, t, depth - 1);
            return cast(t, absd(a, b));
        },
    };

    Expr e = random_choice(rng, ops)();
    internal_assert(e.type() == t) << e.type() << " " << t << " " << e;
    return e;
}

// A custom lowering pass that replaces a specific dummy store RHS with the
// desired test expression. This lets us JIT-evaluate arbitrary vector Exprs.
class InjectExpr : public IRMutator {
    using IRMutator::visit;

    string func_name;
    const std::vector<Expr> &replacements;
    int idx = 0;

    Stmt visit(const Store *op) override {
        // Replace calls to our dummy function with the replacement expr
        internal_assert(idx < (int)replacements.size());
        if (op->name == func_name) {
            return Store::make(op->name, flatten_nested_ramps(replacements[idx++]),
                               op->index, op->param, op->predicate, op->alignment);
        }
        return IRMutator::visit(op);
    }

public:
    InjectExpr(const string &func_name, const std::vector<Expr> &replacements)
        : func_name(func_name), replacements(replacements) {
    }
};

// Evaluate a vector expression by JIT-compiling it. Returns the values
// as a vector of int64_t (to hold any integer type).
// The expression may reference variables a, b, c which are set to fixed values.
bool evaluate_vector_exprs(const std::vector<Expr> &e,
                           Buffer<int64_t> &result) {
    Type t = e[0].type();
    int lanes = t.lanes();

    // Create a Func that outputs a vector of the right size
    Func f("test_func");
    Var x("x"), y("y");

    // We define f(x, y) as a dummy, then inject our expressions via a custom
    // lowering pass
    Expr fuzz_var_sum = 0;
    for (int i = 0; i < fuzz_var_count; i++) {
        fuzz_var_sum += fuzz_vars[i];
    }
    f(x, y) = cast(t.element_of(), fuzz_var_sum);
    f.bound(x, 0, lanes)
        .bound(y, 0, (int)e.size())
        .vectorize(x)
        .unroll(y);

    // The custom lowering pass replaces the dummy RHS
    InjectExpr injector(f.name(), e);

    auto buf = Runtime::Buffer<>(t.element_of(), {lanes, (int)e.size()});

    Pipeline p(f);
    p.add_custom_lowering_pass(&injector, nullptr);
    p.realize(buf);

    // Upcast results to int64 for easier comparison
    internal_assert(result.height() == (int)e.size());
    internal_assert(result.width() == lanes);
    for (int y = 0; y < (int)e.size(); y++) {
        for (int x = 0; x < lanes; x++) {
            if (t.is_uint()) {
                switch (t.bits()) {
                case 8:
                    result(x, y) = buf.as<uint8_t>()(x, y);
                    break;
                case 16:
                    result(x, y) = buf.as<uint16_t>()(x, y);
                    break;
                case 32:
                    result(x, y) = buf.as<uint32_t>()(x, y);
                    break;
                case 64:
                    result(x, y) = buf.as<uint64_t>()(x, y);
                    break;
                default:
                    return false;
                }
            } else {
                switch (t.bits()) {
                case 8:
                    result(x, y) = buf.as<int8_t>()(x, y);
                    break;
                case 16:
                    result(x, y) = buf.as<int16_t>()(x, y);
                    break;
                case 32:
                    result(x, y) = buf.as<int32_t>()(x, y);
                    break;
                case 64:
                    result(x, y) = buf.as<int64_t>()(x, y);
                    break;
                default:
                    return false;
                }
            }
        }
    }

    return true;
}

template<typename T>
T initialize_rng() {
    constexpr size_t kStateWords = T::state_size * sizeof(typename T::result_type) / sizeof(uint32_t);
    vector<uint32_t> random(kStateWords);
    std::generate(random.begin(), random.end(), std::random_device{});
    std::seed_seq seed_seq(random.begin(), random.end());
    return T{seed_seq};
}

bool test_one(RandomEngine &rng) {
    // Pick a random vector width and type
    int lanes = std::uniform_int_distribution(4, 16)(rng);
    Type scalar_t = random_scalar_type(rng);
    Type t = scalar_t.with_lanes(lanes);

    // Pick random deinterleave parameters
    int starting_lane = std::uniform_int_distribution(0, lanes - 1)(rng);
    int ending_lane = std::uniform_int_distribution(0, lanes - 1)(rng);
    int new_lanes = std::abs(ending_lane - starting_lane) + 1;
    int lane_stride = std::uniform_int_distribution(1, new_lanes)(rng);
    // bias it towards small strides
    lane_stride = std::uniform_int_distribution(1, lane_stride)(rng);
    new_lanes /= lane_stride;
    if (starting_lane > ending_lane) {
        lane_stride = -lane_stride;
    }

    // Generate a batch of random vector expressions
    constexpr int batch_size = 32;
    constexpr int depth = 4;
    std::vector<Expr> original(batch_size);
    std::vector<Expr> sliced(batch_size);

    for (int i = 0; i < batch_size; i++) {
        original[i] = random_vector_expr(rng, t, depth);
        sliced[i] = extract_lanes(original[i], starting_lane, lane_stride, new_lanes);
        internal_assert(sliced[i].type() == scalar_t.with_lanes(new_lanes))
            << sliced[i].type() << " vs " << scalar_t.with_lanes(new_lanes);
    }

    // Pick random variable values
    for (int i = 0; i < fuzz_var_count; i++) {
        fuzz_vars[i].set((int)((int8_t)(rng() & 0x0f)));
    }

    // Evaluate both
    Buffer<int64_t> orig_vals({lanes, batch_size}), sliced_vals({new_lanes, batch_size});
    if (!evaluate_vector_exprs(original, orig_vals) ||
        !evaluate_vector_exprs(sliced, sliced_vals)) {
        // Can't evaluate this for whatever reason
        return true;
    }

    // Check that the sliced values match the corresponding lanes of the original
    for (int y = 0; y < batch_size; y++) {
        for (int x = 0; x < new_lanes; x++) {
            int orig_lane = starting_lane + x * lane_stride;
            if (sliced_vals(x, y) != orig_vals(orig_lane, y)) {
                std::cerr << "MISMATCH!\n"
                          << "Original expr: " << original[y] << "\n"
                          << "Original type: " << original[y].type() << "\n"
                          << "Deinterleave params: starting_lane=" << starting_lane
                          << " lane_stride=" << lane_stride
                          << " new_lanes=" << new_lanes << "\n"
                          << "Sliced expr: " << sliced[y] << "\n"
                          << "Variables:";
                for (int j = 0; j < fuzz_var_count; j++) {
                    std::cerr << " " << fuzz_vars[j].name() << "=" << fuzz_vars[j].get() << "\n";
                }
                std::cerr << "\n"
                          << "Original values:";
                for (int j = 0; j < lanes; j++) {
                    std::cerr << " " << orig_vals(j, y);
                }
                std::cerr << "\n"
                          << "Sliced values:";
                for (int j = 0; j < new_lanes; j++) {
                    std::cerr << " " << sliced_vals(j, y);
                }
                std::cerr << "\n";
                return false;
            }
        }
    }

    return true;
}

}  // namespace

int main(int argc, char **argv) {
    auto seed_generator = initialize_rng<RandomEngine>();

    int num_iters = (argc > 1) ? 1 : 32;

    for (int i = 0; i < num_iters; i++) {
        auto seed = seed_generator();
        if (argc > 1) {
            std::istringstream{argv[1]} >> seed;
        }
        std::cout << "Seed: " << seed << std::endl;
        RandomEngine rng{seed};

        if (!test_one(rng)) {
            std::cout << "Failed with seed " << seed << "\n";
            return 1;
        }
    }

    std::cout << "Success!\n";
    return 0;
}
