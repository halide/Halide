#include "CodeGen_TileIR_Dev.h"

#include "CodeGen_GPU_Dev.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "DeviceArgument.h"
#include "FindIntrinsics.h"
#include "IROperator.h"
#include "ExprUsesVar.h"
#include "FlattenNestedRamps.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Scope.h"
#include "StrictifyFloat.h"
#include "TileIR.h"

#include <fstream>
#include <limits>
#include <sstream>

namespace Halide {
namespace Internal {

namespace {

using namespace TileIR;

// Rewrite signed Div/Mod as calls to div_round_to_zero / mod_round_to_zero
// (Tile IR's native DivIOp/RemIOp semantics) plus sign-correction. Halide's
// IR Div/Mod are Euclidean; Tile IR's are truncating toward zero. We do
// this as a Stmt-level pre-pass to amortize simplify+CSE once over the
// whole kernel rather than blowing up the Expr tree at every Div/Mod site.
class LowerEuclideanDivMod : public IRMutator {
    using IRMutator::visit;

    Expr visit(const Div *op) override {
        if (op->type.is_int()) {
            return mutate(lower_euclidean_div(op->a, op->b));
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Mod *op) override {
        if (op->type.is_int()) {
            return mutate(lower_euclidean_mod(op->a, op->b));
        }
        return IRMutator::visit(op);
    }
};


// Pattern-match a Halide VectorReduce(Add, Mul(A, B), M*N) + Load(acc)
// inside a Store and rewrite it to a Call to "tile_ir_mmaf" carrying
// the 2D-tile flat loads as first-class 1D Halide vectors. The Tile IR
// backend lowers that call to cuda_tile.mmaf via reshape ops.
class LowerMma : public IRMutator {
    using IRMutator::visit;

    // Peel off casts / bitcasts that only change element type.
    static Expr strip_cast(const Expr &e) {
        if (auto c = e.as<Cast>()) return c->value;
        return e;
    }

    // Try to decompose a Load index of shape MNK lanes produced by
    // Ramp(Broadcast(outer_base, MN), Broadcast(outer_stride, MN), M)
    //   + Broadcast(Ramp(inner_base, inner_stride, K), MN)
    // Returns true on success with the extracted pieces. The
    // 2D tile is interpreted as shape [M, K] in row-major order with
    // address = outer_base + i*outer_stride + inner_base + k*inner_stride.
    static bool parse_lhs_index(const Expr &idx, int M, int K, int MN,
                                Expr &outer_base, Expr &outer_stride,
                                Expr &inner_base, Expr &inner_stride) {
        const Add *add = idx.as<Add>();
        if (!add) return false;
        const Ramp *outer = add->a.as<Ramp>();
        const Broadcast *bcast = add->b.as<Broadcast>();
        if (!outer || !bcast) {
            // Try swapped order.
            outer = add->b.as<Ramp>();
            bcast = add->a.as<Broadcast>();
            if (!outer || !bcast) return false;
        }
        if (outer->lanes != M) return false;
        const Broadcast *obase = outer->base.as<Broadcast>();
        const Broadcast *ostride = outer->stride.as<Broadcast>();
        if (!obase || !ostride) return false;
        if (obase->lanes != MN || ostride->lanes != MN) return false;
        const Ramp *inner = bcast->value.as<Ramp>();
        if (!inner || inner->lanes != K) return false;
        outer_base = obase->value;
        outer_stride = ostride->value;
        inner_base = inner->base;
        inner_stride = inner->stride;
        return true;
    }

    // Try to decompose a Load index of shape K*N lanes produced by
    // Ramp(Ramp(inner_base, inner_stride, K), Broadcast(outer_stride, K), N)
    // interpreted as shape [K, N] with address = inner_base + k*inner_stride
    // + n*outer_stride.
    static bool parse_rhs_index(const Expr &idx, int K, int N,
                                Expr &inner_base, Expr &inner_stride,
                                Expr &outer_stride) {
        const Ramp *outer = idx.as<Ramp>();
        if (!outer || outer->lanes != N) return false;
        const Ramp *inner = outer->base.as<Ramp>();
        const Broadcast *ostride = outer->stride.as<Broadcast>();
        if (!inner || !ostride) return false;
        if (inner->lanes != K || ostride->lanes != K) return false;
        inner_base = inner->base;
        inner_stride = inner->stride;
        outer_stride = ostride->value;
        return true;
    }

    Stmt visit(const Store *store_op) override {
        const Add *add = store_op->value.as<Add>();
        if (!add) return IRMutator::visit(store_op);

        Expr a_stripped = strip_cast(add->a);
        Expr b_stripped = strip_cast(add->b);

        const VectorReduce *vr = nullptr;
        Expr acc_side;
        if (auto *v = a_stripped.as<VectorReduce>()) { vr = v; acc_side = b_stripped; }
        else if (auto *v = b_stripped.as<VectorReduce>()) { vr = v; acc_side = a_stripped; }
        if (!vr || vr->op != VectorReduce::Add) return IRMutator::visit(store_op);

        // acc must be a Load of the same buffer at the same index.
        const Load *acc_ld = acc_side.as<Load>();
        if (!acc_ld || acc_ld->name != store_op->name) return IRMutator::visit(store_op);
        if (!equal(acc_ld->index, store_op->index)) return IRMutator::visit(store_op);

        // The body of the reduce should be Mul(lhs, rhs).
        const Mul *mul = vr->value.as<Mul>();
        if (!mul) return IRMutator::visit(store_op);

        int MN = vr->type.lanes();
        int MNK = mul->type.lanes();
        if (MN == 0 || MNK % MN != 0) return IRMutator::visit(store_op);
        int K = MNK / MN;
        Halide::Type acc_type = vr->type;

        // Unwrap casts to get the raw f16 loads.
        Expr lhs_inner = strip_cast(mul->a);
        Expr rhs_inner = strip_cast(mul->b);

        // The broadcast-over-M side is the "B" operand (rhs of mmaf):
        // Broadcast(Cast(fp_MN, B_load_KN), M) so it has MN*M = MNK lanes.
        const Broadcast *rhs_bc = rhs_inner.as<Broadcast>();
        const Broadcast *lhs_bc = lhs_inner.as<Broadcast>();
        bool rhs_is_b = rhs_bc && rhs_bc->value.type().lanes() * rhs_bc->lanes == MNK;
        bool lhs_is_b = !rhs_is_b && lhs_bc && lhs_bc->value.type().lanes() * lhs_bc->lanes == MNK;
        if (!rhs_is_b && !lhs_is_b) return IRMutator::visit(store_op);

        Expr B_side = rhs_is_b ? rhs_inner : lhs_inner;
        Expr A_side = rhs_is_b ? lhs_inner : rhs_inner;
        const Broadcast *B_bc = B_side.as<Broadcast>();
        int M = B_bc->lanes;
        if (M <= 0 || MN % M != 0) return IRMutator::visit(store_op);
        int N = MN / M;
        if (M * N * K != MNK) return IRMutator::visit(store_op);

        // Unwrap the cast on the broadcast's inner value to get the B load.
        Expr B_load_expr = strip_cast(B_bc->value);
        const Load *B_ld = B_load_expr.as<Load>();
        const Load *A_ld = A_side.as<Load>();
        if (!A_ld || !B_ld) return IRMutator::visit(store_op);

        // Decompose indices.
        Expr A_ob, A_os, A_ib, A_is;
        if (!parse_lhs_index(A_ld->index, M, K, MN, A_ob, A_os, A_ib, A_is)) {
            return IRMutator::visit(store_op);
        }
        Expr B_ib, B_is, B_os;
        if (!parse_rhs_index(B_ld->index, K, N, B_ib, B_is, B_os)) {
            return IRMutator::visit(store_op);
        }

        // Pass the matrix-load *structure* through to the codegen as a
        // Call intrinsic. The Tile IR backend will build a 2-D pointer
        // tile and emit a single OpLoadPtrTkoOp per matrix, which is the
        // shape tileiras can fold into LDSM/MMA.
        //
        // We thread the buffer identity as a 1-element Load (so the
        // codegen can look up the kernel-arg ID for the buffer) and the
        // 2-D shape and strides as separate scalar Exprs.
        Halide::Type A_elem = A_ld->type.element_of();
        Halide::Type B_elem = B_ld->type.element_of();
        Expr A_row_start = simplify(A_ob + A_ib);
        Expr A_id_load = Load::make(A_elem, A_ld->name, A_row_start,
                                    A_ld->image, A_ld->param,
                                    const_true(), ModulusRemainder());
        Expr B_id_load = Load::make(B_elem, B_ld->name, B_ib,
                                    B_ld->image, B_ld->param,
                                    const_true(), ModulusRemainder());

        // tile_ir_mmaf args:
        //   0: A_id_load (scalar Load -- buffer ident; index = A 2-D base)
        //   1: A_row_stride (Expr, scalar)
        //   2: A_col_stride (Expr, scalar)
        //   3: B_id_load    (scalar Load -- buffer ident; index = B 2-D base)
        //   4: B_row_stride (K stride for B[k,n])
        //   5: B_col_stride (N stride for B[k,n])
        //   6: acc_load     (existing 1-D Load of the M*N accumulator tile)
        //   7-9: M, K, N    (constants)
        Expr mma_call = Call::make(acc_type, "tile_ir_mmaf",
            {A_id_load, A_os, A_is,
             B_id_load, B_is, B_os,
             acc_side,
             IntImm::make(Int(32), M),
             IntImm::make(Int(32), K),
             IntImm::make(Int(32), N)},
            Call::Intrinsic);

        return Store::make(store_op->name, mma_call, store_op->index,
                           store_op->param, store_op->predicate,
                           store_op->alignment);
    }
};

// Per-op encoding follows the pattern generated by cuda-tile-tblgen:
//   opcode(varint)
//   [numResults(varint) if op.isVariadic() -- true when op has ANY Variadic operands OR results]
//   resultTypeIndices(varint per result)
//   [flags(varint) if any optional attrs/operands]
//   attributes (in ODS declaration order, inline for enums, self-contained for others)
//   operands (varint indices, with size prefix if variadic/optional operands)
//   [regions if any]

class TileIR_Emitter : public IRVisitor {
public:
    TileIR_Emitter(TileIR::Module &mod, TileIR::Function &func)
        : module(mod), function(func) {
    }

    uint32_t current_id = 0;
    Scope<uint32_t> symbol_table;
    // Maps buffer-arg name → its registered element type (the T in
    // tile<ptr<T>>). We need this when a load/store uses a different
    // element type (e.g. clustered heap allocs declared as uint8 but
    // accessed as int16/int32), so we can insert a PtrToPtr cast to
    // avoid type-mismatch failures in OffsetOp.
    std::map<std::string, Halide::Type> buffer_elem_types;

private:
    TileIR::Module &module;
    TileIR::Function &function;

    // Track number of operations emitted in the current block scope.
    // Used by ForOp/IfOp to write numOps for region blocks.
    uint32_t op_count = 0;

    Encoder &body() {
        return function.body;
    }

    uint32_t alloc_id() {
        return function.alloc_id();
    }

    // Write an opcode varint and increment the op counter.
    void emit_op(Opcode opcode) {
        if (getenv("HL_TILEIR_TRACE")) {
            debug(0) << "emit_op: #" << op_count << " opcode=0x" << std::hex
                     << (int)opcode << std::dec << "\n";
        }
        body().write_varint(static_cast<uint64_t>(opcode));
        op_count++;
    }

    // Begin a block scope: swap the encoder to a temporary one, reset op_count.
    // Returns saved state (encoder bytes, op_count, ssa_id) for restoration.
    struct BlockScope {
        Encoder saved_body;
        uint32_t saved_op_count;
        uint32_t saved_next_ssa_id;
        uint32_t saved_current_token;
    };

    BlockScope begin_block_scope() {
        BlockScope scope;
        scope.saved_op_count = op_count;
        scope.saved_next_ssa_id = function.next_ssa_id;
        scope.saved_current_token = current_token;
        op_count = 0;
        // Tokens defined outside this region cannot be referenced inside;
        // start fresh and allow the inner body to build its own token chain.
        current_token = UINT32_MAX;
        std::swap(scope.saved_body, function.body);
        return scope;
    }

    // End a block scope: write block header (numArgs, argTypes, numOps) + body
    // to the restored encoder, and roll back SSA IDs.
    void end_block_scope(BlockScope &scope, const std::vector<uint32_t> &arg_type_idxs) {
        uint32_t block_op_count = op_count;
        Encoder block_body;
        std::swap(block_body, function.body);
        // Restore the original encoder
        std::swap(scope.saved_body, function.body);

        // Write block header
        body().write_varint(arg_type_idxs.size());  // numArgs
        for (auto tid : arg_type_idxs) {
            body().write_varint(tid);  // arg type
        }
        body().write_varint(block_op_count);  // numOps

        // Write block body bytes
        body().write_bytes(block_body.data().data(), block_body.size());

        // Restore op count, SSA IDs, and token (block scope is local)
        op_count = scope.saved_op_count;
        function.next_ssa_id = scope.saved_next_ssa_id;
        current_token = scope.saved_current_token;
    }

    // Get the tile type index for a Halide type. In Tile IR, everything is a tile.
    uint32_t type_idx(const Halide::Type &t) {
        return module.types.get_type_idx(t);
    }

    // Get a rank-1 tile type, even for lanes==1. Needed for ExtractOp results
    // which must match the rank of their source (rank 1). Pads to power of 2.
    uint32_t type_idx_1d(const Halide::Type &elem_type, int lanes) {
        return module.types.get_1d_tile_type_idx(elem_type, lanes);
    }

    // Get a rank-1 tile type with exact lane count (no padding).
    // Use for CatOp results where size must = sum of input sizes.
    uint32_t type_idx_exact_1d(const Halide::Type &elem_type, int lanes) {
        return module.types.get_exact_1d_tile_type_idx(elem_type, lanes);
    }

