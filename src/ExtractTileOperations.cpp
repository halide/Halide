#include "ExtractTileOperations.h"

#include "FindIntrinsics.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "MultiRamp.h"
#include "Simplify.h"
#include "Util.h"

/** \file Support extraction of AMX instructions. */

/**
 * https://asciiflow.com/#/share/eJyVUkFugzAQ%2FMrKxwoRhdAkza23SmlySHvogQsBp7FkbGSbAoryiz6nr%2BlLugZDk6ghKvJhbXZmd2b3QEScUbIQBece4XFNFVmQQ0SqiCwegtCLSI1RMBtjZGhl8BIRAHh%2BeoFVbBSr4Pq36ZOiSOBpX5cDCEikSGhuipjzun0pmdnD4%2BqtwX9%2Ffg2cLmUcTML76WyO4VAtWJ%2Ff7kIkWMEJ6gbBae2%2F3q53OHBuFBz3TS1HodPqfvUO3%2F4wO7gQag07IXqVkCuZU4VzyApuWI5BAJkdZ0K1B2ZP2%2BwJ%2FEs%2BjhKY0EYViWFSaMAaO6kypBY1hLCtDRIvMTvsekmlsc2kiGgKMw2cxqkGIyEGjn%2FlzonoIMjPUibeQX5Q1bHGisbav%2FBh2kHW2ESzdlaZkqUltaFd9UZ25TnIrIOg%2Bb7vQykLnv661GysRSaSF1k78HkHcaSbntSReLAtTL%2FscOlaI9rxYaRzzgwUOTrZeOCokLzN0TDqRYvUqtFwB6Fvqco9S5r%2BBCiqsWmNLHabzny2Y7E4PyJHcvwBx0t%2BJw%3D%3D)
 *
 *   LHS Matrix                           RHS Matrix
 *
 *      K                            conceptually      with AMX
 *  ┌────────┐
 *  │12345678│                             N             N*4
 *M │        │                            ┌──┐        ┌────────┐
 *  └────────┘                            │1 │     K/4│1234    │
 *                                        │2 │        │5678    │
 * To properly multiply 2 matrices, the   │3 │        └────────┘
 * AMX instructions perform many 4 byte  K│4 │
 * dot products, this leads to a lot of   │5 │
 * striding over 4 byte areas.            │6 │
 * Normally the row of the LHS matrix,    │7 │
 * 123... would multiply with the column  │8 │
 * of the RHS matrix 123..., but with AMX │8 │
 * this column is split up into a matrix of columns / 4 byte and rows * 4.
 * which then results in K/4 dot products per row.
 *
 */

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

struct Matmul {
    bool result = false;
    Stmt stmt;
    int I = 0;
    int J = 0;
    int K = 0;
};

