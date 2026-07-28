#include "ExtractWMMAOperations.h"

#include "CanonicalizeGPUVars.h"
#include "FindIntrinsics.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "MultiRamp.h"
#include "Simplify.h"
#include "Util.h"

/** \file Support extraction of NVIDIA tensor core (wmma) instructions.
 *
 * The wmma instructions are warp-level: the 32 lanes of a warp cooperate to
 * hold a tile of a matrix, and each instruction is executed by the warp as a
 * whole. The layout of the tile across the lanes' registers is not specified
 * by the architecture, so the only way to get data into or out of a tile is
 * with the wmma load and store instructions, which move a tile between the
 * registers of a warp and a 2D array in memory.
 *
 * Accordingly this pass recognizes exactly three operations on a
 * WMMAAccumulator allocation:
 *
 * 1) Zero-initialization. This one is layout-independent, so it stays a plain
 *    store of zero to the (shrunken) allocation.
 * 2) Accumulation of a matrix multiply, which becomes a pair of wmma loads
 *    feeding a wmma mma.
 * 3) Copying a tile out to memory, which becomes a wmma store.
 *
 * Anything else is an error. Each of those operations is wrapped in a loop over
 * the 32 lanes of a warp, because nothing in the schedule says the tile is
 * spread over a warp - that's a consequence of asking for tensor core storage.
 */

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

// The tile shapes the hardware supports, in the order we try them.
struct Shape {
    int M, N, K;
};
const Shape supported_shapes[] = {{16, 16, 16}, {32, 8, 16}, {8, 32, 16}};

// A matrix operand is stored either with its rows contiguous or its columns
// contiguous.
enum class Layout {
    Row,
    Col,
};

// Every warp is 32 lanes, and every tile shape we support holds 256 elements,
// so each lane holds 8 of them.
constexpr int warp_lanes = 32;
constexpr int fragment_elements = 8;

// The Halide type we use to represent an a or b fragment. Every operand
// fragment is 8 32-bit registers per lane, which is more matrix elements than
// there are for some shapes, because the hardware replicates elements across
// lanes for those.
Type fragment_type(Type element_type) {
    return element_type.with_lanes(16);
}

// The Halide type we use to represent an accumulator fragment.
Type accumulator_fragment_type(Type element_type) {
    return element_type.with_lanes(fragment_elements);
}

// One operand of a matrix multiply, described in the canonical [K, N, M]
// (innermost first) coordinate system.
struct Operand {
    const Load *load = nullptr;
    MultiRamp mr;
    vector<Expr> strides;
};

// Try to interpret an operand as the left-hand side of the multiply: an M x K
// matrix that doesn't depend on the N coordinate.
bool is_lhs(const Operand &op, Layout *layout, Expr *stride) {
    if (!is_const_zero(op.strides[1])) {
        return false;
    }
    if (is_const_one(op.strides[0])) {
        *layout = Layout::Row;
        *stride = op.strides[2];
    } else if (is_const_one(op.strides[2])) {
        *layout = Layout::Col;
        *stride = op.strides[0];
    } else {
        return false;
    }
    return true;
}

// Try to interpret an operand as the right-hand side of the multiply: a K x N
// matrix that doesn't depend on the M coordinate.
bool is_rhs(const Operand &op, Layout *layout, Expr *stride) {
    if (!is_const_zero(op.strides[2])) {
        return false;
    }
    if (is_const_one(op.strides[1])) {
        *layout = Layout::Row;
        *stride = op.strides[0];
    } else if (is_const_one(op.strides[0])) {
        *layout = Layout::Col;
        *stride = op.strides[1];
    } else {
        return false;
    }
    return true;
}

// The wmma instructions are warp-level, so every statement that touches an
// accumulator has to be executed by all 32 lanes of a warp. Nothing in the
// schedule says so - it's a consequence of asking for tensor core storage - so
// the loop over lanes is introduced here.
Stmt in_lane_loop(Stmt s) {
    return For::make(unique_name("wmma_lane") + gpu_thread_name(0),
                     0, warp_lanes - 1, ForType::GPULane, Partition::Never,
                     DeviceAPI::CUDA, std::move(s));
}