    // --- Float binary ops: AddFOp, SubFOp, MulFOp, DivFOp ---
    // Format: opcode | resultType | flags(varint, 1 bit for flush_to_zero UnitAttr) |
    //         rounding_mode(varint enum) | lhs(varint) | rhs(varint)
    uint32_t emit_float_binop(Opcode op, const Halide::Type &t, uint32_t lhs, uint32_t rhs) {
        uint32_t id = alloc_id();
        emit_op(op);
        body().write_varint(type_idx(t));
        body().write_varint(0);                                        // flags: flush_to_zero = not present (bit 0 = 0)
        body().write_varint(static_cast<uint32_t>(RoundNearestEven));  // rounding_mode
        body().write_varint(lhs);
        body().write_varint(rhs);
        return id;
    }

    // --- Integer binary ops with optional overflow: AddIOp, SubIOp, MulIOp, ShLIOp ---
    // Format: opcode | resultType | flags(varint, 1 bit for overflow DefaultValuedAttr) |
    //         [overflow(varint enum) if flag set] | lhs(varint) | rhs(varint)
    uint32_t emit_int_binop_overflow(Opcode op, const Halide::Type &t, uint32_t lhs, uint32_t rhs) {
        uint32_t id = alloc_id();
        emit_op(op);
        body().write_varint(type_idx(t));
        body().write_varint(0);  // flags: overflow not present (uses default NONE)
        body().write_varint(lhs);
        body().write_varint(rhs);
        return id;
    }

    // --- Simple binary ops with no attrs: AndIOp, OrIOp, XOrIOp, RemFOp ---
    // Format: opcode | resultType | lhs(varint) | rhs(varint)
    uint32_t emit_simple_binop(Opcode op, const Halide::Type &t, uint32_t lhs, uint32_t rhs) {
        uint32_t id = alloc_id();
        emit_op(op);
        body().write_varint(type_idx(t));
        body().write_varint(lhs);
        body().write_varint(rhs);
        return id;
    }

    // --- Unary ops with no attrs: AbsFOp, AbsIOp, NegFOp, NegIOp, BitcastOp, ReshapeOp ---
    // Format: opcode | resultType | source(varint)
    uint32_t emit_unary_op(Opcode op, const Halide::Type &t, uint32_t src) {
        uint32_t id = alloc_id();
        emit_op(op);
        body().write_varint(type_idx(t));
        body().write_varint(src);
        return id;
    }

    // Broadcast a scalar tile to a vector tile.
    // BroadcastOp requires same rank, so we reshape scalar (rank 0) to
    // tile<1xT> first, then broadcast to tile<NxT>.
    uint32_t emit_broadcast(const Halide::Type &result_type, uint32_t src) {
        internal_assert(result_type.lanes() > 1);
        // Reshape: tile<T> → tile<1xT>
        uint32_t scalar_idx = module.types.add_scalar(result_type.element_of());
        uint32_t tile_1_idx = module.types.add_tile({1}, scalar_idx);
        uint32_t reshaped = alloc_id();
        emit_op(OpReshapeOp);
        body().write_varint(tile_1_idx);
        body().write_varint(src);

        // Broadcast: tile<1xT> → tile<NxT>
        uint32_t id = alloc_id();
        emit_op(OpBroadcastOp);
        body().write_varint(type_idx(result_type));
        body().write_varint(reshaped);
        return id;
    }

    // Emit an ExtractOp. ExtractOp requires AllRanksMatch, so when extracting
    // from a 1D source to get a scalar result, we extract tile<1xT> then reshape.
    uint32_t emit_extract(const Halide::Type &result_type, uint32_t src_id,
                          int src_lanes, uint32_t index_id) {
        // ExtractOp result must have same rank as source.
        // Source is 1D (tile<NxT>), so result must be 1D too.
        Halide::Type elem_type = result_type.element_of();
        int result_lanes = result_type.lanes();
        bool need_reshape = (src_lanes > 1 && result_lanes <= 1);

        // Result type for ExtractOp must be rank 1 to match the rank-1 source.
        // Use type_idx_1d to force rank 1 even for single-lane results.
        int extract_lanes = need_reshape ? 1 : result_lanes;
        uint32_t extract_type_idx = type_idx_1d(elem_type, extract_lanes);

        uint32_t id = alloc_id();
        emit_op(OpExtractOp);
        body().write_varint(1);  // numResults (isVariadic)
        body().write_varint(extract_type_idx);
        body().write_varint(2);  // numOperands: source + 1 index
        body().write_varint(src_id);
        body().write_varint(index_id);

        if (need_reshape) {
            // Reshape tile<1xT> → tile<T>
            uint32_t scalar_id = alloc_id();
            emit_op(OpReshapeOp);
            body().write_varint(type_idx(elem_type));
            body().write_varint(id);
            return scalar_id;
        }
        return id;
    }

    // Emit a ConstantOp for DenseIntOrFPElements
    // Format: opcode | resultType | value_attr(self-contained DenseElements)
    uint32_t emit_constant_int(const Halide::Type &t, int64_t value) {
        uint32_t id = alloc_id();
        emit_op(OpConstantOp);
        body().write_varint(type_idx(t));
        // DenseIntOrFPElementsAttr: just the constant pool index (not self-contained;
        // the type is inferred from the result type via AllTypesMatch constraint).

        // Build raw data for the constant pool
        std::vector<uint8_t> raw;
        int bytes = t.bits() / 8;
        if (t.is_bool()) bytes = 1;
        if (bytes == 0) bytes = 4;
        uint64_t uval;
        memcpy(&uval, &value, sizeof(uval));
        for (int i = 0; i < bytes; i++) {
            raw.push_back(static_cast<uint8_t>((uval >> (i * 8)) & 0xFF));
        }
        uint32_t pool_idx = module.constants.add(raw);
        body().write_varint(pool_idx);
        return id;
    }

    uint32_t emit_constant_float(const Halide::Type &t, double value) {
        uint32_t id = alloc_id();
        emit_op(OpConstantOp);
        body().write_varint(type_idx(t));

        std::vector<uint8_t> raw;
        if (t.bits() == 64) {
            raw.resize(8);
            memcpy(raw.data(), &value, 8);
        } else {
            float fval = static_cast<float>(value);
            raw.resize(4);
            memcpy(raw.data(), &fval, 4);
        }
        uint32_t pool_idx = module.constants.add(raw);
        body().write_varint(pool_idx);
        return id;
    }

    using IRVisitor::visit;

    void visit(const IntImm *op) override {
        current_id = emit_constant_int(op->type, op->value);
    }

    void visit(const UIntImm *op) override {
        current_id = emit_constant_int(op->type, static_cast<int64_t>(op->value));
    }

    void visit(const FloatImm *op) override {
        current_id = emit_constant_float(op->type, op->value);
    }

    void visit(const StringImm *op) override {
        internal_error << "StringImm not supported in Tile IR codegen\n";
    }

    void visit(const Variable *op) override {
        if (symbol_table.contains(op->name)) {
            current_id = symbol_table.get(op->name);
        } else {
            internal_error << "Undefined variable in Tile IR codegen: " << op->name << "\n";
        }
    }

