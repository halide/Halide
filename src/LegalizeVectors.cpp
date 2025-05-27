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

int max_lanes_for_device(DeviceAPI api, int parent_max_lanes) {
    switch (api) {
    case DeviceAPI::Metal:
    case DeviceAPI::WebGPU:
    case DeviceAPI::Vulkan:
    case DeviceAPI::D3D12Compute:
        return 4;
    case DeviceAPI::OpenCL:
        return 16;
    case DeviceAPI::CUDA:
    case DeviceAPI::Host:
        return 0;  // No max: LLVM based legalization
    case DeviceAPI::None:
        return parent_max_lanes;
    default:
        return 0;
    }
}

std::string vec_name(const string &name, int lane_start, int lane_count) {
    // return name + ".ls" + std::to_string(lane_start) + ".lc" + std::to_string(lane_count);
    return name + ".lanes_" + std::to_string(lane_start) + "_" + std::to_string(lane_start + lane_count - 1);
}

Expr simplify_shuffle(const Shuffle *op) {
    if (op->is_extract_element()) {
        if (op->vectors.size() == 1) {
            if (op->vectors[0].type().is_scalar()) {
                return op->vectors[0];
            } else {
                return Expr(op);
            }
        } else {
            // Figure out which element is comes from.
            int index = op->indices[0];
            internal_assert(index >= 0);
            for (const Expr &vector : op->vectors) {
                if (index < vector.type().lanes()) {
                    if (vector.type().is_scalar()) {
                        return vector;
                    } else {
                        return Shuffle::make_extract_element(vector, index);
                    }
                }
                index -= vector.type().lanes();
            }
            internal_error << "Index out of bounds.";
        }
    }

    // Figure out if all extracted lanes come from 1 component.
    vector<pair<int, int>> src_vec_and_lane_idx = op->vector_and_lane_indices();
    bool all_from_the_same = true;
    bool is_full_vec = src_vec_and_lane_idx[0].second == 0;
    for (int i = 1; i < op->indices.size(); ++i) {
        if (src_vec_and_lane_idx[i].first != src_vec_and_lane_idx[0].first) {
            all_from_the_same = false;
            is_full_vec = false;
            break;
        }
        if (src_vec_and_lane_idx[i].second != i) {
            is_full_vec = false;
        }
    }
    if (all_from_the_same) {
        const Expr &src_vec = op->vectors[src_vec_and_lane_idx[0].first];
        is_full_vec &= src_vec.type().lanes() == op->indices.size();
        int first_lane_in_src = src_vec_and_lane_idx[0].second;
        if (is_full_vec) {
            return src_vec;
        } else {
            const Ramp *ramp = src_vec.as<Ramp>();
            if (ramp && op->is_slice() && op->slice_stride() == 1) {
                return simplify(Ramp::make(ramp->base + first_lane_in_src * ramp->stride, ramp->stride, op->indices.size()));
            }
            vector<int> new_indices;
            for (int i = 0; i < op->indices.size(); ++i) {
                new_indices.push_back(src_vec_and_lane_idx[i].second);
            }
            return Shuffle::make({src_vec}, new_indices);
        }
    }

    return op;
}

class LiftLetToLetStmt : public IRMutator {
public:
    vector<const Let *> lets;
    Expr visit(const Let *op) override {
        lets.push_back(op);
        return mutate(op->body);
    }

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
    int lane_start;
    int lane_count;
    int max_legal_lanes;

    Expr extract_lanes_from_make_struct(const Call *op) {
        internal_assert(op);
        internal_assert(op->is_intrinsic(Call::make_struct));
        vector<Expr> args(op->args.size());
        for (int i = 0; i < op->args.size(); ++i) {
            args[i] = mutate(op->args[i]);
        }
        return Call::make(op->type, Call::make_struct, args, Call::Intrinsic);
    }

    Expr extract_lanes_trace(const Call *op) {
        // user_error << "Cannot legalize vectors when tracing is enabled.";
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
        } else {
            user_warning << "Discarding tracing during vector legalization: " << Expr(op) << "\n";
        }

        // This is feasible: see VectorizeLoops.
        return Expr(0);
    }

