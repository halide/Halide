#include "ExtractTileOperations.h"

#include "IRMatch.h"  // expr_match
#include "IRMutator.h"
#include "IROperator.h"  // Expr + Expr
#include "Util.h"        // ScopedValue

namespace Halide {
namespace Internal {

namespace {

template<int Dim>
struct Tile {
    bool result;
    Expr base;
    Expr stride[Dim];
    int extent[Dim];
};

const auto wild_i32 = Variable::make(Int(32), "*");
const auto wild_i32x = Variable::make(Int(32, 0), "*");

Tile<2> is_2d_tile_index(const Expr &e) {
    // ramp(ramp(base, 1, 4), x4(stride), 4)
    std::vector<Expr> matches;
    if (const auto *r1 = e.as<Ramp>()) {
        if (const auto *r2 = r1->base.as<Ramp>()) {
            auto ramp_2d_pattern = Ramp::make(Ramp::make(wild_i32, wild_i32, r2->lanes), Broadcast::make(wild_i32, r2->lanes), r1->lanes);
            if (expr_match(ramp_2d_pattern, e, matches)) {
                return {true, std::move(matches[0]), {std::move(matches[2]), std::move(matches[1])}, {r1->lanes, r2->lanes}};
            }
        }
    }
    return {};
}

Tile<3> is_3d_tile_index(const Expr &e) {
    std::vector<Expr> matches;
    auto add_sub_pattern = (wild_i32x + wild_i32x) - wild_i32x;
    if (!expr_match(add_sub_pattern, e, matches)) { return {}; }
    // ramp(x16(base), x16(stride), 4) + x16(ramp(idx, 1, 4)) y: 4, x: 4, r: 4
    // ramp(x10(base), x10(stride), 3) + x6(ramp(idx, 1, 5))  y: 2, x: 3, r: 5
    Expr first = std::move(matches[0]);
    Expr second = std::move(matches[1]);
    Expr adj = std::move(matches[2]);
    const auto *r1 = first.as<Ramp>();
    const auto *b2 = second.as<Broadcast>();
    if (!r1 && !b2) {
        // Try switching the order
        r1 = second.as<Ramp>();
        b2 = first.as<Broadcast>();
    }
    if (!r1 || !b2) { return {}; }

    const auto *b1 = r1->base.as<Broadcast>();
    const auto *r2 = b2->value.as<Ramp>();

    if (!b1 || !r2) { return {}; }

    int x_tile = r1->lanes;
    int r_tile = r2->lanes;
    int y_tile = b1->lanes / r_tile;
    if (y_tile != b2->lanes / x_tile) { return {}; }

    auto pattern1 = Ramp::make(Broadcast::make(wild_i32, b1->lanes), Broadcast::make(wild_i32, b1->lanes), r1->lanes);
    if (!expr_match(pattern1, first, matches)) { return {}; }
    Expr base = std::move(matches[0]);
    Expr x_stride = std::move(matches[1]);

    auto pattern2 = Broadcast::make(Ramp::make(wild_i32, wild_i32, r2->lanes), b2->lanes);
    if (!expr_match(pattern2, second, matches)) { return {}; }
    base += std::move(matches[0]);
    Expr r_stride = std::move(matches[1]);

    auto pattern3 = Broadcast::make(wild_i32, b1->lanes * r1->lanes);
    if (!expr_match(pattern3, adj, matches)) { return {}; }
    base -= std::move(matches[0]);

    return {true, base, {x_stride, 0, r_stride}, {x_tile, y_tile, r_tile}};
}

struct NewMatmul {
    bool result = false;
    Stmt stmt;
    int tile_x;
    int tile_y;
    int tile_r;
};

NewMatmul
convert_to_matmul(const Store *op, const std::string &new_name) {
    // m[ramp(0, 1, S)] = VectorAdd(lhs[{XYR tile}] * xX(rhs[{YR tile}])) + m[ramp(0, 1, S)]
    const auto wild_i8x = Variable::make(Int(8, 0), "*");
    const auto wild_i16x = Variable::make(Int(16, 0), "*");
    std::vector<Expr> matches;
    const auto pattern1 = wild_i32x + wild_i32x;
    if (!expr_match(pattern1, op->value, matches)) { return {}; }
    const auto *reduce = matches[0].as<VectorReduce>();
    const auto *load = matches[1].as<Load>();
    if (!reduce || reduce->op != VectorReduce::Add) { return {}; }
    if (!load || load->name != op->name || !equal(load->index, op->index)) { return {}; }

    // FIXME: Add support for uint8 and bf16 for LLVM 13+
    auto pattern2 = cast(Int(32, 0), cast(Int(16, 0), wild_i8x) * wild_i16x);
    if (!expr_match(pattern2, reduce->value, matches)) { return {}; }
    const auto *lhs_load = matches[0].as<Load>();
    // FIXME: When tile_r is not 4 the broadcast is inside the index, not of the value
    const auto *rhs_broadcast = matches[1].as<Broadcast>();
    if (!lhs_load || !rhs_broadcast) { return {}; }
    const auto *rhs_cast = rhs_broadcast->value.as<Cast>();
    if (!rhs_cast || rhs_cast->value.type().element_of() != Int(8)) { return {}; }
    const auto *rhs_load = rhs_cast->value.as<Load>();
    if (!rhs_load) { return {}; }

    const auto lhs_tile = is_3d_tile_index(lhs_load->index);
    const auto rhs_tile = is_2d_tile_index(rhs_load->index);
    // FIXME: When tile_r is not 4 the RHS load will be 4D (x, r/4, y, r%4)
    if (!lhs_tile.result || !rhs_tile.result) { return {}; }

    const int tile_x = lhs_tile.extent[0];
    const int tile_y = lhs_tile.extent[1];
    const int tile_r = lhs_tile.extent[2];
    const int factor = reduce->value.type().lanes() / reduce->type.lanes();
    if (op->index.type().lanes() != tile_x * tile_y ||
        factor != tile_r ||
        tile_y != rhs_tile.extent[0] ||
        tile_r != rhs_tile.extent[1]) {
        return {};
    }

    // {rows, colbytes, var, index}
    auto lhs_var = Variable::make(Handle(), lhs_load->name);
    auto lhs = Call::make(Int(8, 1024), "tile_load", {tile_x, tile_r, lhs_var, lhs_tile.base, lhs_tile.stride[0]}, Call::Intrinsic);
    auto rhs_var = Variable::make(Handle(), rhs_load->name);
    auto rhs = Call::make(Int(8, 1024), "tile_load", {1, tile_y * tile_r, rhs_var, rhs_tile.base, rhs_tile.stride[0]}, Call::Intrinsic);

    // {rows, colbytes, acc, out, lhs, rhs}
    auto out = Load::make(Int(32, 256), new_name, Ramp::make(0, 1, 256), {}, {}, const_true(256), {});
    auto colbytes = tile_y * 32 / rhs_load->type.bits();
    auto matmul = Call::make(Int(32, 256), "tile_matmul", {tile_x, colbytes, tile_r, out, lhs, rhs}, Call::Intrinsic);
    auto store = Store::make(new_name, matmul, Ramp::make(0, 1, 256), Parameter(), const_true(256), ModulusRemainder());
    return {true, std::move(store), tile_x, tile_y, tile_r};
}

Stmt convert_to_zero(const Store *op, int tile_x, int tile_y, const std::string &new_name) {
    if (const auto *ramp = op->index.as<Ramp>()) {
        if (const auto *bcast = op->value.as<Broadcast>()) {
            if (is_const_one(ramp->stride) &&
                is_const_zero(bcast->value) &&
                (bcast->lanes == tile_x * tile_y)) {
                auto rows = Cast::make(Int(16), tile_x);
                auto bytes = op->value.type().bytes();
                auto colbytes = Cast::make(Int(16), tile_y * bytes);
                auto val = Call::make(Int(32, 256), "tile_zero", {rows, colbytes}, Call::Intrinsic);
                auto store = Store::make(new_name, val, Ramp::make(0, 1, 256), Parameter(), const_true(256), ModulusRemainder());
                return store;
            }
        }
    }
    return {};
}

Stmt convert_to_tile_store(const Store *op, const std::string &amx_alloc, int tile_x, int tile_y) {
    auto tile = is_2d_tile_index(op->index);
    if (tile.result && tile.extent[0] == tile_x && tile.extent[1] == tile_y) {
        auto out = Variable::make(Handle(), op->name);
        auto tile_val = Load::make(Int(32, 256), amx_alloc, Ramp::make(0, 1, 256), {}, {}, const_true(256), {});
        auto bytes = op->value.type().bytes();
        internal_assert(bytes == 4) << "AMX store only supported for int32 and float32, not for " << op->value.type() << "\n";
        // {tile_x, tile_y, var, base, stride}
        auto store = Call::make(Bool(2), "tile_store", {tile_x, tile_y * bytes, out, tile.base * bytes, tile.stride[0] * bytes, tile_val}, Call::Intrinsic);
        return Evaluate::make(store);
    }
    return {};
}

class ExtractTileOperations : public IRMutator {
    using IRMutator::visit;

