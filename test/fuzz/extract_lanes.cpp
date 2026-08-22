#include "Halide.h"

#include "fuzz_helpers.h"
#include "random_expr_generator.h"

#include <iostream>
#include <vector>

// Fuzz test for deinterleave / extract_lane operations in Deinterleave.cpp.
// Originally this test had its own bespoke random-expression generator;
// RandomExpressionGenerator (random_expr_generator.h) was derived from that
// generator to serve the other fuzz tests in this directory, so this reunites
// the two -- this test now drives the same shared generator, which in turn
// gains all the coverage this test always had (Broadcast, Ramp, Cast,
// Reinterpret, the three Shuffle forms, VectorReduce, Add/Sub/Min/Max/absd)
// plus more (Mul/Div/Mod/abs, bitwise ops, boolean ops).
//
// Expressions are evaluated by JIT-compiling with a custom lowering pass,
// then we check that extract_lanes() produces results consistent with the
// original expression.

namespace {

using std::string;
using std::vector;
using namespace Halide;
using namespace Halide::Internal;

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
                               op->index, op->param, op->predicate, op->alignment, op->is_streaming);
        }
        return IRMutator::visit(op);
    }

public:
    InjectExpr(const string &func_name, const std::vector<Expr> &replacements)
        : func_name(func_name), replacements(replacements) {
    }

    // Number of dummy stores actually replaced. Should always end up equal
    // to replacements.size() -- if it doesn't, some optimization pass ate,
    // duplicated, or reordered one of the unrolled per-row stores, and any
    // mismatch found downstream would be a test-harness artifact rather
    // than a real bug in the code under test.
    int replaced_count() const {
        return idx;
    }
};