    void visit(const Cast *op) override {
        op->value.accept(this);
        uint32_t val_id = current_id;
        Halide::Type src = op->value.type();
        Halide::Type dst = op->type;

        if (src.is_float() && dst.is_float()) {
            // FToFOp: resultType | flags(1 bit: rounding_mode DefaultValuedAttr) |
            //         [rounding_mode if flag set] | source
            uint32_t id = alloc_id();
            emit_op(OpFToFOp);
            body().write_varint(type_idx(dst));
            body().write_varint(0);  // flags: rounding_mode uses default (not present)
            body().write_varint(val_id);
            current_id = id;
        } else if ((src.is_int() || src.is_uint()) && dst.is_float()) {
            // IToFOp: resultType | signedness(enum) | rounding_mode(enum) | source
            uint32_t id = alloc_id();
            emit_op(OpIToFOp);
            body().write_varint(type_idx(dst));
            body().write_varint(static_cast<uint32_t>(src.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(static_cast<uint32_t>(RoundNearestEven));
            body().write_varint(val_id);
            current_id = id;
        } else if (src.is_float() && (dst.is_int() || dst.is_uint())) {
            // FToIOp: resultType | signedness(enum) | rounding_mode(enum) | source
            uint32_t id = alloc_id();
            emit_op(OpFToIOp);
            body().write_varint(type_idx(dst));
            body().write_varint(static_cast<uint32_t>(dst.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(static_cast<uint32_t>(RoundNearestIntToZero));
            body().write_varint(val_id);
            current_id = id;
        } else if ((src.is_int() || src.is_uint()) && (dst.is_int() || dst.is_uint())) {
            if (dst.bits() > src.bits()) {
                // ExtIOp: resultType | signedness(enum) | source
                uint32_t id = alloc_id();
                emit_op(OpExtIOp);
                body().write_varint(type_idx(dst));
                body().write_varint(static_cast<uint32_t>(src.is_uint() ? SignednessUnsigned : SignednessSigned));
                body().write_varint(val_id);
                current_id = id;
            } else if (dst.bits() < src.bits()) {
                // TruncIOp: resultType | flags(1 bit: overflow) | source
                uint32_t id = alloc_id();
                emit_op(OpTruncIOp);
                body().write_varint(type_idx(dst));
                body().write_varint(0);  // flags: overflow not present
                body().write_varint(val_id);
                current_id = id;
            } else {
                // Same-size int cast: bitcast
                current_id = emit_unary_op(OpBitcastOp, dst, val_id);
            }
        } else {
            current_id = emit_unary_op(OpBitcastOp, dst, val_id);
        }
    }

    void visit(const Reinterpret *op) override {
        op->value.accept(this);
        current_id = emit_unary_op(OpBitcastOp, op->type, current_id);
    }

    void visit(const Add *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_float_binop(OpAddFOp, op->type, a, b);
        } else {
            current_id = emit_int_binop_overflow(OpAddIOp, op->type, a, b);
        }
    }

    void visit(const Sub *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_float_binop(OpSubFOp, op->type, a, b);
        } else {
            current_id = emit_int_binop_overflow(OpSubIOp, op->type, a, b);
        }
    }

    void visit(const Mul *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_float_binop(OpMulFOp, op->type, a, b);
        } else {
            current_id = emit_int_binop_overflow(OpMulIOp, op->type, a, b);
        }
    }

    void visit(const Div *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_float_binop(OpDivFOp, op->type, a, b);
        } else {
            uint32_t id = alloc_id();
            emit_op(OpDivIOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(0);
            body().write_varint(static_cast<uint32_t>(
                op->type.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(a);
            body().write_varint(b);
            current_id = id;
        }
    }

    void visit(const Mod *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_simple_binop(OpRemFOp, op->type, a, b);
        } else {
            uint32_t id = alloc_id();
            emit_op(OpRemIOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(static_cast<uint32_t>(
                op->type.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(a);
            body().write_varint(b);
            current_id = id;
        }
    }

    // MinFOp/MaxFOp: resultType | flags(bit0=propagate_nan, bit1=flush_to_zero) | lhs | rhs
    uint32_t emit_float_minmax(Opcode op, const Halide::Type &t, uint32_t lhs, uint32_t rhs) {
        uint32_t id = alloc_id();
        emit_op(op);
        body().write_varint(type_idx(t));
        body().write_varint(0);  // flags: no propagate_nan, no flush_to_zero
        body().write_varint(lhs);
        body().write_varint(rhs);
        return id;
    }

    // MinIOp/MaxIOp: resultType | signedness(enum) | lhs | rhs
    uint32_t emit_int_minmax(Opcode op, const Halide::Type &t, uint32_t lhs, uint32_t rhs) {
        uint32_t id = alloc_id();
        emit_op(op);
        body().write_varint(type_idx(t));
        body().write_varint(static_cast<uint32_t>(
            t.is_uint() ? SignednessUnsigned : SignednessSigned));
        body().write_varint(lhs);
        body().write_varint(rhs);
        return id;
    }

    void visit(const Min *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_float_minmax(OpMinFOp, op->type, a, b);
        } else {
            current_id = emit_int_minmax(OpMinIOp, op->type, a, b);
        }
    }

    void visit(const Max *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        if (op->type.is_float()) {
            current_id = emit_float_minmax(OpMaxFOp, op->type, a, b);
        } else {
            current_id = emit_int_minmax(OpMaxIOp, op->type, a, b);
        }
    }

    void visit(const EQ *op) override {
        emit_cmpi_or_cmpf(op->a, op->b, op->type, CmpEqual);
    }
    void visit(const NE *op) override {
        emit_cmpi_or_cmpf(op->a, op->b, op->type, CmpNotEqual);
    }
    void visit(const LT *op) override {
        emit_cmpi_or_cmpf(op->a, op->b, op->type, CmpLessThan);
    }
    void visit(const LE *op) override {
        emit_cmpi_or_cmpf(op->a, op->b, op->type, CmpLessThanOrEqual);
    }
    void visit(const GT *op) override {
        emit_cmpi_or_cmpf(op->a, op->b, op->type, CmpGreaterThan);
    }
    void visit(const GE *op) override {
        emit_cmpi_or_cmpf(op->a, op->b, op->type, CmpGreaterThanOrEqual);
    }

    void emit_cmpi_or_cmpf(const Expr &a_expr, const Expr &b_expr,
                           const Halide::Type &result_type, ComparisonPredicate pred) {
        a_expr.accept(this);
        uint32_t a = current_id;
        b_expr.accept(this);
        uint32_t b = current_id;

        uint32_t id = alloc_id();
        if (a_expr.type().is_float()) {
            // CmpFOp: resultType | comparison_predicate(enum) | comparison_ordering(enum) | lhs | rhs
            emit_op(OpCmpFOp);
            body().write_varint(type_idx(result_type));
            body().write_varint(static_cast<uint32_t>(pred));
            body().write_varint(static_cast<uint32_t>(CmpOrdered));
            body().write_varint(a);
            body().write_varint(b);
        } else {
            // CmpIOp: resultType | comparison_predicate(enum) | signedness(enum) | lhs | rhs
            // ODS order: comparison_predicate(attr), lhs(operand), rhs(operand), signedness(attr)
            // tblgen: ALL attributes first, then ALL operands
            emit_op(OpCmpIOp);
            body().write_varint(type_idx(result_type));
            body().write_varint(static_cast<uint32_t>(pred));
            body().write_varint(static_cast<uint32_t>(
                a_expr.type().is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(a);
            body().write_varint(b);
        }
        current_id = id;
    }

    void visit(const And *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        current_id = emit_simple_binop(OpAndIOp, op->type, a, b);
    }

    void visit(const Or *op) override {
        op->a.accept(this);
        uint32_t a = current_id;
        op->b.accept(this);
        uint32_t b = current_id;
        current_id = emit_simple_binop(OpOrIOp, op->type, a, b);
    }

    void visit(const Not *op) override {
        // Not(x) = XOr(x, true)
        op->a.accept(this);
        uint32_t a = current_id;
        uint32_t true_id = emit_constant_int(op->type.element_of(), 1);
        if (op->type.lanes() > 1) {
            true_id = emit_broadcast(op->type, true_id);
        }
        current_id = emit_simple_binop(OpXOrIOp, op->type, a, true_id);
    }

    void visit(const Select *op) override {
        op->condition.accept(this);
        uint32_t cond = current_id;
        op->true_value.accept(this);
        uint32_t tv = current_id;
        op->false_value.accept(this);
        uint32_t fv = current_id;
        // SelectOp: resultType | cond | true | false (no attrs)
        uint32_t id = alloc_id();
        emit_op(OpSelectOp);
        body().write_varint(type_idx(op->type));
        body().write_varint(cond);
        body().write_varint(tv);
        body().write_varint(fv);
        current_id = id;
    }

    void visit(const Ramp *op) override {
        op->base.accept(this);
        uint32_t base_id = current_id;
        op->stride.accept(this);
        uint32_t stride_id = current_id;
        Halide::Type vec_type = op->type;

        // IotaOp: resultType (no attrs, no operands)
        uint32_t iota_id = alloc_id();
        emit_op(OpIotaOp);
        body().write_varint(type_idx(vec_type));

        // Broadcast stride to vector
        uint32_t bcast_stride = emit_broadcast(vec_type, stride_id);

        // iota * stride
        uint32_t scaled_id;
        if (vec_type.is_float()) {
            scaled_id = emit_float_binop(OpMulFOp, vec_type, iota_id, bcast_stride);
        } else {
            scaled_id = emit_int_binop_overflow(OpMulIOp, vec_type, iota_id, bcast_stride);
        }

        // broadcast base
        uint32_t bcast_base = emit_broadcast(vec_type, base_id);

        // scaled + base
        if (vec_type.is_float()) {
            current_id = emit_float_binop(OpAddFOp, vec_type, scaled_id, bcast_base);
        } else {
            current_id = emit_int_binop_overflow(OpAddIOp, vec_type, scaled_id, bcast_base);
        }
    }

    void visit(const Broadcast *op) override {
        op->value.accept(this);
        if (op->type.lanes() > 1) {
            current_id = emit_broadcast(op->type, current_id);
        }
        // lanes==1 broadcast is a no-op (already a scalar tile)
    }

    // Current in-flight memory token, threaded through loads/stores to
    // establish memory ordering and prevent the Tile IR compiler from
    // reordering loads across stores (which breaks serial scans).
    // UINT32_MAX means no token yet (first memory op in scope).
    uint32_t current_token = UINT32_MAX;

    // Emit a MakeTokenOp producing a fresh token SSA value.
    uint32_t emit_make_token() {
        uint32_t id = alloc_id();
        emit_op(OpMakeTokenOp);
        body().write_varint(module.types.add_token());
        return id;
    }

    // Ensure we have a current_token; create one via MakeTokenOp if not.
    uint32_t ensure_current_token() {
        if (current_token == UINT32_MAX) {
            current_token = emit_make_token();
        }
        return current_token;
    }

    // LoadPtrTkoOp flags (verified via bit experimentation):
    //   bit 0 = memory_scope, bit 1 = optimization_hints,
    //   bit 2 = mask, bit 3 = paddingValue, bit 4 = token.
    static constexpr uint32_t LoadFlagMask = 0x4;
    static constexpr uint32_t LoadFlagPadding = 0x8;
    static constexpr uint32_t LoadFlagToken = 0x10;
    // StorePtrTkoOp flags:
    //   bit 0 = memory_scope, bit 1 = optimization_hints,
    //   bit 2 = mask, bit 3 = token.
    static constexpr uint32_t StoreFlagMask = 0x4;
    static constexpr uint32_t StoreFlagToken = 0x8;

    // mask_id: UINT32_MAX = no mask; otherwise a tile<Nxi1> SSA id
    //   matching the result/value lane count.
    void emit_load(const Halide::Type &result_type, uint32_t ptr_id,
                   uint32_t mask_id = UINT32_MAX) {
        uint32_t result_id = alloc_id();
        uint32_t token_out = alloc_id();  // token result

        emit_op(OpLoadPtrTkoOp);
        body().write_varint(type_idx(result_type));
        body().write_varint(module.types.add_token());
        uint32_t flags = 0;
        if (mask_id != UINT32_MAX) flags |= LoadFlagMask;
        if (current_token != UINT32_MAX) flags |= LoadFlagToken;
        body().write_varint(flags);
        body().write_varint(static_cast<uint32_t>(MemOrderWeak));
        body().write_varint(ptr_id);  // source
        if (flags & LoadFlagMask) {
            body().write_varint(mask_id);
        }
        if (flags & LoadFlagToken) {
            body().write_varint(current_token);
        }
        current_token = token_out;
        current_id = result_id;
    }

    void emit_store(uint32_t ptr_id, uint32_t val_id,
                    uint32_t mask_id = UINT32_MAX) {
        uint32_t token_out = alloc_id();  // token result

        emit_op(OpStorePtrTkoOp);
        body().write_varint(module.types.add_token());  // 1 result: token
        uint32_t flags = 0;
        if (mask_id != UINT32_MAX) flags |= StoreFlagMask;
        if (current_token != UINT32_MAX) flags |= StoreFlagToken;
        body().write_varint(flags);
        body().write_varint(static_cast<uint32_t>(MemOrderWeak));
        body().write_varint(ptr_id);
        body().write_varint(val_id);
        if (flags & StoreFlagMask) {
            body().write_varint(mask_id);
        }
        if (flags & StoreFlagToken) {
            body().write_varint(current_token);
        }
        current_token = token_out;
    }

    uint32_t emit_offset_ptr(uint32_t ptr_id, uint32_t idx_id, const Halide::Type &elem_type,
                             const Halide::Type &buffer_elem_type) {
        // OffsetOp: resultType | ptr | offset
        // Constraints: AllTypesMatch<["result","ptr"]>, SameOperandsAndResultShape
        // So ptr must be broadcast to match index shape, and result = ptr type.
        uint32_t scalar_idx = module.types.add_scalar(elem_type.element_of());
        uint32_t ptr_type_idx = module.types.add_pointer(scalar_idx);

        // If the buffer's declared element type differs from the access
        // element type (common when a clustered heap alloc holds multiple
        // types), first cast the pointer via PtrToPtrOp.
        if (buffer_elem_type != elem_type.element_of()) {
            uint32_t buf_scalar_idx = module.types.add_scalar(buffer_elem_type);
            (void)buf_scalar_idx;
            uint32_t scalar_ptr_tile_idx = module.types.add_tile({}, ptr_type_idx);
            uint32_t cast_id = alloc_id();
            emit_op(OpPtrToPtrOp);
            body().write_varint(scalar_ptr_tile_idx);
            body().write_varint(ptr_id);
            ptr_id = cast_id;
        }

        uint32_t ptr_tile_type_idx;
        uint32_t actual_ptr_id = ptr_id;
        if (elem_type.lanes() > 1) {
            // Vector access: broadcast scalar ptr to vector of ptrs
            // Pad to power of 2 (Tile IR requires power-of-2 tile dims)
            int lanes = elem_type.lanes();
            int padded = 1; while (padded < lanes) padded *= 2;
            ptr_tile_type_idx = module.types.add_tile({padded}, ptr_type_idx);
            // Reshape: tile<ptr<T>> → tile<1xptr<T>>
            uint32_t tile_1_ptr_idx = module.types.add_tile({1}, ptr_type_idx);
            uint32_t reshaped = alloc_id();
            emit_op(OpReshapeOp);
            body().write_varint(tile_1_ptr_idx);
            body().write_varint(ptr_id);
            // Broadcast: tile<1xptr<T>> → tile<Nxptr<T>>
            actual_ptr_id = alloc_id();
            emit_op(OpBroadcastOp);
            body().write_varint(ptr_tile_type_idx);
            body().write_varint(reshaped);
        } else {
            ptr_tile_type_idx = module.types.add_tile({}, ptr_type_idx);
        }

        uint32_t id = alloc_id();
        emit_op(OpOffsetOp);
        body().write_varint(ptr_tile_type_idx);
        body().write_varint(actual_ptr_id);
        body().write_varint(idx_id);
        return id;
    }

    // Lower a Halide predicate Expr to a Tile IR mask SSA id, or
    // UINT32_MAX if the predicate is trivially true.
    uint32_t emit_predicate_mask(const Expr &predicate) {
        if (is_const_one(predicate)) {
            return UINT32_MAX;
        }
        predicate.accept(this);
        return current_id;
    }

    Halide::Type lookup_buffer_elem_type(const std::string &name, const Halide::Type &fallback) {
        auto it = buffer_elem_types.find(name);
        if (it != buffer_elem_types.end()) {
            return it->second;
        }
        return fallback.element_of();
    }

    void visit(const Load *op) override {
        internal_assert(symbol_table.contains(op->name))
            << "Buffer not found in Tile IR codegen: " << op->name << "\n";
        uint32_t buf_id = symbol_table.get(op->name);
        op->index.accept(this);
        uint32_t idx_id = current_id;

        uint32_t mask_id = emit_predicate_mask(op->predicate);

        Halide::Type buf_elem = lookup_buffer_elem_type(op->name, op->type);
        uint32_t offset_ptr = emit_offset_ptr(buf_id, idx_id, op->type, buf_elem);
        emit_load(op->type, offset_ptr, mask_id);
    }

    void visit(const Store *op) override {
        internal_assert(symbol_table.contains(op->name))
            << "Buffer not found in Tile IR codegen: " << op->name << "\n";
        uint32_t buf_id = symbol_table.get(op->name);
        op->index.accept(this);
        uint32_t idx_id = current_id;
        op->value.accept(this);
        uint32_t val_id = current_id;

        uint32_t mask_id = emit_predicate_mask(op->predicate);

        Halide::Type buf_elem = lookup_buffer_elem_type(op->name, op->value.type());
        uint32_t offset_ptr = emit_offset_ptr(buf_id, idx_id, op->value.type(), buf_elem);
        emit_store(offset_ptr, val_id, mask_id);
    }

    void visit(const Let *op) override {
        op->value.accept(this);
        ScopedBinding<uint32_t> binding(symbol_table, op->name, current_id);
        op->body.accept(this);
    }

    void visit(const LetStmt *op) override {
        op->value.accept(this);
        ScopedBinding<uint32_t> binding(symbol_table, op->name, current_id);
        op->body.accept(this);
    }

    void visit(const AssertStmt *op) override {
        // Skip assertions in GPU kernels
    }

    void visit(const ProducerConsumer *op) override {
        op->body.accept(this);
    }

    // GetTileBlockIdOp: 3 results (x, y, z), no attrs, no operands
    // We emit one GetTileBlockIdOp and store all 3 result IDs.
    // block_ids[0] = x, block_ids[1] = y, block_ids[2] = z
    uint32_t block_id_x = UINT32_MAX, block_id_y = UINT32_MAX, block_id_z = UINT32_MAX;

    void ensure_block_ids() {
        if (block_id_x != UINT32_MAX) return;
        uint32_t scalar_i32_tile = type_idx(Int(32));
        block_id_x = alloc_id();
        block_id_y = alloc_id();
        block_id_z = alloc_id();
        emit_op(OpGetTileBlockIdOp);
        body().write_varint(scalar_i32_tile);  // result type x
        body().write_varint(scalar_i32_tile);  // result type y
        body().write_varint(scalar_i32_tile);  // result type z
    }

    // Match a Halide `For ko { Lets...; Store(buf, idx, tile_ir_mmaf(...)) }`
    // pattern where the Store's value is our mmaf intrinsic and the
    // accumulator argument is `Load(buf, idx)` at the same address as
    // the Store. If so, emit a Tile IR ForOp with a C-tile iter_arg so
    // the accumulator stays in registers across iterations — this is
    // what tileiras needs to actually emit MMA instructions.
    //
    // Returns true if the pattern matched and was emitted.
    bool try_emit_matmul_loop(const For *op) {
        if (op->for_type != ForType::Serial &&
            op->for_type != ForType::Unrolled) return false;

        // Walk the body: collect enclosing Lets and find the final Store.
        std::vector<std::pair<std::string, Expr>> lets;
        const Stmt *cur = &op->body;
        while (true) {
            if (const LetStmt *ls = cur->as<LetStmt>()) {
                lets.emplace_back(ls->name, ls->value);
                cur = &ls->body;
            } else {
                break;
            }
        }
        const Store *store = cur->as<Store>();
        if (!store) return false;

        const Call *mma = store->value.as<Call>();
        if (!mma || mma->name != "tile_ir_mmaf") return false;
        if (mma->args.size() != 10) return false;

        const Load *acc_load = mma->args[6].as<Load>();
        if (!acc_load || acc_load->name != store->name) return false;
        if (!equal(acc_load->index, store->index)) return false;
        // The acc index must NOT depend on the loop var (otherwise we
        // can't promote it to a single iter_arg).
        if (expr_uses_var(store->index, op->name)) return false;

        const Load *A_id = mma->args[0].as<Load>();
        const Load *B_id = mma->args[3].as<Load>();
        const IntImm *M_imm = mma->args[7].as<IntImm>();
        const IntImm *K_imm = mma->args[8].as<IntImm>();
        const IntImm *N_imm = mma->args[9].as<IntImm>();
        if (!A_id || !B_id || !M_imm || !K_imm || !N_imm) return false;
        int64_t M = M_imm->value, K = K_imm->value, N = N_imm->value;

        Halide::Type elem_op = A_id->type.element_of();
        Halide::Type elem_acc = acc_load->type.element_of();

        // --- Outer-scope work: emit the loop bounds and the initial
        // accumulator load. ---
        op->min.accept(this);
        uint32_t min_id = current_id;
        op->extent().accept(this);
        uint32_t extent_id = current_id;
        uint32_t ub_id = emit_int_binop_overflow(OpAddIOp, Int(32), min_id, extent_id);
        uint32_t step_id = emit_constant_int(Int(32), 1);

        // Initial C load (1D), reshape to 2D.
        acc_load->index.accept(this);
        uint32_t outer_acc_idx_id = current_id;
        // Re-emit the load via emit_load on a 1-D ptr tile.
        internal_assert(symbol_table.contains(store->name));
        uint32_t buf_id = symbol_table.get(store->name);
        Halide::Type buf_elem = lookup_buffer_elem_type(store->name, acc_load->type);
        uint32_t init_acc_ptr = emit_offset_ptr(buf_id, outer_acc_idx_id,
                                                acc_load->type, buf_elem);
        // emit_load updates current_token; capture the post-load token.
        emit_load(acc_load->type, init_acc_ptr);
        uint32_t init_C_1d = current_id;

        // Reshape 1-D MN → 2-D MxN.
        uint32_t scalar_acc_idx = module.types.add_scalar(elem_acc);
        uint32_t C_2d_tidx = module.types.add_tile({M, N}, scalar_acc_idx);
        uint32_t init_C_2d = emit_reshape(init_C_1d, C_2d_tidx);

        // We're going to thread BOTH the C tile and the memory token
        // through the ForOp's iter_args.
        uint32_t init_token = ensure_current_token();
        uint32_t token_type = module.types.add_token();

        // --- Loop body in a sub-encoder. ---
        BlockScope scope = begin_block_scope();
        uint32_t iv_id = alloc_id();
        uint32_t iter_C_id = alloc_id();
        uint32_t iter_token_id = alloc_id();
        current_token = iter_token_id;

        // Bind the loop var, replay the LetStmts (so things like t19,
        // t20 in the test resolve), then emit A/B 2-D loads and the
        // MmaFOp.
        std::vector<ScopedBinding<uint32_t>> let_bindings;
        ScopedBinding<uint32_t> iv_binding(symbol_table, op->name, iv_id);
        for (auto &[name, value] : lets) {
            value.accept(this);
            let_bindings.emplace_back(symbol_table, name, current_id);
        }

        // Emit A/B 2-D loads (same shape as in emit_tile_ir_mmaf).
        A_id->index.accept(this);
        uint32_t A_base = current_id;
        mma->args[1].accept(this);
        uint32_t A_row_s = current_id;
        mma->args[2].accept(this);
        uint32_t A_col_s = current_id;
        B_id->index.accept(this);
        uint32_t B_base = current_id;
        mma->args[4].accept(this);
        uint32_t B_row_s = current_id;
        mma->args[5].accept(this);
        uint32_t B_col_s = current_id;

        uint32_t A_buf_id = symbol_table.get(A_id->name);
        uint32_t B_buf_id = symbol_table.get(B_id->name);

        uint32_t A_idx_2d = build_2d_index_tile(M, K, A_base, A_row_s, A_col_s);
        uint32_t A_ptr_2d = emit_2d_offset_ptr(A_buf_id, A_idx_2d, M, K, elem_op);
        uint32_t A_load_2d = emit_2d_load(A_ptr_2d, M, K, elem_op);

        uint32_t B_idx_2d = build_2d_index_tile(K, N, B_base, B_row_s, B_col_s);
        uint32_t B_ptr_2d = emit_2d_offset_ptr(B_buf_id, B_idx_2d, K, N, elem_op);
        uint32_t B_load_2d = emit_2d_load(B_ptr_2d, K, N, elem_op);

        // MmaFOp: result has same type as acc.
        uint32_t new_C_2d = alloc_id();
        emit_op(OpMmaFOp);
        body().write_varint(C_2d_tidx);
        body().write_varint(A_load_2d);
        body().write_varint(B_load_2d);
        body().write_varint(iter_C_id);

        // ContinueOp yields (new_C, current_token).
        uint32_t final_token_in = current_token;
        emit_op(OpContinueOp);
        body().write_varint(0);  // numResults = 0 (isVariadic)
        body().write_varint(2);  // numOperands = 2 (new_C, token)
        body().write_varint(new_C_2d);
        body().write_varint(final_token_in);

        // Capture body bytes & restore outer encoder.
        uint32_t block_op_count = op_count;
        Encoder block_body;
        std::swap(block_body, function.body);
        std::swap(scope.saved_body, function.body);
        op_count = scope.saved_op_count;
        function.next_ssa_id = scope.saved_next_ssa_id;
        current_token = scope.saved_current_token;

        // Emit the ForOp: 2 results (C tile, token), 5 operands, 1
        // region with 3 block args.
        emit_op(OpForOp);
        body().write_varint(2);            // numResults
        body().write_varint(C_2d_tidx);    // result 0: C tile
        body().write_varint(token_type);   // result 1: token
        body().write_varint(5);            // numOperands (lb, ub, step, init_C, init_token)
        body().write_varint(min_id);
        body().write_varint(ub_id);
        body().write_varint(step_id);
        body().write_varint(init_C_2d);
        body().write_varint(init_token);
        body().write_varint(1);            // numRegions
        body().write_varint(1);            // numBlocks
        body().write_varint(3);            // numBlockArgs
        body().write_varint(type_idx(Int(32)));  // induction var
        body().write_varint(C_2d_tidx);          // C iter_arg
        body().write_varint(token_type);         // token iter_arg
        body().write_varint(block_op_count);
        body().write_bytes(block_body.data().data(), block_body.size());

        // Allocate the ForOp's results: C_final, token_out.
        uint32_t final_C_2d = alloc_id();
        current_token = alloc_id();

        // Reshape final C tile back to 1-D and store.
        uint32_t final_C_1d = emit_reshape(final_C_2d, type_idx(acc_load->type));

        // Emit Store(buf, outer_acc_idx, final_C_1d).
        uint32_t store_ptr = emit_offset_ptr(buf_id, outer_acc_idx_id,
                                             acc_load->type, buf_elem);
        emit_store(store_ptr, final_C_1d);
        return true;
    }

    void visit(const For *op) override {
        if (try_emit_matmul_loop(op)) return;
        if (op->for_type == ForType::GPUBlock) {
            ensure_block_ids();

            uint32_t block_id;
            if (op->name.find("block_id_z") != std::string::npos) {
                block_id = block_id_z;
            } else if (op->name.find("block_id_y") != std::string::npos) {
                block_id = block_id_y;
            } else {
                block_id = block_id_x;
            }

            // Add min offset if non-zero
            uint32_t var_id = block_id;
            if (!is_const_zero(op->min)) {
                op->min.accept(this);
                uint32_t min_id = current_id;
                var_id = emit_int_binop_overflow(OpAddIOp, Int(32), block_id, min_id);
            }

            ScopedBinding<uint32_t> binding(symbol_table, op->name, var_id);
            op->body.accept(this);
        } else if (op->for_type == ForType::Serial ||
                   op->for_type == ForType::Unrolled) {
            // Emit operands (lb, ub, step) before the ForOp itself
            op->min.accept(this);
            uint32_t min_id = current_id;

            // upper bound = min + extent (ForOp takes exclusive upper bound)
            op->extent().accept(this);
            uint32_t extent_id = current_id;
            uint32_t ub_id = emit_int_binop_overflow(OpAddIOp, Int(32), min_id, extent_id);

            // step = 1
            uint32_t step_id = emit_constant_int(Int(32), 1);

            // Thread a memory token through the loop as an iter_arg so
            // that loads/stores across iterations have a serial dataflow
            // dependency. This prevents the compiler from reordering
            // loads of one iteration before stores of the previous.
            uint32_t init_token = ensure_current_token();
            uint32_t token_type = module.types.add_token();

            // Emit the block body into a sub-encoder to count ops
            BlockScope scope = begin_block_scope();

            // Block args: induction variable (i32 tile) + token iter_arg
            uint32_t iv_id = alloc_id();
            uint32_t iter_token_id = alloc_id();
            // The loop body starts with the iter_arg token.
            current_token = iter_token_id;
            {
                ScopedBinding<uint32_t> binding(symbol_table, op->name, iv_id);
                op->body.accept(this);
            }

            // ContinueOp yields the updated token back to the loop.
            uint32_t final_token = current_token;
            emit_op(OpContinueOp);
            body().write_varint(0);  // numResults = 0 (isVariadic)
            body().write_varint(1);  // numOperands = 1 (final token)
            body().write_varint(final_token);

            // Capture block op count and body bytes
            uint32_t block_op_count = op_count;
            Encoder block_body;
            std::swap(block_body, function.body);
            // Restore original encoder
            std::swap(scope.saved_body, function.body);
            op_count = scope.saved_op_count;
            function.next_ssa_id = scope.saved_next_ssa_id;
            // Restore parent token and then update to ForOp's result.
            current_token = scope.saved_current_token;

            // Now emit the ForOp with the captured block
            emit_op(OpForOp);
            body().write_varint(1);           // numResults (1: token)
            body().write_varint(token_type);  // result type = token
            // Operands: lb, ub, step + initValues
            body().write_varint(4);  // numOperands = 4 (lb, ub, step, init)
            body().write_varint(min_id);
            body().write_varint(ub_id);
            body().write_varint(step_id);
            body().write_varint(init_token);

            // 1 region, 1 block, 2 block args
            body().write_varint(1);                  // numRegions
            body().write_varint(1);                  // numBlocks
            body().write_varint(2);                  // numBlockArgs
            body().write_varint(type_idx(Int(32)));  // induction var type
            body().write_varint(token_type);         // iter_arg token type
            body().write_varint(block_op_count);     // numOps
            body().write_bytes(block_body.data().data(), block_body.size());

            // Allocate the ForOp's result (token) in parent scope.
            // The reader assigns SSA IDs to results after the block is
            // parsed, so we do the same.
            current_token = alloc_id();
        } else {
            internal_error << "Unsupported for_type in Tile IR codegen: "
                           << op->for_type << "\n";
        }
    }

    // Helper: emit a block body into a sub-encoder, capturing op count and bytes.
    // The block has 0 args (used by IfOp).
    struct CapturedBlock {
        uint32_t op_count;
        Encoder bytes;
    };

    CapturedBlock capture_block(const Stmt &stmt) {
        BlockScope scope = begin_block_scope();

        stmt.accept(this);

        // YieldOp at end of block (isVariadic due to Variadic operands)
        emit_op(OpYieldOp);
        body().write_varint(0);  // numResults = 0
        body().write_varint(0);  // numOperands = 0

        CapturedBlock result;
        result.op_count = op_count;
        std::swap(result.bytes, function.body);
        std::swap(scope.saved_body, function.body);
        op_count = scope.saved_op_count;
        function.next_ssa_id = scope.saved_next_ssa_id;
        current_token = scope.saved_current_token;
        return result;
    }

    void visit(const IfThenElse *op) override {
        op->condition.accept(this);
        uint32_t cond_id = current_id;

        // Capture then/else blocks first (so we know numOps)
        CapturedBlock then_block = capture_block(op->then_case);
        CapturedBlock else_block;
        bool has_else = op->else_case.defined();
        if (has_else) {
            else_block = capture_block(op->else_case);
        }

        // IfOp: numResults(varint) | condition | numRegions | regions
        emit_op(OpIfOp);
        body().write_varint(0);        // numResults = 0
        body().write_varint(cond_id);  // condition operand

        // IfOp always has 2 regions (then + else), per ODS definition
        body().write_varint(2);  // numRegions

        // Then region: 1 block, 0 block args
        body().write_varint(1);  // numBlocks
        body().write_varint(0);  // numBlockArgs
        body().write_varint(then_block.op_count);
        body().write_bytes(then_block.bytes.data().data(), then_block.bytes.size());

        // Else region: 1 block if has else, 0 blocks otherwise
        if (has_else) {
            body().write_varint(1);  // numBlocks
            body().write_varint(0);  // numBlockArgs
            body().write_varint(else_block.op_count);
            body().write_bytes(else_block.bytes.data().data(), else_block.bytes.size());
        } else {
            body().write_varint(0);  // numBlocks = 0 (empty region)
        }
    }

    void visit(const Evaluate *op) override {
        op->value.accept(this);
    }

    void visit(const Shuffle *op) override {
        if (op->is_concat()) {
            // CatOp: resultType | dim_attr(I64Attr as inline varint) | lhs | rhs
            // Build a balanced binary tree of CatOps so all intermediate
            // sizes are powers of two (required by Tile IR).
            struct CatEntry {
                uint32_t id;
                int lanes;
            };
            std::vector<CatEntry> entries;
            for (const auto &v : op->vectors) {
                v.accept(this);
                entries.push_back({current_id, v.type().lanes()});
            }
            Halide::Type elem_type = op->vectors[0].type().element_of();
            // Pad entry count to next power of 2 so all intermediate CatOp
            // results have power-of-2 sizes (required by Tile IR).
            {
                size_t n = entries.size();
                size_t padded_n = 1;
                while (padded_n < n) padded_n *= 2;
                int pad_lanes = entries[0].lanes;
                for (size_t i = n; i < padded_n; i++) {
                    // Create a dummy zero-filled tile for padding
                    uint32_t zero = emit_constant_int(elem_type, 0);
                    uint32_t dummy = emit_broadcast(elem_type.with_lanes(pad_lanes), zero);
                    entries.push_back({dummy, pad_lanes});
                }
            }
            // Pairwise reduction until one entry remains.
            // All entries are same-sized, so all intermediate results are power-of-2.
            while (entries.size() > 1) {
                std::vector<CatEntry> next;
                for (size_t i = 0; i < entries.size(); i += 2) {
                    if (i + 1 < entries.size()) {
                        int new_lanes = entries[i].lanes + entries[i + 1].lanes;
                        uint32_t id = alloc_id();
                        emit_op(OpCatOp);
                        body().write_varint(type_idx(elem_type.with_lanes(new_lanes)));
                        body().write_varint(0);  // dim = 0
                        body().write_varint(entries[i].id);
                        body().write_varint(entries[i + 1].id);
                        next.push_back({id, new_lanes});
                    } else {
                        next.push_back(entries[i]);
                    }
                }
                entries = next;
            }
            current_id = entries[0].id;
        } else if (op->is_slice() && op->slice_stride() == 1) {
            // Contiguous slice → single ExtractOp.
            internal_assert(op->vectors.size() == 1);
            op->vectors[0].accept(this);
            uint32_t vec_id = current_id;
            uint32_t start_id = emit_constant_int(Int(32), op->slice_begin());
            current_id = emit_extract(op->type, vec_id,
                                      op->vectors[0].type().lanes(), start_id);
        } else if (op->is_extract_element()) {
            internal_assert(op->vectors.size() == 1);
            op->vectors[0].accept(this);
            uint32_t vec_id = current_id;
            uint32_t idx_val = emit_constant_int(Int(32), op->indices[0]);
            current_id = emit_extract(op->type, vec_id,
                                      op->vectors[0].type().lanes(), idx_val);
        } else {
            // General shuffle: extract each element and build result via concat.
            // This is inefficient but handles all cases.
            internal_assert(!op->indices.empty());
            // Emit all source vectors
            std::vector<uint32_t> vec_ids;
            std::vector<int> vec_lanes;
            for (const auto &v : op->vectors) {
                v.accept(this);
                vec_ids.push_back(current_id);
                vec_lanes.push_back(v.type().lanes());
            }
            Halide::Type elem_type = op->type.element_of();

            // Extract each element as tile<1xT> (keep rank 1 for CatOp compatibility)
            std::vector<uint32_t> elem_ids;
            for (int idx : op->indices) {
                // Find which vector and local index
                int vec = 0, local_idx = idx;
                for (size_t v = 0; v < vec_lanes.size(); v++) {
                    if (local_idx < vec_lanes[v]) {
                        vec = v;
                        break;
                    }
                    local_idx -= vec_lanes[v];
                }
                uint32_t idx_val = emit_constant_int(Int(32), local_idx);
                // Extract as tile<1xT> (rank 1 to match rank-1 source)
                uint32_t id = alloc_id();
                emit_op(OpExtractOp);
                body().write_varint(1);  // numResults
                body().write_varint(type_idx_1d(elem_type, 1));
                body().write_varint(2);  // numOperands
                body().write_varint(vec_ids[vec]);
                body().write_varint(idx_val);
                elem_ids.push_back(id);
            }

            if (elem_ids.size() == 1) {
                current_id = elem_ids[0];
            } else {
                // Concat all elements pairwise
                struct CatEntry {
                    uint32_t id;
                    int lanes;
                };
                std::vector<CatEntry> entries;
                for (auto eid : elem_ids) {
                    entries.push_back({eid, 1});
                }
                // Pad to power-of-2 count for balanced tree
                {
                    size_t n = entries.size();
                    size_t padded_n = 1;
                    while (padded_n < n) padded_n *= 2;
                    for (size_t i = n; i < padded_n; i++) {
                        uint32_t zero = emit_constant_int(elem_type, 0);
                        // Reshape scalar to tile<1xT> to match other entries
                        uint32_t dummy = alloc_id();
                        emit_op(OpReshapeOp);
                        body().write_varint(type_idx_1d(elem_type, 1));
                        body().write_varint(zero);
                        entries.push_back({dummy, 1});
                    }
                }
                while (entries.size() > 1) {
                    std::vector<CatEntry> next;
                    for (size_t i = 0; i < entries.size(); i += 2) {
                        if (i + 1 < entries.size()) {
                            int new_lanes = entries[i].lanes + entries[i + 1].lanes;
                            uint32_t id = alloc_id();
                            emit_op(OpCatOp);
                            body().write_varint(type_idx(elem_type.with_lanes(new_lanes)));
                            body().write_varint(0);  // dim = 0
                            body().write_varint(entries[i].id);
                            body().write_varint(entries[i + 1].id);
                            next.push_back({id, new_lanes});
                        } else {
                            next.push_back(entries[i]);
                        }
                    }
                    entries = next;
                }
                current_id = entries[0].id;
            }
        }
    }

    // Emit Tile IR's native OpReduceOp (opcode 0x58).
    //
    // ODS (cuda_tile::ReduceOp):
    //   operands (Variadic<TileType>)
    //   dim (I32Attr)
    //   identities (ArrayAttr of TypedAttr, one per operand)
    //   region (body with 2*N block args: [operand_i_current,
    //           operand_i_prev]; ends in YieldOp)
    //
    // Bytecode (per the generated reader in cuda-tile-tblgen):
    //   opcode
    //   numResults(vi) resultType(vi)                  -- variadic results
    //   numOperands(vi) [srcId(vi)]...                 -- variadic operands
    //   dim_value(vi)                                  -- attr (non-self-contained)
    //   identities: count(vi) + [self-contained elements...]
    //     each element: tag(vi) + type_idx(vi) + value
    //       IntegerAttr: tag=1, value is unsigned varint (width<=64)
    //       FloatAttr:   tag=2, value is signedVarInt of bitcast APInt
    //   numRegions(vi) numBlocks(vi) numBlockArgs(vi)
    //   blockArgType x N
    //   numOps(vi) [body ops]
    void visit(const VectorReduce *op) override {
        // For now only handle single-operand, total-reduction (out_lanes=1).
        // Multi-output reductions (out_lanes > 1, i.e. reducing along one
        // axis of a 2-D tile down to a vector) would need rank-2 input
        // tiles; we don't build those yet.
        int in_lanes = op->value.type().lanes();
        int out_lanes = op->type.lanes();
        internal_assert(in_lanes % out_lanes == 0);
        Halide::Type elem_t = op->type.element_of();

        // Pick the scalar binop for this reduction kind. SaturatingAdd
        // isn't a Tile IR scalar op, so fall back to scalarize for it.
        auto do_binop = [&](uint32_t a, uint32_t b) -> uint32_t {
            switch (op->op) {
            case VectorReduce::Add:
                return elem_t.is_float()
                           ? emit_float_binop(OpAddFOp, elem_t, a, b)
                           : emit_int_binop_overflow(OpAddIOp, elem_t, a, b);
            case VectorReduce::Mul:
                return elem_t.is_float()
                           ? emit_float_binop(OpMulFOp, elem_t, a, b)
                           : emit_int_binop_overflow(OpMulIOp, elem_t, a, b);
            case VectorReduce::Min:
                return elem_t.is_float()
                           ? emit_float_minmax(OpMinFOp, elem_t, a, b)
                           : emit_int_minmax(OpMinIOp, elem_t, a, b);
            case VectorReduce::Max:
                return elem_t.is_float()
                           ? emit_float_minmax(OpMaxFOp, elem_t, a, b)
                           : emit_int_minmax(OpMaxIOp, elem_t, a, b);
            case VectorReduce::And:
                return emit_simple_binop(OpAndIOp, elem_t, a, b);
            case VectorReduce::Or:
                return emit_simple_binop(OpOrIOp, elem_t, a, b);
            case VectorReduce::SaturatingAdd:
                return UINT32_MAX;
            }
            return UINT32_MAX;
        };

        if (out_lanes != 1 || op->op == VectorReduce::SaturatingAdd) {
            // Scalarize fallback: extract each lane and chain the binop.
            auto expr_binop = [&](const Expr &a, const Expr &b) -> Expr {
                switch (op->op) {
                case VectorReduce::Add: return a + b;
                case VectorReduce::Mul: return a * b;
                case VectorReduce::Min: return min(a, b);
                case VectorReduce::Max: return max(a, b);
                case VectorReduce::And: return a && b;
                case VectorReduce::Or:  return a || b;
                case VectorReduce::SaturatingAdd: return saturating_add(a, b);
                }
                return Expr();
            };
            int factor = in_lanes / out_lanes;
            std::vector<Expr> lanes;
            for (int outer = 0; outer < out_lanes; outer++) {
                Expr acc = Shuffle::make_extract_element(op->value, outer * factor);
                for (int inner = 1; inner < factor; inner++) {
                    Expr lane = Shuffle::make_extract_element(op->value, outer * factor + inner);
                    acc = expr_binop(acc, lane);
                }
                lanes.push_back(acc);
            }
            Expr result = (lanes.size() == 1) ? lanes[0] : Shuffle::make_concat(lanes);
            result.accept(this);
            return;
        }

        // Emit the input tile.
        op->value.accept(this);
        uint32_t src_id = current_id;

        // Build the reduction body region into a sub-encoder so we can
        // count ops and splice the bytes back. Block args: [cur, acc]
        // (per the ODS doc: first half = operand_i_current_iter, second
        // half = operand_i_prev_iter/identity).
        BlockScope scope = begin_block_scope();
        uint32_t cur_id = alloc_id();
        uint32_t acc_id = alloc_id();
        uint32_t saved_tok = current_token;
        current_token = UINT32_MAX;  // region is memory-token-free

        uint32_t combined = do_binop(acc_id, cur_id);
        internal_assert(combined != UINT32_MAX);

        // YieldOp(combined) closes the block.
        emit_op(OpYieldOp);
        body().write_varint(0);         // numResults = 0 (isVariadic)
        body().write_varint(1);         // numOperands = 1
        body().write_varint(combined);

        uint32_t block_op_count = op_count;
        Encoder block_body;
        std::swap(block_body, function.body);
        std::swap(scope.saved_body, function.body);
        op_count = scope.saved_op_count;
        function.next_ssa_id = scope.saved_next_ssa_id;
        current_token = saved_tok;

        // Now emit the ReduceOp itself.
        uint32_t result_tidx = type_idx(op->type);
        // Plain scalar type (for IntegerAttr/FloatAttr type field).
        uint32_t scalar_tidx = module.types.add_scalar(elem_t);
        // Rank-0 tile type (for region block args — region ops must be
        // cuda_tile ops on 0-rank tile values).
        uint32_t zero_rank_tile_tidx = module.types.add_tile({}, scalar_tidx);

        emit_op(OpReduceOp);
        body().write_varint(1);             // numResults
        body().write_varint(result_tidx);   // result type

        // NOTE: attributes precede operands in the bytecode, matching the
        // generator order in the cuda_tile BytecodeReaderGen (results →
        // flags → attrs → operands → regions).

        // dim attribute (I32Attr, not self-contained): just the value.
        body().write_varint(0);

        // identities (ArrayAttr, not self-contained): count + per-element
        // self-contained TypedAttrs (IntegerAttr / FloatAttr).
        body().write_varint(1);  // one identity (one operand)
        if (elem_t.is_float()) {
            body().write_varint((uint64_t)AttrFloat);  // tag
            body().write_varint(scalar_tidx);           // type index
            // APFloat::bitcastToAPInt → writeAPInt. For bitWidth <= 64:
            // writeSignedVarInt(limitedValue). For <= 8: single byte.
            uint64_t bits = 0;
            if (elem_t.bits() == 32) {
                float fv = 0.0f;
                if (op->op == VectorReduce::Mul) fv = 1.0f;
                else if (op->op == VectorReduce::Min) fv = std::numeric_limits<float>::infinity();
                else if (op->op == VectorReduce::Max) fv = -std::numeric_limits<float>::infinity();
                uint32_t u = 0; memcpy(&u, &fv, 4);
                bits = u;
            } else if (elem_t.bits() == 64) {
                double fv = 0.0;
                if (op->op == VectorReduce::Mul) fv = 1.0;
                else if (op->op == VectorReduce::Min) fv = std::numeric_limits<double>::infinity();
                else if (op->op == VectorReduce::Max) fv = -std::numeric_limits<double>::infinity();
                memcpy(&bits, &fv, 8);
            }
            if (elem_t.bits() <= 8) {
                body().write_byte((uint8_t)(bits & 0xFF));
            } else {
                body().write_signed_varint((int64_t)bits);
            }
        } else {
            body().write_varint((uint64_t)AttrInteger);  // tag
            body().write_varint(scalar_tidx);             // type index
            // IntegerAttr value: unsigned varint (must fit in width).
            int width = elem_t.bits();
            uint64_t mask = (width >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << width) - 1);
            uint64_t ident = 0;
            switch (op->op) {
            case VectorReduce::Add:
            case VectorReduce::SaturatingAdd:
            case VectorReduce::Or:
                ident = 0; break;
            case VectorReduce::Mul:
                ident = 1; break;
            case VectorReduce::And:
                ident = mask; break;
            case VectorReduce::Min:
                ident = elem_t.is_uint() ? mask
                                         : (((uint64_t)1 << (width - 1)) - 1);
                break;
            case VectorReduce::Max:
                ident = elem_t.is_uint() ? 0
                                         : ((uint64_t)1 << (width - 1)) & mask;
                break;
            }
            // BoolAttr (width==1) is written as a single byte in the
            // inline path; everything else is unsigned varint.
            if (width == 1) {
                body().write_byte((uint8_t)(ident & 1));
            } else {
                body().write_varint(ident & mask);
            }
        }

        // Now the operands (variadic): count + ssa ids.
        body().write_varint(1);
        body().write_varint(src_id);

        // Region: 1 region, 1 block, 2 block args (cur, acc).
        body().write_varint(1);            // numRegions
        body().write_varint(1);            // numBlocks
        body().write_varint(2);            // numBlockArgs
        body().write_varint(zero_rank_tile_tidx);
        body().write_varint(zero_rank_tile_tidx);
        body().write_varint(block_op_count);
        body().write_bytes(block_body.data().data(), block_body.size());

        // Allocate the ReduceOp's result SSA id (after the block).
        current_id = alloc_id();
    }

    void visit(const Allocate *op) override {
        // Allocations should have been hoisted to heap by
        // RemapCUDATileIRLoops and ExtractSharedAndHeapAllocations before
        // reaching here. Any remaining Allocate nodes are pass-through.
        op->body.accept(this);
    }

    void visit(const Free *op) override {
    }

    // Emit OpIotaOp returning a tile<lanes x int_t>. Caller chooses
    // bitwidth via int_t (typically Int(32)).
    uint32_t emit_iota_1d(int lanes, const Halide::Type &int_t) {
        uint32_t tidx = module.types.get_exact_1d_tile_type_idx(int_t, lanes);
        uint32_t id = alloc_id();
        emit_op(OpIotaOp);
        body().write_varint(tidx);
        return id;
    }

    // Emit OpReshapeOp from src to result.
    uint32_t emit_reshape(uint32_t src_id, uint32_t result_tidx) {
        uint32_t id = alloc_id();
        emit_op(OpReshapeOp);
        body().write_varint(result_tidx);
        body().write_varint(src_id);
        return id;
    }

    // Emit OpBroadcastOp from src to result.
    uint32_t emit_broadcast_op(uint32_t src_id, uint32_t result_tidx) {
        uint32_t id = alloc_id();
        emit_op(OpBroadcastOp);
        body().write_varint(result_tidx);
        body().write_varint(src_id);
        return id;
    }

    // Build a 2D index tile<MxK x i32> with values base_id + i*row_stride_id +
    // k*col_stride_id. base_id, row_stride_id, col_stride_id are scalar
    // (rank-0) i32 tile SSA ids.
    uint32_t build_2d_index_tile(int64_t M, int64_t K,
                                 uint32_t base_id, uint32_t row_stride_id,
                                 uint32_t col_stride_id) {
        Halide::Type i32 = Int(32);
        uint32_t scalar_i32_idx = module.types.add_scalar(i32);
        uint32_t tile_Mx1_idx = module.types.add_tile({M, 1}, scalar_i32_idx);
        uint32_t tile_1xK_idx = module.types.add_tile({1, K}, scalar_i32_idx);
        uint32_t tile_MxK_idx = module.types.add_tile({M, K}, scalar_i32_idx);
        uint32_t tile_1x1_idx = module.types.add_tile({1, 1}, scalar_i32_idx);

        // 2-D AddIOp / MulIOp emission (rank-2 tile result type).
        auto add2d = [&](uint32_t a, uint32_t b) {
            uint32_t out = alloc_id();
            emit_op(OpAddIOp);
            body().write_varint(tile_MxK_idx);
            body().write_varint(0);  // overflow=None
            body().write_varint(a);
            body().write_varint(b);
            return out;
        };
        auto mul2d = [&](uint32_t a, uint32_t b) {
            uint32_t out = alloc_id();
            emit_op(OpMulIOp);
            body().write_varint(tile_MxK_idx);
            body().write_varint(0);  // overflow=None
            body().write_varint(a);
            body().write_varint(b);
            return out;
        };

        // i_axis: Iota(M) → Reshape Mx1 → Broadcast MxK → * row_stride
        uint32_t iota_m = emit_iota_1d(M, i32);
        uint32_t iota_m_2d = emit_reshape(iota_m, tile_Mx1_idx);
        uint32_t iota_m_bc = emit_broadcast_op(iota_m_2d, tile_MxK_idx);
        uint32_t row_stride_2d = emit_reshape(row_stride_id, tile_1x1_idx);
        uint32_t row_stride_bc = emit_broadcast_op(row_stride_2d, tile_MxK_idx);
        uint32_t i_term = mul2d(iota_m_bc, row_stride_bc);

        // k_axis: Iota(K) → Reshape 1xK → Broadcast MxK → * col_stride
        uint32_t iota_k = emit_iota_1d(K, i32);
        uint32_t iota_k_2d = emit_reshape(iota_k, tile_1xK_idx);
        uint32_t iota_k_bc = emit_broadcast_op(iota_k_2d, tile_MxK_idx);
        uint32_t col_stride_2d = emit_reshape(col_stride_id, tile_1x1_idx);
        uint32_t col_stride_bc = emit_broadcast_op(col_stride_2d, tile_MxK_idx);
        uint32_t k_term = mul2d(iota_k_bc, col_stride_bc);

        // base broadcast.
        uint32_t base_2d = emit_reshape(base_id, tile_1x1_idx);
        uint32_t base_bc = emit_broadcast_op(base_2d, tile_MxK_idx);

        uint32_t sum1 = add2d(i_term, k_term);
        return add2d(sum1, base_bc);
    }

    // Emit a 2D pointer tile<MxK xptr<elem>> via Reshape+Broadcast on the
    // kernel-arg buffer pointer, then OffsetOp with the 2D index tile.
    uint32_t emit_2d_offset_ptr(uint32_t buf_id, uint32_t idx2d_id,
                                int64_t M, int64_t K,
                                const Halide::Type &elem_type) {
        uint32_t scalar_elem_idx = module.types.add_scalar(elem_type);
        uint32_t ptr_type_idx = module.types.add_pointer(scalar_elem_idx);
        // Buffer ptr arg is rank-0 tile<ptr<T>>.
        uint32_t tile_1x1_ptr_idx = module.types.add_tile({1, 1}, ptr_type_idx);
        uint32_t tile_MxK_ptr_idx = module.types.add_tile({M, K}, ptr_type_idx);

        uint32_t ptr_2d = emit_reshape(buf_id, tile_1x1_ptr_idx);
        uint32_t ptr_2d_bc = emit_broadcast_op(ptr_2d, tile_MxK_ptr_idx);

        uint32_t out = alloc_id();
        emit_op(OpOffsetOp);
        body().write_varint(tile_MxK_ptr_idx);
        body().write_varint(ptr_2d_bc);
        body().write_varint(idx2d_id);
        return out;
    }

    // Emit OpLoadPtrTkoOp on a 2D pointer tile, returning a 2D tile of
    // values. Mask not supported here.
    uint32_t emit_2d_load(uint32_t ptr_2d_id, int64_t M, int64_t K,
                          const Halide::Type &elem_type) {
        uint32_t scalar_idx = module.types.add_scalar(elem_type);
        uint32_t tile_MxK_idx = module.types.add_tile({M, K}, scalar_idx);
        uint32_t result_id = alloc_id();
        uint32_t token_out = alloc_id();
        (void)token_out;
        emit_op(OpLoadPtrTkoOp);
        body().write_varint(tile_MxK_idx);
        body().write_varint(module.types.add_token());
        uint32_t flags = 0;
        if (current_token != UINT32_MAX) flags |= LoadFlagToken;
        body().write_varint(flags);
        body().write_varint(static_cast<uint32_t>(MemOrderWeak));
        body().write_varint(ptr_2d_id);
        if (flags & LoadFlagToken) {
            body().write_varint(current_token);
        }
        // Update current_token to the new one (matches emit_load).
        current_token = token_out;
        return result_id;
    }

    // Emit a complete matmul tile load + mmaf + store-via-reshape from
    // the tile_ir_mmaf intrinsic call.
    void emit_tile_ir_mmaf(const Call *op) {
        // Args: A_id_load, A_row_stride, A_col_stride,
        //       B_id_load, B_row_stride, B_col_stride,
        //       acc_load, M, K, N
        internal_assert(op->args.size() == 10);
        const Load *A_id = op->args[0].as<Load>();
        const Load *B_id = op->args[3].as<Load>();
        internal_assert(A_id && B_id);
        const IntImm *M_imm = op->args[7].as<IntImm>();
        const IntImm *K_imm = op->args[8].as<IntImm>();
        const IntImm *N_imm = op->args[9].as<IntImm>();
        internal_assert(M_imm && K_imm && N_imm);
        int64_t M = M_imm->value, K = K_imm->value, N = N_imm->value;

        Halide::Type elem_op = A_id->type.element_of();
        Halide::Type elem_acc = op->type.element_of();

        // Resolve buffer SSA ids and base indices.
        internal_assert(symbol_table.contains(A_id->name));
        internal_assert(symbol_table.contains(B_id->name));
        uint32_t A_buf_id = symbol_table.get(A_id->name);
        uint32_t B_buf_id = symbol_table.get(B_id->name);

        A_id->index.accept(this);
        uint32_t A_base = current_id;
        op->args[1].accept(this);
        uint32_t A_row_s = current_id;
        op->args[2].accept(this);
        uint32_t A_col_s = current_id;

        B_id->index.accept(this);
        uint32_t B_base = current_id;
        op->args[4].accept(this);
        uint32_t B_row_s = current_id;
        op->args[5].accept(this);
        uint32_t B_col_s = current_id;

        // Build 2D pointer tiles + 2D loads for A and B.
        uint32_t A_idx_2d = build_2d_index_tile(M, K, A_base, A_row_s, A_col_s);
        uint32_t A_ptr_2d = emit_2d_offset_ptr(A_buf_id, A_idx_2d, M, K, elem_op);
        uint32_t A_load_2d = emit_2d_load(A_ptr_2d, M, K, elem_op);

        uint32_t B_idx_2d = build_2d_index_tile(K, N, B_base, B_row_s, B_col_s);
        uint32_t B_ptr_2d = emit_2d_offset_ptr(B_buf_id, B_idx_2d, K, N, elem_op);
        uint32_t B_load_2d = emit_2d_load(B_ptr_2d, K, N, elem_op);

        // Accumulator: arrive as a 1-D MN-lane Halide load. Accept it
        // (which lowers to a 1-D OpLoadPtrTkoOp), then Reshape to 2-D.
        op->args[6].accept(this);
        uint32_t C_1d = current_id;
        uint32_t scalar_acc_idx = module.types.add_scalar(elem_acc);
        uint32_t C_mn_tidx = module.types.add_tile({M, N}, scalar_acc_idx);
        uint32_t C_2d = emit_reshape(C_1d, C_mn_tidx);

        // OpMmaFOp: result is tile<MxN> (same as acc).
        uint32_t result_2d = alloc_id();
        emit_op(OpMmaFOp);
        body().write_varint(C_mn_tidx);
        body().write_varint(A_load_2d);
        body().write_varint(B_load_2d);
        body().write_varint(C_2d);

        // Reshape 2-D → 1-D for the enclosing Store.
        uint32_t result_1d = emit_reshape(result_2d, type_idx(op->type));
        current_id = result_1d;
    }

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::bitwise_and)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            current_id = emit_simple_binop(OpAndIOp, op->type, a, b);
        } else if (op->is_intrinsic(Call::bitwise_or)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            current_id = emit_simple_binop(OpOrIOp, op->type, a, b);
        } else if (op->is_intrinsic(Call::bitwise_xor)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            current_id = emit_simple_binop(OpXOrIOp, op->type, a, b);
        } else if (op->is_intrinsic(Call::div_round_to_zero)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            uint32_t id = alloc_id();
            emit_op(OpDivIOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(0);  // flags: rounding not present (defaults to ZERO)
            body().write_varint(static_cast<uint32_t>(
                op->type.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(a);
            body().write_varint(b);
            current_id = id;
        } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            uint32_t id = alloc_id();
            emit_op(OpRemIOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(static_cast<uint32_t>(
                op->type.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(a);
            body().write_varint(b);
            current_id = id;
        } else if (op->is_intrinsic(Call::bitwise_not)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            uint32_t ones = emit_constant_int(op->type, -1);
            current_id = emit_simple_binop(OpXOrIOp, op->type, a, ones);
        } else if (op->is_intrinsic(Call::shift_left)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            current_id = emit_int_binop_overflow(OpShLIOp, op->type, a, b);
        } else if (op->is_intrinsic(Call::shift_right)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            op->args[1].accept(this);
            uint32_t b = current_id;
            // ShRIOp: resultType | signedness(enum) | lhs | rhs
            uint32_t id = alloc_id();
            emit_op(OpShRIOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(static_cast<uint32_t>(
                op->type.is_uint() ? SignednessUnsigned : SignednessSigned));
            body().write_varint(a);
            body().write_varint(b);
            current_id = id;
        } else if (op->is_intrinsic(Call::abs)) {
            op->args[0].accept(this);
            uint32_t a = current_id;
            Opcode abs_op = op->args[0].type().is_float() ? OpAbsFOp : OpAbsIOp;
            current_id = emit_unary_op(abs_op, op->type, a);
        } else if (op->is_intrinsic(Call::if_then_else)) {
            op->args[0].accept(this);
            uint32_t cond = current_id;
            op->args[1].accept(this);
            uint32_t tv = current_id;
            op->args[2].accept(this);
            uint32_t fv = current_id;
            uint32_t id = alloc_id();
            emit_op(OpSelectOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(cond);
            body().write_varint(tv);
            body().write_varint(fv);
            current_id = id;
        } else if (op->is_intrinsic(Call::undef)) {
            current_id = emit_constant_int(op->type, 0);
        } else if (op->is_intrinsic(Call::likely) ||
                   op->is_intrinsic(Call::likely_if_innermost)) {
            op->args[0].accept(this);
        } else if (op->name == "tile_ir_mmaf") {
            emit_tile_ir_mmaf(op);
        } else if (op->name == "floor_f16" || op->name == "floor_f32" || op->name == "floor_f64") {
            op->args[0].accept(this);
            current_id = emit_unary_op(OpFloorOp, op->type, current_id);
        } else if (op->name == "ceil_f16" || op->name == "ceil_f32" || op->name == "ceil_f64") {
            op->args[0].accept(this);
            current_id = emit_unary_op(OpCeilOp, op->type, current_id);
        } else if (op->name == "sqrt_f16" || op->name == "sqrt_f32" || op->name == "sqrt_f64") {
            // SqrtOp: resultType | flags(1 bit: flush_to_zero) | rounding_mode | source
            op->args[0].accept(this); uint32_t val = current_id;
            uint32_t id = alloc_id();
            emit_op(OpSqrtOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(0);  // flags: no flush_to_zero
            body().write_varint(static_cast<uint32_t>(RoundNearestEven));
            body().write_varint(val);
            current_id = id;
        } else if (op->name == "trunc_f16" || op->name == "trunc_f32" || op->name == "trunc_f64") {
            // trunc via FToI(RoundNearestIntToZero) + IToF
            op->args[0].accept(this); uint32_t val = current_id;
            Halide::Type ft = op->type;
            Halide::Type it = ft.bits() <= 32 ? Int(32) : Int(64);
            if (ft.lanes() > 1) it = it.with_lanes(ft.lanes());
            uint32_t as_int = alloc_id();
            emit_op(OpFToIOp);
            body().write_varint(type_idx(it));
            body().write_varint(static_cast<uint32_t>(SignednessSigned));
            body().write_varint(static_cast<uint32_t>(RoundNearestIntToZero));
            body().write_varint(val);
            uint32_t id = alloc_id();
            emit_op(OpIToFOp);
            body().write_varint(type_idx(ft));
            body().write_varint(static_cast<uint32_t>(SignednessSigned));
            body().write_varint(static_cast<uint32_t>(RoundNearestEven));
            body().write_varint(as_int);
            current_id = id;
        } else if (op->is_intrinsic(Call::round)) {
            // Halide's round is round-half-to-even (banker's rounding).
            // Lower via a Halide expression:
            //   if x is already integer: return x
            //   else r = floor(x + 0.5); if r-x == 0.5 and r is odd, subtract 1
            Expr x = op->args[0];
            Halide::Type ft = op->type;
            Expr half = make_const(ft, 0.5);
            Expr fl = floor(x);
            Expr already_int = (fl == x);
            Expr r = floor(x + half);
            Expr on_half = (r - x == half);
            Halide::Type it = ft.bits() <= 32 ? Int(32) : Int(64);
            if (ft.lanes() > 1) it = it.with_lanes(ft.lanes());
            Expr r_int = cast(it, r);
            Expr is_odd = (r_int & 1) == 1;
            Expr adjusted = select(on_half && is_odd, r - make_const(ft, 1.0), r);
            Expr result = select(already_int, x, adjusted);
            result.accept(this);
        } else if (op->name == "sin_f16" || op->name == "sin_f32" || op->name == "sin_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpSinOp, op->type, val);
        } else if (op->name == "cos_f16" || op->name == "cos_f32" || op->name == "cos_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpCosOp, op->type, val);
        } else if (op->name == "tan_f16" || op->name == "tan_f32" || op->name == "tan_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpTanOp, op->type, val);
        } else if (op->name == "sinh_f16" || op->name == "sinh_f32" || op->name == "sinh_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpSinHOp, op->type, val);
        } else if (op->name == "cosh_f16" || op->name == "cosh_f32" || op->name == "cosh_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpCosHOp, op->type, val);
        } else if (op->name == "tanh_f16" || op->name == "tanh_f32" || op->name == "tanh_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpTanHOp, op->type, val);
        } else if (op->name == "exp_f16" || op->name == "exp_f32" || op->name == "exp_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpExpOp, op->type, val);
        } else if (op->name == "log_f16" || op->name == "log_f32" || op->name == "log_f64") {
            op->args[0].accept(this); uint32_t val = current_id;
            current_id = emit_unary_op(OpLogOp, op->type, val);
        } else if (op->name == "pow_f16" || op->name == "pow_f32" || op->name == "pow_f64") {
            op->args[0].accept(this); uint32_t a = current_id;
            op->args[1].accept(this); uint32_t b = current_id;
            uint32_t id = alloc_id();
            emit_op(OpPowOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(a);
            body().write_varint(b);
            current_id = id;
        } else if (op->name == "fast_inverse_f16" || op->name == "fast_inverse_f32") {
            // 1 / x
            op->args[0].accept(this); uint32_t val = current_id;
            uint32_t one = emit_constant_float(op->type.element_of(), 1.0);
            if (op->type.lanes() > 1) {
                one = emit_broadcast(op->type, one);
            }
            current_id = emit_float_binop(OpDivFOp, op->type, one, val);
        } else if (op->name == "fast_inverse_sqrt_f16" || op->name == "fast_inverse_sqrt_f32" ||
                   op->name == "rsqrt_f16" || op->name == "rsqrt_f32" || op->name == "rsqrt_f64") {
            // RsqrtOp format: type | flags(1 bit flush_to_zero) | source
            op->args[0].accept(this); uint32_t val = current_id;
            uint32_t id = alloc_id();
            emit_op(OpRsqrtOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(0);  // flags
            body().write_varint(val);
            current_id = id;
        } else if (op->is_intrinsic(Call::strict_fma)) {
            // Must preserve FMA semantics (single rounding); don't unstrictify.
            op->args[0].accept(this); uint32_t a = current_id;
            op->args[1].accept(this); uint32_t b = current_id;
            op->args[2].accept(this); uint32_t c = current_id;
            uint32_t id = alloc_id();
            emit_op(OpFmaOp);
            body().write_varint(type_idx(op->type));
            body().write_varint(0);  // flags
            body().write_varint(static_cast<uint32_t>(RoundNearestEven));
            body().write_varint(a);
            body().write_varint(b);
            body().write_varint(c);
            current_id = id;
        } else if (op->is_strict_float_intrinsic()) {
            Expr lowered = unstrictify_float(op);
            lowered.accept(this);
        } else if (op->name == "is_inf_f16" || op->name == "is_inf_f32" ||
                   op->name == "is_inf_f64") {
            // is_inf(x) = (abs(x) == inf)
            Halide::Type ft = op->args[0].type();
            Expr inf = make_const(ft, std::numeric_limits<double>::infinity());
            Expr expr = (abs(op->args[0]) == inf);
            if (op->type != Bool(ft.lanes())) {
                expr = cast(op->type, expr);
            }
            expr.accept(this);
        } else if (op->name == "is_finite_f16" || op->name == "is_finite_f32" ||
                   op->name == "is_finite_f64") {
            Halide::Type ft = op->args[0].type();
            Expr inf = make_const(ft, std::numeric_limits<double>::infinity());
            Expr expr = (abs(op->args[0]) < inf);
            if (op->type != Bool(ft.lanes())) {
                expr = cast(op->type, expr);
            }
            expr.accept(this);
        } else if (op->name == "is_nan_f16" || op->name == "is_nan_f32" ||
                   op->name == "is_nan_f64") {
            // is_nan(x) = (x != x) using unordered compare
            op->args[0].accept(this); uint32_t val = current_id;
            Halide::Type bool_type = Bool(op->args[0].type().lanes());
            uint32_t id = alloc_id();
            emit_op(OpCmpFOp);
            body().write_varint(type_idx(bool_type));
            body().write_varint(static_cast<uint32_t>(CmpNotEqual));
            body().write_varint(static_cast<uint32_t>(CmpUnordered));
            body().write_varint(val);
            body().write_varint(val);
            current_id = id;
            // Cast bool result to op->type (usually uint8)
            if (op->type != bool_type) {
                // Extend from i1 to the result type
                uint32_t ext_id = alloc_id();
                emit_op(OpExtIOp);
                body().write_varint(type_idx(op->type));
                body().write_varint(static_cast<uint32_t>(SignednessUnsigned));
                body().write_varint(id);
                current_id = ext_id;
            }
        } else if (op->is_intrinsic()) {
            // For any other intrinsic, lower it to primitive Halide IR and
            // re-visit. This handles the fixed-point math intrinsics:
            // saturating_cast, saturating_add/sub, widen_right_add/sub/mul,
            // widening_add/sub/mul, halving_add/sub, rounding_shift_left/right, etc.
            Expr lowered = lower_intrinsic(op);
            if (lowered.defined()) {
                lowered.accept(this);
            } else {
                internal_error << "Unsupported intrinsic in Tile IR codegen: "
                               << op->name << "\n";
            }
        } else {
            internal_error << "Unsupported call in Tile IR codegen: "
                           << op->name << "\n";
        }
    }

    // Unsupported IR nodes
    void visit(const Fork *) override {
        internal_error << "Fork not supported in Tile IR\n";
    }
    void visit(const Acquire *) override {
        internal_error << "Acquire not supported in Tile IR\n";
    }
    // Tile IR AtomicRMWMode enum values (from cuda-tile ODS).
    enum AtomicRMWMode : uint32_t {
        RMWAnd = 0,
        RMWOr = 1,
        RMWXor = 2,
        RMWAdd = 3,
        RMWAddF = 4,
        RMWMax = 5,
        RMWMin = 6,
        RMWUMax = 7,
        RMWUMin = 8,
        RMWXchg = 9,
    };

    // Try to match `buf[idx] = buf[idx] OP rhs` (where rhs doesn't
    // reference buf). Returns true on match and fills out mode/rhs.
    // Matches Add, Min, Max, And, Or, Xor.
    // Emit a CAS-retry loop implementing `buf[idx] = F(buf[idx])`
    // atomically. Uses OpLoadPtrTkoOp for the initial read, then an
    // OpLoopOp whose body does: compute `new = F(prev)`, `atomic_cas`,
    // compare-equal, and break-on-success.
    void emit_atomic_cas_loop(const Store *store,
                              const std::vector<std::pair<std::string, Expr>> &lets) {
        internal_assert(symbol_table.contains(store->name))
            << "atomic cas: buffer not found " << store->name << "\n";
        uint32_t buf_id = symbol_table.get(store->name);

        // Re-emit the enclosing lets so references in index/value resolve.
        std::vector<ScopedBinding<uint32_t>> let_bindings;
        let_bindings.reserve(lets.size());
        for (auto &[name, value] : lets) {
            value.accept(this);
            let_bindings.emplace_back(symbol_table, name, current_id);
        }

        // Compute the pointer.
        store->index.accept(this);
        uint32_t idx_id = current_id;
        Halide::Type value_t = store->value.type();
        Halide::Type buf_elem =
            lookup_buffer_elem_type(store->name, value_t);
        uint32_t ptr_id = emit_offset_ptr(buf_id, idx_id, value_t, buf_elem);

        // Initial load: OpLoadPtrTkoOp with no mask, no token.
        uint32_t saved_outer_token = current_token;
        current_token = UINT32_MAX;
        emit_load(value_t, ptr_id);
        uint32_t initial_prev_id = current_id;

        // Substitute the matching load in store->value with a placeholder
        // variable so we can re-emit the expression against the loop's
        // prev iter-arg.
        const std::string placeholder = "__tileir_atomic_prev";
        Expr find = Load::make(value_t, store->name, store->index,
                               Buffer<>(), Parameter(),
                               const_true(value_t.lanes()),
                               ModulusRemainder());
        Expr replace = Variable::make(value_t, placeholder);
        Expr new_value_expr = substitute(find, replace, store->value);

        // Capture the loop body into a sub-encoder.
        BlockScope scope = begin_block_scope();
        uint32_t prev_arg_id = alloc_id();  // block arg: prev
        // Region ops are in a fresh memory-token scope.
        current_token = UINT32_MAX;

        {
            ScopedBinding<uint32_t> prev_binding(symbol_table, placeholder,
                                                 prev_arg_id);
            // Emit new_val = F(prev).
            new_value_expr.accept(this);
        }
        uint32_t new_val_id = current_id;

        // Emit atomic_cas_tko with no mask, no token.
        uint32_t token_type_idx = module.types.add_token();
        uint32_t actual_id = alloc_id();
        uint32_t cas_tok_id = alloc_id();
        (void)cas_tok_id;

        emit_op(OpAtomicCASTkoOp);
        body().write_varint(type_idx(value_t));       // result type
        body().write_varint(token_type_idx);          // result_token type
        body().write_varint(0);                        // flags: no mask, no token
        body().write_varint(static_cast<uint32_t>(MemOrderRelaxed));
        body().write_varint(1);                        // MemoryScope::DEVICE
        body().write_varint(ptr_id);                   // pointers
        body().write_varint(prev_arg_id);              // cmp
        body().write_varint(new_val_id);               // val

        // Compare actual == prev -> tile<i1>. Tile IR's atomic_cas uses
        // bitwise equality on floats, so for float types we bitcast the
        // two scalar tiles to same-width int before comparing.
        uint32_t lhs_id = actual_id;
        uint32_t rhs_id = prev_arg_id;
        Halide::Type cmp_t = value_t;
        if (value_t.is_float()) {
            Halide::Type int_t = Int(value_t.bits(), value_t.lanes());
            uint32_t lhs_cast = alloc_id();
            emit_op(OpBitcastOp);
            body().write_varint(type_idx(int_t));
            body().write_varint(lhs_id);
            uint32_t rhs_cast = alloc_id();
            emit_op(OpBitcastOp);
            body().write_varint(type_idx(int_t));
            body().write_varint(rhs_id);
            lhs_id = lhs_cast;
            rhs_id = rhs_cast;
            cmp_t = int_t;
        }
        Halide::Type bool_t = UInt(1, value_t.lanes());
        uint32_t same_id = alloc_id();
        emit_op(OpCmpIOp);
        body().write_varint(type_idx(bool_t));
        body().write_varint(static_cast<uint32_t>(CmpEqual));
        body().write_varint(static_cast<uint32_t>(
            cmp_t.is_uint() ? SignednessUnsigned : SignednessSigned));
        body().write_varint(lhs_id);
        body().write_varint(rhs_id);

        // Build `if (same) { break actual }` (else is empty).
        BlockScope then_scope = begin_block_scope();
        emit_op(OpBreakOp);
        body().write_varint(0);            // numResults=0 (isVariadic)
        body().write_varint(1);            // numOperands=1
        body().write_varint(actual_id);
        uint32_t then_op_count = op_count;
        Encoder then_bytes;
        std::swap(then_bytes, function.body);
        std::swap(then_scope.saved_body, function.body);
        op_count = then_scope.saved_op_count;
        function.next_ssa_id = then_scope.saved_next_ssa_id;
        current_token = then_scope.saved_current_token;

        // Emit IfOp(same) { break actual } (no else region).
        emit_op(OpIfOp);
        body().write_varint(0);            // numResults=0
        body().write_varint(same_id);
        body().write_varint(2);            // numRegions
        // then region: 1 block, 0 block args, <then_op_count> ops
        body().write_varint(1);
        body().write_varint(0);
        body().write_varint(then_op_count);
        body().write_bytes(then_bytes.data().data(), then_bytes.size());
        // else region: 0 blocks
        body().write_varint(0);

        // Continue with actual as next prev.
        emit_op(OpContinueOp);
        body().write_varint(0);            // numResults=0 (isVariadic)
        body().write_varint(1);            // numOperands=1
        body().write_varint(actual_id);

        // Capture the loop-body bytes and restore the outer encoder.
        uint32_t loop_op_count = op_count;
        Encoder loop_bytes;
        std::swap(loop_bytes, function.body);
        std::swap(scope.saved_body, function.body);
        op_count = scope.saved_op_count;
        function.next_ssa_id = scope.saved_next_ssa_id;
        current_token = saved_outer_token;

        // Now emit OpLoopOp proper.
        //   numResults(vi) + resultType x N   (isVariadic)
        //   numOperands(vi) + initIds x N
        //   numRegions=1, numBlocks=1, numBlockArgs=1, blockArgType
        //   numOps(vi) + body bytes
        emit_op(OpLoopOp);
        body().write_varint(1);                        // numResults=1
        body().write_varint(type_idx(value_t));        // result type = tile<T>
        body().write_varint(1);                        // numOperands=1
        body().write_varint(initial_prev_id);
        body().write_varint(1);                        // numRegions
        body().write_varint(1);                        // numBlocks
        body().write_varint(1);                        // numBlockArgs
        body().write_varint(type_idx(value_t));        // block arg type
        body().write_varint(loop_op_count);
        body().write_bytes(loop_bytes.data().data(), loop_bytes.size());

        // Allocate the loop's result SSA id (discarded).
        current_id = alloc_id();
    }

    static bool match_atomic_rmw(const Store *store, AtomicRMWMode &mode,
                                 Expr &rhs) {
        const Load *load_a = nullptr;
        const Expr *other = nullptr;

        auto try_binop = [&](const Expr &a, const Expr &b, AtomicRMWMode m,
                             bool commutative) -> bool {
            if (auto *la = a.as<Load>();
                la && la->name == store->name &&
                equal(la->index, store->index) &&
                !expr_uses_var(b, store->name)) {
                load_a = la; other = &b; mode = m; return true;
            }
            if (commutative) {
                if (auto *lb = b.as<Load>();
                    lb && lb->name == store->name &&
                    equal(lb->index, store->index) &&
                    !expr_uses_var(a, store->name)) {
                    load_a = lb; other = &a; mode = m; return true;
                }
            }
            return false;
        };

        const Expr &v = store->value;
        bool matched = false;
        bool is_float = v.type().is_float();
        bool is_uint = v.type().is_uint();
        if (const Add *add = v.as<Add>())
            matched = try_binop(add->a, add->b, is_float ? RMWAddF : RMWAdd, true);
        else if (const Min *mn = v.as<Min>())
            matched = try_binop(mn->a, mn->b, is_uint ? RMWUMin : RMWMin, true);
        else if (const Max *mx = v.as<Max>())
            matched = try_binop(mx->a, mx->b, is_uint ? RMWUMax : RMWMax, true);
        else if (const Call *c = v.as<Call>()) {
            if (c->is_intrinsic(Call::bitwise_and))
                matched = try_binop(c->args[0], c->args[1], RMWAnd, true);
            else if (c->is_intrinsic(Call::bitwise_or))
                matched = try_binop(c->args[0], c->args[1], RMWOr, true);
            else if (c->is_intrinsic(Call::bitwise_xor))
                matched = try_binop(c->args[0], c->args[1], RMWXor, true);
        }
        if (!matched) return false;
        rhs = *other;
        (void)load_a;
        return true;
    }

    // Find the Store at the bottom of an Atomic body, peeling off any
    // enclosing LetStmts and Blocks and tracking the lets so that
    // the arg expression (and the index) get them in scope.
    void visit(const Atomic *op) override {
        // Walk into nested lets and blocks looking for a single Store.
        std::vector<std::pair<std::string, Expr>> lets;
        std::vector<Stmt> stack;
        stack.push_back(op->body);
        const Store *store = nullptr;
        while (!stack.empty()) {
            Stmt s = stack.back(); stack.pop_back();
            if (const LetStmt *ls = s.as<LetStmt>()) {
                lets.emplace_back(ls->name, ls->value);
                stack.push_back(ls->body);
            } else if (const Block *b = s.as<Block>()) {
                // Process first before rest; stack is LIFO, push rest first.
                if (b->rest.defined()) stack.push_back(b->rest);
                stack.push_back(b->first);
            } else if (const Store *st = s.as<Store>()) {
                if (store) { store = nullptr; break; }  // multiple stores
                store = st;
            } else if (const Evaluate *ev = s.as<Evaluate>()) {
                (void)ev;  // ignore
            } else {
                store = nullptr;  // unsupported shape
                break;
            }
        }

        AtomicRMWMode mode = RMWAdd;
        Expr rhs;
        if (!store || !is_const_one(store->predicate)) {
            // Unrecognized atomic body shape: fall back to non-atomic.
            op->body.accept(this);
            return;
        }

        const bool is_rmw = match_atomic_rmw(store, mode, rhs);
        if (!is_rmw) {
            // Arbitrary update: emit a CAS loop around the body.
            emit_atomic_cas_loop(store, lets);
            return;
        }

        internal_assert(symbol_table.contains(store->name))
            << "atomic: buffer not found " << store->name << "\n";
        uint32_t buf_id = symbol_table.get(store->name);

        // Re-emit the enclosing lets so that any references in the
        // index / rhs resolve correctly.
        std::vector<ScopedBinding<uint32_t>> let_bindings;
        let_bindings.reserve(lets.size());
        for (auto &[name, value] : lets) {
            value.accept(this);
            let_bindings.emplace_back(symbol_table, name, current_id);
        }

        store->index.accept(this);
        uint32_t idx_id = current_id;
        rhs.accept(this);
        uint32_t arg_id = current_id;

        Halide::Type value_t = store->value.type();
        Halide::Type buf_elem =
            lookup_buffer_elem_type(store->name, value_t);
        uint32_t ptr_id = emit_offset_ptr(buf_id, idx_id, value_t, buf_elem);

        // Emit OpAtomicRMWTkoOp.
        // Layout (from cuda-tile ODS + Bytecode writer):
        //   opcode
        //   resultType (tile), result_token_type
        //   flags (bit 0 = mask present, bit 1 = token present)
        //   attrs (ODS order): memory_ordering_semantics, memory_scope, mode
        //   operands (AttrSizedOperandSegments, fixed order):
        //     pointers (1), arg (1), mask (0 or 1), token (0 or 1)
        uint32_t token_type_idx = module.types.add_token();
        uint32_t result_id = alloc_id();
        uint32_t token_out = alloc_id();

        // Flag bits for optional operands (order: mask bit 0, token bit 1).
        uint32_t flags = 0;
        constexpr uint32_t RMWFlagToken = 0x2;
        if (current_token != UINT32_MAX) flags |= RMWFlagToken;

        emit_op(OpAtomicRMWTkoOp);
        body().write_varint(type_idx(value_t));     // result type
        body().write_varint(token_type_idx);        // result_token type
        body().write_varint(flags);

        // Attributes: memory_ordering_semantics, memory_scope, mode
        // (I32EnumAttr values written as inline varints).
        body().write_varint(static_cast<uint32_t>(MemOrderRelaxed));
        body().write_varint(1);  // MemoryScope::DEVICE
        body().write_varint(static_cast<uint32_t>(mode));

        // Operands: pointers, arg, [mask], [token]
        body().write_varint(ptr_id);
        body().write_varint(arg_id);
        // mask not present (flag bit 0 = 0): nothing to write
        if (flags & RMWFlagToken) {
            body().write_varint(current_token);
        }

        // The atomic_rmw returns a new token; thread it through.
        current_token = token_out;
        current_id = result_id;
        (void)result_id;  // result value is dead here (we don't use it)
    }
    void visit(const Provide *) override {
        internal_error << "Provide not supported in Tile IR\n";
    }
    void visit(const Realize *) override {
        internal_error << "Realize not supported in Tile IR\n";
    }
    void visit(const Block *op) override {
        op->first.accept(this);
        if (op->rest.defined()) {
            op->rest.accept(this);
        }
    }
    void visit(const Prefetch *) override { /* ignore */
    }
};

class CodeGen_TileIR_Dev : public CodeGen_GPU_Dev {
public:
    explicit CodeGen_TileIR_Dev(const Target &target)
        : target(target) {
    }

    void add_kernel(Stmt stmt,
                    const std::string &name,
                    const std::vector<DeviceArgument> &args) override {
        debug(2) << "CodeGen_TileIR_Dev::add_kernel " << name << "\n"
                 << stmt << "\n";

        TileIR::Function func;
        func.name = name;
        func.name_idx = module.strings.add(name);
        func.flags = FuncKindKernel;

        current_kernel_name = name;

        TileIR_Emitter emitter(module, func);
        std::vector<uint32_t> param_type_idxs;

        for (const auto &arg : args) {
            debug(2) << "  arg: " << arg.name << " is_buffer=" << arg.is_buffer << "\n";
            uint32_t type_idx;
            if (arg.is_buffer) {
                // Buffer args: tile<ptr<element_type>>
                uint32_t elem_idx = module.types.add_scalar(arg.type);
                uint32_t ptr_idx = module.types.add_pointer(elem_idx);
                // Scalar tile wrapping the pointer
                type_idx = module.types.add_tile({}, ptr_idx);
            } else {
                // Scalar args: tile<scalar_type> (scalar tile)
                type_idx = module.types.get_type_idx(arg.type);
            }
            param_type_idxs.push_back(type_idx);

            uint32_t arg_id = func.alloc_id();
            emitter.symbol_table.push(arg.name, arg_id);
            if (arg.is_buffer) {
                emitter.buffer_elem_types[arg.name] = arg.type;
            }
        }

        func.func_type_idx = module.types.add_function(
            static_cast<uint32_t>(param_type_idxs.size()), param_type_idxs,
            0, {});

        // Rewrite Euclidean Div/Mod into div_round_to_zero / mod_round_to_zero
        // plus sign correction, so the emitter can use Tile IR's truncating
        // DivIOp/RemIOp directly.
        stmt = LowerEuclideanDivMod().mutate(stmt);
        // CUDATileIR skips the global flatten_nested_ramps pass (so
        // LowerMma can recognize multi-dim shape info during Lower);
        // run the same rewrite locally now that the matmul-pattern
        // mutator has done its job. This keeps the rest of the
        // emitter dealing only with 1D vector exprs.
        stmt = flatten_nested_ramps(stmt);

        // Emit the body
        stmt.accept(&emitter);

        // ReturnOp (isVariadic due to Variadic operands): numResults + numOperands
        func.body.write_varint(static_cast<uint64_t>(OpReturnOp));
        func.body.write_varint(0);  // numResults = 0
        func.body.write_varint(0);  // numOperands = 0

        module.functions.push_back(std::move(func));
    }

    std::vector<char> compile_to_src() override {
        std::vector<char> output;
        module.encode(output);
        debug(2) << "Tile IR bytecode: " << output.size() << " bytes\n";
        {
            std::ofstream f("/tmp/halide_tile_ir.bin", std::ios::binary);
            f.write(output.data(), output.size());
            debug(2) << "Wrote Tile IR bytecode to /tmp/halide_tile_ir.bin\n";
        }
        return output;
    }

    std::string get_current_kernel_name() override {
        return current_kernel_name;
    }

    void dump() override {
    }

    std::string api_unique_name() override {
        // We share a runtime module with the ptx backend.
        return "cuda";
    }

    std::string print_gpu_name(const std::string &name) override {
        return name;
    }

    void init_module() override {
        module = TileIR::Module();
    }

    bool kernel_run_takes_types() const override {
        return false;
    }

private:
    Target target;
    TileIR::Module module;
    std::string current_kernel_name;
};

}  // namespace

std::unique_ptr<CodeGen_GPU_Dev> new_CodeGen_TileIR_Dev(const Target &target) {
    return std::make_unique<CodeGen_TileIR_Dev>(target);
}

Stmt lower_cuda_tile_mma(const Stmt &s) {
    return LowerMma().mutate(s);
}

}  // namespace Internal
}  // namespace Halide