    std::string tile_name;
    std::string amx_alloc;
    std::vector<Stmt> pending_stores;
    bool is_valid = true;
    bool in_allocate = false;
    int found_tile_x = -1;
    int found_tile_y = -1;
    int found_tile_r = -1;

    Stmt visit(const Allocate *op) override {
        if (op->memory_type == MemoryType::AMXTile &&
            op->type.is_int() &&
            op->type.bits() == 32) {
            if (in_allocate) {
                // Found two possible tile allocations
                // FIXME: Handle this better
                is_valid = false;
                return op;
            }
            amx_alloc = op->name + ".amx";
            tile_name = op->name;
            ScopedValue<bool> old_in_alloc(in_allocate, true);
            Stmt body = op->body;

            pending_stores.clear();
            body = mutate(body);
            if (!is_valid) {
                return op;
            }
            if (found_tile_x < 0 || found_tile_y < 0 || found_tile_r < 0) {
                return op;
            }
            if (!pending_stores.empty()) {
                // Really only need to go over the pending stores
                body = mutate(body);
            }
            if (!is_valid) {
                return op;
            }

            return Allocate::make(amx_alloc, Int(32, 256), MemoryType::AMXTile, {1}, const_true(), body);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Free *op) override {
        if (op->name != tile_name) {
            return op;
        }
        return Free::make(amx_alloc);
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->name != tile_name) {
            return IRMutator::visit(op);
        }

        auto body = mutate(op->body);
        return ProducerConsumer::make(amx_alloc, op->is_producer, body);
    }