// The matrix a wmma load or store moves, described as a Load of every element
// of it, with the lanes in row-major order regardless of how it is laid out in
// memory. A row-major matrix has consecutive columns one element apart and its
// rows stride apart; a column-major one is the other way around, which changes
// the strides of the index but not the order of the lanes. So the argument is
// the matrix value, and the memory layout is recoverable from the index.
Expr make_matrix_index(const Expr &base, int rows, int cols, Layout layout,
                       const Expr &stride) {
    Expr one = make_one(base.type());
    Expr col_stride = layout == Layout::Row ? one : stride;
    Expr row_stride = layout == Layout::Row ? stride : one;
    return Ramp::make(Ramp::make(base, col_stride, cols),
                      Broadcast::make(row_stride, cols), rows);
}

Expr make_matrix_address(const string &name, Type element_type, const Expr &base,
                         int rows, int cols, Layout layout, const Expr &stride,
                         const Buffer<> &image, const Parameter &param) {
    Expr index = make_matrix_index(base, rows, cols, layout, stride);
    const int lanes = rows * cols;
    return Load::make(element_type.with_lanes(lanes), name, index, image, param,
                      const_true(lanes), ModulusRemainder());
}

// The shape of the matrix each fragment is taken out of.
void fragment_matrix_shape(Call::IntrinsicOp intrin, const Shape &shape,
                           int *rows, int *cols) {
    *rows = intrin == Call::wmma_matrix_to_fragment_b ? shape.K : shape.M;
    *cols = intrin == Call::wmma_matrix_to_fragment_a ? shape.K : shape.N;
}

Expr make_matrix_to_fragment(Call::IntrinsicOp intrin, const Shape &shape, Layout layout,
                             const Load *load, const Expr &base, const Expr &stride) {
    int rows, cols;
    fragment_matrix_shape(intrin, shape, &rows, &cols);
    Expr address = make_matrix_address(load->name, load->type.element_of(), base,
                                       rows, cols, layout, stride, load->image, load->param);
    Type type = intrin == Call::wmma_matrix_to_fragment_c ?
                    accumulator_fragment_type(load->type.element_of()) :
                    fragment_type(load->type.element_of());
    return Call::make(type, intrin, {shape.M, shape.N, shape.K, std::move(address)},
                      Call::Intrinsic);
}

struct Matmul {
    Stmt stmt;
    Shape shape;
};