public:
    ExtractLanes(int start, int count, int max_legal)
        : lane_start(start), lane_count(count), max_legal_lanes(max_legal) {
    }

    Expr visit(const Shuffle *op) override {
        vector<int> new_indices;
        for (int i = 0; i < lane_count; ++i) {
            new_indices.push_back(op->indices[lane_start + i]);
        }
        Expr result = Shuffle::make(op->vectors, new_indices);
        return simplify_shuffle(result.as<Shuffle>());
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
        for (int i = 0; i < op->args.size(); ++i) {
            const Expr &arg = op->args[i];
            internal_assert(arg.type().lanes() == op->type.lanes()) << "Call argument " << arg << " lane count of " << arg.type().lanes() << " does not match op lane count of " << op->type.lanes();
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
                           << "not supported yet: reinterpret<" << result_type << ">(" << value.type() << ")";

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
        Expr arg = ExtractLanes(input_lane_start, input_lane_count, max_legal_lanes).mutate(op->value);
        // This might fail if the extracted lanes reference a non-existing variable!
        return VectorReduce::make(op->op, arg, lane_count);
    }

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
};

class LiftExceedingVectors : public IRMutator {
    int max_lanes{max_lanes_for_device(DeviceAPI::Host, 0)};

    vector<pair<string, Expr>> lets;
    map<Expr, Expr, ExprCompare> replacements;
    bool just_in_let_defintion{false};
    int in_strict_float = 0;

public:
    Stmt visit(const For *op) override {
        ScopedValue<int> scoped_max_lanes(max_lanes, max_lanes_for_device(op->device_api, max_lanes));
        return IRMutator::visit(op);
    }

    template<typename LetOrLetStmt>
    decltype(LetOrLetStmt::body) visit_let(const LetOrLetStmt *op) {
        ScopedValue<bool> scoped_just_in_let(just_in_let_defintion, true);
        Expr def = mutate(op->value);
        auto body = mutate(op->body);
        if (def.same_as(op->value) && body.same_as(op->body)) {
            return op;
        }
        return LetOrLetStmt::make(op->name, std::move(def), std::move(body));
    }

