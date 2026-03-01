#include "Deinterleave.h"

#include "CSE.h"
#include "Debug.h"
#include "FlattenNestedRamps.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::pair;

std::string variable_name_with_extracted_lanes(
    const std::string &varname, int varlanes,
    int starting_lane, int lane_stride, int new_lanes) {

    if (lane_stride * new_lanes == varlanes) {
        if (starting_lane == 0 && lane_stride == 2) {
            return varname + ".even_lanes";
        } else if (starting_lane == 1 && lane_stride == 2) {
            return varname + ".odd_lanes";
        }
    }
    if (lane_stride == 1) {
        return varname + ".lanes_" + std::to_string(starting_lane) +
               "_to_" + std::to_string(starting_lane + new_lanes - 1);
    } else {
        // Just specify the slice
        std::string name = varname;
        name += ".slice_";
        name += std::to_string(starting_lane);
        name += "_";
        name += std::to_string(lane_stride);
        name += "_";
        name += std::to_string(new_lanes);
        return name;
    }
}

namespace {

class StoreCollector : public IRMutator {
public:
    const std::string store_name;
    const int store_stride, max_stores;
    std::vector<Stmt> &let_stmts;
    std::vector<Stmt> &stores;

    StoreCollector(const std::string &name, int stride, int ms,
                   std::vector<Stmt> &lets, std::vector<Stmt> &ss)
        : store_name(name), store_stride(stride), max_stores(ms),
          let_stmts(lets), stores(ss) {
    }

private:
    using IRMutator::visit;

    // Don't enter any inner constructs for which it's not safe to pull out stores.
    Stmt visit(const For *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const IfThenElse *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const ProducerConsumer *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const Allocate *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const Realize *op) override {
        collecting = false;
        return op;
    }

    bool collecting = true;
    // These are lets that we've encountered since the last collected
    // store. If we collect another store, these "potential" lets
    // become lets used by the collected stores.
    std::vector<Stmt> potential_lets;