Matmul convert_to_matmul(const Store *op, const string &new_name) {
    // We expect the pattern:
    //
    // out[idx] = reduce_add(widen(lhs[multiramp]) * widen(rhs[multiramp])) + out[idx]
    //
    // Though either operand may have been hoisted out to a broadcast or had a
    // lane permutation left on it by vectorization.

    auto fail = [&](const char *reason) -> Matmul {
        user_error << "Matrix multiply not recognized. Store to a WMMAAccumulator "
                   << "allocation must be a zero-initialization or a sum of a vector "
                   << "reduce op and a load from the same allocation. In the following "
                   << "store, " << reason << ".\n"
                   << Stmt(op);
        return Matmul{};
    };

    // Peel lets
    vector<std::pair<string, Expr>> peeled_lets;
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
    const auto *reduce = add->a.as<VectorReduce>();
    if (!reduce || reduce->op != VectorReduce::Add) {
        return fail("the right-hand-side is not a vector reduction plus a load");
    }

    // The load must be to the same addresses as the store (i.e. this is a +=)
    const auto *load = add->b.as<Load>();
    if (!load || load->name != op->name || !equal(load->index, op->index)) {
        return fail("the right-hand-side load is not from the same address as the store");
    }

    // There must be no predicate on the load or store
    if (!is_const_one(load->predicate) || !is_const_one(op->predicate)) {
        return fail("the load or store is predicated");
    }

    if (!reduce->type.is_float() ||
        !(reduce->type.bits() == 32 || reduce->type.bits() == 16)) {
        return fail("the accumulator type is not 32-bit or 16-bit float");
    }

    // The vector reduce must be of a widening multiply. FindIntrinsics does
    // not lift float widening muls, so we just expect a multiply of two casts.
    Expr reduce_value = simplify(lower_intrinsics(reduce->value));
    const auto *mul = reduce_value.as<Mul>();
    if (!mul) {
        return fail("the vector reduction is not of a multiply");
    }
    // Under the casts, broadcasts and lane permutations that vectorization may
    // have left on each operand there must be a load.
    Scope<Expr> empty_scope;
    Operand lhs_op, rhs_op;
    lhs_op.load = is_load_of_multiramp(mul->a, empty_scope, &lhs_op.mr);
    rhs_op.load = is_load_of_multiramp(mul->b, empty_scope, &rhs_op.mr);
    if (!lhs_op.load || !rhs_op.load) {
        return fail("the matrix multiply operands are not loads with affine indices");
    }
    if (!is_const_one(lhs_op.load->predicate) || !is_const_one(rhs_op.load->predicate)) {
        return fail("the matrix multiply operands are predicated loads");
    }
    if (lhs_op.load->type.element_of() != Float(16) ||
        rhs_op.load->type.element_of() != Float(16)) {
        return fail("the matrix multiply operands are not both float16");
    }

    // In a matrix multiply with row-major inputs and outputs, the algorithm
    // looks like:
    //
    // C(j, i) += A(k, i) * B(j, k)
    //
    // (Recall that for matrices where the rows are stored densely in memory,
    // Halide is indexed col-major.) The canonical loop nest order, from
    // innermost out, is k, j, i, which is the coordinate system we compare the
    // operands' access patterns in. In that coordinate system, i indexes the M
    // rows of the output, j indexes its N columns, and k indexes the reduction.
    int MN = reduce->type.lanes();
    int K = reduce->value.type().lanes() / MN;

    // Deduce which operand is which and what tile shape this is by trying each
    // supported shape and seeing which one the access patterns fit.
    const Shape *shape = nullptr;
    Layout lhs_layout = Layout::Row, rhs_layout = Layout::Row;
    Expr lda, ldb;
    for (const Shape &candidate : supported_shapes) {
        if (candidate.M * candidate.N != MN || candidate.K != K) {
            continue;
        }
        vector<int> canonical_shape{candidate.K, candidate.N, candidate.M};
        if (!lhs_op.mr.strides_for_shape(canonical_shape, &lhs_op.strides) ||
            !rhs_op.mr.strides_for_shape(canonical_shape, &rhs_op.strides)) {
            continue;
        }
        if (is_lhs(lhs_op, &lhs_layout, &lda) && is_rhs(rhs_op, &rhs_layout, &ldb)) {
            shape = &candidate;
            break;
        }
        if (is_lhs(rhs_op, &lhs_layout, &lda) && is_rhs(lhs_op, &rhs_layout, &ldb)) {
            std::swap(lhs_op, rhs_op);
            shape = &candidate;
            break;
        }
    }

    if (!shape) {
        return fail("the operands' access patterns do not describe a matrix "
                    "multiply of a tile shape the tensor cores support (16x16x16, "
                    "32x8x16, or 8x32x16)");
    }

    // Build the wmma intrinsics.
    Expr a = make_matrix_to_fragment(Call::wmma_matrix_to_fragment_a, *shape, lhs_layout,
                                     lhs_op.load, lhs_op.mr.base, lda);
    Expr b = make_matrix_to_fragment(Call::wmma_matrix_to_fragment_b, *shape, rhs_layout,
                                     rhs_op.load, rhs_op.mr.base, ldb);

    Type acc_type = accumulator_fragment_type(reduce->type.element_of());
    Expr frag_idx = Ramp::make(0, 1, fragment_elements);
    Expr c = Load::make(acc_type, new_name, frag_idx, {}, {},
                        const_true(fragment_elements), {});

    Expr mma = Call::make(acc_type, Call::wmma_mma,
                          {shape->M, shape->N, shape->K,
                           (int)lhs_layout, (int)rhs_layout,
                           std::move(a), std::move(b), std::move(c)},
                          Call::Intrinsic);

    Stmt store = in_lane_loop(
        Store::make(new_name, std::move(mma), frag_idx, Parameter(),
                    const_true(fragment_elements), ModulusRemainder()));
    for (auto &[name, v] : reverse_view(peeled_lets)) {
        store = LetStmt::make(name, std::move(v), store);
    }
    return {std::move(store), *shape};
}