    Expr visit(const Let *op) override {
        internal_error << "We don't want to process Lets. They should have all been converted to LetStmts.";
        Expr def;
        {
            ScopedValue<bool> scoped_just_in_let(just_in_let_defintion, true);
            def = mutate(op->value);
        }
        auto body = mutate(op->body);
        if (def.same_as(op->value) && body.same_as(op->body)) {
            return op;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Stmt visit(const Store *op) override {
        bool exceeds_lanecount = max_lanes && op->index.type().lanes() > max_lanes;
        if (exceeds_lanecount) {
            Expr value = mutate(op->value);

            // Split up in multiple stores
            int num_vecs = (op->index.type().lanes() + max_lanes - 1) / max_lanes;
            std::vector<Stmt> assignments;
            assignments.reserve(num_vecs);
            for (int i = 0; i < num_vecs; ++i) {
                int lane_start = i * max_lanes;
                int lane_count_for_vec = std::min(op->value.type().lanes() - lane_start, max_lanes);
                Expr rhs = ExtractLanes(lane_start, lane_count_for_vec, max_lanes).mutate(value);
                Expr index = ExtractLanes(lane_start, lane_count_for_vec, max_lanes).mutate(op->index);
                Expr predictate = ExtractLanes(lane_start, lane_count_for_vec, max_lanes).mutate(op->predicate);
                assignments.push_back(Store::make(
                    op->name, std::move(rhs), std::move(index),
                    op->param, std::move(predictate), op->alignment + lane_start));
            }
            return Block::make(assignments);
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        bool exceeds_lanecount = max_lanes && op->type.lanes() > max_lanes;
        if (op->is_intrinsic(Call::strict_float)) {
            in_strict_float++;
        }
        Expr mutated = op;
        if (exceeds_lanecount) {
            std::vector<Expr> args;
            args.reserve(op->args.size());
            bool changed = false;
            for (int i = 0; i < op->args.size(); ++i) {
                bool may_extract = true;
                if (op->is_intrinsic(Call::require)) {
                    may_extract &= i < 2;
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
        if (op->is_intrinsic(Call::strict_float)) {
            in_strict_float--;
        }
        return mutated;
    }

    Stmt mutate(const Stmt &s) override {
        ScopedValue<decltype(lets)> scoped_lets(lets, {});
        ScopedValue<bool> scoped_just_in_let(just_in_let_defintion, false);
        Stmt mutated = IRMutator::mutate(s);
        for (auto &let : reverse_view(lets)) {
            // There is no recurse into let.second. This is handled by repeatedly calling this tranform.
            mutated = LetStmt::make(let.first, let.second, mutated);
        }
        return mutated;
    }

#if 0
    Stmt visit(const IfThenElse *op) override {
        debug(3) << "Visit IfThenElse: " << Stmt(op) << " with max lanes: " << max_lanes << "\n";
        Expr condition;
        decltype(lets) condition_lets;
        {
            ScopedValue<decltype(lets)> scoped_lets(lets, {});
            condition = mutate(op->condition);
            condition_lets = lets;
        }
        Stmt then_case, else_case;
        {
            ScopedValue<decltype(lets)> scoped_lets(lets, {});
            then_case = mutate(op->then_case);
            for (auto &let : reverse_view(lets)) {
                then_case = LetStmt::make(let.first, let.second, then_case);
            }
        }
        {
            ScopedValue<decltype(lets)> scoped_lets(lets, {});
            else_case = mutate(op->else_case);
            for (auto &let : reverse_view(lets)) {
                else_case = LetStmt::make(let.first, let.second, else_case);
            }
        }
        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        }
        Stmt mutated = IfThenElse::make(std::move(condition), std::move(then_case), std::move(else_case));
        for (auto &let : reverse_view(lets)) {
            mutated = LetStmt::make(let.first, let.second, mutated);
        }
        return mutated;
    }
#endif

    Expr mutate(const Expr &e) override {
        bool exceeds_lanecount = max_lanes && e.type().lanes() > max_lanes;

        if (exceeds_lanecount) {
            bool should_extract = true;
            should_extract &= e.node_type() != IRNodeType::Variable;
            should_extract &= e.node_type() != IRNodeType::Let;
            should_extract &= e.node_type() != IRNodeType::Broadcast;
            should_extract &= e.node_type() != IRNodeType::Ramp;
            should_extract &= e.node_type() != IRNodeType::Call;
            should_extract &= e.node_type() != IRNodeType::Add;
            should_extract &= e.node_type() != IRNodeType::Sub;
            should_extract &= e.node_type() != IRNodeType::Mul;
            should_extract &= e.node_type() != IRNodeType::Div;
            should_extract &= e.node_type() != IRNodeType::EQ;
            should_extract &= e.node_type() != IRNodeType::NE;
            should_extract &= e.node_type() != IRNodeType::LT;
            should_extract &= e.node_type() != IRNodeType::GT;
            should_extract &= e.node_type() != IRNodeType::GE;
            should_extract &= e.node_type() != IRNodeType::LE;

            // TODO: Handling of strict_float is not well done.
            // But at least it covers a few basic scenarios.
            // This should be redone once we overhaul strict_float.
            should_extract &= !in_strict_float;

            should_extract &= !just_in_let_defintion;

            debug((should_extract ? 3 : 4)) << "Max lanes (" << max_lanes << ") exceeded (" << e.type().lanes() << ") by: " << e << "\n";
            if (should_extract) {
                std::string name = unique_name('t');
                Expr var = Variable::make(e.type(), name);
                replacements[e] = var;
                lets.emplace_back(name, e);
                debug(3) << "  => Lifted out into " << name << "\n";
                return var;
            }
        }

        ScopedValue<bool> scoped_just_in_let(just_in_let_defintion, false);
        return IRMutator::mutate(e);
    }

public:
    LiftExceedingVectors() = default;
};

class LegalizeVectors : public IRMutator {
    int max_lanes{max_lanes_for_device(DeviceAPI::Host, 0)};

public:
    Stmt visit(const For *op) override {
        ScopedValue<int> scoped_max_lanes(max_lanes, max_lanes_for_device(op->device_api, max_lanes));
        return IRMutator::visit(op);
    }

    template<typename LetOrLetStmt>
    decltype(LetOrLetStmt::body) visit_let(const LetOrLetStmt *op) {
        bool exceeds_lanecount = max_lanes && op->value.type().lanes() > max_lanes;

        if (exceeds_lanecount) {
            int num_vecs = (op->value.type().lanes() + max_lanes - 1) / max_lanes;
            debug(3) << "Legalize let " << op->value.type() << ": " << op->name
                     << " = " << op->value << " into " << num_vecs << " vecs\n";
            decltype(LetOrLetStmt::body) body = IRMutator::mutate(op->body);
            for (int i = num_vecs - 1; i >= 0; --i) {
                int lane_start = i * max_lanes;
                int lane_count_for_vec = std::min(op->value.type().lanes() - lane_start, max_lanes);
                std::string name = vec_name(op->name, lane_start, lane_count_for_vec);

                Expr value = mutate(ExtractLanes(lane_start, lane_count_for_vec, max_lanes).mutate(op->value));

                debug(3) << "  Add: let " << name << " = " << value << "\n";
                body = LetOrLetStmt::make(name, value, body);
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
        internal_error << "Lets should have been lifted into letStmts.";
        return visit_let(op);
    }

    Expr visit(const Shuffle *op) override {
        if (max_lanes == 0) {
            return IRMutator::visit(op);
        }
        internal_assert(op->type.lanes() <= max_lanes) << Expr(op);
        bool requires_mutation = false;
        for (int i = 0; i < op->vectors.size(); ++i) {
            if (op->vectors[i].type().lanes() > max_lanes) {
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
            for (int i = 0; i < op->vectors.size(); ++i) {
                const Expr &vec = op->vectors[i];
                if (vec.type().lanes() > max_lanes) {
                    debug(4) << "  Arg " << i << ": " << vec << "\n";
                    int num_vecs = (vec.type().lanes() + max_lanes - 1) / max_lanes;
                    for (int i = 0; i < num_vecs; i++) {
                        int lane_start = i * max_lanes;
                        int lane_count_for_vec = std::min(vec.type().lanes() - lane_start, max_lanes);
                        new_vectors.push_back(ExtractLanes(lane_start, lane_count_for_vec, max_lanes).mutate(vec));
                    }
                } else {
                    new_vectors.push_back(IRMutator::mutate(vec));
                }
            }
            Expr result = Shuffle::make(new_vectors, op->indices);
            result = simplify_shuffle(result.as<Shuffle>());
            debug(3) << "Legalized " << Expr(op) << " => " << result << "\n";
            return result;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const VectorReduce *op) override {
        if (max_lanes == 0) {
            return IRMutator::visit(op);
        }
        const Expr &arg = op->value;
        if (arg.type().lanes() > max_lanes) {
            int vecs_per_reduction = op->value.type().lanes() / op->type.lanes();
            if (vecs_per_reduction % max_lanes == 0) {
                // This should be possible too. TODO
            }

            internal_assert(op->type.lanes() == 1) << "Vector legalization currently does not support VectorReduce with lanes != 1: " << Expr(op);
            int num_vecs = (arg.type().lanes() + max_lanes - 1) / max_lanes;
            Expr result;
            for (int i = 0; i < num_vecs; i++) {
                int lane_start = i * max_lanes;
                int lane_count_for_vec = std::min(arg.type().lanes() - lane_start, max_lanes);
                Expr partial_arg = mutate(ExtractLanes(lane_start, lane_count_for_vec, max_lanes).mutate(arg));
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
};

}  // namespace

Stmt legalize_vectors(const Stmt &s) {
    // Similar to CSE, lifting out stuff into variables.
    // Pass 1): lift out vectors that exceed lane count into variables
    // Pass 2): Rewrite those vector variables as bundles of vector variables.
    Stmt m0 = simplify(s);
    Stmt m1 = common_subexpression_elimination(m0, false);
    if (!m1.same_as(s)) {
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
        Stmt m = LiftExceedingVectors().mutate(m3);
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

    Stmt m4 = LegalizeVectors().mutate(m3);
    if (!m4.same_as(m3)) {
        debug(3) << "After legalizing vectors:\n"
                 << m3 << "\n";
    }
    if (m4.same_as(m2)) {
        debug(3) << "Vector Legalization did do nothing, returning input.\n";
        return s;
    }
    return simplify(m4);
}
}  // namespace Internal
}  // namespace Halide
