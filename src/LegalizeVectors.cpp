#include "LegalizeVectors.h"
#include "CSE.h"
#include "Deinterleave.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Util.h"

#include <unordered_set>
#include <vector>

namespace Halide {
namespace Internal {

namespace {

using namespace std;

int max_lanes_for_device(DeviceAPI api, int parent_max_lanes) {
    // The environment variable below (HL_FORCE_VECTOR_LEGALIZATION) is here solely for testing purposes.
    // It is useful to "stress-test" this lowering pass by forcing a shorter maximal vector size across
    // all codegen across the entire test suite. This should not be used in real uses of Halide.
    std::string envvar = Halide::Internal::get_env_variable("HL_FORCE_VECTOR_LEGALIZATION");
    if (!envvar.empty()) {
        return std::atoi(envvar.c_str());
    }
    // The remainder of this function correctly determines the number of lanes the device API supports.
    switch (api) {
    case DeviceAPI::Metal:
    case DeviceAPI::WebGPU:
    case DeviceAPI::Vulkan:
    case DeviceAPI::D3D12Compute:
        return 4;
    case DeviceAPI::OpenCL:
        return 16;
    case DeviceAPI::CUDA:
    case DeviceAPI::Hexagon:
    case DeviceAPI::HexagonDma:
    case DeviceAPI::Host:
        return 0;  // No max: LLVM based legalization
    case DeviceAPI::None:
        return parent_max_lanes;
    case DeviceAPI::Default_GPU:
        internal_error << "No GPU API was selected.";
        return 0;
    }
    internal_error << "Unknown Device API";
    return 0;
}

class LiftLetToLetStmt : public IRMutator {
    using IRMutator::visit;

    unordered_set<string> lifted_let_names;
    vector<const Let *> lets;
    Expr visit(const Let *op) override {
        internal_assert(lifted_let_names.count(op->name) == 0)
            << "Let " << op->name << " = ...  cannot be lifted to LetStmt because the name is not unique.";
        lets.push_back(op);
        lifted_let_names.insert(op->name);
        return mutate(op->body);
    }

public:
    Stmt mutate(const Stmt &s) override {
        ScopedValue<decltype(lets)> scoped_lets(lets, {});
        Stmt mutated = IRMutator::mutate(s);
        for (const Let *let : reverse_view(lets)) {
            mutated = LetStmt::make(let->name, let->value, mutated);
        }
        return mutated;
    }

    Expr mutate(const Expr &e) override {
        return IRMutator::mutate(e);
    }
};

class LiftExceedingVectors : public IRMutator {
    using IRMutator::visit;

    int max_lanes;

    vector<pair<string, Expr>> lets;
    bool just_in_let_definition{false};

    Expr visit(const Let *op) override {
        internal_error << "We don't want to process Lets. They should have all been converted to LetStmts.";
        return IRMutator::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        just_in_let_definition = true;
        Expr def = mutate(op->value);
        just_in_let_definition = false;

        Stmt body = mutate(op->body);
        if (def.same_as(op->value) && body.same_as(op->body)) {
            return op;
        }
        return LetStmt::make(op->name, std::move(def), std::move(body));
    }

    Expr visit(const Call *op) override {
        // Custom handling of Call, to prevent certain things from being extracted out
        // of the call arguments, as that's not always allowed.
        bool exceeds_lanecount = op->type.lanes() > max_lanes;
        Expr mutated = op;
        if (exceeds_lanecount) {
            std::vector<Expr> args;
            args.reserve(op->args.size());
            bool changed = false;
            for (int i = 0; i < int(op->args.size()); ++i) {
                bool may_extract = true;
                if (op->is_intrinsic(Call::require)) {
                    // Call::require is special: it behaves a little like if-then-else:
                    // it runs the 3rd argument (the error handling part) only when there
                    // is an error. Extracting that would unconditionally print the error.
                    may_extract &= i < 2;
                }
                if (op->is_intrinsic(Call::if_then_else)) {
                    // Only allow the condition to be extracted.
                    may_extract &= i == 0;
                }
                const Expr &arg = op->args[i];
                if (may_extract) {
                    internal_assert(arg.type().lanes() == op->type.lanes());
                    Expr mutated = mutate(arg);
                    if (!mutated.same_as(arg)) {
                        changed = true;
                    }
                    args.push_back(mutated);
                } else {
                    args.push_back(arg);
                }
            }
            if (!changed) {
                return op;
            }
            mutated = Call::make(op->type, op->name, args, op->call_type);
        } else {
            mutated = IRMutator::visit(op);
        }
        return mutated;
    }

public:
    Stmt mutate(const Stmt &s) override {
        ScopedValue<decltype(lets)> scoped_lets(lets, {});
        just_in_let_definition = false;
        Stmt mutated = IRMutator::mutate(s);
        for (auto &let : reverse_view(lets)) {
            // There is no recurse into let.second. This is handled by repeatedly calling this transform.
            mutated = LetStmt::make(let.first, let.second, mutated);
        }
        return mutated;
    }

