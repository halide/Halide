#include "ExtractWMMAOperations.h"

#include "CanonicalizeGPUVars.h"
#include "FindIntrinsics.h"
#include "IREquality.h"
#include "IRMutator.h"
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

MatmulInfo analyze_matmul(const Store *op) {
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
    info.lhs.load = is_load_of_multiramp(mul->a, empty_scope, &info.lhs.mr);
    info.rhs.load = is_load_of_multiramp(mul->b, empty_scope, &info.rhs.mr);
    if (!info.lhs.load || !info.rhs.load) {
        return fail("the matrix multiply operands are not loads with affine indices");
    }
    if (!is_const_one(info.lhs.load->predicate) || !is_const_one(info.rhs.load->predicate)) {
        return fail("the matrix multiply operands are predicated loads");
    }
    const Type operand_type = info.lhs.load->type.element_of();
    if (info.rhs.load->type.element_of() != operand_type) {
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
    // An allocation may hold several fragments as disjoint sub-tiles, each of
    // which becomes its own allocation.
    vector<MultiRamp> subtiles;

    Type value_type() const {
        return element_type.with_lanes(
            elements_per_lane(role, shape, element_type));
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

    // The loops over GPU blocks, threads, and lanes we're inside of.
    vector<string> gpu_loop_vars;

    // The lane a gather in the statement currently being mutated reads as. A
    // gather is a warp-wide operation, so the statement it appears in has to be
    // run by every lane of the warp.
    Expr gather_lane;

    // Every gather is wrapped in a loop over the lanes of a warp by the
    // innermost statement that contains it. Statements nest, so the innermost
    // one is the one whose mutation finishes first.
    Stmt mutate(const Stmt &s) override {
        ScopedValue<Expr> outer(gather_lane, Expr());
        Stmt result = IRMutator::mutate(s);
        if (gather_lane.defined()) {
            result = in_lane_loop(gather_lane, std::move(result));
        }
        return result;
    }

    using IRMutator::mutate;

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
    // those copies, not between subtiles within one, so drop it.
    Expr index_within_thread(const Expr &index) {
        Expr idx = index;
        for (const string &v : gpu_loop_vars) {
            idx = substitute(v, 0, idx);
        }
        return simplify(idx);
    }

    string subtile_name(Fragment *f, const Expr &index) {
        int idx = get_subtile(index_within_thread(index),
                              "tensor core fragment", &f->subtiles);
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
                return make_fused_operand(f, operand, shape, lane);
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
                            const Expr &lane) {
        user_assert(shape.K == f->shape.N)
            << "The result of a tensor core matrix multiply is fed straight into "
            << "another as its a operand, but it is " << f->shape.M << "x"
            << f->shape.N << " and the second one wants an a operand with "
            << shape.K << " columns.\n";

        vector<int> indices;
        int subtile = containing_subtile(f, operand.load->index, &indices);
        user_assert(subtile >= 0 && is_whole_tile(f, indices))
            << "A tensor core accumulator fed straight into another matrix "
            << "multiply must be used a whole tile at a time.\n"
            << Expr(operand.load);

        const string name = f->fragment_name + std::to_string(subtile);
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
        } else if (const Min *op = e.as<Min>()) {
            auto [a, b] = pair(op->a, op->b);
            return Min::make(a, b);
        } else if (const Max *op = e.as<Max>()) {
            auto [a, b] = pair(op->a, op->b);
            return Max::make(a, b);
        } else if (const Cast *op = e.as<Cast>()) {
            return Cast::make(t, to_fragment(op->value, dest));
        } else if (const Select *op = e.as<Select>()) {
            auto [a, b] = pair(op->true_value, op->false_value);
            return Select::make(to_fragment(op->condition, dest), a, b);
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
        }
        // A vector spread over the tile along one axis, such as a row
        // statistic a softmax subtracts. Every lane holds the whole vector, so
        // this becomes a per-lane selection out of it.
        int axis = 0;
        Expr vec = broadcast_along_axis(e, dest->shape, &axis);
        if (vec.defined()) {
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

    // Recognize a value covering the whole matrix that is really a vector
    // spread along one of its axes: entry (r, c) is v[r] if the axis is zero,
    // or v[c] if it is one. Vectorization leaves this as a broadcast of the
    // vector, possibly under a transpose, so ask where each entry comes from
    // rather than trying to match the shape.
    static Expr broadcast_along_axis(const Expr &e, const Shape &shape, int *axis) {
        const Shuffle *shuffle = e.as<Shuffle>();
        const Broadcast *b = (shuffle && shuffle->vectors.size() == 1 ?
                                  shuffle->vectors[0] :
                                  e)
                                 .as<Broadcast>();
        if (!b || !b->value.type().is_vector()) {
            return Expr{};
        }
        const int width = b->value.type().lanes();
        auto source = [&](int i) {
            return (shuffle ? shuffle->indices[i] : i) % width;
        };
        if (e.type().lanes() != shape.M * shape.N ||
            (width != shape.M && width != shape.N)) {
            return Expr{};
        }
        // Axis zero is indexed by the row, axis one by the column.
        for (int a = 0; a < 2; a++) {
            if (width != (a ? shape.N : shape.M)) {
                continue;
            }
            bool ok = true;
            for (int r = 0; r < shape.M && ok; r++) {
                for (int c = 0; c < shape.N; c++) {
                    ok &= source(r * shape.N + c) == (a ? c : r);
                }
            }
            if (ok) {
                *axis = a;
                return b->value;
            }
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
    Stmt convert_to_elementwise(const Store *op, Fragment *f) {
        vector<int> indices;
        int subtile = containing_subtile(f, op->index, &indices);
        user_assert(subtile >= 0 && is_whole_tile(f, indices))
            << "A tensor core fragment computed elementwise must be written a "
            << "whole tile at a time.\n"
            << Stmt(op);
        Expr value = to_fragment(mutate(op->value), f);
        return Store::make(f->fragment_name + std::to_string(subtile), std::move(value),
                           Ramp::make(0, 1, f->value_type().lanes()), op->param,
                           const_true(f->value_type().lanes()), ModulusRemainder(),
                           op->is_streaming);
    }

    // What an elementwise op tells us about the fragment it writes: it holds
    // the matrix the same way the fragments it is built from do.
    void record_elementwise(const Store *op, Fragment *f) {
        const Fragment *src = nullptr;
        for (const Fragment *read : fragments_read(op->value)) {
            if (read->found_shape) {
                src = read;
                break;
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
        subtile_name(f, op->index);
    }

    Stmt convert_to_fill(const Store *op, Fragment *f) {
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
        const int lanes = f->value_type().lanes();
        Expr lane = make_lane(unique_name("wmma_lane") + gpu_thread_name(0));
        Expr value;
        if (is_const_zero(op->value)) {
            // Zeroing a fragment is layout-independent, so it doesn't need an
            // instruction - the registers just get set to zero.
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
            int rows, cols;
            fragment_matrix_shape(f->role, f->shape, &rows, &cols);
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
        if (!is_gpu(op->for_type)) {
            return IRMutator::visit(op);
        }
        gpu_loop_vars.push_back(op->name);
        Stmt s = IRMutator::visit(op);
        gpu_loop_vars.pop_back();
        return s;
    }

    Stmt visit(const Allocate *op) override {
        if (op->memory_type != MemoryType::Tile) {
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
            user_assert(f.role != Role::Unknown)
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

    // Which subtile of a fragment a read lies in, and which entries of it the
    // read wants, as indices into the M x N matrix in row-major order. A read
    // may be of only part of a tile - one row of it, say - so rather than being
    // a subtile in its own right it lies within one that some other access
    // established. Returns -1 if it doesn't lie within any of them.
    int containing_subtile(Fragment *f, const Expr &index, vector<int> *indices) {
        MultiRamp read;
        if (!is_multiramp(index_within_thread(index), Scope<Expr>::empty_scope(), &read)) {
            return -1;
        }

        // Enumerate where the entries sit relative to the start of the read, in
        // the order a multiramp puts its lanes in.
        vector<int> offsets{0};
        for (size_t d = 0; d < read.strides.size(); d++) {
            auto s = as_const_int(read.strides[d]);
            if (!s) {
                return -1;
            }
            vector<int> next;
            for (int i = 0; i < read.lanes[d]; i++) {
                for (int o : offsets) {
                    next.push_back(o + i * (int)*s);
                }
            }
            offsets.swap(next);
        }

        // Try each subtile in turn. They are disjoint, so at most one can hold
        // all the entries read.
        const Shape &shape = f->shape;
        for (int i = 0; i < (int)f->subtiles.size(); i++) {
            const MultiRamp &tile = f->subtiles[i];
            WMMAMatrixLayout mem;
            auto start = as_const_int(simplify(read.base - tile.base));
            if (!start || !wmma_matrix_layout(tile, shape.M, shape.N, &mem)) {
                continue;
            }
            auto stride = as_const_int(mem.stride);
            if (!stride) {
                continue;
            }
            // The matrix a fragment inflates to is in row-major order.
            indices->clear();
            for (int o : offsets) {
                const int e = o + (int)*start;
                const int row = mem.row_major ? e / (int)*stride : e % (int)*stride;
                const int col = mem.row_major ? e % (int)*stride : e / (int)*stride;
                if (row < 0 || row >= shape.M || col < 0 || col >= shape.N) {
                    indices->clear();
                    break;
                }
                indices->push_back(row * shape.N + col);
            }
            if (indices->size() == offsets.size()) {
                return i;
            }
        }
        return -1;
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

        user_assert(f->role == Role::Accumulator)
            << "The tensor core fragment " << f->name << " is read outside a tensor "
            << "core instruction, but it holds " << role_name(f->role) << " of a matrix "
            << "multiply. Only an accumulator can be read that way.\n";
        user_assert(is_const_one(op->predicate))
            << "Read of a tensor core accumulator not supported. The read has a "
            << "predicate.\n"
            << Expr(op);

        vector<int> indices;
        int found = containing_subtile(f, op->index, &indices);
        user_assert(found >= 0)
            << "Read of a tensor core accumulator not supported. The entries read "
            << "do not all lie within a single tile.\n"
            << Expr(op);
        const string name = f->fragment_name + std::to_string(found);
        const Shape &shape = f->shape;

        Type element_type = op->type.element_of();
        Expr frag = Load::make(element_type.with_lanes(accumulator_elements), name,
                               Ramp::make(0, 1, accumulator_elements));
        if (!gather_lane.defined()) {
            gather_lane = make_lane(unique_name("wmma_lane") + gpu_thread_name(0));
        }
        Expr matrix = Call::make(element_type.with_lanes(shape.M * shape.N),
                                 Call::wmma_fragment_to_matrix_d,
                                 {shape.M, shape.N, shape.K, std::move(frag), gather_lane},
                                 Call::Intrinsic);
        return Shuffle::make({std::move(matrix)}, indices);
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
            return IRMutator::visit(op);
        }

        if (!is_accumulation(op)) {
            // A fill from a matrix in memory needs to know the role and the
            // shape, so it waits for the second pass, and tells us neither.
            // Anything else that isn't a matrix multiply is an elementwise op
            // on the tile, which takes its layout from what it reads.
            if (is_fill(op) && fragments_read(op->value).empty()) {
                return pass == 0 ? Stmt(op) : convert_to_fill(op, f);
            }
            if (pass == 0) {
                record_elementwise(op, f);
                return op;
            }
            return convert_to_elementwise(op, f);
        }

        MatmulInfo info = analyze_matmul(op);
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
    return (op->is_intrinsic(Call::wmma_matrix_to_fragment_a) ||
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