    Expr visit(const Load *op) override {
        if (!collecting) {
            return op;
        }

        // If we hit a load from the buffer we're trying to collect
        // stores for, stop collecting to avoid reordering loads and
        // stores from the same buffer.
        if (op->name == store_name) {
            collecting = false;
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (!collecting) {
            return op;
        }

        // By default, do nothing.
        Stmt stmt = op;

        if (stores.size() >= (size_t)max_stores) {
            // Already have enough stores.
            collecting = false;
            return stmt;
        }

        // Make sure this Store doesn't do anything that causes us to
        // stop collecting.
        stmt = IRMutator::visit(op);
        if (!collecting) {
            return stmt;
        }

        if (op->name != store_name) {
            // Not a store to the buffer we're looking for.
            return stmt;
        }

        const Ramp *r = op->index.as<Ramp>();
        if (!r || !is_const(r->stride, store_stride)) {
            // Store doesn't store to the ramp we're looking
            // for. Can't interleave it. Since we don't want to
            // reorder stores, stop collecting.
            collecting = false;
            return stmt;
        }

        // This store is good, collect it and replace with a no-op.
        stores.emplace_back(op);
        stmt = Evaluate::make(0);

        // Because we collected this store, we need to save the
        // potential lets since the last collected store.
        let_stmts.insert(let_stmts.end(), potential_lets.begin(), potential_lets.end());
        potential_lets.clear();
        return stmt;
    }

    Expr visit(const Call *op) override {
        if (!op->is_pure()) {
            // Avoid reordering calls to impure functions
            collecting = false;
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (!collecting) {
            return op;
        }

        // Walk inside the let chain
        Stmt stmt = IRMutator::visit(op);

        // If we're still collecting, we need to save the entire let chain as potential lets.
        if (collecting) {
            Stmt body;
            do {
                potential_lets.emplace_back(op);
                body = op->body;
            } while ((op = body.as<LetStmt>()));
        }
        return stmt;
    }

    Stmt visit(const Block *op) override {
        if (!collecting) {
            return op;
        }

        Stmt first = mutate(op->first);
        Stmt rest = op->rest;
        // We might have decided to stop collecting during mutation of first.
        if (collecting) {
            rest = mutate(rest);
        }
        return Block::make(first, rest);
    }
};

Stmt collect_strided_stores(const Stmt &stmt, const std::string &name, int stride, int max_stores,
                            std::vector<Stmt> lets, std::vector<Stmt> &stores) {

    StoreCollector collect(name, stride, max_stores, lets, stores);
    return collect.mutate(stmt);
}

class ExtractLanes : public IRGraphMutator {
public:
    ExtractLanes(
        int starting_lane, int lane_stride, int new_lanes,
        const Scope<> &sliceable_lets,
        Scope<std::vector<VectorSlice>> &requested_slices)
        : starting_lane(starting_lane),
          lane_stride(lane_stride),
          new_lanes(new_lanes),
          sliceable_lets(sliceable_lets),
          requested_slices(requested_slices) {
    }

private:
    int starting_lane;
    int lane_stride;
    int new_lanes;

    // lets for which we have even and odd lane specializations
    const Scope<> &sliceable_lets;
    Scope<std::vector<VectorSlice>> &requested_slices;  // We populate this with the slices we need from the external_lets.

    using IRMutator::visit;

    Expr extract_lanes_from_make_struct(const Call *op) {
        internal_assert(op);
        internal_assert(op->is_intrinsic(Call::make_struct));
        std::vector<Expr> args(op->args.size());
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
            std::vector<Expr> args = {
                func,
                values, coords,
                type_code, type_bits, Expr(new_lanes),
                event, parent_id, idx, Expr(new_lanes * num_vecs),
                tag};
            Expr result = Call::make(Int(32), Call::trace, args, Call::Extern);
            debug(4) << "  => " << result << "\n";
            return result;
        }

        internal_error << "Unhandled trace call in ExtractLanes: " << *event;
    }

    Expr visit(const VectorReduce *op) override {
        int factor = op->value.type().lanes() / op->type.lanes();
        if (lane_stride != 1) {
            std::vector<int> input_lanes;
            for (int i = 0; i < new_lanes; ++i) {
                int lane_start = (starting_lane + lane_stride * i) * factor;
                for (int j = 0; j < factor; j++) {
                    input_lanes.push_back(lane_start + j);
                }
            }
            Expr in = Shuffle::make({op->value}, input_lanes);
            return VectorReduce::make(op->op, in, new_lanes);
        } else {
            Expr in;
            {
                ScopedValue<int> old_starting_lane(starting_lane, starting_lane * factor);
                ScopedValue<int> old_new_lanes(new_lanes, new_lanes * factor);
                in = mutate(op->value);
            }
            return VectorReduce::make(op->op, in, new_lanes);
        }
    }

    Expr visit(const Broadcast *op) override {
        if (const Call *call = op->value.as<Call>()) {
            if (call->name == Call::trace) {
                Expr value = extract_lanes_trace(call);
                if (new_lanes == 1) {
                    return value;
                } else {
                    return Broadcast::make(value, new_lanes);
                }
            }
        }
        if (new_lanes == 1) {
            if (op->value.type().lanes() == 1) {
                return op->value;
            } else {
                int old_starting_lane = starting_lane;
                int old_lane_stride = lane_stride;
                starting_lane = starting_lane % op->value.type().lanes();
                lane_stride = op->value.type().lanes();
                Expr e = mutate(op->value);
                starting_lane = old_starting_lane;
                lane_stride = old_lane_stride;
                return e;
            }
        }
        if (op->value.type().lanes() > 1) {
            // There is probably a more efficient way to do this.
            return mutate(flatten_nested_ramps(op));
        }

        return Broadcast::make(op->value, new_lanes);
    }

    Expr visit(const Load *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else {
            Type t = op->type.with_lanes(new_lanes);
            ModulusRemainder align = op->alignment;
            // The alignment of a Load refers to the alignment of the first
            // lane, so we can preserve the existing alignment metadata if the
            // deinterleave is asking for any subset of lanes that includes the
            // first. Otherwise we just drop it. We could check if the index is
            // a ramp with constant stride or some other special case, but if
            // that's the case, the simplifier is very good at figuring out the
            // alignment, and it has access to context (e.g. the alignment of
            // enclosing lets) that we do not have here.
            if (starting_lane != 0) {
                align = ModulusRemainder();
            }
            return Load::make(t, op->name, mutate(op->index), op->image, op->param, mutate(op->predicate), align);
        }
    }

    Expr visit(const Ramp *op) override {
        int base_lanes = op->base.type().lanes();
        if (base_lanes > 1) {
            if (new_lanes == 1) {
                int index = starting_lane / base_lanes;
                Expr expr = op->base + cast(op->base.type(), index) * op->stride;
                ScopedValue<int> old_starting_lane(starting_lane, starting_lane % base_lanes);
                ScopedValue<int> old_lane_stride(lane_stride, base_lanes);
                expr = mutate(expr);
                return expr;
            } else if (base_lanes == lane_stride &&
                       starting_lane < base_lanes) {
                // Base class mutator actually works fine in this
                // case, but we only want one lane from the base and
                // one lane from the stride.
                ScopedValue<int> old_new_lanes(new_lanes, 1);
                return IRMutator::visit(op);
            } else {
                // There is probably a more efficient way to this by
                // generalizing the two cases above.
                return mutate(flatten_nested_ramps(op));
            }
        }
        Expr expr = op->base + cast(op->base.type(), starting_lane) * op->stride;
        internal_assert(expr.type() == op->base.type());
        if (new_lanes > 1) {
            expr = Ramp::make(expr, op->stride * lane_stride, new_lanes);
        }
        return expr;
    }

    Expr give_up_and_shuffle(const Expr &e) {
        // Uh-oh, we don't know how to deinterleave this vector expression
        // Make llvm do it
        std::vector<int> indices;
        indices.reserve(new_lanes);
        for (int i = 0; i < new_lanes; i++) {
            indices.push_back(starting_lane + lane_stride * i);
        }
        return Shuffle::make({e}, indices);
    }

    Expr visit(const Variable *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else {

            Type t = op->type.with_lanes(new_lanes);
            /*
            internal_assert((op->type.lanes() - starting_lane + lane_stride - 1) / lane_stride == new_lanes)
                << "Deinterleaving with lane stride " << lane_stride << " and staring lane " << starting_lane
                << " for var of Type " << op->type << " to " << t << " drops lanes unexpectedly."
                << " Deinterleaver probably recursed too deep into types of different lane count.";
                */

            if (sliceable_lets.contains(op->name)) {
                // The variable accessed is marked as sliceable by the caller.
                // Let's request a slice and pretend it exists.
                std::string sliced_var_name = variable_name_with_extracted_lanes(
                    op->name, op->type.lanes(),
                    starting_lane, lane_stride, new_lanes);

                VectorSlice new_sl; // When C++20 lands: Designated initializer
                new_sl.start = starting_lane;
                new_sl.stride = lane_stride;
                new_sl.count = new_lanes;
                new_sl.variable_name = sliced_var_name;

                if (auto *vec = requested_slices.shallow_find(op->name)) {
                    bool found = false;
                    for (const VectorSlice &existing_sl : *vec) {
                        if (existing_sl.start == starting_lane &&
                            existing_sl.stride == lane_stride &&
                            existing_sl.count == new_lanes) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        vec->push_back(std::move(new_sl));
                    }
                } else {
                    requested_slices.push(op->name, {std::move(new_sl)});
                }
                return Variable::make(t, sliced_var_name, op->image, op->param, op->reduction_domain);
            } else {
                return give_up_and_shuffle(op);
            }
        }
    }

    Expr visit(const Cast *op) override {
        if (op->type.is_scalar()) {
            return op;
        } else {
            Type t = op->type.with_lanes(new_lanes);
            return Cast::make(t, mutate(op->value));
        }
    }

    Expr visit(const Reinterpret *op) override {
        // Written with assistance from Gemini 3 Pro, which required a lot of baby-sitting.

        // Simple case of a scalar reinterpret: always one lane:
        if (op->type.is_scalar()) {
            return op;
        }

        int out_bits = op->type.bits();
        int in_bits = op->value.type().bits();

        internal_assert(out_bits % in_bits == 0 || in_bits % out_bits == 0);

        // Case A: Stride 1. Calculate everything with bit-offsets
        if (lane_stride == 1) {

            // Compute range of bits required from the input.
            int start_bit = starting_lane * out_bits;
            int total_bits = new_lanes * out_bits;
            int end_bit = start_bit + total_bits;

            // Convert this to a range of lane indices
            int start_input_lane = start_bit / in_bits;
            int end_input_lane = (end_bit + in_bits - 1) / in_bits;
            int num_input_lanes = end_input_lane - start_input_lane;

            // Actually now get those lanes from the input.
            Expr extracted_input_lanes;
            {
                ScopedValue<int> old_sl(starting_lane, start_input_lane);
                ScopedValue<int> old_nl(new_lanes, num_input_lanes);
                extracted_input_lanes = mutate(op->value);
            }

            // The range of lanes we extracted from the input still might be too big, because
            // we had to grab whole elements from the input, which can be coarser if out_bits > in_bits.
            // So calculate how many lanes we extracted, when measured in the reinterpreted output type.
            int intm_lanes = (num_input_lanes * in_bits) / out_bits;
            Expr reinterprted = Reinterpret::make(op->type.with_lanes(intm_lanes), extracted_input_lanes);

            // Now calculate how many we output Type lanes we need to trim away.
            int bits_to_strip_front = start_bit - (start_input_lane * in_bits);
            int lanes_to_strip_front = bits_to_strip_front / out_bits;

            if (lanes_to_strip_front == 0) {
                internal_assert(reinterprted.type().lanes() == new_lanes);
                return reinterprted;
            } else {
                return Shuffle::make_slice(reinterprted, lanes_to_strip_front, 1, new_lanes);
            }
        }

        // Case B: Stride != 1. We are effectively gathering.
        // We will rewrite those Reinterprets as a Concat of Reinterprets of extracted lanes.
        std::vector<Expr> chunks(new_lanes);
        for (int i = 0; i < new_lanes; ++i) {
            // Find the bit range of this element in the output
            int start_bit = (starting_lane + lane_stride * i) * out_bits;
            int end_bit = start_bit + out_bits;

            // Map it to input lanes
            int start_input_lane = start_bit / in_bits;
            int end_input_lane = (end_bit + in_bits - 1) / in_bits;
            int num_input_lanes = end_input_lane - start_input_lane;

            // Grab this range of lanes from the input
            Expr input_chunk;
            {
                ScopedValue<int> s_start(starting_lane, start_input_lane);
                ScopedValue<int> s_stride(lane_stride, 1);
                ScopedValue<int> s_len(new_lanes, num_input_lanes);
                input_chunk = mutate(op->value);
            }

            // Reinterpret the chunk.
            int extracted_bits = num_input_lanes * in_bits;
            int reinterpreted_lanes = extracted_bits / out_bits;
            internal_assert(reinterpreted_lanes != 0);

            Expr reinterpreted = Reinterpret::make(op->type.with_lanes(reinterpreted_lanes), input_chunk);

            // Now, in case of demotion:
            // Example:
            // R = ExtractLanes(Reinterpret([u32, u32, u32, u32], u8), 0, 2, 4)
            //   = ExtractLanes([u8_0, u8_1, u8_2, u8_3, ...], 0, 2, 4)
            //   = [u8_0, u8_2, u8_4, u8_6]
            // A single extracted u32 element is too large, even after reinterpreting.
            // So we need to slice the reinterpreted result.
            int bit_offset = start_bit - (start_input_lane * in_bits);
            int lane_offset = bit_offset / out_bits;

            if (lane_offset == 0 && reinterpreted_lanes == 1) {
                chunks[i] = std::move(input_chunk);
            } else {
                chunks[i] = Shuffle::make_extract_element(reinterpreted, lane_offset);
            }
        }

        // In case of demotion, we will potentially extract and reinterpret the same input lane several times.
        // Simplification afterwards will turn them into Lets.

        return Shuffle::make_concat(chunks);
    }

    Expr visit(const Call *op) override {
        internal_assert(op->type.lanes() >= starting_lane + lane_stride * (new_lanes - 1)) << Expr(op) << starting_lane << " " << lane_stride << " " << new_lanes;
        Type t = op->type.with_lanes(new_lanes);

        // Don't mutate scalars
        if (op->type.is_scalar()) {
            return op;
        } else {
            // Vector calls are always parallel across the lanes, so we
            // can just deinterleave the args.

            // Beware of intrinsics for which this is not true!
            auto args = mutate(op->args);
            return Call::make(t, op->name, args, op->call_type,
                              op->func, op->value_index, op->image, op->param);
        }
    }

    Expr visit(const Shuffle *op) override {
        // Special case 1: Scalar extraction
        if (new_lanes == 1) {
            // Find in which vector it sits.
            int index = starting_lane;
            for (const auto &vec : op->vectors) {
                if (index < vec.type().lanes()) {
                    // We found the source vector. Extract the scalar from it.
                    ScopedValue<int> old_start(starting_lane, index);
                    ScopedValue<int> old_stride(lane_stride, 1);  // Stride doesn't matter for scalar
                    ScopedValue<int> old_count(new_lanes, 1);
                    return mutate(vec);
                }
                index -= vec.type().lanes();
            }
            internal_error << "extract_lane index out of bounds: " << Expr(op) << " " << index << "\n";
        }

        if (op->is_interleave()) {
            // Special case where we can discard some of the vector arguments entirely.
            internal_assert(starting_lane >= 0);
            int n_vectors = (int)op->vectors.size();

            // Case A: Stride is a multiple of the number of input vectors.
            // Example: extract_lanes(interleave(A, B), stride=4)
            //          result comes from either A or B, depending on starting lane modulo number of vectors,
            //          required stride of said vector is lane_stride / num_vectors
            if (lane_stride % n_vectors == 0) {
                const Expr &vec = op->vectors[starting_lane % n_vectors];
                if (vec.type().lanes() == new_lanes) {
                    // We need all lanes of this vector, just return it.
                    return vec;
                } else {
                    // We don't need all lanes, unfortunately. Let's extract the part we need.
                    ScopedValue<int> old_starting_lane(starting_lane, starting_lane / n_vectors);
                    ScopedValue<int> old_lane_stride(lane_stride, lane_stride / n_vectors);
                    return mutate(vec);
                }
            }

            // Case B: Number of vectors is a multiple of the stride.
            // Eg: extract_lanes(interleave(a, b, c, d, e, f), start=8, stride=3)
            //  = extract_lanes(a0, b0, c0, d0, e0, f0, a1, b1, c1, d1, e1, f1, ...)
            //  = (a2, c2, e2, c1, ...)
            //  = interleave(a, c)
            if (n_vectors % lane_stride == 0) {
                int num_required_vectors = n_vectors / lane_stride;

                // The result is only an interleave if the number of constituent
                // vectors divides the number of total required lanes.
                if (new_lanes % num_required_vectors == 0) {
                    int lanes_per_vec = new_lanes / num_required_vectors;

                    // Pick up every lane-stride vector.
                    std::vector<Expr> new_vectors(num_required_vectors);
                    for (size_t i = 0; i < new_vectors.size(); i++) {
                        int absolute_lane_index = starting_lane + i * lane_stride;
                        int src_vec_idx = absolute_lane_index % n_vectors;
                        int vec_lane_start = absolute_lane_index / n_vectors;
                        const Expr &vec = op->vectors[src_vec_idx];

                        ScopedValue<int> old_starting_lane(starting_lane, vec_lane_start);
                        ScopedValue<int> old_lane_stride(lane_stride, 1);
                        ScopedValue<int> old_new_lanes(new_lanes, lanes_per_vec);
                        new_vectors[i] = mutate(vec);
                    }
                    return Shuffle::make_interleave(new_vectors);
                }
            }
        }

        // General case fallback
        std::vector<int> indices(new_lanes);
        bool constant_stride = true;
        for (int i = 0; i < new_lanes; i++) {
            int idx = op->indices[i * lane_stride + starting_lane];
            indices[i] = idx;
            if (i > 1 && constant_stride) {
                int stride = indices[1] - indices[0];
                if (indices[i] != indices[i - 1] + stride) {
                    constant_stride = false;
                }
            }
        }

        // One optimization if we take a slice of a single vector.
        if (constant_stride) {
            int stride = indices[1] - indices[0];
            int first_idx = indices.front();
            int last_idx = indices.back();

            // Find which vector contains this range
            int current_bound = 0;
            for (const auto &vec : op->vectors) {
                int vec_lanes = vec.type().lanes();

                // Check if the START of the ramp is in this vector
                if (first_idx >= current_bound && first_idx < current_bound + vec_lanes) {

                    // We found the vector containing the start.
                    // Now, because it is a linear ramp, we only need to check if the
                    // END of the ramp is also within this same vector.
                    // (This handles negative strides, forward strides, and broadcasts correctly).
                    if (last_idx >= current_bound && last_idx < current_bound + vec_lanes) {

                        // Calculate the start index relative to this specific vector
                        int local_start = first_idx - current_bound;

                        ScopedValue<int> s_start(starting_lane, local_start);
                        ScopedValue<int> s_stride(lane_stride, stride);
                        // new_lanes is already correct
                        return mutate(vec);
                    }

                    // If the start is here but the end is elsewhere, the ramp crosses
                    // vector boundaries. We cannot optimize this as a single vector extraction.
                    break;
                }
                current_bound += vec_lanes;
            }
        }

        return Shuffle::make(op->vectors, indices);
    }
};

}  // namespace

Expr extract_lanes(Expr original_expr, int starting_lane, int lane_stride, int new_lanes, const Scope<> &lets, Scope<std::vector<VectorSlice>> &requested_sliced_lets) {
    internal_assert(starting_lane + (new_lanes - 1) * lane_stride <= original_expr.type().lanes())
        << "Extract lanes with start:" << starting_lane << ", stride:" << lane_stride << ", new_lanes:" << new_lanes << "  "
        << "out of " << original_expr.type() << " which goes out of bounds.";

    debug(3) << "ExtractLanes "
             << "(start:" << starting_lane << ", stride:" << lane_stride << ", new_lanes:" << new_lanes << "): "
             << original_expr << " of Type: " << original_expr.type() << "\n";
    Type original_type = original_expr.type();
    Expr e = substitute_in_all_lets(original_expr);
    ExtractLanes d(starting_lane, lane_stride, new_lanes, lets, requested_sliced_lets);
    e = d.mutate(e);
    e = common_subexpression_elimination(e);
    debug(3) << "   => " << e << "\n";
    Type final_type = e.type();
    internal_assert(original_type.code() == final_type.code()) << "Underlying types not identical after extract_lanes.";
    e = simplify(e);
    internal_assert(new_lanes == final_type.lanes())
        << "Number of lanes incorrect after extract_lanes: " << final_type.lanes() << " while expected was " << new_lanes << ": extract_lanes(" << starting_lane << ", " << lane_stride << ", " << new_lanes << "):\n"
        << "Input: " << original_expr << "\nResult: " << e;
    return e;
}

Expr extract_lanes(Expr e, int starting_lane, int lane_stride, int new_lanes) {
    Scope<> lets;
    Scope<std::vector<VectorSlice>> req;
    return extract_lanes(std::move(e), starting_lane, lane_stride, new_lanes, lets, req);
}

Expr extract_even_lanes(const Expr &e) {
    internal_assert(e.type().lanes() % 2 == 0);
    return extract_lanes(e, 0, 2, e.type().lanes() / 2);
}

Expr extract_odd_lanes(const Expr &e) {
    internal_assert(e.type().lanes() % 2 == 0);
    return extract_lanes(e, 1, 2, e.type().lanes() / 2);
}

Expr extract_lane(const Expr &e, int lane) {
    return extract_lanes(e, lane, e.type().lanes(), 1);
}

namespace {

// Change name to DenisfyStridedLoadsAndStores?
class Interleaver : public IRMutator {
    Scope<> vector_lets;
    Scope<std::vector<VectorSlice>> requested_sliced_lets;