    Expr mutate(const Expr &e) override {
        bool exceeds_lanecount = e.type().lanes() > max_lanes;

        if (exceeds_lanecount) {
            bool should_extract = false;
            should_extract |= e.node_type() == IRNodeType::Shuffle;
            should_extract |= e.node_type() == IRNodeType::VectorReduce;

            should_extract &= !just_in_let_definition;

            debug((should_extract ? 3 : 4)) << "Max lanes (" << max_lanes << ") exceeded (" << e.type().lanes() << ") by: " << e << "\n";
            if (should_extract) {
                std::string name = unique_name('t');
                Expr var = Variable::make(e.type(), name);
                lets.emplace_back(name, e);
                debug(3) << "  => Lifted out into " << name << "\n";
                return var;
            }
        }

        just_in_let_definition = false;
        return IRMutator::mutate(e);
    }

    LiftExceedingVectors(int max_lanes)
        : max_lanes(max_lanes) {
        internal_assert(max_lanes != 0) << "LiftExceedingVectors should not be called when there is no lane limit.";
    }
};

class LegalizeVectors : public IRMutator {
    using IRMutator::visit;

    int max_lanes;

    Scope<> sliceable_vectors;
    Scope<std::vector<VectorSlice>> requested_slices;

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        bool exceeds_lanecount = op->value.type().lanes() > max_lanes;