    Expr visit(const Load* op) override {
        if (op->name == tile_name) {
          // Any tile load will be matched elsewhere, so a load here means that
          // the AMX tile is used outside of a tile instruction.
          is_valid = false;
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        if (op->name != tile_name) {
            const auto *load = op->value.as<Load>();
            if (!load || load->name != tile_name) {
                return op;
            }
            auto store = convert_to_tile_store(op, amx_alloc, found_tile_x, found_tile_y);
            if (store.defined()) {
                return store;
            } else {
                // Found store of tile_name that is not a tile store.
                is_valid = false;
                return op;
            }
        }

        auto matmul = convert_to_matmul(op, amx_alloc);
        if (matmul.result) {
            if ((found_tile_x > 0 && matmul.tile_x != found_tile_x) ||
                (found_tile_r > 0 && matmul.tile_r != found_tile_r) ||
                (found_tile_y > 0 && matmul.tile_y != found_tile_y)) {
                is_valid = false;
                return op;
            }
            found_tile_x = matmul.tile_x;
            found_tile_y = matmul.tile_y;
            found_tile_r = matmul.tile_r;
            return matmul.stmt;
        }

        if (found_tile_x < 0 || found_tile_y < 0) {
            pending_stores.emplace_back(op);
            return op;
        }

        auto zero = convert_to_zero(op, found_tile_x, found_tile_y, amx_alloc);
        if (zero.defined()) {
            return zero;
        }

        // Otherwise there is some other operation using the allocation, so we cannot use the AMX instructions
        is_valid = false;
        return op;
    }
};

}  // namespace

Stmt extract_tile_operations(const Stmt &s) {
    return ExtractTileOperations().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