Matmul convert_to_matmul(const Store *op, const string &new_name) {
    // We expect the pattern:
    //
    // out[idx] = reduce_add(widen(lhs[multiramp]) * widen(rhs[multiramp])) + out[idx]
    //
    // Though if the multiramp has an outer dimension of stride zero it may have
    // been hoisted outwards to just a broadcast of the widened value.

    auto fail = [&](const char *reason) -> Matmul {
        user_error << "Matrix multiply not recognized. Store to AMX allocation must be a "
                   << "zero-initialization or a sum of a vector reduce op and a load from "
                   << "the same allocation. In the following store, " << reason << ".\n"
                   << Stmt(op);
        return Matmul{};
    };

    // Peel lets
    std::vector<std::pair<std::string, Expr>> peeled_lets;
    Expr value = op->value;
    while (const Let *let = value.as<Let>()) {
        peeled_lets.emplace_back(let->name, let->value);
        value = let->body;
    }

    // The RHS must be an add
    const auto *add = value.as<Add>();
    if (!add) {
        return fail("the right-hand-side is not an add");
    }

    // The add must be between a vector reduce and a load. The simplifier will
    // have placed the vector reduce to the left, due to canonicalization of
    // commutative ops.
    Expr lhs = add->a;
    const auto *reduce = lhs.as<VectorReduce>();
    if (!reduce || reduce->op != VectorReduce::Add) {
        return fail("the right-hand-side is not a vector reduction plus a load");
    }

    // The load must be to the same addresses as the store (i.e. this is a +=)
    Expr rhs = add->b;
    const auto *load = rhs.as<Load>();
    if (!load || load->name != op->name || !equal(load->index, op->index)) {
        return fail("the right-hand-side load is not from the same address as the store");
    }

    // There must be no predicate on the load or store
    if (!is_const_one(load->predicate) || !is_const_one(op->predicate)) {
        return fail("the load or store is predicated");
    }

    // The vector reduce must be of a multiply. Unpack it and rebind lhs and rhs
    // to mean the lhs and rhs of the mul. For integers we normalize various
    // ways of doing the widening multiply by running find_intrinsics. For
    // floats we do the opposite and canonicalize away from intrinsics, because
    // FindIntrinsics does not currently lift float widening_muls.
    if (reduce->type.is_int_or_uint()) {
        Expr reduce_value = find_intrinsics(reduce->value);

        const auto *cast = reduce_value.as<Cast>();
        if (!cast) {
            return fail("the vector reduction operand or result types are not supported");
        }

        if (const auto *call = Call::as_intrinsic(cast->value, {Call::widening_mul})) {
            // Simplify to convert bit-math back to multiply, div, mod
            lhs = simplify(lower_intrinsics(call->args[0]));
            rhs = simplify(lower_intrinsics(call->args[1]));
        } else {
            return fail("the vector reduction is not of a widening multiply");
        }

        if (lhs.type().bits() != 8 ||
            rhs.type().bits() != 8) {
            return fail("the vector reduction operand or result types are not supported");
        }

    } else {
        // Lower a widening_mul intrinsic, as they can be used but aren't lifted to for bf16.
        Expr reduce_value = simplify(lower_intrinsics(reduce->value));
        const auto *mul = reduce_value.as<Mul>();
        if (!mul) {
            return fail("the vector reduction is not of a widening multiply");
        }
        lhs = mul->a;
        rhs = mul->b;
    }

    // There may be a broadcast next (it can get hoisted outside of other ops)
    auto debroadcast = [](Expr &e) -> int {
        if (const Broadcast *b = e.as<Broadcast>()) {
            int lanes = b->lanes;
            e = b->value;
            return lanes;
        } else {
            return 1;
        }
    };
    int lhs_broadcast = debroadcast(lhs);
    int rhs_broadcast = debroadcast(rhs);

    // Unpack the casts, if it was a direct multiply. This should only happen
    // for floats (the integer branch above already extracted the cast inputs
    // from the widening_mul intrinsic).
    if (reduce->type.is_float()) {
        const auto *lhs_cast = lhs.as<Cast>();
        const auto *rhs_cast = rhs.as<Cast>();
        if (!lhs_cast || !rhs_cast) {
            return fail("the vector reduction is not of a widening multiply");
        }
        lhs = lhs_cast->value;
        rhs = rhs_cast->value;
        if (!lhs.type().is_bfloat() ||
            rhs.type().element_of() != lhs.type().element_of()) {
            return fail("the vector reduction operand or result types are not supported");
        }
    }

    // Underneath all of this must be a load
    // TODO: What if we want to multiply by the same matrix multiple times? It might be a let binding.
    const auto *lhs_load = lhs.as<Load>();
    const auto *rhs_load = rhs.as<Load>();
    if (!lhs_load || !rhs_load) {
        return fail("the matrix multiply operands are not loads");
    }
    // The loads must be unpredicated
    if (!is_const_one(lhs_load->predicate) || !is_const_one(rhs_load->predicate)) {
        return fail("the matrix multiply operands are predicated loads");
    }

    // Now we analyze the load indices as multiramps
    MultiRamp lhs_mr, rhs_mr;
    Scope<Expr> empty_scope;
    if (!is_multiramp(lhs_load->index, empty_scope, &lhs_mr) ||
        !is_multiramp(rhs_load->index, empty_scope, &rhs_mr)) {
        return fail("the matrix multiply loads indices are not affine");
    }

    // Add back on any broadcasts as a stride-0 outer dim.
    auto add_broadcast = [](MultiRamp &mr, int extent) {
        if (extent > 1) {
            mr.strides.push_back(make_zero(mr.base.type()));
            mr.lanes.push_back(extent);
        }
    };
    add_broadcast(lhs_mr, lhs_broadcast);
    add_broadcast(rhs_mr, rhs_broadcast);

    // In a matrix multiply with row-major inputs and outputs, the algorithm
    // looks like:
    //
    // C(j, i) += A(k, i) * B(j, k)
    //
    // (Recall that for matrices where the rows are stored densely in memory, Halide
    // is indexed col-major)
    // The canonical loop nest order, from innermost out, is k, j, i. So you'd
    // expect the following vector shape (again, from innermost out):
    // [K, J, I]
    // And the following strides for A and B respectively:
    // [1, 0, ?] [?, 1, 0]
    // Where the question marks are the strides in memory that separate rows the LHS and RHS.

    // AMX however splits the storage of K into 32-bit chunks and reorders that
    // innermost for both A and B. This changes the algorithm to:
    // C(j, i) += A(ki, ko, i) * B(ki, j, ko)
    // the shape to:
    // [Ki, Ko, J, I]
    // and the strides to:
    // [1, Ki, 0, ?] [1, ?, Ki, 0]

    // So next we need to:
    // 1) Deduce what Ki, Ko, are.
    // 2) Deduce which is the LHS and which is the RHS
    // 3) Deduce what I, J are.
    // 4) extract those two question marks, and validate the
    // rest is as-expected.

    // The reduction's K dimension will be split into inner and outer elements.
    int element_width = lhs_load->type.bytes();
    int K = reduce->value.type().lanes() / reduce->type.lanes();
    int Ki = 4 / element_width;
    int Ko = K / Ki;

    // Now deduce LHS and RHS. First some helpers.
    auto swap_sides = [&]() {
        std::swap(lhs, rhs);
        std::swap(lhs_mr, rhs_mr);
        std::swap(lhs_load, rhs_load);
    };

    auto swizzled = [](const MultiRamp &mr) {
        // Count the number of non-broadcast dimensions
        int count = 0;
        for (int i = 0; i < mr.dimensions(); i++) {
            count += !is_const_zero(mr.strides[i]);
        }
        return count > 2;
    };

    auto has_trailing_zero = [](const MultiRamp &mr) {
        return !mr.lanes.empty() && is_const_zero(mr.strides.back());
    };

    // The RHS is the one that's swizzled. The LHS should be stored densely in
    // K. If both sides are stored densely either Ko is one (there's no outer
    // dimension in the swizzle) or J is one (there's no dimension that would go
    // between Ki and Ko). The RHS is the one that doesn't depend on I, so it
    // should have a trailing zero stride. If neither side is swizzled and
    // neither side has a trailing zero stride then it doesn't matter which side
    // is which.
    if (swizzled(lhs_mr) || (!swizzled(rhs_mr) && has_trailing_zero(lhs_mr))) {
        swap_sides();
    }

    auto unique_lanes = [](const MultiRamp &mr) {
        int u = 1;
        for (int i = 0; i < mr.dimensions(); i++) {
            if (!is_const_zero(mr.strides[i])) {
                u *= mr.lanes[i];
            }
        }
        return u;
    };

    // Now deduce I, J. The output has I * J lanes. The LHS has I * K unique
    // addresses loaded, and the RHS has J * K unique addresses.
    int IJ = reduce->type.lanes();
    int IK = unique_lanes(lhs_mr);
    int I = IK / K;
    int J = IJ / I;

    // Coerce both MRs into the canonical [Ki, Ko, J, I] shape (innermost
    // first). When Ko == 1, the second slot is just an extent-1 dim and
    // strides_for_shape will return a 0 stride there. The expected strides
    // we'll then validate are:
    //   lhs: [1, Ki, 0, ?]   (with `?` the LHS row stride)
    //   rhs: [1, ?, Ki, 0]   (with `?` the RHS row stride between Ko chunks)
    std::vector<int>
        shape{Ki, Ko, J, I};
    std::vector<Expr> lhs_strides, rhs_strides;
    if (!lhs_mr.strides_for_shape(shape, &lhs_strides) ||
        !rhs_mr.strides_for_shape(shape, &rhs_strides)) {
        return fail("a matrix multiply operand has an unsupported access pattern");
    }

    if (!is_const_one(lhs_strides[0]) ||
        !is_const_one(rhs_strides[0]) ||
        (Ko > 1 && !is_const(lhs_strides[1], Ki)) ||
        (J > 1 && !is_const(rhs_strides[2], Ki)) ||
        !is_const_zero(lhs_strides[2]) ||
        !is_const_zero(rhs_strides[3])) {
        return fail("the storage layout for a matrix multiply operand is unsupported by AMX");
    }

    // Both sides of the multiply must be things that fit in AMX registers. We
    // could manually split up too-large matrices here into a collection of
    // matrix multiply ops, but for now we just assert.
    {
        Type t = op->value.type();
        bool result_ok = t.bytes() * I * J <= 1024;
        bool lhs_ok = lhs.type().bytes() * I * K <= 1024;
        bool rhs_ok = rhs.type().bytes() * K * J <= 1024;
        if (!result_ok || !lhs_ok || !rhs_ok) {
            return fail("one more more matrices are too large to fit in AMX registers (more than 1024 bytes)");
        }
        if (I > 16) {
            return fail("the result matrix has more than 16 rows");
        }
        if (Ko > 16) {
            return fail("the RHS matrix has more than 16 rows");
        }
    }

    Expr rhs_stride_bytes = Ko > 1 ? rhs_strides[1] * element_width : make_zero(rhs_mr.base.type());
    Expr lhs_stride_bytes = lhs_strides[3] * element_width;

    // Build the AMX intrinsics.
    auto lhs_var = Variable::make(Handle(), lhs_load->name);
    auto lhs_type = lhs_load->type.with_lanes(1024 / element_width);
    auto lhs_call = Call::make(lhs_type, "tile_load",
                               {I, K * element_width, lhs_var,
                                lhs_mr.base * element_width, lhs_stride_bytes},
                               Call::Intrinsic);

    auto rhs_var = Variable::make(Handle(), rhs_load->name);
    auto rhs_type = rhs_load->type.with_lanes(1024 / element_width);
    auto col_bytes = J * 4;  // 4 bytes per innermost K slice
    auto rhs_call = Call::make(rhs_type, "tile_load",
                               {Ko, col_bytes, rhs_var,
                                rhs_mr.base * element_width, rhs_stride_bytes},
                               Call::Intrinsic);

    Type res_type = op->value.type().with_lanes(256);
    Expr subtile_idx = Ramp::make(0, 1, 256);
    auto out_load = Load::make(res_type, new_name, subtile_idx, {}, {}, const_true(256), {});

    auto matmul = Call::make(res_type, "tile_matmul",
                             {I, col_bytes, K, out_load, lhs_call, rhs_call},
                             Call::Intrinsic);
    auto store = Store::make(new_name, matmul, std::move(subtile_idx), Parameter(), const_true(256), ModulusRemainder());
    for (auto &[name, value] : reverse_view(peeled_lets)) {
        store = LetStmt::make(name, std::move(value), store);
    }
    return {true, std::move(store), I, J, K};
}