        if (exceeds_lanecount) {
            int num_vecs = (op->value.type().lanes() + max_lanes - 1) / max_lanes;
            debug(3) << "Legalize let " << op->value.type() << ": " << op->name
                     << " = " << op->value << " into " << num_vecs << " vecs\n";

            // First mark this Let as sliceable before mutating the body:
            ScopedBinding<> vector_is_slicable(sliceable_vectors, op->name);

            Stmt body = mutate(op->body);
            // Here we know which requested vector variable slices should be created for the body of the Let/LetStmt to work.

            if (std::vector<VectorSlice> *reqs = requested_slices.shallow_find(op->name)) {
                for (const VectorSlice &sl : *reqs) {
                    Expr value = extract_lanes(op->value, sl.start, sl.stride, sl.count, sliceable_vectors, requested_slices);
                    value = mutate(value);
                    body = LetOrLetStmt::make(sl.variable_name, value, body);
                    debug(3) << "  Add: let " << sl.variable_name << " = " << value << "\n";
                }
            }
            return body;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Let *op) override {
        // TODO is this still true?
        internal_error << "Lets should have been lifted into LetStmts.";
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        bool exceeds_lanecount = op->index.type().lanes() > max_lanes;
        if (exceeds_lanecount) {
            // Split up in multiple stores
            int num_vecs = (op->index.type().lanes() + max_lanes - 1) / max_lanes;
            std::vector<Stmt> assignments;
            assignments.reserve(num_vecs);
            for (int i = 0; i < num_vecs; ++i) {
                int lane_start = i * max_lanes;
                int lane_count_for_vec = std::min(op->value.type().lanes() - lane_start, max_lanes);

                Expr rhs = extract_lanes(op->value, lane_start, 1, lane_count_for_vec, sliceable_vectors, requested_slices);
                Expr index = extract_lanes(op->index, lane_start, 1, lane_count_for_vec, sliceable_vectors, requested_slices);
                Expr predictate = extract_lanes(op->predicate, lane_start, 1, lane_count_for_vec, sliceable_vectors, requested_slices);
                assignments.push_back(Store::make(
                    op->name, std::move(rhs), std::move(index),
                    op->param, std::move(predictate), op->alignment + lane_start));
            }
            Stmt result = Block::make(assignments);
            debug(3) << "Legalized store " << Stmt(op) << " => " << result << "\n";
            return result;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Shuffle *op) override {
        // Primary violatation: there are too many output lanes.
        if (op->type.lanes() > max_lanes) {
            // Break it down in multiple legal-output-length shuffles, and concatenate them back together.
            int total_lanes = op->type.lanes();

            std::vector<Expr> output_chunks;
            output_chunks.reserve((total_lanes + max_lanes - 1) / max_lanes);
            for (int i = 0; i < total_lanes; i += max_lanes) {
                int slice_len = std::min(max_lanes, total_lanes - i);

                std::vector<int> slice_indices(slice_len);
                for (int k = 0; k < slice_len; ++k) {
                    slice_indices[k] = op->indices[i + k];
                }

                Expr sub_shuffle = Shuffle::make(op->vectors, slice_indices);

                output_chunks.push_back(mutate(sub_shuffle));
            }
            return Shuffle::make_concat(output_chunks);
        }

        // Secondary violation: input vectors have too many lanes.
        bool requires_mutation = false;
        for (const auto &vec : op->vectors) {
            if (vec.type().lanes() > max_lanes) {
                requires_mutation = true;
                break;
            }
        }

        if (requires_mutation) {
            debug(4) << "Legalizing Shuffle " << Expr(op) << "\n";
            // We are dealing with a shuffle of an exceeding-lane-count vector argument.
            // We can assume the variable here has extracted lane variables in surrounding Lets.
            // So let's hope it's a simple case, and we can legalize.

            vector<Expr> new_vectors;
            vector<pair<int, int>> vector_and_lane_indices = op->vector_and_lane_indices();
            for (int i = 0; i < int(op->vectors.size()); ++i) {
                const Expr &vec = op->vectors[i];
                if (vec.type().lanes() > max_lanes) {
                    debug(4) << "  Arg " << i << ": " << vec << "\n";
                    int num_vecs = (vec.type().lanes() + max_lanes - 1) / max_lanes;
                    for (int i = 0; i < num_vecs; i++) {
                        int lane_start = i * max_lanes;
                        int lane_count_for_vec = std::min(vec.type().lanes() - lane_start, max_lanes);
                        new_vectors.push_back(extract_lanes(vec, lane_start, 1, lane_count_for_vec, sliceable_vectors, requested_slices));
                    }
                } else {
                    new_vectors.push_back(IRMutator::mutate(vec));
                }
            }
            Expr result = simplify(Shuffle::make(new_vectors, op->indices));
            debug(3) << "Legalized " << Expr(op) << " => " << result << "\n";
            return result;
        }

        // Base case: everything legal in this Shuffle
        return IRMutator::visit(op);
    }

    Expr make_binary_reduce_op(VectorReduce::Operator op, Expr a, Expr b) {
        switch (op) {
        case VectorReduce::Add:
            return a + b;
        case VectorReduce::SaturatingAdd:
            return saturating_add(a, b);
        case VectorReduce::Mul:
            return a * b;
        case VectorReduce::Min:
            return min(a, b);
        case VectorReduce::Max:
            return max(a, b);
        case VectorReduce::And:
            return a && b;
        case VectorReduce::Or:
            return a || b;
        default:
            internal_error << "Unknown VectorReduce operator\n";
            return Expr();
        }
    }

    Expr visit(const VectorReduce *op) override {
        // Written with the help of Gemini 3 Pro.
        Expr value = mutate(op->value);

        int input_lanes = value.type().lanes();
        int output_lanes = op->type.lanes();

        // Base case: we don't need legalization.
        if (input_lanes <= max_lanes && output_lanes <= max_lanes) {
            if (value.same_as(op->value)) {
                return op;
            } else {
                return VectorReduce::make(op->op, value, output_lanes);
            }
        }

        // Recursive splitting strategy.
        // Case A: Segmented Reduction (Multiple Output Lanes)
        // Example: VectorReduce( <16 lanes>, output_lanes=2 ) with max_lanes=4.
        // Input is too big. We split the OUTPUT domain.
        // We calculate which chunk of the input corresponds to the first half of the output.
        if (output_lanes > 1) {
            // 1. Calculate good splitting point
            int out_split = output_lanes / 2;

            // 2. However, do align to max_lanes to keep chunks native-sized if possible
            if (out_split > max_lanes) {
                out_split = (out_split / max_lanes) * max_lanes;
            } else if (output_lanes > max_lanes) {
                // If the total is > max, but half is < max (e.g. 6),
                // we want to peel 'max' (4) rather than split (3).
                out_split = max_lanes;
            }

            // Take remainder beyond the split point
            int out_remaining = output_lanes - out_split;
            internal_assert(out_remaining >= 1);

            // Calculate the reduction factor to find where to split the input
            // e.g., 16 input -> 2 output means factor is 8.
            // If we want the first 1 output lane, we need the first 8 input lanes.
            int reduction_factor = input_lanes / output_lanes;
            int in_split = out_split * reduction_factor;
            int in_remaining = input_lanes - in_split;

            Expr arg_lo = extract_lanes(value, 0, 1, in_split, sliceable_vectors, requested_slices);
            Expr arg_hi = extract_lanes(value, in_split, 1, in_remaining, sliceable_vectors, requested_slices);

            // Recursively mutate the smaller reductions
            Expr res_lo = mutate(VectorReduce::make(op->op, arg_lo, out_split));
            Expr res_hi = mutate(VectorReduce::make(op->op, arg_hi, out_remaining));

            // Concatenate the results to form the new vector
            return Shuffle::make_concat({res_lo, res_hi});
        }

        // Case B: Horizontal Reduction (Single Output Lane)
        // Example: VectorReduce( <16 lanes>, output_lanes=1 ) with max_lanes=4.
        // We cannot split the output. We must split the INPUT, reduce both halves
        // to scalars, and then combine them.
        if (output_lanes == 1) {
            int in_split = input_lanes / 2;
            int in_remaining = input_lanes - in_split;

            // Extract input halves
            Expr arg_lo = extract_lanes(value, 0, 1, in_split, sliceable_vectors, requested_slices);
            Expr arg_hi = extract_lanes(value, in_split, 1, in_remaining, sliceable_vectors, requested_slices);

            // Recursively reduce both halves to scalars
            Expr res_lo = mutate(VectorReduce::make(op->op, arg_lo, 1));
            Expr res_hi = mutate(VectorReduce::make(op->op, arg_hi, 1));

            // Combine using the standard binary operator for this reduction type
            return make_binary_reduce_op(op->op, res_lo, res_hi);
        }

        internal_error << "Unreachable";
        return op;
    }

public:
    LegalizeVectors(int max_lanes)
        : max_lanes(max_lanes) {
        internal_assert(max_lanes != 0) << "LegalizeVectors should not be called when there is no lane limit.";
    }
};

}  // namespace

Stmt legalize_vectors_in_device_loop(const For *op) {
    int max_lanes = max_lanes_for_device(op->device_api, 0);

    // Similar to CSE, lifting out stuff into variables.
    // Pass 1): lift out Shuffles that exceed lane count into variables
    // Pass 2): Rewrite those vector variables as bundles of vector variables, while legalizing all other stuff.
    Stmt m0 = simplify(op->body);
    Stmt m1 = common_subexpression_elimination(m0, false);
    if (!m1.same_as(op->body)) {
        debug(3) << "After CSE:\n"
                 << m1 << "\n";
    }
    Stmt m2 = LiftLetToLetStmt().mutate(m1);
    if (!m2.same_as(m1)) {
        debug(3) << "After lifting Lets to LetStmts:\n"
                 << m2 << "\n";
    }

    Stmt m3 = m2;
    while (true) {
        Stmt m = LiftExceedingVectors(max_lanes).mutate(m3);
        bool modified = !m3.same_as(m);
        m3 = std::move(m);
        if (!modified) {
            debug(3) << "Nothing got lifted out\n";
            break;
        } else {
            debug(3) << "Atfer lifting exceeding vectors:\n"
                     << m3 << "\n";
        }
    }

    Stmt m4 = LegalizeVectors(max_lanes).mutate(m3);
    if (!m4.same_as(m3)) {
        debug(3) << "After legalizing vectors:\n"
                 << m4 << "\n";
    }
    if (m4.same_as(m2)) {
        debug(3) << "Vector Legalization did do nothing, returning input.\n";
        return op;
    }
    Stmt m5 = simplify(m4);
    if (!m4.same_as(m5)) {
        debug(3) << "After simplify:\n"
                 << m5 << "\n";
    }
    return For::make(op->name, op->min, op->max, op->for_type,
                     op->partition_policy, op->device_api, m5);
}

Stmt legalize_vectors(const Stmt &s) {
    return mutate_with(s, [&](auto *self, const For *op) {
        if (max_lanes_for_device(op->device_api, 0)) {
            return legalize_vectors_in_device_loop(op);
        }
        return self->visit_base(op);
    });
}
}  // namespace Internal
}  // namespace Halide