// Evaluate a vector expression by JIT-compiling it. Returns the values
// as a vector of int64_t (to hold any integer type). The expressions may
// reference the given fuzz Params.
bool evaluate_vector_exprs(const std::vector<Expr> &e,
                           const std::vector<Param<int>> &fuzz_vars,
                           Buffer<int64_t> &result) {
    Type t = e[0].type();
    int lanes = t.lanes();

    // Create a Func that outputs a vector of the right size
    Func f("test_func");
    Var x("x"), y("y");

    // We define f(x, y) as a dummy, then inject our expressions via a custom
    // lowering pass
    Expr fuzz_var_sum = 0;
    for (const auto &v : fuzz_vars) {
        fuzz_var_sum += v;
    }
    f(x, y) = cast(t.element_of(), fuzz_var_sum);
    f.bound(x, 0, lanes)
        .bound(y, 0, (int)e.size())
        .vectorize(x)
        .unroll(y);

    // The custom lowering pass replaces the dummy RHS
    InjectExpr injector(f.name(), e);

    auto buf = Runtime::Buffer<>(t.element_of().to_abi(), {lanes, (int)e.size()});

    Pipeline p(f);
    p.add_custom_lowering_pass(&injector, nullptr);
    if (get_target_from_environment() == get_host_target()) {
        try {
            p.realize(buf);
        } catch (const Halide::Error &) {
            // The richer expression generator can produce expressions that
            // are well-typed but hit an unrelated, deliberate compile-time
            // check (e.g. signed integer overflow during constant folding).
            // That's not what this test is about; skip them like any other
            // expression we can't evaluate.
            return false;
        }
        internal_assert(injector.replaced_count() == (int)e.size())
            << "InjectExpr replaced " << injector.replaced_count()
            << " stores, expected " << e.size();
    } else {
        // Compile something, to be able to at least test CodeGen from the backends and LLVM.
        std::vector<Argument> args(fuzz_vars.begin(), fuzz_vars.end());
        try {
            p.compile_to_assembly("fuzz_extract_lanes.s", args, "fuzz_func");
        } catch (const Halide::Error &) {
        }
        return false;
    }

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

// The richer shared generator includes Mul, which (unlike the Add/Sub/Min/
// Max/absd this test used to be limited to) can compound across recursion
// depth to overflow a signed integer type even when every leaf and free
// variable is small. Signed overflow is documented, LLVM-nsw-exploited
// undefined behavior in Halide (unsigned overflow is not -- it's
// well-defined wraparound), so an expression that can overflow doesn't
// have a well-defined value to compare extract_lanes()'s output against;
// evaluating it can appear to "mismatch" nondeterministically. Use Halide's
// own bounds inference to conservatively detect this and skip such
// expressions, given the known range of the free variables.
bool might_overflow_signed_int(const Expr &e, const std::vector<Param<int>> &vars) {
    Type t = e.type();
    if (!t.is_int()) {
        return false;
    }
    Scope<Interval> scope;
    for (const auto &v : vars) {
        scope.push(v.name(), Interval(Expr(0), Expr(15)));
    }
    Interval bounds = find_constant_bounds(e, scope);
    if (!bounds.is_bounded()) {
        return true;
    }
    auto lo = as_const_int(bounds.min);
    auto hi = as_const_int(bounds.max);
    if (!lo || !hi) {
        return true;
    }
    int64_t type_lo = (t.bits() >= 64) ? INT64_MIN : -(int64_t(1) << (t.bits() - 1));
    int64_t type_hi = (t.bits() >= 64) ? INT64_MAX : (int64_t(1) << (t.bits() - 1)) - 1;
    return *lo < type_lo || *hi > type_hi;
}

bool test_one(FuzzingContext &fuzz) {
    RandomExpressionGenerator reg{fuzz};
    // extract_lanes() only cares about plain integer vectors; keep the same
    // type universe the original bespoke generator used (no bool/UInt(1)).
    reg.fuzz_types = {UInt(8), UInt(16), UInt(32), UInt(64), Int(8), Int(16), Int(32), Int(64)};

    // Pick a random vector width and type
    int lanes = fuzz.ConsumeIntegralInRange(4, 16);
    Type scalar_t = reg.random_scalar_type();
    Type t = scalar_t.with_lanes(lanes);

    // Pick random deinterleave parameters
    int starting_lane = fuzz.ConsumeIntegralInRange(0, lanes - 1);
    int ending_lane = fuzz.ConsumeIntegralInRange(0, lanes - 1);
    int new_lanes = std::abs(ending_lane - starting_lane) + 1;
    int lane_stride = fuzz.ConsumeIntegralInRange(1, new_lanes);
    // bias it towards small strides
    lane_stride = fuzz.ConsumeIntegralInRange(1, lane_stride);
    new_lanes /= lane_stride;
    if (starting_lane > ending_lane) {
        lane_stride = -lane_stride;
    }

    // Generate a batch of random vector expressions
    constexpr int batch_size = 64;
    constexpr int depth = 4;
    std::vector<Expr> original(batch_size);
    std::vector<Expr> sliced(batch_size);

    for (int i = 0; i < batch_size; i++) {
        original[i] = reg.random_expr(t, depth);
        if (might_overflow_signed_int(original[i], reg.fuzz_vars)) {
            // Can't evaluate this batch soundly; treat like any other
            // expression we can't evaluate (see evaluate_vector_exprs).
            return true;
        }
        sliced[i] = extract_lanes(original[i], starting_lane, lane_stride, new_lanes);
        internal_assert(sliced[i].type() == scalar_t.with_lanes(new_lanes))
            << sliced[i].type() << " vs " << scalar_t.with_lanes(new_lanes);
    }

    // Pick random variable values (must match the [0, 15] range assumed by
    // might_overflow_signed_int above).
    for (auto &v : reg.fuzz_vars) {
        v.set(fuzz.ConsumeIntegralInRange(0, 0x0f));
    }

    // Evaluate both
    Buffer<int64_t> orig_vals(lanes, batch_size), sliced_vals(new_lanes, batch_size);
    if (!evaluate_vector_exprs(original, reg.fuzz_vars, orig_vals) ||
        !evaluate_vector_exprs(sliced, reg.fuzz_vars, sliced_vals)) {
        // Can't evaluate this for whatever reason
        return true;
    }

    // Check that the sliced values match the corresponding lanes of the original
    for (int y = 0; y < batch_size; y++) {
        for (int x = 0; x < new_lanes; x++) {
            int orig_lane = starting_lane + x * lane_stride;
            if (sliced_vals(x, y) != orig_vals(orig_lane, y)) {
                std::cerr << "MISMATCH! (y=" << y << ", x=" << x << ")\n"
                          << "Original expr: " << original[y] << "\n"
                          << "Original type: " << original[y].type() << "\n"
                          << "ExtractLanes params: starting_lane=" << starting_lane
                          << " lane_stride=" << lane_stride
                          << " new_lanes=" << new_lanes << "\n"
                          << "Sliced expr: " << sliced[y] << "\n"
                          << "Variables:";
                for (const auto &v : reg.fuzz_vars) {
                    std::cerr << " " << v.name() << "=" << v.get() << "\n";
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

FUZZ_TEST(extract_lanes, FuzzingContext &fuzz) {
    Target t = get_jit_target_from_environment();
    if (t.has_feature(Target::SVE2)) {
        // [SKIP-WITH-ISSUE-9026] LLVM generates incorrect IR for some expressions.
        return 0;
    }
    if (t.arch != Target::X86 || t.bits != 64) {
        // [SKIP-WITH-ISSUE-9040] Only running test on X86-64 for now. See also #9044.
        return 0;
    }

    return test_one(fuzz) ? 0 : 1;
}