Stmt convert_to_zero(const Store *op, const string &new_name, int I, int J) {
    auto rows = Cast::make(Int(16), I);
    auto bytes = op->value.type().bytes();
    auto colbytes = Cast::make(Int(16), J * bytes);
    const auto &store_type = op->value.type();
    auto tile_zero_type = store_type.with_lanes(1024 / store_type.bytes());
    auto val = Call::make(tile_zero_type, "tile_zero", {rows, colbytes}, Call::Intrinsic);
    Expr subtile_idx = Ramp::make(0, 1, 256);
    return Store::make(new_name, std::move(val), std::move(subtile_idx), Parameter(), const_true(256), ModulusRemainder());
}

Stmt convert_to_tile_store(const Store *op, const string &amx_name, int I, int J) {
    auto fail = [&](const char *reason) {
        user_error << "Store of AMX register to memory not supported. "
                   << reason << ".\n"
                   << Stmt(op);
        return Stmt{};
    };

    if (!is_const_one(op->predicate)) {
        return fail("The store has a predicate");
    }
    MultiRamp mr;
    if (!is_multiramp(op->index, Scope<Expr>::empty_scope(), &mr)) {
        return fail("The store index is not affine");
    }
    if (mr.total_lanes() != I * J) {
        return fail("There are too many lanes for the deduced matrix shape");
    }

    // Coerce the index into the canonical 2D shape: stride-1 inner of
    // lanes J, row-stride outer of lanes I. Either dim may be
    // extent 1 — strides_for_shape returns a zero stride for those slots.
    std::vector<Expr> mr_strides;
    if (!mr.strides_for_shape({J, I}, &mr_strides)) {
        return fail("The store index is incompatible with the deduced matrix shape");
    }
    if (J > 1 && !is_const_one(mr_strides[0])) {
        return fail("The innermost stride of the store index is not one");
    }
    Expr x_stride = mr_strides[1];

    auto out_var = Variable::make(Handle(), op->name);
    auto tile_type = op->value.type().with_lanes(256);
    Expr subtile_idx = Ramp::make(0, 1, 256);
    auto tile_val = Load::make(tile_type, amx_name, std::move(subtile_idx), {}, {}, const_true(256), {});
    auto bytes = op->value.type().bytes();
    // This should have been caught earlier, so internal assert
    internal_assert(bytes == 4)
        << "AMX store only supported for int32 and float32 output, not for "
        << op->value.type() << "\n";
    auto store = Call::make(Int(32), "tile_store",
                            {I, J * bytes, std::move(out_var),
                             mr.base * bytes, x_stride * bytes, std::move(tile_val)},
                            Call::Intrinsic);
    return Evaluate::make(std::move(store));
}

