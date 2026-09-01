#include "ExtractWMMAOperations.h"

#include "CanonicalizeGPUVars.h"
#include "FindIntrinsics.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "ExprUsesVar.h"
#include "IRVisitor.h"
#include "IROperator.h"
#include "MultiRamp.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Util.h"

#include <map>

/** \file Support extraction of NVIDIA tensor core (wmma) instructions.
 *
 * The wmma instructions are warp-level: the 32 lanes of a warp cooperate to
 * hold a tile of a matrix, and each instruction is executed by the warp as a
 * whole. The layout of the tile across the lanes' registers is not specified
 * by the architecture, so the only way to get data into or out of a tile is
 * with the wmma load and store instructions, which move a tile between the
 * registers of a warp and a 2D array in memory.
 *
 * A tile allocation holds one of the three matrices of a multiply, and
 * which one follows from how it is used. An allocation accumulated into by a
 * matrix multiply is the accumulator; one read as an operand of a multiply is
 * that operand. The role determines which accesses are legal:
 *
 * - An accumulator may be zero-initialized, filled from a matrix in memory,
 *   accumulated into by a matrix multiply, and copied back out to memory.
 * - An operand may be filled from a matrix in memory and read by a matrix
 *   multiply.
 *
 * Anything else is an error. Every one of those operations is wrapped in a loop
 * over the 32 lanes of a warp, because nothing in the schedule says the tile is
 * spread over a warp - that's a consequence of asking for tensor core storage.
 *
 * Operands don't have to be staged in fragments. If a multiply reads its
 * operand straight out of shared or global memory, the load into registers is
 * synthesized at the multiply instead. Staging one explicitly is how a schedule
 * says to load it once and reuse it across several multiplies.
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

// Which of the three matrices of the multiply a fragment holds.
enum class Role {
    Unknown,
    A,
    B,
    Accumulator,
};

Call::IntrinsicOp intrinsic_for_role(Role role) {
    switch (role) {
    case Role::A:
        return Call::wmma_matrix_to_fragment_a;
    case Role::B:
        return Call::wmma_matrix_to_fragment_b;
    default:
        return Call::wmma_matrix_to_fragment_c;
    }
}

const char *role_name(Role role) {
    switch (role) {
    case Role::A:
        return "the first operand";
    case Role::B:
        return "the second operand";
    case Role::Accumulator:
        return "the accumulator";
    default:
        return "no part";
    }
}

// Every warp is 32 lanes, and an accumulator tile holds 256 elements whatever
// its shape, so each lane holds 8 of them whatever their type.
constexpr int warp_lanes = 32;
constexpr int accumulator_elements = 8;
// Half precision operand fragments are eight 32-bit registers per lane
// whatever the shape, which is sixteen elements. For the shapes that hold
// fewer than that the hardware replicates elements across lanes.
constexpr int half_operand_elements = 16;

// The multiplicand and accumulator type combinations the tensor cores have an
// instruction for. Half precision multiplicands accumulate into halves or
// floats, brain floats accumulate into floats, and eight-bit integers
// accumulate into 32-bit ones. The multiplicands must match each other: at
// this shape there is no instruction for mixed signedness.
bool wmma_types_supported(Type operand, Type accumulator) {
    if (operand == Float(16)) {
        return accumulator == Float(16) || accumulator == Float(32);
    } else if (operand == BFloat(16)) {
        return accumulator == Float(32);
    } else if (operand == Int(8) || operand == UInt(8)) {
        return accumulator == Int(32);
    }
    return false;
}

// The shape of the matrix each fragment is taken out of.
void fragment_matrix_shape(Role role, const Shape &shape, int *rows, int *cols) {
    *rows = role == Role::B ? shape.K : shape.M;
    *cols = role == Role::A ? shape.K : shape.N;
}

// How many elements of a fragment each lane holds. Every type but half
// precision holds exactly its share of the matrix, so this follows from the
// shape; half precision is the fixed size above however small the shape is.
int elements_per_lane(Role role, const Shape &shape, Type t) {
    if (role == Role::Accumulator) {
        return accumulator_elements;
    }
    if (t == Float(16)) {
        return half_operand_elements;
    }
    int rows, cols;
    fragment_matrix_shape(role, shape, &rows, &cols);
    return rows * cols / warp_lanes;
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
// The lane of the warp a tensor core operation runs in. Every wmma intrinsic
// takes it, because their values depend on it and nothing else in their
// arguments does.
Expr make_lane(const string &name) {
    return Variable::make(Int(32), name);
}

Stmt in_lane_loop(const Expr &lane, Stmt s) {
    const Variable *v = lane.as<Variable>();
    internal_assert(v) << "the lane of a tensor core operation is not a variable\n";
    return For::make(v->name, 0, warp_lanes - 1, ForType::GPULane, Partition::Never,
                     DeviceAPI::CUDA, std::move(s));
}

// Whether a Stmt reads a fragment as the whole matrix it holds. Assembling the
// matrix takes the registers of every lane, so the whole warp has to reach it.
bool reads_a_whole_matrix(const Stmt &s) {
    class Finder : public IRVisitor {
        using IRVisitor::visit;
        void visit(const Call *op) override {
            found = found || (op->is_intrinsic(Call::wmma_fragment_to_matrix_d) &&
                              op->args.size() == 4);
            IRVisitor::visit(op);
        }

    public:
        bool found = false;
    } finder;
    s.accept(&finder);
    return finder.found;
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
                      const_true(lanes), ModulusRemainder(), false);
}

Expr make_matrix_to_fragment(Role role, const Shape &shape, Layout layout,
                             const Load *load, const Expr &base, const Expr &stride,
                             const Expr &lane) {
    int rows, cols;
    fragment_matrix_shape(role, shape, &rows, &cols);
    Expr address = make_matrix_address(load->name, load->type.element_of(), base,
                                       rows, cols, layout, stride, load->image, load->param);
    Type type = load->type.element_of().with_lanes(
        elements_per_lane(role, shape, load->type.element_of()));
    return Call::make(type, intrinsic_for_role(role),
                      {shape.M, shape.N, shape.K, std::move(address), lane},
                      Call::Intrinsic);
}

// A store to a fragment recognized as the accumulation of a matrix multiply,
// broken down into the pieces the multiply is built from.
struct MatmulInfo {
    Shape shape;
    Operand lhs, rhs;
    Layout lhs_layout = Layout::Row, rhs_layout = Layout::Row;
    Expr lda, ldb;
    Type accumulator_type;
    vector<std::pair<string, Expr>> peeled_lets;
};

// Whether a store accumulates into the allocation it writes, which is the
// shape a matrix multiply arrives in. Anything else that isn't a fill is an
// elementwise op on the tile.
bool is_accumulation(const Store *op) {
    Expr value = op->value;
    while (const Let *let = value.as<Let>()) {
        value = let->body;
    }
    const Add *add = value.as<Add>();
    if (!add || !add->a.as<VectorReduce>()) {
        return false;
    }
    const Load *load = add->b.as<Load>();
    return load && load->name == op->name && equal(load->index, op->index);
}

// An accumulation whose reduction is of a fragment rather than of a product of
// two matrices. That is a reduction along an axis of the tile, which happens
// where the fragments sit rather than in the matrix unit.
const VectorReduce *reduction_of_a_fragment(const Store *op) {
    Expr value = op->value;
    while (const Let *let = value.as<Let>()) {
        value = let->body;
    }
    const Add *add = value.as<Add>();
    return add ? add->a.as<VectorReduce>() : nullptr;
}

// Whether a reduced value is a product of two operands, which is what a matrix
// multiply reduces and a reduction along an axis of a tile does not. Widening
// casts sit between the multiply and the reduction.
bool is_product(Expr e) {
    while (const Cast *op = e.as<Cast>()) {
        e = op->value;
    }
    return e.as<Mul>() != nullptr;
}

MatmulInfo analyze_matmul(const Store *op,
                          const std::function<bool(const string &)> &is_accumulator) {
    // We expect the pattern:
    //
    // out[idx] = reduce_add(widen(lhs[multiramp]) * widen(rhs[multiramp])) + out[idx]
    //
    // Though either operand may have been hoisted out to a broadcast or had a
    // lane permutation left on it by vectorization.

    auto fail = [&](const char *reason) -> MatmulInfo {
        user_error << "Matrix multiply not recognized. Store to a Tile "
                   << "allocation must be a zero-initialization, a fill from a matrix "
                   << "in memory, or a sum of a vector reduce op and a load from the "
                   << "same allocation. In the following store, " << reason << ".\n"
                   << Stmt(op);
        return MatmulInfo{};
    };

    MatmulInfo info;

    // Peel lets
    Expr value = op->value;
    while (const Let *let = value.as<Let>()) {
        info.peeled_lets.emplace_back(let->name, let->value);
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

    info.accumulator_type = reduce->type.element_of();
    if (!(info.accumulator_type == Float(32) ||
          info.accumulator_type == Float(16) ||
          info.accumulator_type == Int(32))) {
        return fail("the accumulator type is not float32, float16, or int32");
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
    auto operand_load = [&](const Expr &e, MultiRamp *mr) -> const Load * {
        if (const Load *l = is_load_of_multiramp(e, empty_scope, mr)) {
            return l;
        }
        // The accumulator of an earlier multiply feeding this one arrives
        // under a narrowing round trip: narrowed to the operand's precision,
        // then widened again for the multiply. The relayout that takes it out
        // of the accumulator performs exactly that rounding, so look under
        // the pair - but only for an accumulator, where it does. The pair may
        // sit under a broadcast that vectorization hoisted the operand into.
        std::function<Expr(const Expr &)> strip = [&](const Expr &b) -> Expr {
            if (const Broadcast *bc = b.as<Broadcast>()) {
                Expr v = strip(bc->value);
                if (!v.same_as(bc->value)) {
                    return Broadcast::make(v, bc->lanes);
                }
                return b;
            }
            const Cast *widen = b.as<Cast>();
            const Cast *narrow = widen ? widen->value.as<Cast>() : nullptr;
            if (narrow &&
                widen->type.element_of() == Float(32) &&
                narrow->type.element_of() == Float(16) &&
                narrow->value.type().element_of() == Float(32)) {
                return narrow->value;
            }
            return b;
        };
        Expr stripped = strip(e);
        if (!stripped.same_as(e)) {
            const Load *l = is_load_of_multiramp(stripped, empty_scope, mr);
            if (l && is_accumulator(l->name)) {
                return l;
            }
        }
        return nullptr;
    };
    info.lhs.load = operand_load(mul->a, &info.lhs.mr);
    info.rhs.load = operand_load(mul->b, &info.rhs.mr);
    if (!info.lhs.load || !info.rhs.load) {
        return fail("the matrix multiply operands are not loads with affine indices");
    }
    if (!is_const_one(info.lhs.load->predicate) || !is_const_one(info.rhs.load->predicate)) {
        return fail("the matrix multiply operands are predicated loads");
    }
    // An operand that is the accumulator of an earlier matrix multiply is not
    // stored as the type it is multiplied as - the relayout that takes it out
    // of the accumulator converts it. So the other operand says what type this
    // multiply is in.
    const bool lhs_fused = is_accumulator(info.lhs.load->name);
    const bool rhs_fused = is_accumulator(info.rhs.load->name);
    if (lhs_fused && rhs_fused) {
        return fail("both operands are accumulators of earlier matrix multiplies");
    }
    const Type operand_type =
        (lhs_fused ? info.rhs : info.lhs).load->type.element_of();
    if (!lhs_fused && !rhs_fused &&
        info.rhs.load->type.element_of() != operand_type) {
        return fail("the matrix multiply operands do not have the same type");
    }
    if (!wmma_types_supported(operand_type, info.accumulator_type)) {
        return fail("there is no tensor core instruction that multiplies these "
                    "operands into an accumulator of this type");
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
    for (const Shape &candidate : supported_shapes) {
        if (candidate.M * candidate.N != MN || candidate.K != K) {
            continue;
        }
        vector<int> canonical_shape{candidate.K, candidate.N, candidate.M};
        if (!info.lhs.mr.strides_for_shape(canonical_shape, &info.lhs.strides) ||
            !info.rhs.mr.strides_for_shape(canonical_shape, &info.rhs.strides)) {
            continue;
        }
        if (is_lhs(info.lhs, &info.lhs_layout, &info.lda) &&
            is_rhs(info.rhs, &info.rhs_layout, &info.ldb)) {
            shape = &candidate;
            break;
        }
        if (is_lhs(info.rhs, &info.lhs_layout, &info.lda) &&
            is_rhs(info.lhs, &info.rhs_layout, &info.ldb)) {
            std::swap(info.lhs, info.rhs);
            shape = &candidate;
            break;
        }
    }

    if (!shape) {
        return fail("the operands' access patterns do not describe a matrix "
                    "multiply of a tile shape the tensor cores support (16x16x16, "
                    "32x8x16, or 8x32x16)");
    }
    info.shape = *shape;
    return info;
}

// Whether a store to a fragment fills it, as opposed to a matrix multiply
// accumulating into it.
bool is_fill(const Store *op) {
    if (is_const_zero(op->value)) {
        return true;
    }
    if (const Broadcast *b = op->value.as<Broadcast>(); b && b->value.type().is_scalar()) {
        // The same value at every entry, which doesn't depend on the layout.
        return true;
    }
    MultiRamp mr;
    const Load *load = is_load_of_multiramp(op->value, Scope<Expr>::empty_scope(), &mr);
    return load && load->name != op->name;
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
    Expr frag = Load::make(element_type.with_lanes(accumulator_elements), new_name,
                           Ramp::make(0, 1, accumulator_elements));
    const int lanes = shape.M * shape.N;
    Expr lane = make_lane(unique_name("wmma_lane") + gpu_thread_name(0));
    Expr matrix = Call::make(element_type.with_lanes(lanes), Call::wmma_fragment_to_matrix_d,
                             {shape.M, shape.N, shape.K, std::move(frag), lane},
                             Call::Intrinsic);
    Expr owned = Call::make(UInt(1, lanes), Call::wmma_lane_owns,
                            {shape.M, shape.N, shape.K, lane}, Call::Intrinsic);
    return in_lane_loop(lane,
                        Store::make(op->name, std::move(matrix), std::move(index),
                                    op->param, std::move(owned), ModulusRemainder(),
                                    op->is_streaming));
}

// Everything we learn about one tile allocation from the way it is
// used. The role and the shape come from the matrix multiplies it takes part
// in, so neither is known until those have been found.
struct Fragment {
    string name;
    // The prefix of the names of the per-fragment allocations this one becomes.
    string fragment_name;
    Type element_type;
    Role role = Role::Unknown;
    Shape shape{};
    bool found_shape = false;
    // The shape a fill from memory says this could be, for a fragment that
    // nothing else says the shape of. It isn't taken up until something reads
    // the fragment in a way that needs it, because a fill says nothing about
    // which role the fragment plays and an operand's role comes from the
    // multiply that reads it.
    Shape fill_shape{};
    bool found_fill_shape = false;
    // A fragment may hold a value that is uniform along one axis of the
    // matrix, such as the row maximum a softmax subtracts. It is held as a
    // whole tile with the value repeated along that axis, which is what makes
    // reading it elementwise with a tile free, but it is written and read as a
    // vector indexed by the other axis: zero if the row indexes it and one if
    // the column does. A whole tile is -1.
    int axis = -1;
    // An allocation may hold several fragments as disjoint sub-tiles, each of
    // which becomes its own allocation.
    vector<MultiRamp> subtiles;

    Type value_type() const {
        return element_type.with_lanes(
            elements_per_lane(role, shape, element_type));
    }

    // How many entries the value stored here has, before it is spread over the
    // lanes of the warp. A fragment uniform along an axis is written and read
    // as just the axis that indexes it.
    int logical_lanes() const {
        int rows, cols;
        fragment_matrix_shape(role, shape, &rows, &cols);
        return axis < 0 ? rows * cols : (axis == 0 ? rows : cols);
    }
};

class ExtractWMMAOperations : public IRMutator {
    using IRMutator::visit;

    // Everything we've learned about each fragment allocation, and the names of
    // the ones we're currently inside of. The records outlive the first pass so
    // that the second one can use them.
    std::map<string, Fragment> fragments;
    vector<string> in_scope;

    // In the first pass we recognize the matrix multiplies, which is what tells
    // us what role each fragment plays and what shape it is. In the second we
    // rewrite everything, which needs to know both.
    int pass = 0;

    // The loops over GPU blocks, threads, and lanes we're inside of, and the
    // subset of them that are over threads, which the lanes of a warp differ
    // in and the blocks around it do not.
    vector<string> gpu_loop_vars;
    vector<string> gpu_thread_vars;
    // The subset of those that are over the lanes of a warp rather than over
    // warps. The lanes of a warp share a fragment, where warps each have their
    // own, so the two are not interchangeable here.
    vector<string> gpu_lane_vars;

    // The values of the LetStmts we are inside. Lowering hoists a stored value
    // out of the loops it does not vary in, which for a fragment holding
    // something uniform along an axis is most of them, and what is left at the
    // store is a bare variable that says nothing about which fragments went
    // into it.
    vector<std::pair<string, Expr>> let_values;

    // The range of every enclosing loop, so that a loop that runs once can be
    // recognized as one.
    Scope<Interval> loop_bounds;

    // Lets whose value to_fragment has restricted to this lane's share of the
    // matrix, so that uses of them must be restricted too.
    Scope<> matrix_lets;

    // The conditions of the enclosing if statements. A reduction whose bound
    // depends on a pure loop variable lowers to a guarded body, and bounds
    // inference refines the regions inside the guard with its condition, so
    // deciding whether an offset into a fragment is zero can take knowing
    // that the guard holds.
    vector<Expr> facts;

    // Whether we are rewriting the value of a store to a fragment. A read of a
    // fragment that holds a value spread along an axis of the tile only means
    // something there, where to_fragment can match it against the axis the
    // value being computed is spread along.
    bool in_fragment_value = false;

    Fragment *find_fragment(const string &name) {
        for (const string &n : in_scope) {
            if (n == name) {
                return &fragments[name];
            }
        }
        return nullptr;
    }

    // The fragments a value reads, in the order they appear.
    vector<const Fragment *> fragments_read(const Expr &e) {
        class Reads : public IRVisitor {
            using IRVisitor::visit;
            ExtractWMMAOperations *self;
            void visit(const Load *op) override {
                IRVisitor::visit(op);
                if (const Fragment *f = self->find_fragment(op->name)) {
                    found.push_back(f);
                }
            }

        public:
            vector<const Fragment *> found;
            Reads(ExtractWMMAOperations *self)
                : self(self) {
            }
        } reads(this);
        e.accept(&reads);
        return reads.found;
    }

    // A fragment allocation may sit outside the loops over GPU threads, in
    // which case each thread gets its own copy of it and only ever touches its
    // own slice. Any dependence of the index on the thread is selecting between
    // those copies, not between subtiles within one, so drop it. Blocks are not
    // like that: an allocation is inside the kernel, so a block can only ever
    // touch its own, and any dependence on the block picks a subtile for real.
    // The lanes of a warp are not like that either - they share one fragment
    // rather than having one each - so a dependence on the lane is rejected
    // where it would mean something.
    Expr index_within_thread(const Expr &index) {
        Expr idx = index;
        for (const string &v : gpu_thread_vars) {
            idx = substitute(v, 0, idx);
        }
        return simplify(idx);
    }

    // An expression with the LetStmts around it substituted back in, so that
    // the fragments it reads can be found.
    Expr without_lets(const Expr &e) {
        Expr result = e;
        // Innermost first, so that a let referring to an earlier one is
        // resolved before that one is substituted.
        for (const auto &[name, value] : reverse_view(let_values)) {
            if (expr_uses_var(result, name)) {
                result = substitute(name, value, result);
            }
        }
        return result;
    }

    // The bounds a fact implies for the variables it compares, layered over
    // the given scope. Only the comparison shapes the guards produce are
    // handled; anything else implies nothing.
    void refine_bounds_with_fact(const Expr &f, Scope<Interval> &scope) {
        if (const And *op = f.as<And>()) {
            refine_bounds_with_fact(op->a, scope);
            refine_bounds_with_fact(op->b, scope);
            return;
        }
        Expr a, b;  // the fact as a <= b
        if (const LE *le = f.as<LE>()) {
            a = le->a;
            b = le->b;
        } else if (const LT *lt = f.as<LT>()) {
            if (!lt->a.type().is_int()) {
                return;
            }
            a = lt->a;
            b = lt->b - 1;
        } else if (const EQ *eq = f.as<EQ>()) {
            for (const Expr &side : {eq->a, eq->b}) {
                if (const Variable *v = side.as<Variable>()) {
                    scope.push(v->name,
                               Interval::single_point(side.same_as(eq->a) ? eq->b : eq->a));
                }
            }
            return;
        } else {
            return;
        }
        if (const Variable *v = a.as<Variable>()) {
            Interval i = scope.contains(v->name) ? scope.get(v->name) : Interval::everything();
            i.max = simplify(i.has_upper_bound() ? min(i.max, b) : b);
            scope.push(v->name, i);
        }
        if (const Variable *v = b.as<Variable>()) {
            Interval i = scope.contains(v->name) ? scope.get(v->name) : Interval::everything();
            i.min = simplify(i.has_lower_bound() ? max(i.min, a) : a);
            scope.push(v->name, i);
        }
    }

    // Resolve the selects, mins, and maxes in an index that the conditions
    // of the enclosing ifs settle. Bounds refinement under a guarded access
    // leaves offsets like max(i, r) - i that are only provably zero given
    // that the guard (r <= i, say) holds.
    Expr apply_facts(const Expr &e, bool threaded) {
        if (facts.empty()) {
            return e;
        }
        Expr all = const_true();
        for (const Expr &f : facts) {
            all = all && without_lets(f);
        }
        if (threaded) {
            all = index_within_thread(all);
        }
        all = simplify(all, loop_bounds);
        if (can_prove(!all, loop_bounds)) {
            // Contradictory facts: either dead code, or the collapse to one
            // representative thread lost the threads the guard holds for.
            // Either way they say nothing consistent about the index, so
            // leave it alone.
            return e;
        }
        Scope<Interval> refined;
        refined.set_containing_scope(&loop_bounds);
        refine_bounds_with_fact(all, refined);
        class ApplyFacts : public IRMutator {
            using IRMutator::visit;
            const Expr &fact;
            const Scope<Interval> &bounds;

            bool provably_false(const Expr &c) {
                return can_prove(!(fact && c), bounds);
            }

            Expr visit(const Select *op) override {
                if (provably_false(op->condition)) {
                    return mutate(op->false_value);
                }
                if (provably_false(!op->condition)) {
                    return mutate(op->true_value);
                }
                return IRMutator::visit(op);
            }

            Expr visit(const Max *op) override {
                Expr a = mutate(op->a), b = mutate(op->b);
                if (provably_false(a < b)) {
                    return a;
                }
                if (provably_false(b < a)) {
                    return b;
                }
                return Max::make(std::move(a), std::move(b));
            }

            Expr visit(const Min *op) override {
                Expr a = mutate(op->a), b = mutate(op->b);
                if (provably_false(a < b)) {
                    return b;
                }
                if (provably_false(b < a)) {
                    return a;
                }
                return Min::make(std::move(a), std::move(b));
            }

        public:
            ApplyFacts(const Expr &fact, const Scope<Interval> &bounds)
                : fact(fact), bounds(bounds) {
            }
        } apply(all, refined);
        Expr result = apply(e);
        // simplify only uses variable bounds to settle comparisons, so a
        // variable the facts pin to a point has to be substituted by hand.
        for (auto it = refined.cbegin(); it != refined.cend(); ++it) {
            const Interval &i = it.value();
            if (i.is_single_point() ||
                (i.is_bounded() && can_prove(i.min == i.max))) {
                result = substitute(it.name(), i.min, result);
            }
        }
        return simplify(result, refined);
    }

    // An index with the lets substituted back in and simplified knowing what
    // the enclosing loops bound its variables to, which is what settles
    // questions like whether an offset into a fragment is really zero. Likely
    // markers say which side of a branch to generate good code for rather than
    // anything about the value, and loop partitioning has not consumed them
    // yet, so they only get in the way here.
    Expr index_for_analysis(const Expr &index) {
        Expr idx = substitute_in_all_lets(index);
        idx = remove_likelies(without_lets(idx));
        // The facts go first in symbolic form, while the index still names
        // the threads they talk about; then the dependence on the thread
        // collapses to picking this thread's copy, and the facts go again in
        // the same collapsed form, whose aligned atoms prove more.
        idx = apply_facts(idx, false);
        idx = index_within_thread(idx);
        idx = apply_facts(idx, true);
        return simplify(idx, loop_bounds);
    }

    Stmt visit(const LetStmt *op) override {
        let_values.emplace_back(op->name, op->value);
        Stmt s = IRMutator::visit(op);
        let_values.pop_back();
        return s;
    }

    Stmt visit(const IfThenElse *op) override {
        facts.push_back(op->condition);
        Stmt then_case = mutate(op->then_case);
        facts.pop_back();
        Stmt else_case = op->else_case;
        if (else_case.defined()) {
            facts.push_back(simplify(!op->condition));
            else_case = mutate(op->else_case);
            facts.pop_back();
        }
        Expr condition = mutate(op->condition);
        if (condition.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            return op;
        }
        return IfThenElse::make(std::move(condition), std::move(then_case),
                                std::move(else_case));
    }

    string subtile_name(Fragment *f, const Expr &index) {
        // Which tile an access lands in has to be settled here, so the index
        // needs the surrounding lets substituted back in for the same reason
        // the stored value does.
        int idx = get_subtile(index_for_analysis(index),
                              "tensor core fragment " + f->name, &f->subtiles);
        internal_assert(idx >= 0);  // errors handled already
        return f->fragment_name + std::to_string(idx);
    }

    void set_role(Fragment *f, Role role) {
        user_assert(f->role == Role::Unknown || f->role == role)
            << "The tensor core fragment " << f->name << " is used as both "
            << role_name(f->role) << " and " << role_name(role) << " of a matrix "
            << "multiply. Those are held in registers in different layouts, so a "
            << "fragment can only play one of those roles. Stage it through memory "
            << "in between.\n";
        f->role = role;
    }

    void set_shape(Fragment *f, const Shape &shape) {
        user_assert(!f->found_shape ||
                    (shape.M == f->shape.M && shape.N == f->shape.N &&
                     shape.K == f->shape.K))
            << "Found inconsistent tile shapes for the tensor core fragment "
            << f->name << " across the matrix multiplies that use it: "
            << f->shape.M << "x" << f->shape.N << "x" << f->shape.K << " vs "
            << shape.M << "x" << shape.N << "x" << shape.K << ".";
        f->shape = shape;
        f->found_shape = true;
    }

    // Which subtile of a fragment an access refers to, as an index over just
    // the tile. An operand is read by the multiply at the shape of the whole
    // reduction, with one axis broadcast, so its index has to be projected back
    // down to the tile before it can be compared against the fill that wrote
    // it.
    string operand_subtile_name(Fragment *f, const Expr &base, Role role,
                                const Shape &shape, Layout layout, const Expr &stride) {
        int rows, cols;
        fragment_matrix_shape(role, shape, &rows, &cols);
        return subtile_name(f, make_matrix_index(base, rows, cols, layout, stride));
    }

    // Note what a matrix multiply tells us about an operand staged in a
    // fragment. An operand read straight out of memory tells us nothing,
    // because its load gets synthesized at the multiply.
    // Whether an operand is the accumulator of an earlier matrix multiply,
    // used without ever being written out. It keeps the layout it was
    // accumulated in, so it is turned into an operand where it sits rather
    // than being read as one.
    bool is_fused_operand(const Fragment *f, Role role) const {
        return f->role == Role::Accumulator && role == Role::A;
    }

    void record_operand(const Operand &operand, Role role, const Shape &shape,
                        Layout layout, const Expr &stride) {
        if (Fragment *f = find_fragment(operand.load->name)) {
            if (is_fused_operand(f, role)) {
                set_shape(f, shape);
                return;
            }
            set_role(f, role);
            set_shape(f, shape);
            operand_subtile_name(f, operand.mr.base, role, shape, layout, stride);
        }
    }

    // The value a matrix multiply uses for one of its operands: the fragment it
    // was staged in, or a load synthesized here if it wasn't staged.
    Expr operand_value(const Operand &operand, Role role, const Shape &shape,
                       Layout layout, const Expr &stride, const Expr &lane) {
        if (Fragment *f = find_fragment(operand.load->name)) {
            if (is_fused_operand(f, role)) {
                return make_fused_operand(f, operand, shape, layout, stride, lane);
            }
            const int lanes = f->value_type().lanes();
            const string name =
                operand_subtile_name(f, operand.mr.base, role, shape, layout, stride);
            return Load::make(f->value_type(), name, Ramp::make(0, 1, lanes));
        }
        return make_matrix_to_fragment(role, shape, layout, operand.load,
                                       operand.mr.base, stride, lane);
    }

    // An a operand taken out of the accumulator it was left in. The two hold
    // the matrix in different layouts, so this is a relayout, but both of them
    // are register layouts and the backend does it in place.
    Expr make_fused_operand(Fragment *f, const Operand &operand, const Shape &shape,
                            Layout layout, const Expr &stride, const Expr &lane) {
        user_assert(shape.K == f->shape.N)
            << "The result of a tensor core matrix multiply is fed straight into "
            << "another as its a operand, but it is " << f->shape.M << "x"
            << f->shape.N << " and the second one wants an a operand with "
            << shape.K << " columns.\n";

        // An operand covers M x N x K, not the tile, because it is replicated
        // along the axis it doesn't depend on. So ask for the tile by the
        // address it starts at, the way an operand in memory is asked for.
        const string name =
            operand_subtile_name(f, operand.mr.base, Role::A, shape, layout, stride);
        Expr acc = Load::make(f->value_type(), name,
                              Ramp::make(0, 1, f->value_type().lanes()));
        Expr matrix = Call::make(f->element_type.with_lanes(shape.M * shape.K),
                                 Call::wmma_fragment_to_matrix_d,
                                 {shape.M, shape.K, shape.K, std::move(acc), lane},
                                 Call::Intrinsic);
        Type operand_type =
            Float(16).with_lanes(elements_per_lane(Role::A, shape, Float(16)));
        return Call::make(operand_type, Call::wmma_matrix_to_fragment_a,
                          {shape.M, shape.N, shape.K, std::move(matrix), lane},
                          Call::Intrinsic);
    }

    // Restrict a value covering the whole matrix to the part of it this lane
    // holds, by pushing the restriction inward until it meets something that
    // is already a fragment. Elementwise ops don't care how the matrix is
    // spread over the warp, so they let it through; anything that does care
    // has to be recognized here or reported.
    // Where in the tile each entry lands, if that is what this is: a value
    // that steps by one along one axis of the tile and not at all along the
    // other is an index along that axis, plus wherever the tile starts. Each
    // lane can compute it for the entries it holds.
    Expr tile_coordinate(const Expr &e, const Fragment *dest) {
        const Shape &shape = dest->shape;
        const int lanes = shape.M * shape.N;
        MultiRamp mr;
        std::vector<Expr> strides;
        if (!fragment_lane.defined() || !e.type().is_int() ||
            e.type().lanes() != lanes ||
            !is_multiramp(e, Scope<Expr>::empty_scope(), &mr) ||
            !mr.strides_for_shape({shape.N, shape.M}, &strides)) {
            return Expr{};
        }
        // The columns are the inner dimension of the tile, and the rows the
        // outer one. Axis zero indexes the rows.
        int axis = -1;
        if (is_const_zero(strides[0])) {
            axis = 0;
        } else if (is_const_zero(strides[1])) {
            axis = 1;
        } else {
            return Expr{};
        }
        const Expr stride = strides[axis ? 0 : 1];
        if (is_const_zero(stride)) {
            // Flat along both axes, so it is the same everywhere and whatever
            // reads it doesn't need to know where in the tile it landed.
            return Expr{};
        }
        // One index per entry this lane holds, not one per entry of the tile.
        const int held = dest->value_type().lanes();
        Expr index = Call::make(Int(32, held), Call::wmma_entry_index,
                                {shape.M, shape.N, axis, fragment_lane},
                                Call::Intrinsic);
        return simplify(Broadcast::make(mr.base, held) +
                        Broadcast::make(stride, held) * index);
    }

    // A vector spread along an axis of the tile that lives in memory every lane
    // can reach. Loading it as a matrix whose leading dimension is zero makes
    // every row (or every column) the vector itself, so the hardware spreads it
    // for free, rather than every lane taking a copy of the whole vector and
    // selecting out of it.
    Expr broadcast_matrix_along_axis(const Expr &vec, int axis, const Fragment *dest) {
        const Load *load = vec.as<Load>();
        if (!fragment_lane.defined() || !load || find_fragment(load->name) ||
            !is_const_one(load->predicate) || !is_shared_memory(load->name) ||
            load->type.element_of() != Float(32)) {
            return Expr{};
        }
        const Ramp *ramp = load->index.as<Ramp>();
        if (!ramp || !is_const_one(ramp->stride) || ramp->lanes != vec.type().lanes()) {
            return Expr{};
        }
        // The load reads eight bytes at a time whatever the leading dimension,
        // so the vector has to start on an eight byte boundary. What the load
        // says about itself was worked out with the enclosing lets in scope, so
        // it can know things the index alone doesn't say, and the other way
        // round once the index has been folded. Either proving it is enough.
        const int align = 8 / load->type.bytes();
        auto aligned = [&](const ModulusRemainder &m) {
            return m.modulus % align == 0 && m.remainder % align == 0;
        };
        if (!aligned(load->alignment) && !aligned(modulus_remainder(ramp->base))) {
            return Expr{};
        }
        // Which way round the matrix is laid out is what decides which axis the
        // vector ends up spread along: row major reaches it by column, and
        // column major by row.
        return make_matrix_to_fragment(Role::Accumulator, dest->shape,
                                       axis ? Layout::Row : Layout::Col, load,
                                       ramp->base, make_zero(Int(32)), fragment_lane);
    }

    Expr to_fragment(const Expr &e, const Fragment *dest) {
        const int lanes = dest->value_type().lanes();
        const Type t = e.type().element_of().with_lanes(lanes);
        auto pair = [&](const Expr &a, const Expr &b) {
            return std::make_pair(to_fragment(a, dest), to_fragment(b, dest));
        };
        if (const Add *op = e.as<Add>()) {
            auto [a, b] = pair(op->a, op->b);
            return Add::make(a, b);
        } else if (const Sub *op = e.as<Sub>()) {
            auto [a, b] = pair(op->a, op->b);
            return Sub::make(a, b);
        } else if (const Mul *op = e.as<Mul>()) {
            auto [a, b] = pair(op->a, op->b);
            return Mul::make(a, b);
        } else if (const Div *op = e.as<Div>()) {
            auto [a, b] = pair(op->a, op->b);
            return Div::make(a, b);
        } else if (const EQ *op = e.as<EQ>()) {
            auto [a, b] = pair(op->a, op->b);
            return EQ::make(a, b);
        } else if (const NE *op = e.as<NE>()) {
            auto [a, b] = pair(op->a, op->b);
            return NE::make(a, b);
        } else if (const LT *op = e.as<LT>()) {
            auto [a, b] = pair(op->a, op->b);
            return LT::make(a, b);
        } else if (const LE *op = e.as<LE>()) {
            auto [a, b] = pair(op->a, op->b);
            return LE::make(a, b);
        } else if (const And *op = e.as<And>()) {
            auto [a, b] = pair(op->a, op->b);
            return And::make(a, b);
        } else if (const Or *op = e.as<Or>()) {
            auto [a, b] = pair(op->a, op->b);
            return Or::make(a, b);
        } else if (const Not *op = e.as<Not>()) {
            return Not::make(to_fragment(op->a, dest));
        } else if (const Min *op = e.as<Min>()) {
            auto [a, b] = pair(op->a, op->b);
            return Min::make(a, b);
        } else if (const Max *op = e.as<Max>()) {
            auto [a, b] = pair(op->a, op->b);
            return Max::make(a, b);
        } else if (const Cast *op = e.as<Cast>()) {
            return Cast::make(t, to_fragment(op->value, dest));
        } else if (const Reinterpret *op = e.as<Reinterpret>()) {
            // Reinterpreting is per lane, so long as it keeps the lane count.
            if (op->value.type().lanes() == e.type().lanes()) {
                return Reinterpret::make(t, to_fragment(op->value, dest));
            }
        } else if (const Select *op = e.as<Select>()) {
            auto [a, b] = pair(op->true_value, op->false_value);
            return Select::make(to_fragment(op->condition, dest), a, b);
        } else if (const Call *op = e.as<Call>(); op && is_lanewise(op)) {
            // A call that computes each lane of its result from the same lane
            // of its arguments doesn't care either. Arguments that don't cover
            // the matrix aren't a share of it to take.
            vector<Expr> args;
            args.reserve(op->args.size());
            for (const Expr &arg : op->args) {
                args.push_back(arg.type().lanes() == e.type().lanes() ?
                                   to_fragment(arg, dest) :
                                   arg);
            }
            return Call::make(t, op->name, args, op->call_type, op->func,
                              op->value_index, op->image, op->param);
        } else if (const Let *op = e.as<Let>()) {
            // Something the body uses more than once, which the expansion of a
            // transcendental is full of. If it covers the matrix, take this
            // lane's share of it here rather than at every use of it.
            if (op->value.type().lanes() != e.type().lanes()) {
                return Let::make(op->name, op->value, to_fragment(op->body, dest));
            }
            Expr value = to_fragment(op->value, dest);
            ScopedBinding<> bind(matrix_lets, op->name);
            return Let::make(op->name, std::move(value), to_fragment(op->body, dest));
        } else if (const Variable *op = e.as<Variable>();
                   op && matrix_lets.contains(op->name)) {
            // A let whose value was restricted above.
            return Variable::make(t, op->name);
        } else if (const Broadcast *op = e.as<Broadcast>()) {
            // The same value at every entry, so which entries this lane holds
            // doesn't matter.
            if (op->value.type().is_scalar()) {
                return Broadcast::make(op->value, lanes);
            }
        } else if (const Shuffle *op = e.as<Shuffle>()) {
            // A read of a whole tile, which the load visitor turned into a
            // gather. Taking this lane's share of it gives back the fragment
            // it was gathered from.
            const Call *c = op->vectors.size() == 1 ? op->vectors[0].as<Call>() : nullptr;
            if (c && c->is_intrinsic(Call::wmma_fragment_to_matrix_d) &&
                is_identity_shuffle(op) && same_shape(c, dest)) {
                return c->args[3];
            }
        } else if (Fragment *src = uniform_fragment_read(e, dest->axis)) {
            // A read of a fragment that is itself uniform along the same axis,
            // so it is already held the way the one being written is.
            return load_fragment(src, e.as<Load>()->index);
        } else if (const VectorReduce *op = e.as<VectorReduce>()) {
            return reduce_along_axis(op, dest);
        }
        if (Expr coord = tile_coordinate(e, dest); coord.defined()) {
            return coord;
        }
        // A vector spread over the tile along one axis, such as a row
        // statistic a softmax subtracts.
        int axis = 0;
        Expr vec = broadcast_along_axis(e, dest->shape, &axis);
        if (vec.defined()) {
            if (Fragment *src = uniform_fragment_read(vec, axis)) {
                // A fragment that already holds the vector spread that way, so
                // the broadcast has happened already.
                return load_fragment(src, vec.as<Load>()->index);
            }
            // A vector computed elementwise from other vectors, which the
            // simplifier lifted the broadcast out of. Broadcasting each of
            // them instead puts the cases above back within reach of the
            // fragments underneath.
            if (Expr sunk = sink_broadcast(e, vec); sunk.defined()) {
                return to_fragment(sunk, dest);
            }
            if (Expr spread = broadcast_matrix_along_axis(vec, axis, dest);
                spread.defined()) {
                return spread;
            }
            // Every lane holds the whole vector, so this becomes a per-lane
            // selection out of it.
            return Call::make(t, Call::wmma_vector_to_fragment,
                              {dest->shape.M, dest->shape.N, axis, std::move(vec)},
                              Call::Intrinsic);
        }
        user_error << "This value is computed into a tensor core fragment, but how "
                   << "it is spread over the lanes of a warp doesn't follow from how "
                   << "the fragments it is built from are:\n"
                   << e << "\n";
        return Expr{};
    }

    // A read of a fragment that holds a value uniform along one axis of the
    // matrix, indexed by the axis given.
    Fragment *uniform_fragment_read(const Expr &e, int axis) {
        const Load *op = e.as<Load>();
        Fragment *f = op ? find_fragment(op->name) : nullptr;
        return (f && f->axis >= 0 && f->axis == axis) ? f : nullptr;
    }

    Expr load_fragment(Fragment *f, const Expr &index) {
        return Load::make(f->value_type(), subtile_name(f, index),
                          Ramp::make(0, 1, f->value_type().lanes()));
    }

    static Expr combine(VectorReduce::Operator op, const Expr &a, const Expr &b) {
        switch (op) {
        case VectorReduce::Add:
            return Add::make(a, b);
        case VectorReduce::Mul:
            return Mul::make(a, b);
        case VectorReduce::Min:
            return Min::make(a, b);
        case VectorReduce::Max:
            return Max::make(a, b);
        case VectorReduce::And:
            return And::make(a, b);
        case VectorReduce::Or:
            return Or::make(a, b);
        default:
            return Expr{};
        }
    }

    // Several tiles read side by side along one axis, which the load visitor
    // turned into a gather out of one inflated fragment per tile. Gives back
    // the fragments, in the order the tiles run along the axis.
    static bool is_wide_tile_shuffle(const Shuffle *op, const Shape &shape, int tiles) {
        const int width = tiles * shape.N;
        if (!op || (int)op->vectors.size() != tiles ||
            (int)op->indices.size() != shape.M * width) {
            return false;
        }
        // Entry (row, col) of the wide matrix is entry (row, col mod N) of the
        // tile that column falls in.
        for (int i = 0; i < shape.M * width; i++) {
            const int row = i / width, col = i % width;
            const int want = (col / shape.N) * shape.M * shape.N + row * shape.N +
                             col % shape.N;
            if (op->indices[i] != want) {
                return false;
            }
        }
        return true;
    }

    // One tile's worth of a value covering several tiles of the matrix side by
    // side. Taking a slice is per entry, so it passes through elementwise ops
    // the same way taking a lane's share does; where it meets the shuffle that
    // laid the tiles out it picks one of them, and where it meets a vector
    // spread along an axis it narrows the spread. Undefined if the value is not
    // built out of things a slice can be pushed into.
    Expr slice_tile(const Expr &e, const Shape &shape, int tiles, int i) {
        const int narrow = shape.M * shape.N;
        if (e.type().lanes() != narrow * tiles) {
            // Not a share of the matrix, so it is the same at every entry.
            return e;
        }
        const Type t = e.type().element_of().with_lanes(narrow);
        auto in = [&](const Expr &a) { return slice_tile(a, shape, tiles, i); };
        auto pair = [&](const Expr &a, const Expr &b) {
            Expr sa = in(a), sb = in(b);
            return std::make_pair(sa, sb);
        };
        auto defined = [](const std::pair<Expr, Expr> &p) {
            return p.first.defined() && p.second.defined();
        };
        if (const Shuffle *op = e.as<Shuffle>()) {
            if (is_wide_tile_shuffle(op, shape, tiles)) {
                // Read one of the tiles as a whole tile, which is the form
                // to_fragment turns back into the fragment it came from.
                vector<int> identity(narrow);
                for (int j = 0; j < narrow; j++) {
                    identity[j] = j;
                }
                return Shuffle::make({op->vectors[i]}, identity);
            }
            // A vector indexed by the row, spread along it. Every tile sees
            // the whole of it, so only the width of the spread changes.
            const Broadcast *b = op->is_transpose() && op->transpose_factor() == shape.M ?
                                     op->vectors[0].as<Broadcast>() :
                                     nullptr;
            if (b && b->lanes == shape.N * tiles &&
                b->value.type().lanes() == shape.M) {
                return Shuffle::make_transpose(Broadcast::make(b->value, shape.N),
                                               shape.M);
            }
            return Expr{};
        } else if (const Broadcast *op = e.as<Broadcast>()) {
            if (op->value.type().is_scalar()) {
                return Broadcast::make(op->value, narrow);
            }
            // A vector indexed by the column, spread down the rows. Each tile
            // sees its own part of it.
            if (op->lanes == shape.M && op->value.type().lanes() == shape.N * tiles) {
                return Broadcast::make(
                    Shuffle::make_slice(op->value, i * shape.N, 1, shape.N), shape.M);
            }
            return Expr{};
        } else if (const Add *op = e.as<Add>()) {
            auto p = pair(op->a, op->b);
            return defined(p) ? Add::make(p.first, p.second) : Expr{};
        } else if (const Sub *op = e.as<Sub>()) {
            auto p = pair(op->a, op->b);
            return defined(p) ? Sub::make(p.first, p.second) : Expr{};
        } else if (const Mul *op = e.as<Mul>()) {
            auto p = pair(op->a, op->b);
            return defined(p) ? Mul::make(p.first, p.second) : Expr{};
        } else if (const Div *op = e.as<Div>()) {
            auto p = pair(op->a, op->b);
            return defined(p) ? Div::make(p.first, p.second) : Expr{};
        } else if (const Min *op = e.as<Min>()) {
            auto p = pair(op->a, op->b);
            return defined(p) ? Min::make(p.first, p.second) : Expr{};
        } else if (const Max *op = e.as<Max>()) {
            auto p = pair(op->a, op->b);
            return defined(p) ? Max::make(p.first, p.second) : Expr{};
        } else if (const Cast *op = e.as<Cast>()) {
            Expr v = in(op->value);
            return v.defined() ? Cast::make(t, v) : Expr{};
        } else if (const Reinterpret *op = e.as<Reinterpret>();
                   op && op->value.type().lanes() == e.type().lanes()) {
            Expr v = in(op->value);
            return v.defined() ? Reinterpret::make(t, v) : Expr{};
        } else if (const Select *op = e.as<Select>()) {
            Expr c = in(op->condition);
            auto p = pair(op->true_value, op->false_value);
            return c.defined() && defined(p) ? Select::make(c, p.first, p.second) :
                                               Expr{};
        } else if (const Call *op = e.as<Call>(); op && is_lanewise(op)) {
            vector<Expr> args;
            args.reserve(op->args.size());
            for (const Expr &arg : op->args) {
                Expr a = in(arg);
                if (!a.defined()) {
                    return Expr{};
                }
                args.push_back(std::move(a));
            }
            return Call::make(t, op->name, args, op->call_type, op->func,
                              op->value_index, op->image, op->param);
        } else if (const Let *op = e.as<Let>();
                   op && op->value.type().lanes() != e.type().lanes()) {
            // A let of something that isn't a share of the matrix means the
            // same thing in every tile. One that is would have to be named
            // once per tile, which is not worth doing here.
            Expr body = in(op->body);
            return body.defined() ? Let::make(op->name, op->value, body) : Expr{};
        }
        return Expr{};
    }

    // A reduction along one axis of a tile. The entries being reduced together
    // are spread over the lanes of the warp, so this is a butterfly: exchange
    // entries with the ones a power of two away along the axis and combine,
    // once per bit of it. That leaves every entry holding the whole row's or
    // column's worth, which is how a fragment uniform along an axis is held,
    // so the result needs nothing further doing to it.
    Expr reduce_along_axis(const VectorReduce *op, const Fragment *dest) {
        const Shape &shape = dest->shape;
        // A matrix value has its entries in row-major order, so a reduction
        // over adjacent entries reduces a row, leaving a value indexed by the
        // row.
        const int width = shape.N;
        const int factor = op->value.type().lanes() / op->type.lanes();
        const int tiles = factor / width;
        Expr combined = combine(op->op, make_zero(Float(32)), make_zero(Float(32)));
        user_assert(dest->axis == 0 && factor == tiles * width && tiles >= 1 &&
                    op->type.lanes() == shape.M && combined.defined())
            << "Reduction into a tensor core fragment not supported. Only a "
            << "reduction of whole " << shape.M << "x" << shape.N << " tiles down "
            << "to one value per row is, and this one takes " << op->value.type().lanes()
            << " entries to " << op->type.lanes() << ".\n"
            << Expr(op);

        // A reduction along more than a tile's worth of an axis reads several
        // tiles side by side. They hold the matrix the same way as each other,
        // so combining them takes no cross-lane traffic and leaves one tile's
        // worth to reduce. Doing that first is what keeps the butterfly below
        // to one, rather than one per tile.
        vector<Expr> pieces;
        for (int i = 0; tiles > 1 && i < tiles; i++) {
            Expr piece = slice_tile(op->value, shape, tiles, i);
            user_assert(piece.defined())
                << "Reduction into a tensor core fragment not supported. It runs "
                << "across " << tiles << " tiles, but it is not built out of whole "
                << "tiles side by side along the axis being reduced.\n"
                << Expr(op);
            pieces.push_back(to_fragment(piece, dest));
        }
        Expr v = tiles == 1 ? to_fragment(op->value, dest) : pieces[0];
        for (int i = 1; i < (int)pieces.size(); i++) {
            v = combine(op->op, v, pieces[i]);
        }
        vector<std::pair<string, Expr>> lets;
        for (int bit = 0; (1 << bit) < width; bit++) {
            // Each round reads the one before it twice, so name it rather than
            // leaving it to be recomputed.
            lets.emplace_back(unique_name("wmma_reduce"), v);
            Expr var = Variable::make(v.type(), lets.back().first);
            Expr swapped = Call::make(var.type(), Call::wmma_axis_xor,
                                      {shape.M, shape.N, 1 - dest->axis, bit, var},
                                      Call::Intrinsic);
            v = combine(op->op, var, std::move(swapped));
        }
        for (const auto &[name, value] : reverse_view(lets)) {
            v = Let::make(name, value, v);
        }
        return v;
    }

    // Recognize a value covering the whole matrix that is really a vector
    // spread along one of its axes: entry (r, c) is v[r] if the axis is zero,
    // or v[c] if it is one. A vector repeated once per row of the matrix is
    // the one indexed by the column, and transposing that gives the one
    // indexed by the row.
    static Expr broadcast_along_axis(const Expr &e, const Shape &shape, int *axis) {
        if (e.type().lanes() != shape.M * shape.N) {
            return Expr{};
        }
        Expr value = e;
        *axis = 1;
        if (const Shuffle *op = value.as<Shuffle>()) {
            // The transpose factor is the width of the matrix underneath it,
            // which is the one whose columns index the vector.
            if (!op->is_transpose() || op->transpose_factor() != shape.M) {
                return Expr{};
            }
            value = op->vectors[0];
            *axis = 0;
        }
        const int copies = *axis ? shape.M : shape.N;
        const Broadcast *op = value.as<Broadcast>();
        if (op && op->lanes == copies) {
            return op->value;
        }
        return Expr{};
    }

    // Spread a different vector along the same axis as an expression
    // broadcast_along_axis matched. That was either a broadcast or a transpose
    // of one, and the vector given has the lane count of the one it was built
    // from, so the wrappers are rebuilt as they were: nothing moves across the
    // transpose, and its indices go on meaning what they meant.
    static Expr broadcast_like(const Expr &model, const Expr &v) {
        if (const Shuffle *op = model.as<Shuffle>()) {
            return Shuffle::make({broadcast_like(op->vectors[0], v)}, op->indices);
        }
        return Broadcast::make(v, model.as<Broadcast>()->lanes);
    }

    // Sink a broadcast along an axis past an elementwise op, so that a value
    // covering the whole matrix which was computed by broadcasting an
    // elementwise combination of vectors becomes the same combination of
    // separately broadcast vectors. Undefined if the vector broadcast is not
    // an elementwise combination, which is where the recursion stops.
    Expr sink_broadcast(const Expr &model, const Expr &vec) {
        // An operand as wide as the vector is broadcast the same way. A
        // narrower one is a scalar or a broadcast of one, which is already the
        // same at every entry.
        auto in = [&](const Expr &e) {
            return e.type().lanes() == vec.type().lanes() ? broadcast_like(model, e) : e;
        };
        auto pair = [&](const Expr &a, const Expr &b) {
            return std::make_pair(in(a), in(b));
        };
        if (const Add *op = vec.as<Add>()) {
            auto [a, b] = pair(op->a, op->b);
            return Add::make(a, b);
        } else if (const Sub *op = vec.as<Sub>()) {
            auto [a, b] = pair(op->a, op->b);
            return Sub::make(a, b);
        } else if (const Mul *op = vec.as<Mul>()) {
            auto [a, b] = pair(op->a, op->b);
            return Mul::make(a, b);
        } else if (const Div *op = vec.as<Div>()) {
            auto [a, b] = pair(op->a, op->b);
            return Div::make(a, b);
        } else if (const Min *op = vec.as<Min>()) {
            auto [a, b] = pair(op->a, op->b);
            return Min::make(a, b);
        } else if (const Max *op = vec.as<Max>()) {
            auto [a, b] = pair(op->a, op->b);
            return Max::make(a, b);
        } else if (const Cast *op = vec.as<Cast>()) {
            return Cast::make(op->type.with_lanes(model.type().lanes()), in(op->value));
        } else if (const Select *op = vec.as<Select>()) {
            auto [a, b] = pair(op->true_value, op->false_value);
            return Select::make(in(op->condition), a, b);
        } else if (const Call *op = vec.as<Call>(); op && is_lanewise(op)) {
            vector<Expr> args;
            args.reserve(op->args.size());
            for (const Expr &arg : op->args) {
                args.push_back(in(arg));
            }
            return Call::make(op->type.with_lanes(model.type().lanes()), op->name,
                              args, op->call_type, op->func, op->value_index,
                              op->image, op->param);
        }
        return Expr{};
    }

    static bool is_identity_shuffle(const Shuffle *op) {
        for (int i = 0; i < (int)op->indices.size(); i++) {
            if (op->indices[i] != i) {
                return false;
            }
        }
        return true;
    }

    static bool same_shape(const Call *fragment_to_matrix, const Fragment *dest) {
        auto arg = [&](int i) { return as_const_int(fragment_to_matrix->args[i]); };
        return arg(0) && arg(1) && *arg(0) == dest->shape.M && *arg(1) == dest->shape.N;
    }

    // An elementwise op on whole tiles, which happens where the fragments sit.
    // The lane whose share of a tile is being computed, while it is being
    // computed. A value that depends on where in the tile an entry lands needs
    // it, and nothing else does.
    Expr fragment_lane;

    Stmt convert_to_elementwise(const Store *op, Fragment *f) {
        string name;
        if (f->axis >= 0) {
            // A value spread along an axis is written as just that axis.
            name = subtile_name(f, op->index);
        } else {
            vector<int> indices;
            int subtile = containing_subtile(f, op->index, &indices);
            user_assert(subtile >= 0 && is_whole_tile(f, indices))
                << "A tensor core fragment computed elementwise must be written a "
                << "whole tile at a time.\n"
                << Stmt(op);
            name = f->fragment_name + std::to_string(subtile);
        }
        // The tile lives in the registers of a whole warp, so every lane has
        // to run the store to write the share of it that it holds.
        Expr lane = make_lane(unique_name("wmma_lane") + gpu_thread_name(0));
        Expr value;
        {
            // Reads of a fragment spread along an axis mean something here and
            // nowhere else, so the load visitor leaves them for to_fragment.
            ScopedValue<bool> bind(in_fragment_value, true);
            ScopedValue<Expr> bind_lane(fragment_lane, lane);
            value = to_fragment(mutate(op->value), f);
        }
        return in_lane_loop(
            lane,
            Store::make(name, std::move(value),
                        Ramp::make(0, 1, f->value_type().lanes()), op->param,
                        const_true(f->value_type().lanes()), ModulusRemainder(),
                        op->is_streaming));
    }

    // A tile filled from a matrix in memory, with the shape that says. A fill
    // is the one access that doesn't have to know the shape to be written, so
    // for a tile that is only filled, worked on elementwise and copied back
    // out, this is the only thing that knows it.
    void record_fill_shape(const Store *op, Fragment *f) {
        MultiRamp dest, src;
        const Load *matrix =
            is_load_of_multiramp(op->value, Scope<Expr>::empty_scope(), &src);
        if (!matrix || find_fragment(matrix->name) ||
            !is_multiramp(op->index, Scope<Expr>::empty_scope(), &dest)) {
            return;
        }
        for (const Shape &candidate : supported_shapes) {
            WMMAMatrixLayout in_fragment, in_memory;
            if (wmma_matrix_layout(dest, candidate.M, candidate.N, &in_fragment) &&
                wmma_matrix_layout(src, candidate.M, candidate.N, &in_memory)) {
                f->fill_shape = candidate;
                f->found_fill_shape = true;
                return;
            }
        }
    }

    // What an elementwise op tells us about the fragment it writes: it holds
    // the matrix the same way the fragments it is built from do.
    void record_elementwise(const Store *op, Fragment *f) {
        Fragment *src = nullptr;
        const Expr value = without_lets(op->value);
        for (const Fragment *read : fragments_read(value)) {
            if (read->found_shape) {
                src = const_cast<Fragment *>(read);
                break;
            }
        }
        if (!src) {
            // Nothing read here has a shape yet, so take one from a fill if
            // one of them was filled from memory. Only a fragment read outside
            // a matrix multiply can be settled this way, which is why it waits
            // until here: an operand's role comes from the multiply that reads
            // it, and this would be the wrong answer for one.
            for (const Fragment *read : fragments_read(value)) {
                if (read->found_fill_shape) {
                    src = const_cast<Fragment *>(read);
                    set_role(src, Role::Accumulator);
                    set_shape(src, src->fill_shape);
                    break;
                }
            }
        }
        user_assert(src)
            << "The tensor core fragment " << f->name << " is computed from "
            << "something other than another fragment, so there is nothing to say "
            << "how it should be spread over the lanes of a warp.\n"
            << Stmt(op);
        user_assert(src->role == Role::Accumulator)
            << "The tensor core fragment " << f->name << " is computed from "
            << role_name(src->role) << " of a matrix multiply. Only an accumulator "
            << "can be used that way.\n";
        set_role(f, Role::Accumulator);
        set_shape(f, src->shape);

        // Anything else read here that only knows the shape it was filled with
        // is elementwise with what settled the shape, so it has that shape and
        // is spread over the lanes the same way. A causal mask read in
        // alongside the scores it masks is one of these.
        for (const Fragment *read : fragments_read(value)) {
            if (!read->found_shape && read->found_fill_shape) {
                Fragment *fill = const_cast<Fragment *>(read);
                set_role(fill, Role::Accumulator);
                set_shape(fill, src->shape);
            }
        }

        // A store narrower than the tile writes a value that is uniform along
        // one axis of it, such as a row maximum. The other axis indexes it.
        const int lanes = op->value.type().lanes();
        const Shape &shape = f->shape;
        if (lanes != shape.M * shape.N) {
            user_assert(lanes == shape.M || lanes == shape.N)
                << "The tensor core fragment " << f->name << " is written " << lanes
                << " entries at a time, which is neither the whole " << shape.M << "x"
                << shape.N << " tile nor one of its axes.\n"
                << Stmt(op);
            const int axis = lanes == shape.M ? 0 : 1;
            user_assert(f->axis < 0 || f->axis == axis)
                << "The tensor core fragment " << f->name << " is written both as a "
                << "value along the rows of the tile and as one along its columns.\n"
                << Stmt(op);
            f->axis = axis;
        }
        subtile_name(f, op->index);
    }

    Stmt convert_to_fill(const Store *op, Fragment *f) {
        const int lanes = f->value_type().lanes();
        Expr lane = make_lane(unique_name("wmma_lane") + gpu_thread_name(0));
        // A value that is the same at every entry doesn't care which entries a
        // lane holds, so it needs no instruction: the registers just get set.
        const Broadcast *uniform = op->value.as<Broadcast>();
        if (uniform && !uniform->value.type().is_scalar()) {
            uniform = nullptr;
        }

        if (f->axis >= 0) {
            user_assert(uniform)
                << "A tensor core fragment that holds a value spread along an axis "
                << "of the tile can only be filled with one that is the same "
                << "everywhere.\n"
                << Stmt(op);
            return in_lane_loop(
                lane, Store::make(subtile_name(f, op->index),
                                  Broadcast::make(uniform->value, lanes),
                                  Ramp::make(0, 1, lanes)));
        }

        int rows, cols;
        fragment_matrix_shape(f->role, f->shape, &rows, &cols);
        MultiRamp dest_mr;
        WMMAMatrixLayout dest;
        user_assert(is_multiramp(op->index, Scope<Expr>::empty_scope(), &dest_mr) &&
                    wmma_matrix_layout(dest_mr, rows, cols, &dest))
            << "A tensor core fragment must be filled a whole tile at a time, but "
            << "this fill is not to a dense " << rows << "x" << cols << " tile of "
            << f->name << ".\n"
            << Stmt(op);
        const string name = subtile_name(
            f, make_matrix_index(dest.base, rows, cols,
                                 dest.row_major ? Layout::Row : Layout::Col, dest.stride));
        Expr value;
        if (uniform) {
            value = Broadcast::make(uniform->value, lanes);
        } else if (is_const_zero(op->value)) {
            value = make_zero(f->value_type());
        } else {
            auto fail = [&](const char *reason) {
                user_error << "Fill of a tensor core fragment not supported. "
                           << reason << ".\n"
                           << Stmt(op);
                return Expr{};
            };

            MultiRamp mr;
            const Load *matrix =
                is_load_of_multiramp(op->value, Scope<Expr>::empty_scope(), &mr);
            internal_assert(matrix);  // is_fill checked this
            WMMAMatrixLayout mem;
            if (find_fragment(matrix->name)) {
                value = fail("A fragment can only be filled from a matrix in memory, "
                             "not from another fragment, because the layout in "
                             "registers is not known");
            } else if (matrix->type.element_of() != f->element_type) {
                value = fail("A fragment can only be filled from a matrix of the same "
                             "type, because the hardware does not convert on the way in");
            } else if (!is_const_one(matrix->predicate)) {
                value = fail("The load is predicated");
            } else if (!wmma_matrix_layout(mr, rows, cols, &mem)) {
                value = fail("The matrix loaded from is not a dense tile of the right "
                             "shape");
            } else {
                value = make_matrix_to_fragment(
                    f->role, f->shape, mem.row_major ? Layout::Row : Layout::Col,
                    matrix, mem.base, mem.stride, lane);
            }
        }
        return in_lane_loop(
            lane, Store::make(name, std::move(value), Ramp::make(0, 1, lanes)));
    }

    Stmt convert_to_matmul(const Store *op, Fragment *f, const MatmulInfo &info) {
        Expr lane = make_lane(unique_name("wmma_lane") + gpu_thread_name(0));
        Expr a = operand_value(info.lhs, Role::A, info.shape, info.lhs_layout, info.lda, lane);
        Expr b = operand_value(info.rhs, Role::B, info.shape, info.rhs_layout, info.ldb, lane);

        Type acc_type = info.accumulator_type.with_lanes(accumulator_elements);
        Expr frag_idx = Ramp::make(0, 1, accumulator_elements);
        const string name = subtile_name(f, op->index);
        Expr c = Load::make(acc_type, name, frag_idx);

        Expr mma = Call::make(acc_type, Call::wmma_mma,
                              {info.shape.M, info.shape.N, info.shape.K,
                               (int)info.lhs_layout, (int)info.rhs_layout,
                               std::move(a), std::move(b), std::move(c), lane},
                              Call::Intrinsic);

        Stmt store = in_lane_loop(
            lane, Store::make(name, std::move(mma), frag_idx));
        for (const auto &[let_name, v] : reverse_view(info.peeled_lets)) {
            store = LetStmt::make(let_name, v, store);
        }
        return store;
    }

    Stmt visit(const For *op) override {
        ScopedBinding<Interval> bind_bounds(loop_bounds, op->name,
                                            Interval(op->min, op->max));

        // A loop that runs once names the same value throughout, so treat its
        // variable like a let. Sliding window leaves loops in this shape once
        // the likely markers that loop partitioning has yet to consume are set
        // aside, which can_prove does for itself.
        if (can_prove(without_lets(op->min == op->max), loop_bounds)) {
            let_values.emplace_back(op->name, op->min);
            Stmt s = IRMutator::visit(op);
            let_values.pop_back();
            return s;
        }

        if (!is_gpu(op->for_type)) {
            return IRMutator::visit(op);
        }
        const bool thread = op->for_type == ForType::GPUThread ||
                            op->for_type == ForType::GPULane;
        gpu_loop_vars.push_back(op->name);
        if (thread) {
            gpu_thread_vars.push_back(op->name);
            if (op->for_type == ForType::GPULane) {
                gpu_lane_vars.push_back(op->name);
            }
        }
        Stmt s = IRMutator::visit(op);
        if (thread) {
            if (op->for_type == ForType::GPULane) {
                gpu_lane_vars.pop_back();
            }
            gpu_thread_vars.pop_back();
        }
        gpu_loop_vars.pop_back();
        return s;
    }

    // Where each allocation lives: how many loops over GPU threads it sits
    // inside, and what it was scheduled into.
    struct AllocationPlace {
        int thread_depth;
        MemoryType memory_type;
    };
    Scope<AllocationPlace> allocation_place;

    // Whether every lane of a warp can reach the same allocation at an address
    // of its own. One made inside a loop over threads is per thread whatever
    // it was scheduled as, and one asked for in registers is per thread
    // wherever it was made. One that isn't in scope at all is a buffer the
    // caller passed in, which is in global memory.
    bool is_shared_memory(const string &name) {
        const AllocationPlace *place = allocation_place.find(name);
        return !place || (place->thread_depth == 0 &&
                          place->memory_type != MemoryType::Register &&
                          place->memory_type != MemoryType::Stack);
    }

    Stmt visit(const Allocate *op) override {
        if (op->memory_type != MemoryType::Tile) {
            ScopedBinding<AllocationPlace> bind(
                allocation_place, op->name,
                AllocationPlace{(int)gpu_thread_vars.size(), op->memory_type});
            return IRMutator::visit(op);
        }

        user_assert(op->type == Float(32) || op->type == Float(16) ||
                    op->type == Int(32))
            << "Tensor core fragments must hold 32-bit or 16-bit floats, or "
            << "32-bit integers, but " << op->name << " holds " << op->type << ".\n";

        Fragment &f = fragments[op->name];
        if (pass == 0) {
            f.name = op->name;
            f.fragment_name = op->name + ".wmma.";
            f.element_type = op->type;
        }

        in_scope.push_back(op->name);
        Stmt body = mutate(op->body);
        in_scope.pop_back();

        if (pass == 0) {
            // Storage nothing touches has no layout to work out. A Func whose
            // value turns out to be unused still gets an allocation - it is
            // there to cover a load, and the load went away with the value -
            // so an untouched fragment is not a mistake. The loop below drops
            // it, having no subtiles to give it.
            user_assert(f.role != Role::Unknown || f.subtiles.empty())
                << op->name << " is stored in Tile memory, but no matrix "
                << "multiply was found that accumulates into it or reads it as an "
                << "operand, so we can't tell what layout it should have.\n";
            return op;
        }

        // Each fragment is one tile's worth of storage per lane. The allocations
        // stay outside the loops over lanes, because a loop over lanes is a loop
        // over threads, and register allocations outside a thread loop already
        // get replicated per thread.
        for (int i = 0; i < (int)f.subtiles.size(); i++) {
            body = Allocate::make(f.fragment_name + std::to_string(i), f.element_type,
                                  MemoryType::Tile, {f.value_type().lanes()},
                                  const_true(), body);
        }
        return body;
    }

    Stmt visit(const Atomic *op) override {
        if (find_fragment(op->producer_name)) {
            // A tensor core fragment is per-thread register storage, so there's
            // nothing for another thread to race with. The atomic is there
            // because the fragment is scheduled outside the loops over threads,
            // which makes it look shared.
            user_assert(op->mutex_name.empty())
                << "Accumulating into a tensor core fragment should not need a "
                << "mutex.\n";
            return mutate(op->body);
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Free *op) override {
        Fragment *f = find_fragment(op->name);
        if (!f || pass == 0) {
            return op;
        }
        Stmt s;
        for (int i = 0; i < (int)f->subtiles.size(); i++) {
            Stmt free = Free::make(f->fragment_name + std::to_string(i));
            s = s.defined() ? Block::make(std::move(s), std::move(free)) : std::move(free);
        }
        return s;
    }

    Stmt visit(const ProducerConsumer *op) override {
        Fragment *f = find_fragment(op->name);
        if (!f) {
            return IRMutator::visit(op);
        }
        return ProducerConsumer::make(f->fragment_name, op->is_producer, mutate(op->body));
    }

    // Which subtile of a fragment each entry of a read lies in, and which entry
    // of that subtile it is, as an index into the M x N matrix in row-major
    // order. A read may be of only part of a tile - one row of it, say - so
    // rather than being a subtile in its own right it lies within ones that
    // other accesses established. It may also run across several of them, which
    // is what reducing along an axis longer than a tile does. Fails if any
    // entry lies outside all of them.
    bool read_entries(Fragment *f, const Expr &index, vector<int> *subtile,
                      vector<int> *entry) {
        MultiRamp read;
        if (!is_multiramp(index_for_analysis(index), Scope<Expr>::empty_scope(), &read)) {
            return false;
        }

        // Enumerate where the entries sit relative to the start of the read, in
        // the order a multiramp puts its lanes in.
        vector<int> offsets{0};
        for (size_t d = 0; d < read.strides.size(); d++) {
            auto s = as_const_int(read.strides[d]);
            if (!s) {
                return false;
            }
            vector<int> next;
            for (int i = 0; i < read.lanes[d]; i++) {
                for (int o : offsets) {
                    next.push_back(o + i * (int)*s);
                }
            }
            offsets.swap(next);
        }

        // Where each subtile starts relative to the read, and how it is laid
        // out. They are disjoint, so at most one holds any given entry.
        const Shape &shape = f->shape;
        const int subtiles = (int)f->subtiles.size();
        vector<int> start(subtiles), stride(subtiles);
        vector<bool> row_major(subtiles), usable(subtiles, false);
        for (int i = 0; i < subtiles; i++) {
            const MultiRamp &tile = f->subtiles[i];
            WMMAMatrixLayout mem;
            auto base = as_const_int(simplify(read.base - tile.base));
            if (!base || !wmma_matrix_layout(tile, shape.M, shape.N, &mem)) {
                continue;
            }
            auto s = as_const_int(mem.stride);
            if (!s) {
                continue;
            }
            start[i] = (int)*base;
            stride[i] = (int)*s;
            row_major[i] = mem.row_major;
            usable[i] = true;
        }

        subtile->clear();
        entry->clear();
        for (int o : offsets) {
            int found = -1, index_in_tile = 0;
            for (int i = 0; i < subtiles && found < 0; i++) {
                if (!usable[i]) {
                    continue;
                }
                // The matrix a fragment inflates to is in row-major order.
                const int e = o + start[i];
                const int row = row_major[i] ? e / stride[i] : e % stride[i];
                const int col = row_major[i] ? e % stride[i] : e / stride[i];
                if (row >= 0 && row < shape.M && col >= 0 && col < shape.N) {
                    found = i;
                    index_in_tile = row * shape.N + col;
                }
            }
            if (found < 0) {
                return false;
            }
            subtile->push_back(found);
            entry->push_back(index_in_tile);
        }
        return true;
    }

    // The same, for a read that has to lie within a single subtile. Returns -1
    // if it doesn't.
    int containing_subtile(Fragment *f, const Expr &index, vector<int> *indices) {
        vector<int> subtile;
        if (!read_entries(f, index, &subtile, indices)) {
            return -1;
        }
        for (int s : subtile) {
            if (s != subtile[0]) {
                return -1;
            }
        }
        return subtile.empty() ? -1 : subtile[0];
    }

    // Whether a read takes a whole tile in row-major order, which is what the
    // instruction that copies one out to memory does.
    static bool is_whole_tile(const Fragment *f, const vector<int> &indices) {
        if ((int)indices.size() != f->shape.M * f->shape.N) {
            return false;
        }
        for (int i = 0; i < (int)indices.size(); i++) {
            if (indices[i] != i) {
                return false;
            }
        }
        return true;
    }

    // A read of a fragment that isn't part of a tensor core instruction. The
    // entries wanted are spread across the lanes of the warp, so gather them.
    Expr visit(const Load *op) override {
        Fragment *f = find_fragment(op->name);
        if (!f) {
            return IRMutator::visit(op);
        }
        if (pass == 0) {
            // A read tells us nothing about the fragment, and it lands inside a
            // subtile some other access already established, so there's nothing
            // to record.
            return op;
        }

        if (f->axis >= 0) {
            // What is in the registers is the whole tile with this value
            // repeated along an axis, not the vector this reads like, so the
            // read only means anything where to_fragment can match it against
            // the value being computed. Leave it for there.
            user_assert(in_fragment_value)
                << "The tensor core fragment " << f->name << " holds a value spread "
                << "along an axis of the tile. It can only be read to compute "
                << "another fragment, because how it is spread over the lanes of a "
                << "warp only matches a fragment spread the same way.\n"
                << Expr(op);
            return op;
        }

        user_assert(f->role == Role::Accumulator)
            << "The tensor core fragment " << f->name << " is read outside a tensor "
            << "core instruction, but it holds " << role_name(f->role) << " of a matrix "
            << "multiply. Only an accumulator can be read that way.\n";
        user_assert(is_const_one(op->predicate))
            << "Read of a tensor core accumulator not supported. The read has a "
            << "predicate.\n"
            << Expr(op);

        // Which entry each lane wants is patched into the shuffle by the
        // runtime, so it has to be the same in every lane of the warp. A loop
        // over warps around it may differ, because warps each have their own
        // copy of the fragment and that only picks between those, which is
        // what index_within_thread takes the thread loop variables to be zero
        // for.
        for (const string &v : gpu_lane_vars) {
            user_assert(!expr_uses_var(op->index, v))
                << "Read of a tensor core accumulator not supported. Which entry "
                << "is read varies with " << v << ", so the lanes of the warp do "
                << "not all read the same one. How a fragment is spread over the "
                << "lanes is only known once the pipeline is running, so an entry "
                << "has to be named the same way by every lane.\n"
                << Expr(op);
        }

        vector<int> subtile, entry;
        user_assert(read_entries(f, op->index, &subtile, &entry))
            << "Read of a tensor core accumulator not supported. The entries read "
            << "do not all lie within tiles of it: " << index_for_analysis(op->index) << "\n"
            << Expr(op);

        // One inflated fragment per subtile the read touches, in the order it
        // reaches them, and indices into them laid end to end.
        const Shape &shape = f->shape;
        const int lanes = shape.M * shape.N;
        vector<Expr> matrices;
        vector<int> position(f->subtiles.size(), -1);
        vector<int> indices;
        for (int i = 0; i < (int)subtile.size(); i++) {
            if (position[subtile[i]] < 0) {
                position[subtile[i]] = (int)matrices.size();
                matrices.push_back(inflate_subtile(f, subtile[i], op->type.element_of()));
            }
            indices.push_back(position[subtile[i]] * lanes + entry[i]);
        }
        return Shuffle::make(matrices, indices);
    }

    // A fragment as the whole matrix it holds, with its entries in row-major
    // order. No lane argument: reading entries doesn't demote a matrix-wide
    // operation to a per-fragment one, so it introduces no loop over lanes for
    // the lane to come from. Whichever lane runs it reads the same entries,
    // which the runtime fills in.
    Expr inflate_subtile(Fragment *f, int subtile, Type element_type) {
        const Shape &shape = f->shape;
        Expr frag = Load::make(element_type.with_lanes(accumulator_elements),
                               f->fragment_name + std::to_string(subtile),
                               Ramp::make(0, 1, accumulator_elements));
        return Call::make(element_type.with_lanes(shape.M * shape.N),
                          Call::wmma_fragment_to_matrix_d,
                          {shape.M, shape.N, shape.K, std::move(frag)},
                          Call::Intrinsic);
    }

    Stmt visit(const Store *op) override {
        Fragment *f = find_fragment(op->name);

        if (!f) {
            // A store to memory of a whole tile read out of a fragment copies
            // that tile out.
            Expr store_index;
            const Load *load = peel_store_permutations(op, &store_index).as<Load>();
            Fragment *src = load ? find_fragment(load->name) : nullptr;
            if (src && pass == 0) {
                return op;
            }
            vector<int> indices;
            int subtile = src ? containing_subtile(src, load->index, &indices) : -1;
            if (subtile >= 0 && is_whole_tile(src, indices)) {
                user_assert(src->role == Role::Accumulator)
                    << "The tensor core fragment " << src->name << " is copied out to "
                    << "memory, but it holds " << role_name(src->role) << " of a matrix "
                    << "multiply. Only an accumulator can be copied out.\n";
                return convert_to_tile_store(op, store_index,
                                             src->fragment_name + std::to_string(subtile),
                                             src->shape);
            }
            // Not a copy of a whole tile out to memory. Recurse, so that reads
            // of a fragment in here become gathers.
            Stmt stmt = IRMutator::visit(op);
            if (gpu_lane_vars.empty() && reads_a_whole_matrix(stmt)) {
                stmt = in_lane_loop(make_lane(unique_name("wmma_lane") + gpu_thread_name(0)),
                                    stmt);
            }
            return stmt;
        }

        // A reduction along an axis of the tile accumulates, but it is not a
        // matrix multiply: what it reduces is something already held in
        // fragments rather than a product of two operands. It need not be a
        // fragment on its own - a softmax reduces an expression over one.
        const VectorReduce *reduce = reduction_of_a_fragment(op);
        const bool axis_reduction = reduce && !is_product(reduce->value) &&
                                    !fragments_read(reduce->value).empty();

        if (!is_accumulation(op) || axis_reduction) {
            // A fill from a matrix in memory needs to know the role and the
            // shape, so it waits for the second pass, and tells us neither.
            // Anything else that isn't a matrix multiply is an elementwise op
            // on the tile, which takes its layout from what it reads.
            if (is_fill(op) && fragments_read(op->value).empty()) {
                if (pass == 0) {
                    record_fill_shape(op, f);
                    return op;
                }
                return convert_to_fill(op, f);
            }
            if (pass == 0) {
                record_elementwise(op, f);
                return op;
            }
            return convert_to_elementwise(op, f);
        }

        MatmulInfo info = analyze_matmul(op, [&](const string &name) {
            const Fragment *operand = find_fragment(name);
            return operand && operand->role == Role::Accumulator;
        });
        if (pass == 0) {
            set_role(f, Role::Accumulator);
            set_shape(f, info.shape);
            subtile_name(f, op->index);
            record_operand(info.lhs, Role::A, info.shape, info.lhs_layout, info.lda);
            record_operand(info.rhs, Role::B, info.shape, info.rhs_layout, info.ldb);
            return op;
        }
        return convert_to_matmul(op, f, info);
    }

public:
    void next_pass() {
        pass = 1;
    }
};

}  // namespace

Stmt extract_wmma_operations(const Stmt &s) {
    ExtractWMMAOperations mutator;
    // The first pass only looks. What it learns about a fragment from one use
    // of it is needed at all the others, including the ones it has already
    // walked past.
    mutator(s);
    mutator.next_pass();
    return mutator(s);
}

bool is_wmma_intrinsic(const Call *op) {
    return (op->is_intrinsic(Call::wmma_axis_xor) ||
            op->is_intrinsic(Call::wmma_matrix_to_fragment_a) ||
            op->is_intrinsic(Call::wmma_matrix_to_fragment_b) ||
            op->is_intrinsic(Call::wmma_matrix_to_fragment_c) ||
            op->is_intrinsic(Call::wmma_mma) ||
            op->is_intrinsic(Call::wmma_vector_to_fragment));
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