// Whether a store to an accumulator is its initialization, as opposed to a
// matrix multiply accumulating into it.
bool is_initialization(const Store *op, const string &tile_name) {
    if (is_const_zero(op->value)) {
        return true;
    }
    MultiRamp mr;
    const Load *load = is_load_of_multiramp(op->value, Scope<Expr>::empty_scope(), &mr);
    return load && load->name != tile_name;
}

Stmt convert_to_init(const Store *op, const string &new_name, const Shape &shape) {
    Type element_type = op->value.type().element_of();
    Expr value;
    if (is_const_zero(op->value)) {
        // Zeroing an accumulator is layout-independent, so it doesn't need an
        // instruction - the registers just get set to zero.
        value = make_zero(accumulator_fragment_type(element_type));
    } else {
        auto fail = [&](const char *reason) {
            user_error << "Initialization of a tensor core accumulator not supported. "
                       << reason << ".\n"
                       << Stmt(op);
            return Expr{};
        };

        MultiRamp mr;
        const Load *matrix = is_load_of_multiramp(op->value, Scope<Expr>::empty_scope(), &mr);
        internal_assert(matrix);  // is_initialization checked this
        if (matrix->type.element_of() != element_type) {
            value = fail("An accumulator can only be initialized from a matrix of the "
                         "same type, because the hardware does not convert on the way in");
        } else if (!is_const_one(matrix->predicate)) {
            value = fail("The load is predicated");
        } else {
            WMMAMatrixLayout mem;
            if (!wmma_matrix_layout(mr, shape.M, shape.N, &mem)) {
                value = fail("The matrix loaded from is not a dense tile of the right shape");
            } else {
                value = make_matrix_to_fragment(
                    Call::wmma_matrix_to_fragment_c, shape,
                    mem.row_major ? Layout::Row : Layout::Col, matrix, mem.base, mem.stride);
            }
        }
    }
    Expr frag_idx = Ramp::make(0, 1, fragment_elements);
    return in_lane_loop(
        Store::make(new_name, std::move(value), frag_idx, Parameter(),
                    const_true(fragment_elements), ModulusRemainder()));
}

Stmt convert_to_tile_store(const Store *op, const Expr &store_index,
                           const string &new_name, const Shape &shape) {
    auto fail = [&](const char *reason) {
        user_error << "Store of a tensor core accumulator to memory not supported. "
                   << reason << ".\n"
                   << Stmt(op);
        return Stmt{};
    };

    if (!is_const_one(op->predicate)) {
        return fail("The store has a predicate");
    }
    MultiRamp mr;
    if (!is_multiramp(store_index, Scope<Expr>::empty_scope(), &mr)) {
        return fail("The store index is not affine");
    }
    WMMAMatrixLayout mem;
    if (!wmma_matrix_layout(mr, shape.M, shape.N, &mem)) {
        return fail("The store is not to a dense tile of the deduced matrix shape");
    }
    Layout layout = mem.row_major ? Layout::Row : Layout::Col;

    // Each lane writes the entries of the matrix that it holds. The index
    // enumerates the matrix in row-major order, which is the lane order of the
    // accumulator.
    Expr index = make_matrix_index(mem.base, shape.M, shape.N, layout, mem.stride);
    Type element_type = op->value.type().element_of();
    Expr frag = Load::make(accumulator_fragment_type(element_type), new_name,
                           Ramp::make(0, 1, fragment_elements), {}, {},
                           const_true(fragment_elements), {});
    const int lanes = shape.M * shape.N;
    Expr matrix = Call::make(element_type.with_lanes(lanes), Call::wmma_fragment_to_matrix_d,
                             {shape.M, shape.N, shape.K, std::move(frag)},
                             Call::Intrinsic);
    Expr owned = Call::make(UInt(1, lanes), Call::wmma_lane_owns,
                            {shape.M, shape.N, shape.K}, Call::Intrinsic);
    return in_lane_loop(Store::make(op->name, std::move(matrix), std::move(index),
                                    op->param, std::move(owned), ModulusRemainder(),
                                    op->is_streaming));
}