class ExtractTileOperations : public IRMutator {
    using IRMutator::visit;

    string tile_name;
    string amx_name;
    int pass = 0;
    bool in_allocate = false;
    int found_I = -1;
    int found_J = -1;
    int found_K = -1;

    // An AMXTile allocation may represent multiple AMX accumulator
    // registers as 2D sub-tiles. This map tracks those.
    std::vector<MultiRamp> amx_subtiles;

    // Returns a unique subtile index for a load or store index, or -1 if it
    // overlaps with an existing subtile, or is otherwise poorly behaved.
    int get_subtile(const Expr &index) {
        MultiRamp mr;
        if (!is_multiramp(index, Scope<Expr>::empty_scope(), &mr)) {
            user_error << "Access to AMX tile not affine: " << index << "\n";
        }
        if (!can_prove(mr.alias_free())) {
            // What are you doing?
            user_error << "Access to AMX tile may have duplicated lanes: " << index << "\n";
        }
        if (amx_subtiles.empty()) {
            amx_subtiles.push_back(std::move(mr));
            return 0;
        }

        // All strides and lanes must match across all subtiles, or we give up.
        const MultiRamp &first = amx_subtiles[0];
        if (mr.dimensions() != first.dimensions()) {
            user_error
                << "Access to AMX tile does not have the same shape as other accesses to the same memory.";
            return -1;
        }
        for (int i = 0; i < first.dimensions(); i++) {
            if (!can_prove(mr.strides[i] == first.strides[i]) ||
                mr.lanes[i] != first.lanes[i]) {
                user_error
                    << "Access to AMX tile has different size and strides to other "
                    << "accesses to the same memory. All accesses must have the same "
                    << "subtile size and strides: " << index;
            }
        }

        // Now check for disjointedness
        // Add a synthetic dimension, the purpose of which will become clear.
        mr.strides.emplace_back();
        mr.lanes.push_back(2);
        for (int i = 0; i < (int)amx_subtiles.size(); i++) {
            auto &other = amx_subtiles[i];
            // One of two things must be true:
            // 1) All of the lanes of mr equal the corresponding lane of
            // other. We've already checked the strides and lanes, so it's just
            // a matter of checking the base.
            if (can_prove(mr.base == other.base)) {
                return i;
            }

            // 2) None of the lanes or mr equal any of the lanes of other. To do
            // this we'll construct a combined mr that can be either 'mr' or
            // 'other', and ask if it's alias-free. This is what the synthetic
            // dimension was for.
            mr.strides.back() = mr.base - other.base;
            if (!can_prove(mr.alias_free())) {
                user_error
                    << "Failed to prove access to AMX does not partially overlap "
                    << "another distinct access: " << index;
                return -1;
            }
        }

        // Didn't already exist and didn't alias with anything.
        mr.strides.pop_back();
        mr.lanes.pop_back();
        amx_subtiles.push_back(std::move(mr));
        return (int)amx_subtiles.size() - 1;
    }

