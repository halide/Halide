#include "LegalizeVectors.h"
#include "CSE.h"
#include "Deinterleave.h"
#include "DeviceInterface.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Simplify.h"
#include "Util.h"

#include <optional>

namespace Halide {
namespace Internal {

namespace {

using namespace std;

const char *legalization_error_guide = "\n(This issue can most likely be resolved by reducing lane count for vectorize() calls in the schedule, or disabling it.)";

int max_lanes_for_device(DeviceAPI api, int parent_max_lanes) {
    std::string envvar = Halide::Internal::get_env_variable("HL_FORCE_VECTOR_LEGALIZATION");
    if (!envvar.empty()) {
        return std::atoi(envvar.c_str());
    }
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

std::string vec_name(const string &name, int lane_start, int lane_count) {
    return name + ".lanes_" + std::to_string(lane_start) + "_" + std::to_string(lane_start + lane_count - 1);
}

class LiftLetToLetStmt : public IRMutator {
    using IRMutator::visit;

    vector<const Let *> lets;
    Expr visit(const Let *op) override {
        for (const Let *existing : lets) {
            internal_assert(existing->name != op->name)
                << "Let " << op->name << " = ...  cannot be lifted to LetStmt because the name is not unique.";
        }
        lets.push_back(op);
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

class ExtractLanes : public IRMutator {
    using IRMutator::visit;

    int lane_start;
    int lane_count;

    Expr extract_lanes_from_make_struct(const Call *op) {
        internal_assert(op);
        internal_assert(op->is_intrinsic(Call::make_struct));
        vector<Expr> args(op->args.size());
        for (int i = 0; i < int(op->args.size()); ++i) {
            args[i] = mutate(op->args[i]);
        }
        return Call::make(op->type, Call::make_struct, args, Call::Intrinsic);
    }

    Expr extract_lanes_trace(const Call *op) {
        auto event = as_const_int(op->args[6]);
        internal_assert(event);
        if (*event == halide_trace_load || *event == halide_trace_store) {
            debug(3) << "Extracting Trace Lanes: " << Expr(op) << "\n";
            const Expr &func = op->args[0];
            Expr values = extract_lanes_from_make_struct(op->args[1].as<Call>());
            Expr coords = extract_lanes_from_make_struct(op->args[2].as<Call>());
            const Expr &type_code = op->args[3];
            const Expr &type_bits = op->args[4];
            int type_lanes = *as_const_int(op->args[5]);
            const Expr &event = op->args[6];
            const Expr &parent_id = op->args[7];
            const Expr &idx = op->args[8];
            int size = *as_const_int(op->args[9]);
            const Expr &tag = op->args[10];

            int num_vecs = op->args[2].as<Call>()->args.size();
            internal_assert(size == type_lanes * num_vecs) << Expr(op);
            vector<Expr> args = {
                func,
                values, coords,
                type_code, type_bits, Expr(lane_count),
                event, parent_id, idx, Expr(lane_count * num_vecs),
                tag};
            Expr result = Call::make(Int(32), Call::trace, args, Call::Extern);
            debug(4) << "  => " << result << "\n";
            return result;
        }

        internal_error << "Unhandled trace call in LegalizeVectors' ExtractLanes: " << *event << legalization_error_guide << "\n"
                       << "Please report this error on GitHub." << legalization_error_guide;
        return Expr(0);
    }

    Expr visit(const Shuffle *op) override {
        vector<int> new_indices;
        new_indices.reserve(lane_count);
        for (int i = 0; i < lane_count; ++i) {
            new_indices.push_back(op->indices[lane_start + i]);
        }
        return simplify(Shuffle::make(op->vectors, new_indices));
    }

    Expr visit(const Ramp *op) override {
        if (lane_count == 1) {
            return simplify(op->base + op->stride * lane_start);
        }
        return simplify(Ramp::make(op->base + op->stride * lane_start, op->stride, lane_count));
    }

    Expr visit(const Broadcast *op) override {
        Expr value = op->value;
        if (const Call *call = op->value.as<Call>()) {
            if (call->name == Call::trace) {
                value = extract_lanes_trace(call);
            }
        }
        if (lane_count == 1) {
            return value;
        } else {
            return Broadcast::make(value, lane_count);
        }
    }

    Expr visit(const Variable *op) override {
        return Variable::make(op->type.with_lanes(lane_count), vec_name(op->name, lane_start, lane_count));
    }

    Expr visit(const Load *op) override {
        return Load::make(op->type.with_lanes(lane_count),
                          op->name,
                          mutate(op->index),
                          op->image, op->param,
                          mutate(op->predicate),
                          op->alignment + lane_start);
    }

    Expr visit(const Call *op) override {
        internal_assert(op->type.lanes() >= lane_start + lane_count);
        Expr mutated = op;
        std::vector<Expr> args;
        args.reserve(op->args.size());
        for (int i = 0; i < int(op->args.size()); ++i) {
            const Expr &arg = op->args[i];
            internal_assert(arg.type().lanes() == op->type.lanes())
                << "Call argument " << arg << " lane count of " << arg.type().lanes()
                << " does not match op lane count of " << op->type.lanes();
            Expr mutated = mutate(arg);
            internal_assert(!mutated.same_as(arg));
            args.push_back(mutated);
        }
        mutated = Call::make(op->type.with_lanes(lane_count), op->name, args, op->call_type);
        return mutated;
    }

    Expr visit(const Cast *op) override {
        return Cast::make(op->type.with_lanes(lane_count), mutate(op->value));
    }

    Expr visit(const Reinterpret *op) override {
        Type result_type = op->type.with_lanes(lane_count);
        int result_scalar_bits = op->type.element_of().bits();
        int input_scalar_bits = op->value.type().element_of().bits();

        Expr value = op->value;
        // If the bit widths of the scalar elements are the same, it's easy.
        if (result_scalar_bits == input_scalar_bits) {
            value = mutate(value);
        } else {
            // Otherwise, there can be two limiting aspects: the input lane count and the resulting lane count.
            // In order to construct a correct Reinterpret from a small type to a wider type, we
            // will need to produce multiple Reinterprets, all able to hold the lane count of the input
            // and concatate the results together.
            // Even worse, reinterpreting uint8x8 to uint64 would require intermediate reinterprets
            // if the maximul legal vector length is 4.
            //
            // TODO implement this for all scenarios
            internal_error << "Vector legalization for Reinterpret to different bit size per element is "
                           << "not supported yet: reinterpret<" << op->type << ">(" << value.type() << ")"
                           << legalization_error_guide;

            // int input_lane_start = lane_start * result_scalar_bits / input_scalar_bits;
            // int input_lane_count = lane_count * result_scalar_bits / input_scalar_bits;
        }
        Expr result = Reinterpret::make(result_type, value);
        debug(3) << "Legalized " << Expr(op) << " to " << result << "\n";
        return result;
    }

    Expr visit(const VectorReduce *op) override {
        internal_assert(op->type.lanes() >= lane_start + lane_count);
        int vecs_per_reduction = op->value.type().lanes() / op->type.lanes();
        int input_lane_start = vecs_per_reduction * lane_start;
        int input_lane_count = vecs_per_reduction * lane_count;
        Expr arg = ExtractLanes(input_lane_start, input_lane_count).mutate(op->value);
        // This might fail if the extracted lanes reference a non-existing variable!
        return VectorReduce::make(op->op, arg, lane_count);
    }

public:
    // Small helper to assert the transform did what it's supposed to do.
    Expr mutate(const Expr &e) override {
        Type original_type = e.type();
        internal_assert(original_type.lanes() >= lane_start + lane_count)
            << "Cannot extract lanes " << lane_start << " through " << lane_start + lane_count - 1
            << " when the input type is " << original_type;
        Expr result = IRMutator::mutate(e);
        Type new_type = result.type();
        internal_assert(new_type.lanes() == lane_count)
            << "We didn't correctly legalize " << e << " of type " << original_type << ".\n"
            << "Got back: " << result << " of type " << new_type << ", expected " << lane_count << " lanes.";
        return result;
    }

    Stmt mutate(const Stmt &s) override {
        return IRMutator::mutate(s);
    }

    ExtractLanes(int start, int count)
        : lane_start(start), lane_count(count) {
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
            // There is no recurse into let.second. This is handled by repeatedly calling this tranform.
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

    Stmt visit(const LetStmt *op) override {
        bool exceeds_lanecount = op->value.type().lanes() > max_lanes;

        if (exceeds_lanecount) {
            int num_vecs = (op->value.type().lanes() + max_lanes - 1) / max_lanes;
            debug(3) << "Legalize let " << op->value.type() << ": " << op->name
                     << " = " << op->value << " into " << num_vecs << " vecs\n";
            Stmt body = IRMutator::mutate(op->body);
            for (int i = num_vecs - 1; i >= 0; --i) {
                int lane_start = i * max_lanes;
                int lane_count_for_vec = std::min(op->value.type().lanes() - lane_start, max_lanes);
                std::string name = vec_name(op->name, lane_start, lane_count_for_vec);

                Expr value = mutate(ExtractLanes(lane_start, lane_count_for_vec).mutate(op->value));

                debug(3) << "  Add: let " << name << " = " << value << "\n";
                body = LetStmt::make(name, value, body);
            }
            return body;
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Let *op) override {
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
                Expr rhs = ExtractLanes(lane_start, lane_count_for_vec).mutate(op->value);
                Expr index = ExtractLanes(lane_start, lane_count_for_vec).mutate(op->index);
                Expr predictate = ExtractLanes(lane_start, lane_count_for_vec).mutate(op->predicate);
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
        internal_assert(op->type.lanes() <= max_lanes) << Expr(op);
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
                        new_vectors.push_back(ExtractLanes(lane_start, lane_count_for_vec).mutate(vec));
                    }
                } else {
                    new_vectors.push_back(IRMutator::mutate(vec));
                }
            }
            Expr result = simplify(Shuffle::make(new_vectors, op->indices));
            debug(3) << "Legalized " << Expr(op) << " => " << result << "\n";
            return result;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        const Expr &arg = op->value;
        if (arg.type().lanes() > max_lanes) {
            // TODO: The transformation below is not allowed under strict_float, but
            // I don't immediately know what to do here.
            // This should be an internal_assert.

            internal_assert(op->type.lanes() == 1)
                << "Vector legalization currently does not support VectorReduce with lanes != 1: " << Expr(op)
                << legalization_error_guide;
            int num_vecs = (arg.type().lanes() + max_lanes - 1) / max_lanes;
            Expr result;
            for (int i = 0; i < num_vecs; i++) {
                int lane_start = i * max_lanes;
                int lane_count_for_vec = std::min(arg.type().lanes() - lane_start, max_lanes);
                Expr partial_arg = mutate(ExtractLanes(lane_start, lane_count_for_vec).mutate(arg));
                Expr partial_red = VectorReduce::make(op->op, std::move(partial_arg), op->type.lanes());
                if (i == 0) {
                    result = partial_red;
                } else {
                    switch (op->op) {
                    case VectorReduce::Add:
                        result = result + partial_red;
                        break;
                    case VectorReduce::SaturatingAdd:
                        result = saturating_add(result, partial_red);
                        break;
                    case VectorReduce::Mul:
                        result = result * partial_red;
                        break;
                    case VectorReduce::Min:
                        result = min(result, partial_red);
                        break;
                    case VectorReduce::Max:
                        result = max(result, partial_red);
                        break;
                    case VectorReduce::And:
                        result = result && partial_red;
                        break;
                    case VectorReduce::Or:
                        result = result || partial_red;
                        break;
                    }
                }
            }
            return result;
        } else {
            return IRMutator::visit(op);
        }
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
    class LegalizeDeviceLoops : public IRMutator {
        using IRMutator::visit;
        Stmt visit(const For *op) override {
            if (max_lanes_for_device(op->device_api, 0)) {
                return legalize_vectors_in_device_loop(op);
            } else {
                return IRMutator::visit(op);
            }
        }
    } mutator;
    return mutator.mutate(s);
}
}  // namespace Internal
}  // namespace Halide