class ExtractWMMAOperations : public IRMutator {
    using IRMutator::visit;

    string tile_name;
    string wmma_name;
    int pass = 0;
    bool in_allocate = false;
    bool found_shape = false;
    Shape shape{};

    // A WMMAAccumulator allocation may hold several accumulator fragments as
    // 2D sub-tiles. This tracks them.
    vector<MultiRamp> subtiles;

    string get_subtile_name(const Expr &index) {
        int idx = Halide::Internal::get_subtile(index, "tensor core accumulator", &subtiles);
        internal_assert(idx >= 0);  // errors handled already
        return wmma_name + std::to_string(idx);
    }

    Stmt visit(const Allocate *op) override {
        if (op->memory_type != MemoryType::WMMAAccumulator) {
            return IRMutator::visit(op);
        }

        user_assert(op->type == Float(32) || op->type == Float(16))
            << "Tensor core accumulators must hold 32-bit or 16-bit floats, but "
            << op->name << " holds " << op->type << ".\n";

        user_assert(!in_allocate)
            << "Already in a tensor core accumulator allocation at the allocation for "
            << op->name << ". We do not currently support multiple nested tensor core "
            << "matrix multiplies.";

        ScopedValue<string> old_wmma_name(wmma_name, op->name + ".wmma.");
        ScopedValue<string> old_tile_name(tile_name, op->name);
        ScopedValue<bool> old_in_alloc(in_allocate, true);

        // In the first pass we recognize the matrix multiplies, which is what
        // tells us the tile shape. In the second we recognize the
        // zero-initializations and the stores out to memory, both of which
        // need to know the shape.
        pass = 0;
        Stmt body = mutate(op->body);
        user_assert(found_shape)
            << op->name << " is stored in WMMAAccumulator memory, but no matrix "
            << "multiply operation was found that stores to it, so the shape of the "
            << "tile was unable to be determined.\n";
        pass = 1;
        body = mutate(body);

        // Each fragment is one accumulator's worth of storage per lane. The
        // allocations stay outside the loops over lanes, because a loop over
        // lanes is a loop over threads, and register allocations outside a
        // thread loop already get replicated per thread.
        for (int i = 0; i < (int)subtiles.size(); i++) {
            body = Allocate::make(wmma_name + std::to_string(i), op->type,
                                  MemoryType::WMMAAccumulator, {fragment_elements},
                                  const_true(), body);
        }
        return body;
    }

    Stmt visit(const Free *op) override {
        if (op->name != tile_name) {
            return op;
        }
        Stmt s;
        for (int i = 0; i < (int)subtiles.size(); i++) {
            Stmt f = Free::make(wmma_name + std::to_string(i));
            s = s.defined() ? Block::make(std::move(s), std::move(f)) : std::move(f);
        }
        return s;
    }

    Stmt visit(const ProducerConsumer *op) override {
        if (op->name != tile_name) {
            return IRMutator::visit(op);
        }
        return ProducerConsumer::make(wmma_name, op->is_producer, mutate(op->body));
    }

