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
    // out[ramp] = reduce_add(widen(lhs[multiramp]) * widen(rhs[multiramp])) + out[ramp]
    //
    // Though if the multiramp has an outer dimension of stride zero it may have
    // been hoisted outwards to just a broadcast of the widened value.

    debug(3) << "Potential matmul:\n"
             << Stmt(op) << "\n";

    // The output's index must be the canonical ramp(0, 1, S).
    if (const Ramp *r = op->index.as<Ramp>();
        !r || !is_const_zero(r->base) || !is_const_one(r->stride)) {
        debug(3) << "Output index not simple ramp\n";
        return {};
    }

    // The RHS must be an add
    const auto *add = op->value.as<Add>();
    if (!add) {
        debug(3) << "RHS not an add\n";
        return {};
    }

    // The add must be between a vector reduce and a load. The simplifier will
    // have placed the vector reduce to the left, due to canonicalization of
    // commutative ops.
    Expr lhs = add->a;
    const auto *reduce = lhs.as<VectorReduce>();
    if (!reduce || reduce->op != VectorReduce::Add) {
        debug(3) << "LHS of add not a VectorReduce\n";
        return {};
    }

    // The load must be to the same addresses as the store (i.e. this is a +=)
    Expr rhs = add->b;
    const auto *load = rhs.as<Load>();
    if (!load || load->name != op->name || !equal(load->index, op->index)) {
        debug(3) << "Load doesn't match store\n";
        return {};
    }

    // There must be no predicate on the load or store
    if (!is_const_one(load->predicate) || !is_const_one(op->predicate)) {
        debug(3) << "Predicated\n";
        return {};
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
            debug(3) << "Reduce not an int cast\n";
            return {};
        }

        if (const auto *call = Call::as_intrinsic(cast->value, {Call::widening_mul})) {
            // Simplify to convert bit-math back to multiply, div, mod
            lhs = simplify(call->args[0]);
            rhs = simplify(call->args[1]);
        } else {
            debug(3) << "Reduce not a widening mul\n";
            return {};
        }

        if (lhs.type().bits() != 8 ||
            rhs.type().bits() != 8) {
            debug(3) << "Reduce not a widening mul of 8-bit integers\n";
            return {};
        }

    } else {
        // Lower a widening_mul intrinsic, as they can be used but aren't lifted to for bf16.
        Expr reduce_value = simplify(lower_intrinsics(reduce->value));
        const auto *mul = reduce_value.as<Mul>();
        if (!mul) {
            debug(3) << "Reduce not a multiply\n";
            return {};
        }
        lhs = mul->a;
        rhs = mul->b;
    }

    // There may be a broadcast next (it can get hoisted outside of other ops)
    auto debroadcast = [](Expr &e) -> int {
        if (const Broadcast *b = e.as<Broadcast>()) {
            e = b->value;
            return b->lanes;
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
            debug(3) << "No widening casts\n";
            return {};
        }
        lhs = lhs_cast->value;
        rhs = rhs_cast->value;
        if (!lhs.type().is_bfloat() ||
            rhs.type().element_of() != lhs.type().element_of()) {
            debug(3) << "Bad inner cast type: " << lhs.type() << " " << rhs.type() << "\n";
            return {};
        }
    }

    // Underneath all of this must be a load
    // TODO: What if we want to multiply by the same matrix multiple times? It might be a let binding.
    const auto *lhs_load = lhs.as<Load>();
    const auto *rhs_load = rhs.as<Load>();
    if (!lhs_load || !rhs_load) {
        debug(3) << "Not loads\n";
        return {};
    }
    // The loads must be unpredicated
    if (!is_const_one(lhs_load->predicate) || !is_const_one(rhs_load->predicate)) {
        debug(3) << "Loads predicated\n";
        return {};
    }

    // Now we analyze the load indices as multiramps
    MultiRamp lhs_mr, rhs_mr;
    Scope<Expr> empty_scope;
    if (!is_multiramp(lhs_load->index, empty_scope, &lhs_mr) ||
        !is_multiramp(rhs_load->index, empty_scope, &rhs_mr)) {
        debug(3) << "Indices not multiaffine: \n"
                 << "lhs: " << lhs_load->index << "\n"
                 << "rhs: " << rhs_load->index << "\n";
        return {};
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

    // Normalize by making the RHS the one with the trailing zero stride. If
    // neither side has one, this isn't a matmul we can recognize.
    auto has_trailing_zero = [](const MultiRamp &mr) {
        return !mr.lanes.empty() && is_const_zero(mr.strides.back());
    };
    if (!has_trailing_zero(rhs_mr)) {
        if (!has_trailing_zero(lhs_mr)) {
            debug(3) << "Neither side has a trailing stride-zero dim\n";
            return {};
        }
        std::swap(lhs, rhs);
        std::swap(lhs_mr, rhs_mr);
        std::swap(lhs_load, rhs_load);
    }

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
    // 1) Deduce what Ki, Ko, I, J are.
    // 2) extract those two question marks, and validate the
    // rest is as-expected.

    // The reduction's K dimension will be split into inner and outer elements.
    int element_width = lhs_load->type.bytes();
    int K = reduce->value.type().lanes() / reduce->type.lanes();
    int Ki = 4 / element_width;
    int Ko = K / Ki;

    // I is the extent of the trailing stride-zero dim of the RHS (validated
    // above by the swap-or-fail step). J follows from the output lane count.
    int I = rhs_mr.lanes.back();
    int J = reduce->type.lanes() / I;

    // Coerce both MRs into the canonical [Ki, Ko, J, I] shape (innermost
    // first). When Ko == 1, the second slot is just an extent-1 dim and
    // strides_for_shape will return a 0 stride there. The expected strides
    // we'll then validate are:
    //   lhs: [1, Ki, 0, ?]   (with `?` the LHS row stride)
    //   rhs: [1, ?, Ki, 0]   (with `?` the RHS row stride between Ko chunks)
    std::vector<int> shape{Ki, Ko, J, I};
    std::vector<Expr> lhs_strides, rhs_strides;
    if (!lhs_mr.strides_for_shape(shape, &lhs_strides)) {
        debug(3) << "lhs_mr has incompatible shape\n";
        return {};
    }
    if (!rhs_mr.strides_for_shape(shape, &rhs_strides)) {
        debug(3) << "rhs_mr has incompatible shape\n";
        return {};
    }

    if (!is_const_one(lhs_strides[0]) || !is_const_one(rhs_strides[0])) {
        debug(3) << "Innermost stride not 1\n";
        return {};
    }
    if (Ko > 1 && !is_const(lhs_strides[1], Ki)) {
        debug(3) << "lhs stride Ko not Ki\n";
        return {};
    }
    if (!is_const_zero(lhs_strides[2])) {
        debug(3) << "lhs stride J not 0\n";
        return {};
    }
    if (!is_const(rhs_strides[2], Ki)) {
        debug(3) << "rhs stride J not Ki\n";
        return {};
    }
    if (!is_const_zero(rhs_strides[3])) {
        debug(3) << "rhs stride I not 0\n";
        return {};
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
    auto out_load = Load::make(res_type, new_name, Ramp::make(0, 1, 256), {}, {}, const_true(256), {});

    auto matmul = Call::make(res_type, "tile_matmul",
                             {I, col_bytes, K, out_load, lhs_call, rhs_call},
                             Call::Intrinsic);
    auto store = Store::make(new_name, matmul, Ramp::make(0, 1, 256), Parameter(), const_true(256), ModulusRemainder());
    return {true, std::move(store), I, J, K};
}

Stmt convert_to_zero(const Store *op, int I, int J, const string &new_name) {
    const auto *ramp = op->index.as<Ramp>();
    if (!ramp ||
        !is_const_one(ramp->stride) ||
        !is_const_zero(ramp->base) ||
        !is_const_zero(op->value) ||
        op->value.type().lanes() != I * J) {
        return {};
    }
    auto rows = Cast::make(Int(16), I);
    auto bytes = op->value.type().bytes();
    auto colbytes = Cast::make(Int(16), J * bytes);
    const auto &store_type = op->value.type();
    auto tile_zero_type = store_type.with_lanes(1024 / store_type.bytes());
    auto val = Call::make(tile_zero_type, "tile_zero", {rows, colbytes}, Call::Intrinsic);
    return Store::make(new_name, std::move(val), Ramp::make(0, 1, 256), Parameter(), const_true(256), ModulusRemainder());
}

Stmt convert_to_tile_store(const Store *op, const string &amx_name, int I, int J) {
    debug(3) << "Considering tile store: " << Stmt(op);
    if (!is_const_one(op->predicate)) {
        debug(3) << "Predicated\n";
        return {};
    }
    MultiRamp mr;
    if (!is_multiramp(op->index, Scope<Expr>::empty_scope(), &mr)) {
        debug(3) << "Index not a multiramp\n";
        return {};
    }
    if (mr.total_lanes() != I * J) {
        debug(3) << "Index has wrong number of lanes\n";
        return {};
    }

    // Coerce the index into the canonical 2D shape: stride-1 inner of
    // lanes J, row-stride outer of lanes I. Either dim may be
    // extent 1 — strides_for_shape returns a zero stride for those slots.
    std::vector<Expr> mr_strides;
    if (!mr.strides_for_shape({J, I}, &mr_strides)) {
        debug(3) << "Index has incompatible shape\n";
        return {};
    }
    if (J > 1 && !is_const_one(mr_strides[0])) {
        debug(3) << "Inner stride not 1\n";
        return {};
    }
    Expr x_stride = mr_strides[1];

    auto out_var = Variable::make(Handle(), op->name);
    auto tile_type = op->value.type().with_lanes(256);
    auto tile_val = Load::make(tile_type, amx_name, Ramp::make(0, 1, 256), {}, {}, const_true(256), {});
    auto bytes = op->value.type().bytes();
    internal_assert(bytes == 4) << "AMX store only supported for int32 and float32 output, not for " << op->value.type() << "\n";
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
    vector<Stmt> pending_stores;
    bool in_allocate = false;
    int found_I = -1;
    int found_J = -1;
    int found_K = -1;

    Stmt visit(const Allocate *op) override {
        if (op->memory_type == MemoryType::AMXTile) {
            user_assert(
                (op->type.is_int() && op->type.bits() == 32) ||
                (op->type.is_float() && op->type.bits() == 32))
                << "scheduled tile operations must yield 32-bit integers or 32-bit floats";

            user_assert(!in_allocate) << "Already in AMX allocation: " << amx_name;
            ScopedValue<string> old_amx_name(amx_name, op->name + ".amx");
            ScopedValue<string> old_tile_name(tile_name, op->name);
            ScopedValue<bool> old_in_alloc(in_allocate, true);
            Stmt body = op->body;

            pending_stores.clear();
            body = mutate(body);
            if (found_I < 0 || found_J < 0 || found_K < 0) {
                return op;
            }
            if (!pending_stores.empty()) {
                body = mutate(body);
            }

            // Always size 256, regardless of how big the matrix multiply was
            return Allocate::make(amx_name, op->type.element_of(),
                                  MemoryType::AMXTile, {256}, const_true(), body);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Free *op) override {
        if (op->name != tile_name) {
            return op;
        }
        return Free::make(amx_name);
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
        if (op->name != tile_name) {
            const auto *load = op->value.as<Load>();
            if (!load || load->name != tile_name) {
                return op;
            }
            auto store = convert_to_tile_store(op, amx_name, found_I, found_J);
            user_assert(store.defined()) << "Store to AMX tile allocation of a non-tile value";
            return store;
        }

        auto matmul = convert_to_matmul(op, amx_name);
        if (matmul.result) {
            user_assert(
                (found_I < 0 || matmul.I == found_I) &&
                (found_J < 0 || matmul.J == found_J) &&
                (found_K < 0 || matmul.K == found_K))
                << "Found different tile sizes for AMX tile allocation";
            found_I = matmul.I;
            found_J = matmul.J;
            found_K = matmul.K;

            return matmul.stmt;
        }

        if (found_I < 0 || found_J < 0) {
            pending_stores.emplace_back(op);
            return op;
        }

        auto zero = convert_to_zero(op, found_I, found_J, amx_name);
        if (zero.defined()) {
            return zero;
        }

        user_error << "Found non-tile operations for AMX tile allocation";
        return op;
    }
};

}  // namespace

Stmt extract_tile_operations(const Stmt &s) {
    return ExtractTileOperations()(s);
}
}  // namespace Internal
}  // namespace Halide