    // Returns an index expression for a given load or store index. user_asserts if impossible
    std::string get_subtile_name(const Expr &index) {
        int idx = get_subtile(index);
        internal_assert(idx >= 0);  // errors handled already
        return amx_name + std::to_string(idx);
    }

    Stmt visit(const Allocate *op) override {
        if (op->memory_type == MemoryType::AMXTile) {
            user_assert(
                (op->type.is_int() && op->type.bits() == 32) ||
                (op->type.is_float() && op->type.bits() == 32))
                << "scheduled tile operations must yield 32-bit integers or 32-bit floats";

            // We only support one live AMX allocation at a time for now
            user_assert(!in_allocate)
                << "Already in AMX allocation at allocation for " << op->name
                << ". We do not currently support multiple nested AMX matrix multiplies.";
            ScopedValue<string> old_amx_name(amx_name, op->name + ".amx.");
            ScopedValue<string> old_tile_name(tile_name, op->name);
            ScopedValue<bool> old_in_alloc(in_allocate, true);
            Stmt body = op->body;

            pass = 0;
            body = mutate(body);
            user_assert(found_I >= 0 && found_J >= 0 && found_K >= 0)
                << op->name << " is stored in AMXTile memory, but no matrix multiply "
                << "operation was found that stores to it, so the shape of the tile "
                << "was unable to be determined.\n";
            pass = 1;
            body = mutate(body);

            for (int i = 0; i < (int)amx_subtiles.size(); i++) {
                body = Allocate::make(amx_name + std::to_string(i), op->type.element_of(),
                                      MemoryType::AMXTile, {256}, const_true(), body);
            }
            return body;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Free *op) override {
        if (op->name != tile_name) {
            return op;
        }
        Stmt s;
        for (int i = 0; i < (int)amx_subtiles.size(); i++) {
            Stmt f = Free::make(amx_name + std::to_string(i));
            if (s.defined()) {
                s = Block::make(std::move(s), std::move(f));
            } else {
                s = std::move(f);
            }
        }
        return s;
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->name != tile_name) {
            return IRMutator::visit(op);
        }
        auto body = mutate(op->body);
        return ProducerConsumer::make(amx_name, op->is_producer, std::move(body));
    }