    Expr visit(const Load *op) override {
        user_assert(op->name != tile_name)
            << "Tensor core accumulator " << tile_name
            << " used outside a tensor core instruction";
        return IRMutator::visit(op);
    }

    Stmt visit(const Store *op) override {
        // There are three operations on an accumulator:
        // 1) Zero-initialization
        // 2) Matrix multiply
        // 3) Stores to memory
        //
        // The matrix multiply is what tells us the tile shape, so we recognize
        // those in the first pass and the other two in the second.

        if (op->name != tile_name) {
            Expr store_index;
            const Load *load = peel_store_permutations(op, &store_index).as<Load>();
            if (load && load->name == tile_name) {
                return pass == 1 ?
                           convert_to_tile_store(op, store_index,
                                                 get_subtile_name(load->index), shape) :
                           Stmt(op);
            }
            // Not a copy of a tile out to memory. Recurse, so that any use of
            // the accumulator buried in here gets reported as an error.
            return IRMutator::visit(op);
        }

        string subtile_name = get_subtile_name(op->index);

        if (is_initialization(op, tile_name)) {
            return pass == 1 ? convert_to_init(op, subtile_name, shape) : Stmt(op);
        }

        if (pass == 1) {
            return op;
        }

        Matmul matmul = convert_to_matmul(op, subtile_name);
        user_assert(!found_shape ||
                    (matmul.shape.M == shape.M &&
                     matmul.shape.N == shape.N &&
                     matmul.shape.K == shape.K))
            << "Found inconsistent tile shapes for a WMMAAccumulator allocation across "
            << "multiple matrix multiplies that store to it.";
        shape = matmul.shape;
        found_shape = true;
        return matmul.stmt;
    }
};

}  // namespace

Stmt extract_wmma_operations(const Stmt &s) {
    return ExtractWMMAOperations()(s);
}

bool is_wmma_intrinsic(const Call *op) {
    return (op->is_intrinsic(Call::wmma_matrix_to_fragment_a) ||
            op->is_intrinsic(Call::wmma_matrix_to_fragment_b) ||
            op->is_intrinsic(Call::wmma_matrix_to_fragment_c) ||
            op->is_intrinsic(Call::wmma_mma));
}

Expr peel_store_permutations(const Store *op, Expr *index) {
    Expr value = op->value;
    *index = op->index;
    while (const Shuffle *shuffle = value.as<Shuffle>()) {
        if (!shuffle->is_transpose()) {
            break;
        }
        // Transposing both sides of the store cancels out.
        const int rows = value.type().lanes() / shuffle->transpose_factor();
        *index = Shuffle::make_transpose(*index, rows);
        value = shuffle->vectors[0];
    }
    return value;
}

bool wmma_matrix_layout(const MultiRamp &mr, int rows, int cols,
                        WMMAMatrixLayout *result) {
    std::vector<Expr> strides;
    if (mr.total_lanes() != rows * cols ||
        !mr.strides_for_shape({cols, rows}, &strides)) {
        return false;
    }
    // A row-major matrix has its columns one element apart.
    if (is_const_one(strides[0])) {
        result->row_major = true;
        result->stride = strides[1];
    } else if (is_const_one(strides[1])) {
        result->row_major = false;
        result->stride = strides[0];
    } else {
        return false;
    }
    result->base = mr.base;
    return true;
}

bool is_wmma_matrix_store(const Store *op) {
    Expr index;
    const Call *value = peel_store_permutations(op, &index).as<Call>();
    return value && value->is_intrinsic(Call::wmma_fragment_to_matrix_d);
}

int wmma_matrix_arg(const Call *op) {
    if (op->is_intrinsic(Call::wmma_matrix_to_fragment_a) ||
        op->is_intrinsic(Call::wmma_matrix_to_fragment_b) ||
        op->is_intrinsic(Call::wmma_matrix_to_fragment_c)) {
        return 3;
    }
    return -1;
}

}  // namespace Internal
}  // namespace Halide