    using IRMutator::visit;

    bool should_deinterleave = false;
    int num_lanes;

    Expr deinterleave_expr(const Expr &e) {
        std::vector<Expr> exprs;
        exprs.reserve(num_lanes);
        for (int i = 0; i < num_lanes; i++) {
            exprs.emplace_back(extract_lanes(e, i, num_lanes, e.type().lanes() / num_lanes, vector_lets, requested_sliced_lets));
        }
        return Shuffle::make_interleave(exprs);
    }

    template<typename LetOrLetStmt>
    auto visit_let(const LetOrLetStmt *op) -> decltype(op->body) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const LetOrLetStmt *op;
            Expr new_value;
            ScopedBinding<> binding;
            Frame(const LetOrLetStmt *op, Expr v, Scope<void> &scope)
                : op(op),
                  new_value(std::move(v)),
                  binding(new_value.type().is_vector(), scope, op->name) {
            }
        };
        std::vector<Frame> frames;
        decltype(op->body) result;

        do {
            result = op->body;
            frames.emplace_back(op, mutate(op->value), vector_lets);
        } while ((op = result.template as<LetOrLetStmt>()));

        result = mutate(result);

        for (const auto &frame : reverse_view(frames)) {
            Expr value = std::move(frame.new_value);

            // The original variable:
            result = LetOrLetStmt::make(frame.op->name, value, result);

            // For vector lets, we may additionally need a lets for the requested slices of this variable:
            if (value.type().is_vector()) {
                if (std::vector<VectorSlice> *reqs = requested_sliced_lets.shallow_find(frame.op->name)) {
                    for (const VectorSlice &sl : *reqs) {
                        result = LetOrLetStmt::make(
                            sl.variable_name,
                            extract_lanes(value, sl.start, sl.stride, sl.count, vector_lets, requested_sliced_lets), result);
                    }
                }
            }
        }