    Expr visit(const Load *op) override {
        user_assert(op->name != tile_name) << "AMX tile allocation used outside a tile instruction";
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        // There are three operations on a tile register:
        // 1) Zero-initialization
        // 2) Matrix multiply
        // 3) Stores to memory

        // For the matrix multiply we can deduce the tile shape. The stores to
        // memory and zero-intialization may be flat loads and stores, but to
        // emit the code we need to know the shape. We do two passes - in the
        // first we just recognize the matrix multiplies, and in the second we
        // recognize the initializations and stores.

        // All three convert ops either succeed, or do their own user_error internally.

        if (op->name != tile_name) {
            const auto *load = op->value.as<Load>();
            if (!load || load->name != tile_name) {
                return op;
            }
            if (pass == 1) {
                return convert_to_tile_store(op, get_subtile_name(load->index), found_I, found_J);
            } else {
                return op;
            }
        }

        std::string subtile_name = get_subtile_name(op->index);

        if (is_const_zero(op->value)) {
            if (pass == 1) {
                return convert_to_zero(op, subtile_name, found_I, found_J);
            } else {
                return op;
            }
        }

        if (pass == 0) {
            auto matmul = convert_to_matmul(op, subtile_name);
            user_assert((found_I < 0 || matmul.I == found_I) &&
                        (found_J < 0 || matmul.J == found_J) &&
                        (found_K < 0 || matmul.K == found_K))
                << "Found inconsistent tile sizes for AMX tile allocation across multiple "
                << "matrix multiplies that store to it.";
            found_I = matmul.I;
            found_J = matmul.J;
            found_K = matmul.K;
            return matmul.stmt;
        } else {
            return op;
        }
    }
};

}  // namespace

Stmt extract_tile_operations(const Stmt &s) {
    return ExtractTileOperations()(s);
}
}  // namespace Internal
}  // namespace Halide