        return result;
    }

    Expr visit(const Let *op) override {
        return visit_let(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_let(op);
    }

    Expr visit(const Ramp *op) override {
        if (op->stride.type().is_vector() &&
            is_const_one(op->stride) &&
            !op->base.as<Ramp>() &&
            !op->base.as<Broadcast>()) {
            // We have a ramp with a computed vector base.  If we
            // deinterleave we'll get ramps of stride 1 with a
            // computed scalar base.
            should_deinterleave = true;
            num_lanes = op->stride.type().lanes();
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Mod *op) override {
        const Ramp *r = op->a.as<Ramp>();
        for (int i = 2; i <= 4; ++i) {
            if (r &&
                is_const(op->b, i) &&
                (r->type.lanes() % i) == 0) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Div *op) override {
        const Ramp *r = op->a.as<Ramp>();
        for (int i = 2; i <= 4; ++i) {
            if (r &&
                is_const(op->b, i) &&
                (r->type.lanes() % i) == 0 &&
                r->type.lanes() > i) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        if (!op->is_pure() &&
            !op->is_intrinsic(Call::unsafe_promise_clamped) &&
            !op->is_intrinsic(Call::promise_clamped)) {
            // deinterleaving potentially changes the order of execution.
            should_deinterleave = false;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        bool should_deinterleave_idx = should_deinterleave;

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        bool should_deinterleave_predicate = should_deinterleave;

        Expr expr;
        if (should_deinterleave_idx && (should_deinterleave_predicate || is_const_one(predicate))) {
            // If we want to deinterleave both the index and predicate
            // (or the predicate is one), then deinterleave the
            // resulting load.
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
            expr = deinterleave_expr(expr);
        } else if (should_deinterleave_idx) {
            // If we only want to deinterleave the index and not the
            // predicate, deinterleave the index prior to the load.
            idx = deinterleave_expr(idx);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else if (should_deinterleave_predicate) {
            // Similarly, deinterleave the predicate prior to the load
            // if we don't want to deinterleave the index.
            predicate = deinterleave_expr(predicate);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else if (!idx.same_as(op->index) || !predicate.same_as(op->index)) {
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else {
            expr = op;
        }

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;
        return expr;
    }

    Stmt visit(const Store *op) override {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        if (should_deinterleave) {
            idx = deinterleave_expr(idx);
        }

        should_deinterleave = false;
        Expr value = mutate(op->value);
        if (should_deinterleave) {
            value = deinterleave_expr(value);
        }

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        if (should_deinterleave) {
            predicate = deinterleave_expr(predicate);
        }

        Stmt stmt = Store::make(op->name, value, idx, op->param, predicate, op->alignment);

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;

        return stmt;
    }

    HALIDE_NEVER_INLINE Stmt gather_stores(const Block *op) {
        const LetStmt *let = op->first.as<LetStmt>();
        const Store *store = op->first.as<Store>();

        // Gather all the let stmts surrounding the first.
        std::vector<Stmt> let_stmts;
        while (let) {
            let_stmts.emplace_back(let);
            store = let->body.as<Store>();
            let = let->body.as<LetStmt>();
        }

        // There was no inner store.
        if (!store) {
            return Stmt();
        }

        const Ramp *r0 = store->index.as<Ramp>();

        // It's not a store of a ramp index.
        if (!r0) {
            return Stmt();
        }

        auto optional_stride = as_const_int(r0->stride);

        // The stride isn't a constant or is <= 1
        if (!optional_stride || *optional_stride <= 1) {
            return Stmt();
        }

        const int64_t stride = *optional_stride;
        const int lanes = r0->lanes;
        const int64_t expected_stores = stride;

        // Collect the rest of the stores.
        std::vector<Stmt> stores;
        stores.emplace_back(store);
        Stmt rest = collect_strided_stores(op->rest, store->name,
                                           stride, expected_stores,
                                           let_stmts, stores);

        // Check the store collector didn't collect too many
        // stores (that would be a bug).
        internal_assert(stores.size() <= (size_t)expected_stores);

        // Not enough stores collected.
        if (stores.size() != (size_t)expected_stores) {
            return Stmt();
        }

        // Too many stores and lanes to represent in a single vector
        // type.
        int max_bits = sizeof(halide_type_t::lanes) * 8;
        // mul_would_overflow is for signed types, but vector lanes
        // are unsigned, so add a bit.
        max_bits++;
        if (mul_would_overflow(max_bits, stores.size(), lanes)) {
            return Stmt();
        }

        Type t = store->value.type();
        Expr base;
        std::vector<Expr> args(stores.size());
        std::vector<Expr> predicates(stores.size());

        int64_t min_offset = 0;
        std::vector<int64_t> offsets(stores.size());

        std::string load_name;
        Buffer<> load_image;
        Parameter load_param;
        for (size_t i = 0; i < stores.size(); ++i) {
            const Ramp *ri = stores[i].as<Store>()->index.as<Ramp>();
            internal_assert(ri);

            // Mismatched store vector laness.
            if (ri->lanes != lanes) {
                return Stmt();
            }

            auto offs = as_const_int(simplify(ri->base - r0->base));

            // Difference between bases is not constant.
            if (!offs) {
                return Stmt();
            }

            offsets[i] = *offs;
            min_offset = std::min(min_offset, *offs);
        }

        // Gather the args for interleaving.
        for (size_t i = 0; i < stores.size(); ++i) {
            int64_t j = offsets[i] - min_offset;
            if (j == 0) {
                base = stores[i].as<Store>()->index.as<Ramp>()->base;
            }

            // The offset is not between zero and the stride.
            if (j < 0 || (size_t)j >= stores.size()) {
                return Stmt();
            }

            // We already have a store for this offset.
            if (args[j].defined()) {
                return Stmt();
            }

            args[j] = stores[i].as<Store>()->value;
            predicates[j] = stores[i].as<Store>()->predicate;
        }

        // One of the stores should have had the minimum offset.
        internal_assert(base.defined());

        // Generate a single interleaving store.
        t = t.with_lanes(lanes * stores.size());
        Expr index = Ramp::make(base, make_one(base.type()), t.lanes());
        Expr value = Shuffle::make_interleave(args);
        Expr predicate = Shuffle::make_interleave(predicates);
        Stmt new_store = Store::make(store->name, value, index, store->param, predicate, ModulusRemainder());

        // Rewrap the let statements we pulled off.
        while (!let_stmts.empty()) {
            const LetStmt *let = let_stmts.back().as<LetStmt>();
            new_store = LetStmt::make(let->name, let->value, new_store);
            let_stmts.pop_back();
        }

        // Continue recursively into the stuff that
        // collect_strided_stores didn't collect.
        Stmt stmt = Block::make(new_store, mutate(rest));

        // Success!
        return stmt;
    }

    Stmt visit(const Block *op) override {
        Stmt s = gather_stores(op);
        if (s.defined()) {
            return s;
        } else {
            Stmt first = mutate(op->first);
            Stmt rest = mutate(op->rest);
            if (first.same_as(op->first) && rest.same_as(op->rest)) {
                return op;
            } else {
                return Block::make(first, rest);
            }
        }
    }

public:
    Interleaver() = default;
};

}  // namespace

Stmt rewrite_interleavings(const Stmt &s) {
    return Interleaver().mutate(s);
}

namespace {
void check(Expr a, const Expr &even, const Expr &odd) {
    a = simplify(a);
    Expr correct_even = extract_even_lanes(a);
    Expr correct_odd = extract_odd_lanes(a);
    if (!equal(correct_even, even)) {
        internal_error << correct_even << " != " << even << "\n";
    }
    if (!equal(correct_odd, odd)) {
        internal_error << correct_odd << " != " << odd << "\n";
    }
}
}  // namespace

void deinterleave_vector_test() {
    std::pair<Expr, Expr> result;
    Expr x = Variable::make(Int(32), "x");
    Expr ramp = Ramp::make(x + 4, 3, 8);
    Expr ramp_a = Ramp::make(x + 4, 6, 4);
    Expr ramp_b = Ramp::make(x + 7, 6, 4);
    Expr broadcast = Broadcast::make(x + 4, 16);
    Expr broadcast_a = Broadcast::make(x + 4, 8);
    const Expr &broadcast_b = broadcast_a;

    check(ramp, ramp_a, ramp_b);
    check(broadcast, broadcast_a, broadcast_b);

    check(Load::make(ramp.type(), "buf", ramp, Buffer<>(), Parameter(), const_true(ramp.type().lanes()), ModulusRemainder()),
          Load::make(ramp_a.type(), "buf", ramp_a, Buffer<>(), Parameter(), const_true(ramp_a.type().lanes()), ModulusRemainder()),
          Load::make(ramp_b.type(), "buf", ramp_b, Buffer<>(), Parameter(), const_true(ramp_b.type().lanes()), ModulusRemainder()));

    Expr vec_x = Variable::make(Int(32, 4), "vec_x");
    Expr vec_y = Variable::make(Int(32, 4), "vec_y");
    check(Shuffle::make({vec_x, vec_y}, {0, 4, 2, 6, 4, 2, 3, 7, 1, 2, 3, 4}),
          Shuffle::make({vec_x, vec_y}, {0, 2, 4, 3, 1, 3}),
          Shuffle::make({vec_x, vec_y}, {4, 6, 2, 7, 2, 4}));

    std::cout << "deinterleave_vector test passed\n";
}

}  // namespace Internal
}  // namespace Halide
