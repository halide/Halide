#include <sstream>
#include <utility>

#include "AlignLoads.h"
#include "CSE.h"
#include "CodeGen_Internal.h"
#include "CodeGen_Posix.h"
#include "Debug.h"
#include "HexagonOptimize.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"
#include "LoopCarry.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"
#include "Util.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

using namespace llvm;

#ifdef WITH_HEXAGON

namespace {

/** A code generator that emits Hexagon code from a given Halide stmt. */
class CodeGen_Hexagon : public CodeGen_Posix {
public:
    /** Create a Hexagon code generator for the given Hexagon target. */
    CodeGen_Hexagon(const Target &);

protected:
    void compile_func(const LoweredFunc &f,
                      const std::string &simple_name, const std::string &extern_name) override;

    void init_module() override;

    std::string mcpu_target() const override;
    std::string mcpu_tune() const override;
    std::string mattrs() const override;
    int isa_version;
    bool use_soft_float_abi() const override;
    int native_vector_bits() const override;

    llvm::Function *define_hvx_intrinsic(llvm::Function *intrin, Type ret_ty,
                                         const std::string &name,
                                         std::vector<Type> arg_types,
                                         int flags);

    int is_hvx_v65_or_later() const {
        return (isa_version >= 65);
    }

    using CodeGen_Posix::visit;

    /** Nodes for which we want to emit specific hexagon intrinsics */
    ///@{
    void visit(const Max *) override;
    void visit(const Min *) override;
    void visit(const Call *) override;
    void visit(const Mul *) override;
    void visit(const Select *) override;
    void visit(const Allocate *) override;
    ///@}

    /** Call an LLVM intrinsic, potentially casting the operands to
     * match the type of the function. */
    ///@{
    llvm::Value *call_intrin_cast(llvm::Type *ret_ty, llvm::Function *F,
                                  std::vector<llvm::Value *> Ops);
    llvm::Value *call_intrin_cast(llvm::Type *ret_ty, int id,
                                  std::vector<llvm::Value *> Ops);
    ///@}

    /** Define overloads of CodeGen_LLVM::call_intrin that determine
     * the intrin_lanes from the type, and allows the function to
     * return null if the maybe option is true and the intrinsic is
     * not found. */
    ///@{
    llvm::Value *call_intrin(Type t, const std::string &name,
                             std::vector<Expr>, bool maybe = false);
    llvm::Value *call_intrin(llvm::Type *t, const std::string &name,
                             std::vector<llvm::Value *>, bool maybe = false);
    ///@}

    /** Override CodeGen_LLVM to use hexagon intrinics when possible. */
    ///@{
    llvm::Value *interleave_vectors(const std::vector<llvm::Value *> &v) override;
    llvm::Value *shuffle_vectors(llvm::Value *a, llvm::Value *b,
                                 const std::vector<int> &indices) override;
    using CodeGen_Posix::shuffle_vectors;
    ///@}

    /** Generate a LUT lookup using vlut instructions. */
    ///@{
    llvm::Value *vlut(llvm::Value *lut, llvm::Value *indices, int min_index = 0, int max_index = 1 << 30);
    llvm::Value *vlut(llvm::Value *lut, const std::vector<int> &indices);
    ///@}

    llvm::Value *vdelta(llvm::Value *lut, const std::vector<int> &indices);

    /** Because HVX intrinsics operate on vectors of i32, using them
     * requires a lot of extraneous bitcasts, which make it difficult
     * to manipulate the IR. This function avoids generating redundant
     * bitcasts. */
    llvm::Value *create_bitcast(llvm::Value *v, llvm::Type *ty);

private:
    /** Generates code for computing the size of an allocation from a
     * list of its extents and its size. Fires a runtime assert
     * (halide_error) if the size overflows 2^31 -1, the maximum
     * positive number an int32_t can hold. */
    llvm::Value *codegen_cache_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents, int padding);

    /** Generate a LUT (8/16 bit, max_index < 256) lookup using vlut instructions. */
    llvm::Value *vlut256(llvm::Value *lut, llvm::Value *indices, int min_index = 0, int max_index = 255);

    /** Wrapper to create a vector populated with a constant value in each lane. */
    Value *create_vector(llvm::Type *ty, int val);
};

CodeGen_Hexagon::CodeGen_Hexagon(const Target &t)
    : CodeGen_Posix(t) {
    if (target.has_feature(Halide::Target::HVX_v68)) {
        isa_version = 68;
    } else if (target.has_feature(Halide::Target::HVX_v66)) {
        isa_version = 66;
    } else if (target.has_feature(Halide::Target::HVX_v65)) {
        isa_version = 65;
    } else {
        isa_version = 62;
    }
    user_assert(target.has_feature(Target::HVX))
        << "Creating a Codegen target for Hexagon without the hvx target feature.\n";
}

Stmt call_halide_qurt_hvx_lock(const Target &target) {
    Expr hvx_lock =
        Call::make(Int(32), "halide_qurt_hvx_lock", {}, Call::Extern);
    string hvx_lock_result_name = unique_name("hvx_lock_result");
    Expr hvx_lock_result_var = Variable::make(Int(32), hvx_lock_result_name);
    Stmt check_hvx_lock = LetStmt::make(
        hvx_lock_result_name, hvx_lock,
        AssertStmt::make(EQ::make(hvx_lock_result_var, 0), hvx_lock_result_var));
    return check_hvx_lock;
}

Stmt call_halide_qurt_hvx_unlock() {
    Expr hvx_unlock =
        Call::make(Int(32), "halide_qurt_hvx_unlock", {}, Call::Extern);
    string hvx_unlock_result_name = unique_name("hvx_unlock_result");
    Expr hvx_unlock_result_var = Variable::make(Int(32), hvx_unlock_result_name);
    Stmt check_hvx_unlock =
        LetStmt::make(hvx_unlock_result_name, hvx_unlock,
                      AssertStmt::make(EQ::make(hvx_unlock_result_var, 0),
                                       hvx_unlock_result_var));
    return check_hvx_unlock;
}

// Wrap the stmt in a call to qurt_hvx_lock, calling qurt_hvx_unlock
// as a destructor if successful.
Stmt acquire_hvx_context(Stmt stmt, const Target &target) {
    // Modify the stmt to add a call to halide_qurt_hvx_lock, and
    // register a destructor to call halide_qurt_hvx_unlock.
    Stmt check_hvx_lock = call_halide_qurt_hvx_lock(target);
    Expr dummy_obj = reinterpret(Handle(), cast<uint64_t>(1));
    Expr hvx_unlock =
        Call::make(Handle(), Call::register_destructor,
                   {Expr("halide_qurt_hvx_unlock_as_destructor"), dummy_obj},
                   Call::Intrinsic);

    stmt = Block::make(Evaluate::make(hvx_unlock), stmt);
    stmt = Block::make(check_hvx_lock, stmt);
    return stmt;
}

bool is_dense_ramp(const Expr &x) {
    const Ramp *r = x.as<Ramp>();
    if (!r) {
        return false;
    }

    return is_const_one(r->stride);
}

// In Hexagon, we assume that we can read one vector past the end of
// buffers. Using this assumption, this mutator replaces vector
// predicated dense loads with scalar predicated dense loads.
class SloppyUnpredicateLoadsAndStores : public IRMutator {
    using IRMutator::visit;

    // The first and last lanes of all monotonic vectors in scope
    Scope<std::pair<Expr, Expr>> monotonic_vectors;

    // If a vector monotonically increases or decreases across the
    // lanes, return the first and last lane.
    std::pair<Expr, Expr> get_extreme_lanes(const Expr &e) {
        if (const Ramp *r = e.as<Ramp>()) {
            return {r->base, r->base + r->stride * (r->lanes - 1)};
        } else if (const Broadcast *b = e.as<Broadcast>()) {
            return {b->value, b->value};
        } else if (const LT *op = e.as<LT>()) {
            if (!op->a.type().is_bool()) {
                auto a = get_extreme_lanes(op->a);
                auto b = get_extreme_lanes(op->b);
                if (a.first.defined() && b.first.defined()) {
                    return {a.first < b.first, a.second < b.second};
                }
            }
        } else if (const LE *op = e.as<LE>()) {
            if (!op->a.type().is_bool()) {
                auto a = get_extreme_lanes(op->a);
                auto b = get_extreme_lanes(op->b);
                if (a.first.defined() && b.first.defined()) {
                    return {a.first <= b.first, a.second <= b.second};
                }
            }
        } else if (const Variable *op = e.as<Variable>()) {
            if (const auto *p = monotonic_vectors.find(op->name)) {
                return *p;
            }
        } else if (const Let *op = e.as<Let>()) {
            auto v = get_extreme_lanes(op->value);
            ScopedBinding<std::pair<Expr, Expr>> bind(v.first.defined(), monotonic_vectors, op->name, v);
            return get_extreme_lanes(op->body);
        }
        return {Expr(), Expr()};
    }

    Expr visit(const Let *op) override {
        auto v = get_extreme_lanes(op->value);
        ScopedBinding<std::pair<Expr, Expr>> bind(op->value.type().is_vector() && v.first.defined(),
                                                  monotonic_vectors, op->name, v);
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        if (is_const_one(op->predicate)) {
            // These are handled fine
            return IRMutator::visit(op);
        }

        Expr predicate = mutate(op->predicate);
        Expr index = mutate(op->index);

        if (is_dense_ramp(index) || index.as<Broadcast>()) {
            // Make the predicate into a scalar that is true if any of the lanes are
            // true.

            Expr condition;

            // If the predicate is monotonic increasing or decreasing
            // over the vector lanes, we can just check the last or
            // first lane, respectively. We won't bother to
            // distinguish between the two cases though, so we just or
            // them both together.
            auto v = get_extreme_lanes(predicate);
            if (v.first.defined()) {
                internal_assert(v.first.type() == Bool() &&
                                v.second.type() == Bool())
                    << "The extreme lanes of a bool vector should be scalar bools\n";
                condition = simplify(v.first || v.second);
            } else {
                // Take an OR over all lanes.
                condition = VectorReduce::make(VectorReduce::Or, predicate, 1);
                condition = simplify(condition);
            }

            Expr load = Load::make(op->type, op->name, index, op->image, op->param,
                                   const_true(op->type.lanes()), op->alignment);

            return Call::make(op->type, Call::if_then_else,
                              {condition, load}, Call::PureIntrinsic);
        } else {
            // It's a predicated vector gather. Just scalarize. We'd
            // prefer to keep it in a loop, but that would require
            // some sort of loop Expr. Another option would be
            // introducing a set of runtime functions to do predicated
            // loads.
            Expr load = Load::make(op->type, op->name, index, op->image, op->param,
                                   const_true(op->type.lanes()), op->alignment);
            return Call::make(op->type, Call::if_then_else,
                              {predicate, load}, Call::PureIntrinsic);
        }
    }

    Stmt visit(const Store *op) override {
        if (is_const_one(op->predicate)) {
            return IRMutator::visit(op);
        }

        Expr predicate = mutate(op->predicate);
        Expr value = mutate(op->value);
        Expr index = mutate(op->index);

        int lanes = value.type().lanes();

        if (const Broadcast *scalar_pred = predicate.as<Broadcast>()) {
            Stmt unpredicated_store = Store::make(op->name, value, index, op->param, const_true(lanes), op->alignment);
            return IfThenElse::make(scalar_pred->value, unpredicated_store);
        }

        if (predicate.same_as(op->predicate) && value.same_as(op->value) && index.same_as(op->index)) {
            return op;
        } else {
            return Store::make(op->name, value, index, op->param, predicate, op->alignment);
        }
    }
};

Stmt sloppy_unpredicate_loads_and_stores(const Stmt &s) {
    return SloppyUnpredicateLoadsAndStores().mutate(s);
}

class InjectHVXLocks : public IRMutator {
public:
    InjectHVXLocks(const Target &t)
        : target(t) {
        uses_hvx_var = Variable::make(Bool(), "uses_hvx");
    }
    bool uses_hvx = false;

private:
    Expr uses_hvx_var;
    using IRMutator::visit;
    // Primarily, we do two things when we encounter a parallel for loop.
    // First, we check if the paralell for loop uses_hvx and accordingly
    // acqure_hvx_context i.e. acquire and release HVX locks.
    // Then we insert a conditional unlock before the for loop, let's call
    // this the prolog, and a conditional lock after the for loop which
    // we shall call the epilog. So the code for a parallel loop that uses
    // hvx should look like so.
    //
    // if (uses_hvx_var) {
    //     halide_qurt_hvx_unlock();
    // }
    // parallel_for {
    //     halide_qurt_hvx_lock();
    //     ...
    //     ...
    //     halide_qurt_hvx_unlock();
    // }
    // if (uses_hvx_var) {
    //     halide_qurt_hvx_lock();
    // }
    //
    // When we move up to the enclosing scope we substitute the value of uses_hvx
    // into the IR that should convert the conditionals to constants.
    Stmt visit(const For *op) override {
        if (op->for_type == ForType::Parallel) {
            bool old_uses_hvx = uses_hvx;
            uses_hvx = false;

            Stmt body = mutate(op->body);
            Stmt s;
            if (uses_hvx) {
                body = acquire_hvx_context(body, target);
                body = substitute("uses_hvx", true, body);
                Stmt new_for = For::make(op->name, op->min, op->extent, op->for_type,
                                         op->partition_policy, op->device_api, body);
                Stmt prolog =
                    IfThenElse::make(uses_hvx_var, call_halide_qurt_hvx_unlock());
                Stmt epilog =
                    IfThenElse::make(uses_hvx_var, call_halide_qurt_hvx_lock(target));
                s = Block::make({prolog, new_for, epilog});
                debug(4) << "Wrapping prolog & epilog around par loop\n"
                         << s << "\n";
            } else {
                // We do not substitute false for "uses_hvx" into the body as we do in
                // the true case because we want to defer that to an enclosing scope.
                // The logic is that in case this scope doesn't use_hvx (we are here in
                // the else because of that) then an enclosing scope might. However,
                // substituting false for "uses_hvx" at this stage will remove the
                // prolog and epilog checks that will be needed as the enclosing scope
                // uses hvx. This is exhibited by the following code structure
                //
                // for_par(z..) {//uses hvx
                //   for_par(y..) {  // doesn't use hvx
                //     for_par(x..) { // uses hvx
                //        vector code
                //     }
                //   }
                //   vector code
                // }
                // If we substitute false in the else here, we'll get
                // for_par(z.) {
                //   halide_qurt_hvx_lock();
                //   for_par(y..) {
                //     if (false) {
                //        halide_qurt_hvx_unlock(); // will get optimized away.
                //     }
                //     for_par(x..) {
                //        halide_qurt_hvx_lock();  // double lock. Not good.
                //        vector code
                //        halide_qurt_hvx_unlock();
                //     }
                //     if (false) {
                //        halide_qurt_hvx_lock();
                //     }
                //   }
                //   vector code
                //   halide_qurt_unlock
                // }
                s = For::make(op->name, op->min, op->extent, op->for_type,
                              op->partition_policy, op->device_api, body);
            }

            uses_hvx = old_uses_hvx;
            return s;
        }
        return IRMutator::visit(op);
    }
    Expr visit(const Variable *op) override {
        uses_hvx = uses_hvx || op->type.is_vector();
        return op;
    }
    Expr visit(const Ramp *op) override {
        uses_hvx = uses_hvx || op->type.is_vector();
        return op;
    }
    Expr visit(const Broadcast *op) override {
        uses_hvx = uses_hvx || op->lanes > 1;
        return op;
    }
    Expr visit(const Call *op) override {
        uses_hvx = uses_hvx || op->type.is_vector();

        if (op->name == "halide_do_par_for") {
            // If we see a call to halide_do_par_for() at this point, it should mean that
            // this statement was produced via HexagonOffload calling lower_parallel_tasks()
            // explicitly; in this case, we won't see any parallel For statements, since they've
            // all been transformed into closures already. To mirror the pattern above,
            // we need to wrap the halide_do_par_for() call with an unlock/lock pair, but
            // that's hard to do in Halide IR (we'd need to produce a Stmt to enforce the ordering,
            // and the resulting Stmt can't easily be substituted for the Expr here). Rather than
            // make fragile assumptions about the structure of the IR produced by lower_parallel_tasks(),
            // we'll use a trick: we'll define a WEAK_INLINE function, _halide_hexagon_do_par_for,
            // which simply encapsulates the unlock()/do_par_for()/lock() sequences, and swap out
            // the call here. Since it is inlined, and since uses_hvx_var gets substituted at the end,
            // we end up with LLVM IR that properly includes (or omits) the unlock/lock pair depending
            // on the final value of uses_hvx_var in this scope.

            internal_assert(op->call_type == Call::Extern);
            internal_assert(op->args.size() == 4);

            std::vector<Expr> args = op->args;
            args.push_back(cast<int>(uses_hvx_var));

            return Call::make(Int(32), "_halide_hexagon_do_par_for", args, Call::Extern);
        }
        return op;
    }

    Target target;
};

Stmt inject_hvx_lock_unlock(Stmt body, const Target &target) {
    InjectHVXLocks i(target);
    body = i.mutate(body);
    if (i.uses_hvx) {
        body = acquire_hvx_context(body, target);
    }
    body = substitute("uses_hvx", i.uses_hvx, body);
    body = simplify(body);
    return body;
}

void CodeGen_Hexagon::compile_func(const LoweredFunc &f,
                                   const string &simple_name,
                                   const string &extern_name) {
    CodeGen_Posix::begin_func(f.linkage, simple_name, extern_name, f.args);

    Stmt body = f.body;

    debug(1) << "Hexagon: Unpredicating loads and stores...\n";
    // Replace dense vector predicated loads with sloppy scalarized
    // predicates, and scalarize predicated stores
    body = sloppy_unpredicate_loads_and_stores(body);
    debug(2) << "Hexagon: Lowering after unpredicating loads/stores:\n"
             << body << "\n\n";

    if (is_hvx_v65_or_later()) {
        // Generate vscatter-vgathers before optimize_hexagon_shuffles.
        debug(1) << "Hexagon: Looking for vscatter-vgather...\n";
        body = scatter_gather_generator(body);
        debug(2) << "Hexagon: Lowering after vscatter-vgather:\n"
                 << body << "\n\n";
    }

    debug(1) << "Hexagon: Optimizing shuffles...\n";
    // vlut always indexes 64 bytes of the LUT at a time, even in 128 byte mode.
    const int lut_alignment = 64;
    body = optimize_hexagon_shuffles(body, lut_alignment);
    debug(2) << "Hexagon: Lowering after optimizing shuffles:\n"
             << body << "\n\n";

    debug(1) << "Hexagon: Aligning loads for HVX....\n";
    body = align_loads(body, target.natural_vector_size(Int(8)), 8);
    body = common_subexpression_elimination(body);
    // Don't simplify here, otherwise it will re-collapse the loads we
    // want to carry across loop iterations.
    debug(2) << "Hexagon: Lowering after aligning loads:\n"
             << body << "\n\n";

    debug(1) << "Hexagon: Carrying values across loop iterations...\n";
    // Use at most 16 vector registers for carrying values.
    body = loop_carry(body, 16);
    body = simplify(body);
    debug(2) << "Hexagon: Lowering after forwarding stores:\n"
             << body << "\n\n";

    // Optimize the IR for Hexagon.
    debug(1) << "Hexagon: Optimizing Hexagon instructions...\n";
    body = optimize_hexagon_instructions(body, target);
    debug(2) << "Hexagon: Lowering after optimizing Hexagon instructions:\n"
             << body << "\n\n";

    debug(1) << "Hexagon: Adding calls to qurt_hvx_lock, if necessary...\n";
    body = inject_hvx_lock_unlock(body, target);
    debug(2) << "Hexagon: Lowering after adding calls to qurt_hvx_lock:\n"
             << body << "\n\n";

    debug(1) << "Hexagon: function body for " << simple_name << " :\n";
    debug(1) << body << "\n";

    body.accept(this);

    CodeGen_Posix::end_func(f.args);
}

struct HvxIntrinsic {
    enum {
        BroadcastScalarsToWords = 1 << 0,  // Some intrinsics need scalar arguments
                                           // broadcasted up to 32 bits.
        v65OrLater = 1 << 1,
    };
    llvm::Intrinsic::ID id;
    halide_type_t ret_type;
    const char *name;
    halide_type_t arg_types[4];
    int flags;
};

// TODO: these should probably be declared constexpr, but that would
// require marking various halide_type_t methods as constexpr, and an
// obscure bug in MSVC2017 can cause compilation failures for them.
// The bug appears to be fixed in MSVC2019, so when we move to that
// as a baseline for Windows, this should be revisited.
halide_type_t i8 = halide_type_t(halide_type_int, 8);
halide_type_t i16 = halide_type_t(halide_type_int, 16);
halide_type_t i32 = halide_type_t(halide_type_int, 32);
halide_type_t u8 = halide_type_t(halide_type_uint, 8);
halide_type_t u16 = halide_type_t(halide_type_uint, 16);
halide_type_t u32 = halide_type_t(halide_type_uint, 32);

// Define vectors that are 1x and 2x the Hexagon HVX width --
// Note that we use placeholders here (which we fix up when processing
// the table) as we don't know the HVX width until we know the target
// we're using; this approach lets us make a compact table with static
// data, rather than having to assemble it at runtime.
constexpr int kOneX = 64 * 8;

halide_type_t i8v1 = i8.with_lanes(kOneX / 8);
halide_type_t i16v1 = i16.with_lanes(kOneX / 16);
halide_type_t i32v1 = i32.with_lanes(kOneX / 32);
halide_type_t u8v1 = u8.with_lanes(kOneX / 8);
halide_type_t u16v1 = u16.with_lanes(kOneX / 16);
halide_type_t u32v1 = u32.with_lanes(kOneX / 32);

halide_type_t i8v2 = i8v1.with_lanes(i8v1.lanes * 2);
halide_type_t i16v2 = i16v1.with_lanes(i16v1.lanes * 2);
halide_type_t i32v2 = i32v1.with_lanes(i32v1.lanes * 2);
halide_type_t u8v2 = u8v1.with_lanes(u8v1.lanes * 2);
halide_type_t u16v2 = u16v1.with_lanes(u16v1.lanes * 2);
halide_type_t u32v2 = u32v1.with_lanes(u32v1.lanes * 2);

// clang-format off
#define INTRINSIC_128B(id) llvm::Intrinsic::hexagon_V6_##id##_128B
const HvxIntrinsic intrinsic_wrappers[] = {
    // Zero/sign extension:
    {INTRINSIC_128B(vzb), u16v2, "zxt.vub", {u8v1}},
    {INTRINSIC_128B(vzh), u32v2, "zxt.vuh", {u16v1}},
    {INTRINSIC_128B(vsb), i16v2, "sxt.vb", {i8v1}},
    {INTRINSIC_128B(vsh), i32v2, "sxt.vh", {i16v1}},

    // Similar to zxt/sxt, but without deinterleaving the result.
    {INTRINSIC_128B(vunpackub), u16v2, "unpack.vub", {u8v1}},
    {INTRINSIC_128B(vunpackuh), u32v2, "unpack.vuh", {u16v1}},
    {INTRINSIC_128B(vunpackb), i16v2, "unpack.vb", {i8v1}},
    {INTRINSIC_128B(vunpackh), i32v2, "unpack.vh", {i16v1}},

    // Truncation:
    // (Yes, there really are two fs in the b versions, and 1 f in
    // the h versions.)
    {INTRINSIC_128B(vshuffeb), i8v1, "trunc.vh", {i16v2}},
    {INTRINSIC_128B(vshufeh), i16v1, "trunc.vw", {i32v2}},
    {INTRINSIC_128B(vshuffob), i8v1, "trunclo.vh", {i16v2}},
    {INTRINSIC_128B(vshufoh), i16v1, "trunclo.vw", {i32v2}},

    // Downcast with saturation:
    {INTRINSIC_128B(vsathub), u8v1, "trunc_satub.vh", {i16v2}},
    {INTRINSIC_128B(vsatwh), i16v1, "trunc_sath.vw", {i32v2}},
    {INTRINSIC_128B(vsatuwuh), u16v1, "trunc_satuh.vuw", {u32v2}},

    {INTRINSIC_128B(vroundhub), u8v1, "trunc_satub_rnd.vh", {i16v2}},
    {INTRINSIC_128B(vroundhb), i8v1, "trunc_satb_rnd.vh", {i16v2}},
    {INTRINSIC_128B(vrounduhub), u8v1, "trunc_satub_rnd.vuh", {u16v2}},
    {INTRINSIC_128B(vroundwuh), u16v1, "trunc_satuh_rnd.vw", {i32v2}},
    {INTRINSIC_128B(vroundwh), i16v1, "trunc_sath_rnd.vw", {i32v2}},
    {INTRINSIC_128B(vrounduwuh), u16v1, "trunc_satuh_rnd.vuw", {u32v2}},

    // vpack does not interleave its input.
    {INTRINSIC_128B(vpackhub_sat), u8v1, "pack_satub.vh", {i16v2}},
    {INTRINSIC_128B(vpackwuh_sat), u16v1, "pack_satuh.vw", {i32v2}},
    {INTRINSIC_128B(vpackhb_sat), i8v1, "pack_satb.vh", {i16v2}},
    {INTRINSIC_128B(vpackwh_sat), i16v1, "pack_sath.vw", {i32v2}},
    {INTRINSIC_128B(vpackeb), i8v1, "pack.vh", {i16v2}},
    {INTRINSIC_128B(vpackeh), i16v1, "pack.vw", {i32v2}},
    {INTRINSIC_128B(vpackob), i8v1, "packhi.vh", {i16v2}},
    {INTRINSIC_128B(vpackoh), i16v1, "packhi.vw", {i32v2}},

    // Widening adds. There are other instructions that add two vub and two vuh
    // but do not widen.
    // To differentiate those from the widening ones, we encode the return type
    // in the name here.
    {INTRINSIC_128B(vaddubh), u16v2, "add_vuh.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vaddhw), i32v2, "add_vw.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vadduhw), u32v2, "add_vuw.vuh.vuh", {u16v1, u16v1}},

    // Widening subtracts. There are other instructions that subtact two vub and
    // two vuh but do not widen.
    // To differentiate those from the widening ones, we encode the return type
    // in the name here.
    {INTRINSIC_128B(vsububh), i16v2, "sub_vh.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vsubhw), i32v2, "sub_vw.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vsubuhw), i32v2, "sub_vw.vuh.vuh", {u16v1, u16v1}},

    // Adds/subtract of unsigned values with saturation.
    {INTRINSIC_128B(vaddubsat), u8v1, "sat_add.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vadduhsat), u16v1, "sat_add.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vadduwsat), u32v1, "sat_add.vuw.vuw", {u32v1, u32v1}},
    {INTRINSIC_128B(vaddhsat), i16v1, "sat_add.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vaddwsat), i32v1, "sat_add.vw.vw", {i32v1, i32v1}},
    {INTRINSIC_128B(vaddubsat_dv), u8v2, "sat_add.vub.vub.dv", {u8v2, u8v2}},
    {INTRINSIC_128B(vadduhsat_dv), u16v2, "sat_add.vuh.vuh.dv", {u16v2, u16v2}},
    {INTRINSIC_128B(vadduwsat_dv), u32v2, "sat_add.vuw.vuw.dv", {u32v2, u32v2}},
    {INTRINSIC_128B(vaddhsat_dv), i16v2, "sat_add.vh.vh.dv", {i16v2, i16v2}},
    {INTRINSIC_128B(vaddwsat_dv), i32v2, "sat_add.vw.vw.dv", {i32v2, i32v2}},

    {INTRINSIC_128B(vsububsat), i8v1, "sat_sub.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vsubuhsat), i16v1, "sat_sub.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vsubhsat), i16v1, "sat_sub.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vsubwsat), i32v1, "sat_sub.vw.vw", {i32v1, i32v1}},
    {INTRINSIC_128B(vsububsat_dv), i8v2, "sat_sub.vub.vub.dv", {u8v2, u8v2}},
    {INTRINSIC_128B(vsubuhsat_dv), i16v2, "sat_sub.vuh.vuh.dv", {u16v2, u16v2}},
    {INTRINSIC_128B(vsubhsat_dv), i16v2, "sat_sub.vh.vh.dv", {i16v2, i16v2}},
    {INTRINSIC_128B(vsubwsat_dv), i32v2, "sat_sub.vw.vw.dv", {i32v2, i32v2}},

    // Absolute value:
    {INTRINSIC_128B(vabsh), u16v1, "abs.vh", {i16v1}},
    {INTRINSIC_128B(vabsw), u32v1, "abs.vw", {i32v1}},
    {INTRINSIC_128B(vabsb), u8v1, "abs.vb", {i8v1}, HvxIntrinsic::v65OrLater},

    // Absolute difference:
    {INTRINSIC_128B(vabsdiffub), u8v1, "absd.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vabsdiffuh), u16v1, "absd.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vabsdiffh), u16v1, "absd.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vabsdiffw), u32v1, "absd.vw.vw", {i32v1, i32v1}},

    // Averaging:
    {INTRINSIC_128B(vavgub), u8v1, "avg.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vavguh), u16v1, "avg.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vavguw), u32v1, "avg.vuw.vuw", {u32v1, u32v1}, HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vavgb), i8v1, "avg.vb.vb", {i8v1, i8v1}, HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vavgh), i16v1, "avg.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vavgw), i32v1, "avg.vw.vw", {i32v1, i32v1}},

    {INTRINSIC_128B(vavgubrnd), u8v1, "avg_rnd.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vavguhrnd), u16v1, "avg_rnd.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vavguwrnd), u32v1, "avg_rnd.vuw.vuw", {u32v1, u32v1}, HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vavgbrnd), i8v1, "avg_rnd.vb.vb", {i8v1, i8v1}, HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vavghrnd), i16v1, "avg_rnd.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vavgwrnd), i32v1, "avg_rnd.vw.vw", {i32v1, i32v1}},

     // This one is weird: i8_sat((u8 - u8)/2). It both saturates and averages.
    {INTRINSIC_128B(vnavgub), i8v1, "navg.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vnavgb), i8v1, "navg.vb.vb", {i8v1, i8v1}, HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vnavgh), i16v1, "navg.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vnavgw), i32v1, "navg.vw.vw", {i32v1, i32v1}},

    // Non-widening multiplication:
    {INTRINSIC_128B(vmpyih), i16v1, "mul.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vmpyihb), i16v1, "mul.vh.b", {i16v1, i8}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyiwh), i32v1, "mul.vw.h", {i32v1, i16}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyiwb), i32v1, "mul.vw.b", {i32v1, i8}, HvxIntrinsic::BroadcastScalarsToWords},

    {INTRINSIC_128B(vmpyih_acc), i16v1, "add_mul.vh.vh.vh", {i16v1, i16v1, i16v1}},
    {INTRINSIC_128B(vmpyihb_acc), i16v1, "add_mul.vh.vh.b", {i16v1, i16v1, i8}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyiwh_acc), i32v1, "add_mul.vw.vw.h", {i32v1, i32v1, i16}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyiwb_acc), i32v1, "add_mul.vw.vw.b", {i32v1, i32v1, i8}, HvxIntrinsic::BroadcastScalarsToWords},

    // Widening vector multiplication:
    {INTRINSIC_128B(vmpyubv), u16v2, "mpy.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vmpyuhv), u32v2, "mpy.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vmpybv), i16v2, "mpy.vb.vb", {i8v1, i8v1}},
    {INTRINSIC_128B(vmpyhv), i32v2, "mpy.vh.vh", {i16v1, i16v1}},

    {INTRINSIC_128B(vmpyubv_acc), u16v2, "add_mpy.vuh.vub.vub", {u16v2, u8v1, u8v1}},
    {INTRINSIC_128B(vmpyuhv_acc), u32v2, "add_mpy.vuw.vuh.vuh", {u32v2, u16v1, u16v1}},
    {INTRINSIC_128B(vmpybv_acc), i16v2, "add_mpy.vh.vb.vb", {i16v2, i8v1, i8v1}},
    {INTRINSIC_128B(vmpyhv_acc), i32v2, "add_mpy.vw.vh.vh", {i32v2, i16v1, i16v1}},

    // Inconsistencies: both are vector instructions despite the
    // missing 'v', and the signedness is indeed swapped.
    {INTRINSIC_128B(vmpybusv), i16v2, "mpy.vub.vb", {u8v1, i8v1}},
    {INTRINSIC_128B(vmpyhus), i32v2, "mpy.vh.vuh", {i16v1, u16v1}},

    {INTRINSIC_128B(vmpybusv_acc), i16v2, "add_mpy.vh.vub.vb", {i16v2, u8v1, i8v1}},
    {INTRINSIC_128B(vmpyhus_acc), i32v2, "add_mpy.vw.vh.vuh", {i32v2, i16v1, u16v1}},

    // Widening scalar multiplication:
    {INTRINSIC_128B(vmpyub), u16v2, "mpy.vub.ub", {u8v1, u8}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyuh), u32v2, "mpy.vuh.uh", {u16v1, u16}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyh), i32v2, "mpy.vh.h", {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpybus), i16v2, "mpy.vub.b", {u8v1, i8}, HvxIntrinsic::BroadcastScalarsToWords},

    {INTRINSIC_128B(vmpyub_acc), u16v2, "add_mpy.vuh.vub.ub", {u16v2, u8v1, u8}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyuh_acc), u32v2, "add_mpy.vuw.vuh.uh", {u32v2, u16v1, u16}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpybus_acc), i16v2, "add_mpy.vh.vub.b", {i16v2, u8v1, i8}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyhsat_acc), i32v2, "satw_add_mpy.vw.vh.h", {i32v2, i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords},

    // Widening vector multiplication, with horizontal reduction.
    {INTRINSIC_128B(vrmpyubv), u32v1, "add_4mpy.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vrmpybv), i32v1, "add_4mpy.vb.vb", {i8v1, i8v1}},
    {INTRINSIC_128B(vrmpybusv), i32v1, "add_4mpy.vub.vb", {i8v1, i8v1}},
    {INTRINSIC_128B(vrmpyubv_acc), u32v1, "acc_add_4mpy.vuw.vub.vub", {u32v1, u8v1, u8v1}},
    {INTRINSIC_128B(vrmpybv_acc), i32v1, "acc_add_4mpy.vw.vb.vb", {i32v1, i8v1, i8v1}},
    {INTRINSIC_128B(vrmpybusv_acc), i32v1, "acc_add_4mpy.vw.vub.vb", {i32v1, i8v1, i8v1}},

    // Widening scalar multiplication, with horizontal reduction.
    {INTRINSIC_128B(vdmpybus), i16v1, "add_2mpy.vub.b", {u8v1, i32}},
    {INTRINSIC_128B(vdmpyhb), i32v1, "add_2mpy.vh.b", {i16v1, i32}},
    {INTRINSIC_128B(vdmpybus_acc), i16v1, "acc_add_2mpy.vh.vub.b", {i16v1, u8v1, i32}},
    {INTRINSIC_128B(vdmpyhb_acc), i32v1, "acc_add_2mpy.vw.vh.b", {i32v1, i16v1, i32}},
    // Saturating versions of vdmpy.
    {INTRINSIC_128B(vdmpyhsat), i32v1, "add_2mpy.vh.h", {i16v1, i32}},
    {INTRINSIC_128B(vdmpyhsusat), i32v1, "add_2mpy.vh.uh", {i16v1, u32}},
    {INTRINSIC_128B(vdmpyhvsat), i32v1, "add_2mpy.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vmpabus), i16v2, "add_2mpy.vub.vub.b.b", {i8v2, i32}},
    {INTRINSIC_128B(vmpabus_acc), i16v2, "acc_add_2mpy.vh.vub.vub.b.b", {i16v2, i8v2, i32}},
    {INTRINSIC_128B(vmpahb), i32v2, "add_2mpy.vh.vh.b.b", {i16v2, i32}},
    {INTRINSIC_128B(vmpahb_acc), i32v2, "acc_add_2mpy.vw.vh.vh.b.b", {i32v2, i16v2, i32}},

    // TODO: These don't generate correctly because the vectors
    // aren't interleaved correctly.
    //{ vdmpybus_dv, i16v2, "add_2mpy.vub.b.dv", {u8v2, i32} },
    //{ vdmpyhb_dv, i32v2, "add_2mpy.vh.b.dv", {i16v2, i32} },
    //{ vdmpybus_dv_acc, i16v2, "acc_add_2mpy.vh.vub.b.dv", {i16v2, u8v2, i32} },
    //{ vdmpyhb_dv_acc, i32v2, "acc_add_2mpy.vw.vh.b.dv", {i32v2, i16v2, i32} },

    // vtmpy
    // TODO: These (and many vdmpy variants) should have 16-bit scalars with BroadcastScalarsToWords, so
    // we don't need to replicate the arguments in HexagonOptimize.cpp. However, this triggers opaque
    // failures in LLVM.
    {INTRINSIC_128B(vtmpybus), i16v2, "add_3mpy.vub.b", {u8v2, i32}},
    {INTRINSIC_128B(vtmpyb), i16v2, "add_3mpy.vb.b", {i8v2, i32}},
    {INTRINSIC_128B(vtmpyhb), i32v2, "add_3mpy.vh.b", {u16v2, i32}},
    {INTRINSIC_128B(vtmpybus_acc), i16v2, "acc_add_3mpy.vh.vub.b", {i16v2, u8v2, i32}},
    {INTRINSIC_128B(vtmpyb_acc), i16v2, "acc_add_3mpy.vh.vb.b", {i16v2, i8v2, i32}},
    {INTRINSIC_128B(vtmpyhb_acc), i32v2, "acc_add_3mpy.vw.vh.b", {i32v2, u16v2, i32}},

    {INTRINSIC_128B(vrmpybus), i32v1, "add_4mpy.vub.b", {u8v1, i32}},
    {INTRINSIC_128B(vrmpyub), u32v1, "add_4mpy.vub.ub", {u8v1, u32}},
    {INTRINSIC_128B(vrmpybus_acc), i32v1, "acc_add_4mpy.vw.vub.b", {i32v1, u8v1, i32}},
    {INTRINSIC_128B(vrmpyub_acc), u32v1, "acc_add_4mpy.vuw.vub.ub", {u32v1, u8v1, u32}},

    // Multiply keep high half, with multiplication by 2.
    {INTRINSIC_128B(vmpyhvsrs), i16v1, "trunc_satw_mpy2_rnd.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vmpyhss), i16v1, "trunc_satw_mpy2.vh.h", {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords},
    {INTRINSIC_128B(vmpyhsrs), i16v1, "trunc_satw_mpy2_rnd.vh.h", {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords},

    // Min/max:
    {INTRINSIC_128B(vmaxub), u8v1, "max.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vmaxuh), u16v1, "max.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vmaxh), i16v1, "max.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vmaxw), i32v1, "max.vw.vw", {i32v1, i32v1}},

    {INTRINSIC_128B(vminub), u8v1, "min.vub.vub", {u8v1, u8v1}},
    {INTRINSIC_128B(vminuh), u16v1, "min.vuh.vuh", {u16v1, u16v1}},
    {INTRINSIC_128B(vminh), i16v1, "min.vh.vh", {i16v1, i16v1}},
    {INTRINSIC_128B(vminw), i32v1, "min.vw.vw", {i32v1, i32v1}},

    // Shifts
    // We map arithmetic and logical shifts to just "shr", depending on type.
    {INTRINSIC_128B(vlsrhv), u16v1, "shr.vuh.vh", {u16v1, u16v1}},
    {INTRINSIC_128B(vlsrwv), u32v1, "shr.vuw.vw", {u32v1, u32v1}},
    {INTRINSIC_128B(vasrhv), i16v1, "shr.vh.vh", {i16v1, u16v1}},
    {INTRINSIC_128B(vasrwv), i32v1, "shr.vw.vw", {i32v1, u32v1}},

    // Rounding shift right
    {INTRINSIC_128B(vasrhubrndsat), u8v1, "trunc_satub_shr_rnd.vh", {i16v2, u16}},
    {INTRINSIC_128B(vasrhbrndsat), i8v1, "trunc_satb_shr_rnd.vh", {i16v2, u16}},
    {INTRINSIC_128B(vasruhubrndsat), u8v1, "trunc_satub_shr_rnd.vuh", {u16v2, u16}, HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vasrwuhrndsat), u16v1, "trunc_satuh_shr_rnd.vw", {i32v2, u32}},
    {INTRINSIC_128B(vasrwhrndsat), i16v1, "trunc_sath_shr_rnd.vw", {i32v2, u32}},
    {INTRINSIC_128B(vasruwuhrndsat), u16v1, "trunc_satuh_shr_rnd.vuw", {u32v2, u32}},

    {INTRINSIC_128B(vaslhv), u16v1, "shl.vuh.vh", {u16v1, u16v1}},
    {INTRINSIC_128B(vaslwv), u32v1, "shl.vuw.vw", {u32v1, u32v1}},
    {INTRINSIC_128B(vaslhv), i16v1, "shl.vh.vh", {i16v1, u16v1}},
    {INTRINSIC_128B(vaslwv), i32v1, "shl.vw.vw", {i32v1, u32v1}},

    {INTRINSIC_128B(vlsrh), u16v1, "shr.vuh.h", {u16v1, u16}},
    {INTRINSIC_128B(vlsrw), u32v1, "shr.vuw.w", {u32v1, u32}},
    {INTRINSIC_128B(vasrh), i16v1, "shr.vh.h", {i16v1, u16}},
    {INTRINSIC_128B(vasrw), i32v1, "shr.vw.w", {i32v1, u32}},

    {INTRINSIC_128B(vaslh), u16v1, "shl.vuh.h", {u16v1, u16}},
    {INTRINSIC_128B(vaslw), u32v1, "shl.vuw.w", {u32v1, u32}},
    {INTRINSIC_128B(vaslh), i16v1, "shl.vh.h", {i16v1, u16}},
    {INTRINSIC_128B(vaslw), i32v1, "shl.vw.w", {i32v1, u32}},
    {INTRINSIC_128B(vasrh_acc), i16v1, "add_shr.vh.vh.uh", {i16v1, i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords | HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vaslh_acc), i16v1, "add_shl.vh.vh.uh", {i16v1, i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords | HvxIntrinsic::v65OrLater},
    {INTRINSIC_128B(vasrw_acc), i32v1, "add_shr.vw.vw.uw", {i32v1, i32v1, i32}},
    {INTRINSIC_128B(vaslw_acc), i32v1, "add_shl.vw.vw.uw", {i32v1, i32v1, i32}},

    {INTRINSIC_128B(vasrwh), i16v1, "trunc_shr.vw.uw", {i32v2, u32}},
    {INTRINSIC_128B(vasrhubsat), u8v1, "trunc_satub_shr.vh.uh", {i16v2, u16}},
    {INTRINSIC_128B(vasrwuhsat), u16v1, "trunc_satuh_shr.vw.uw", {i32v2, u32}},
    {INTRINSIC_128B(vasrwhsat), i16v1, "trunc_sath_shr.vw.uw", {i32v2, u32}},
    {INTRINSIC_128B(vror), u8v1, "vror", {u8v1, i32}},

    // Bit counting
    {INTRINSIC_128B(vnormamth), u16v1, "cls.vh", {u16v1}},
    {INTRINSIC_128B(vnormamtw), u32v1, "cls.vw", {u32v1}},
};
// clang-format on

// TODO: Many variants of the above functions are missing. They
// need to be implemented in the runtime module, or via
// fall-through to CodeGen_LLVM.

void CodeGen_Hexagon::init_module() {
    CodeGen_Posix::init_module();

    // LLVM's HVX vector intrinsics don't include the type of the
    // operands, they all operate on vectors of 32 bit integers. To make
    // it easier to generate code, we define wrapper intrinsics with
    // the correct type (plus the necessary bitcasts).
    const auto fix_lanes = [&](const halide_type_t &t) -> halide_type_t {
        if (t.lanes == 1) {
            return t;
        }
        const int lanes_actual = ((int)t.lanes * native_vector_bits()) / kOneX;
        return t.with_lanes(lanes_actual);
    };

    vector<Type> arg_types;
    for (const HvxIntrinsic &i : intrinsic_wrappers) {
        llvm::Intrinsic::ID id = i.id;
        internal_assert(id != llvm::Intrinsic::not_intrinsic);
        // Get the real intrinsic.
        llvm::Function *intrin = llvm::Intrinsic::getOrInsertDeclaration(module.get(), id);
        halide_type_t ret_type = fix_lanes(i.ret_type);
        arg_types.clear();
        for (const auto &a : i.arg_types) {
            if (a.bits == 0) {
                break;
            }
            arg_types.emplace_back(fix_lanes(a));
        }
        define_hvx_intrinsic(intrin, ret_type, i.name, arg_types, i.flags);
    }
}

llvm::Function *CodeGen_Hexagon::define_hvx_intrinsic(llvm::Function *intrin,
                                                      Type ret_ty,
                                                      const string &name,
                                                      vector<Type> arg_types,
                                                      int flags) {
    internal_assert(intrin) << "Null definition for intrinsic '" << name << "'\n";
    llvm::FunctionType *intrin_ty = intrin->getFunctionType();
    bool broadcast_scalar_word = flags & HvxIntrinsic::BroadcastScalarsToWords;
    bool v65OrLater = flags & HvxIntrinsic::v65OrLater;

    if (v65OrLater && !is_hvx_v65_or_later()) {
        return nullptr;
    }

    // Get the types of the arguments we want to pass.
    vector<llvm::Type *> llvm_arg_types;
    llvm_arg_types.reserve(arg_types.size());
    for (Type i : arg_types) {
        llvm_arg_types.push_back(llvm_type_of(i));
    }

    // Make a wrapper intrinsic.
    llvm::FunctionType *wrapper_ty =
        llvm::FunctionType::get(llvm_type_of(ret_ty), llvm_arg_types, false);
    llvm::Function *wrapper =
        llvm::Function::Create(wrapper_ty, llvm::GlobalValue::InternalLinkage,
                               "halide.hexagon." + name, module.get());
    llvm::BasicBlock *block =
        llvm::BasicBlock::Create(module->getContext(), "entry", wrapper);
    IRBuilderBase::InsertPoint here = builder->saveIP();
    builder->SetInsertPoint(block);

    vector<Value *> args;
    for (Value &arg : wrapper->args()) {
        args.push_back(&arg);
    }

    if (args.size() + 1 == intrin_ty->getNumParams()) {
        // This intrinsic needs the first argument split into the high and low
        // vectors.
        Value *dv = args[0];
        int vec_lanes = native_vector_bits() / arg_types[0].bits();
        Value *low = slice_vector(dv, 0, vec_lanes);
        Value *high = slice_vector(dv, vec_lanes, vec_lanes);

        args[0] = high;
        args.insert(args.begin() + 1, low);

        Type split_type =
            arg_types.front().with_lanes(arg_types.front().lanes() / 2);
        arg_types[0] = split_type;
        arg_types.insert(arg_types.begin() + 1, split_type);
    }

    // Replace args with bitcasts if necessary.
    internal_assert(args.size() == intrin_ty->getNumParams());
    for (size_t i = 0; i < args.size(); i++) {
        llvm::Type *arg_ty = intrin_ty->getParamType(i);
        if (args[i]->getType() != arg_ty) {
            if (arg_ty->isVectorTy()) {
                args[i] = builder->CreateBitCast(args[i], arg_ty);
            } else {
                if (broadcast_scalar_word) {
                    llvm::Function *fn = nullptr;
                    // We know it is a scalar type. We can have 8 bit, 16 bit or 32 bit
                    // types only.
                    unsigned bits = arg_types[i].bits();
                    const char *fn_name = "";
                    switch (bits) {
                    case 8:
                        fn_name = "halide.hexagon.dup4.b";
                        break;
                    case 16:
                        fn_name = "halide.hexagon.dup2.h";
                        break;
                    default:
                        internal_error
                            << "unhandled broadcast_scalar_word in define_hvx_intrinsic";
                    }
                    fn = module->getFunction(fn_name);
                    internal_assert(fn) << "Unable to find function " << fn_name << " in define_hvx_intrinsic.";
                    args[i] = builder->CreateCall(fn, {args[i]});
                } else if (args[i]->getType()->isIntegerTy()) {
                    args[i] =
                        builder->CreateIntCast(args[i], arg_ty, arg_types[i].is_int());
                } else {
                    args[i] = builder->CreateBitCast(args[i], arg_ty);
                }
            }
        }
    }

    // Call the real intrinsic.
    Value *ret = builder->CreateCall(intrin, args);

    // Cast the result, if necessary.
    if (ret->getType() != wrapper_ty->getReturnType()) {
        ret = builder->CreateBitCast(ret, wrapper_ty->getReturnType());
    }

    builder->CreateRet(ret);

    // Always inline these wrappers.
    wrapper->addFnAttr(llvm::Attribute::AlwaysInline);

    builder->restoreIP(here);

    llvm::verifyFunction(*wrapper);
    return wrapper;
}

Value *CodeGen_Hexagon::create_bitcast(Value *v, llvm::Type *ty) {
    if (BitCastInst *c = dyn_cast<BitCastInst>(v)) {
        return create_bitcast(c->getOperand(0), ty);
    } else if (isa<PoisonValue>(v)) {
        return PoisonValue::get(ty);
    } else if (v->getType() != ty) {
        v = builder->CreateBitCast(v, ty);
    }
    return v;
}

Value *CodeGen_Hexagon::call_intrin_cast(llvm::Type *ret_ty, llvm::Function *F,
                                         vector<Value *> Ops) {
    llvm::FunctionType *FType = F->getFunctionType();
    internal_assert(FType->getNumParams() == Ops.size());
    for (unsigned I = 0; I < FType->getNumParams(); ++I) {
        Ops[I] = create_bitcast(Ops[I], FType->getParamType(I));
    }
    Value *ret = builder->CreateCall(F, Ops);
    return create_bitcast(ret, ret_ty);
}

Value *CodeGen_Hexagon::call_intrin_cast(llvm::Type *ret_ty, int id,
                                         vector<Value *> Ops) {
    llvm::Function *intrin = llvm::Intrinsic::getOrInsertDeclaration(module.get(), (llvm::Intrinsic::ID)id);
    return call_intrin_cast(ret_ty, intrin, std::move(Ops));
}

Value *CodeGen_Hexagon::interleave_vectors(const vector<llvm::Value *> &v) {
    llvm::Type *v_ty = v[0]->getType();
    llvm::Type *element_ty = get_vector_element_type(v_ty);
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements =
        native_vector_bits() / element_ty->getScalarSizeInBits();
    int result_elements = get_vector_num_elements(v_ty) * v.size();
    if (v.size() == 2) {
        // Interleaving two vectors.
        Value *a = v[0];
        Value *b = v[1];

        if (result_elements == native_elements &&
            (element_bits == 8 || element_bits == 16)) {
            llvm::Type *native_ty = get_vector_type(element_ty, native_elements);
            // This is an interleave of two half native vectors, use
            // vshuff.
            llvm::Intrinsic::ID vshuff = element_bits == 8 ? INTRINSIC_128B(vshuffb) : INTRINSIC_128B(vshuffh);
            return call_intrin_cast(native_ty, vshuff,
                                    {concat_vectors({a, b})});
        } else {
            // Break them into native vectors, use vshuffvdd, and
            // concatenate the shuffled results.
            llvm::Type *native2_ty = get_vector_type(element_ty, native_elements * 2);
            Value *bytes = codegen(-(element_bits / 8));
            vector<Value *> ret;
            for (int i = 0; i < result_elements / 2; i += native_elements) {
                Value *a_i = slice_vector(a, i, native_elements);
                Value *b_i = slice_vector(b, i, native_elements);
                Value *ret_i = call_intrin_cast(
                    native2_ty,
                    INTRINSIC_128B(vshuffvdd),
                    {b_i, a_i, bytes});
                if ((i + native_elements) * 2 > result_elements) {
                    // This is the last vector, and it has some extra
                    // elements. Slice it down.
                    ret_i = slice_vector(ret_i, 0, result_elements - i * 2);
                }
                ret.push_back(ret_i);
            }
            return concat_vectors(ret);
        }
    } else if (v.size() == 3) {
        // Interleaving 3 vectors - this generates awful code if we let LLVM do it,
        // so we use vdelta.
        Value *lut = concat_vectors(v);

        std::vector<int> indices;
        for (int i = 0; i < get_vector_num_elements(v_ty); i++) {
            for (size_t j = 0; j < v.size(); j++) {
                indices.push_back(j * get_vector_num_elements(v_ty) + i);
            }
        }

        return vdelta(lut, indices);
    }
    return CodeGen_Posix::interleave_vectors(v);
}

// Check if indices form a strided ramp, allowing undef elements to
// pretend to be part of the ramp.
bool is_strided_ramp(const vector<int> &indices, int &start, int &stride) {
    int size = static_cast<int>(indices.size());

    // To find the proposed start and stride, find two non-undef elements.
    int x0 = -1;
    int x1 = -1;
    for (int i = 0; i < size; i++) {
        if (indices[i] != -1) {
            if (x0 == -1) {
                x0 = i;
            } else {
                x1 = i;
                break;
            }
        }
    }

    if (x1 == -1) {
        // If we don't have enough non-undef elements, we can pretend
        // the ramp is anything we want!
        stride = 1;
        start = x0 != -1 ? indices[x0] - x0 : 0;
        return true;
    }

    int dx = x1 - x0;
    int dy = indices[x1] - indices[x0];
    stride = dy / dx;
    start = indices[x0] - stride * x0;

    // Verify that all of the non-undef elements are part of the strided ramp.
    for (int i = 0; i < size; i++) {
        if (indices[i] != -1 && indices[i] != start + i * stride) {
            return false;
        }
    }
    return true;
}

bool is_concat_or_slice(const vector<int> &indices) {
    // Skip undef elements at the beginning and the end.
    size_t begin = 0;
    while (begin < indices.size() && indices[begin] == -1) {
        ++begin;
    }
    size_t end = indices.size();
    while (end > 1 && indices[end - 1] == -1) {
        --end;
    }

    // Check that the remaining elements are a dense ramp.
    for (size_t i = begin; i + 1 < end; i++) {
        if (indices[i] + 1 != indices[i + 1]) {
            return false;
        }
    }

    return true;
}

Value *CodeGen_Hexagon::shuffle_vectors(Value *a, Value *b,
                                        const vector<int> &indices) {
    llvm::Type *a_ty = a->getType();
    llvm::Type *b_ty = b->getType();
    internal_assert(a_ty == b_ty);

    int a_elements = get_vector_num_elements(a_ty);

    llvm::Type *element_ty = get_vector_element_type(a->getType());
    internal_assert(element_ty);
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements = native_vector_bits() / element_bits;
    llvm::Type *native_ty = get_vector_type(element_ty, native_elements);
    llvm::Type *native2_ty = get_vector_type(element_ty, native_elements * 2);

    int result_elements = static_cast<int>(indices.size());
    internal_assert(result_elements > 0);
    llvm::Type *result_ty = get_vector_type(element_ty, result_elements);

    // Try to rewrite shuffles that only access the elements of b.
    int min = indices[0];
    for (size_t i = 1; i < indices.size(); i++) {
        if (indices[i] != -1 && indices[i] < min) {
            min = indices[i];
        }
    }
    if (min >= a_elements) {
        vector<int> shifted_indices(indices);
        for (int &i : shifted_indices) {
            if (i != -1) {
                i -= a_elements;
            }
        }
        return shuffle_vectors(b, shifted_indices);
    }

    // Try to rewrite shuffles that only access the elements of a.
    int max = *std::max_element(indices.begin(), indices.end());
    if (max < a_elements) {
        BitCastInst *a_cast = dyn_cast<BitCastInst>(a);
        CallInst *a_call = dyn_cast<CallInst>(a_cast ? a_cast->getOperand(0) : a);
        llvm::Function *vcombine = llvm::Intrinsic::getOrInsertDeclaration(module.get(), INTRINSIC_128B(vcombine));
        if (a_call && a_call->getCalledFunction() == vcombine) {
            // Rewrite shuffle(vcombine(a, b), x) to shuffle(a, b)
            return shuffle_vectors(
                create_bitcast(a_call->getArgOperand(1), native_ty),
                create_bitcast(a_call->getArgOperand(0), native_ty), indices);
        } else if (ShuffleVectorInst *a_shuffle = dyn_cast<ShuffleVectorInst>(a)) {
            bool is_identity = true;
            for (int i = 0; i < a_elements; i++) {
                int mask_i = a_shuffle->getMaskValue(i);
                is_identity = is_identity && (mask_i == i || mask_i == -1);
            }
            if (is_identity) {
                return shuffle_vectors(a_shuffle->getOperand(0),
                                       a_shuffle->getOperand(1), indices);
            }
        }
    }

    // Try to rewrite shuffles of (maybe strided) ramps.
    int start = 0, stride = 0;
    if (!is_strided_ramp(indices, start, stride)) {
        if (is_concat_or_slice(indices)) {
            // Let LLVM handle concat or slices.
            return CodeGen_Posix::shuffle_vectors(a, b, indices);
        }
        return vdelta(concat_vectors({a, b}), indices);
    }

    if (stride == 1) {
        if (result_ty == native2_ty && a_ty == native_ty && b_ty == native_ty) {
            // This is a concatenation of a and b, where a and b are
            // native vectors. Use vcombine.
            internal_assert(start == 0);
            return call_intrin_cast(native2_ty,
                                    INTRINSIC_128B(vcombine),
                                    {b, a});
        }
        if (result_ty == native_ty && a_ty == native2_ty && max < a_elements) {
            // Extract a and b from a double vector.
            b = call_intrin_cast(native_ty, INTRINSIC_128B(hi), {a});
            a = call_intrin_cast(native_ty, INTRINSIC_128B(lo), {a});
            a_ty = a->getType();
            b_ty = b->getType();
            a_elements = get_vector_num_elements(a_ty);
        }
        if (start == 0 && result_ty == a_ty) {
            return a;
        }
        if (start == a_elements && result_ty == b_ty) {
            return b;
        }
        if (result_ty == native_ty && a_ty == native_ty && b_ty == native_ty) {
            // Use valign to select a subset of the concatenation of a
            // and b.
            int bytes_off = start * (element_bits / 8);
            int reverse_bytes = (native_vector_bits() / 8) - bytes_off;
            llvm::Intrinsic::ID intrin_id =
                INTRINSIC_128B(valignb);
            // v(l)align is a bit more efficient if the offset fits in
            // 3 bits, so if the offset is with in 3 bits from the
            // high end, use vlalign instead.
            if (bytes_off <= 7) {
                intrin_id = INTRINSIC_128B(valignbi);
            } else if (reverse_bytes <= 7) {
                intrin_id = INTRINSIC_128B(vlalignbi);
                bytes_off = reverse_bytes;
            }
            return call_intrin_cast(native_ty, intrin_id, {b, a, codegen(bytes_off)});
        }
        return CodeGen_Posix::shuffle_vectors(a, b, indices);
    } else if (stride == 2 && (start == 0 || start == 1)) {
        // For stride 2 shuffles, we can use vpack or vdeal.
        // It's hard to use call_intrin here. We'll just slice and
        // concat manually.
        Value *ab = max < a_elements ? a : concat_vectors({a, b});
        vector<Value *> ret;
        for (int i = 0; i < result_elements; i += native_elements) {
            Value *ab_i0 = slice_vector(ab, i * 2, native_elements);
            Value *ab_i1 = slice_vector(ab, i * 2 + native_elements, native_elements);
            Value *ret_i;
            if (element_bits == 8) {
                llvm::Intrinsic::ID intrin = start == 0 ? INTRINSIC_128B(vpackeb) : INTRINSIC_128B(vpackob);
                ret_i =
                    call_intrin_cast(native_ty, intrin, {ab_i1, ab_i0});
            } else if (element_bits == 16) {
                llvm::Intrinsic::ID intrin = start == 0 ? INTRINSIC_128B(vpackeh) : INTRINSIC_128B(vpackoh);
                ret_i =
                    call_intrin_cast(native_ty, intrin, {ab_i1, ab_i0});
            } else if (element_bits % 8 == 0) {
                // Need to use vdealw, followed by lo/hi.
                // TODO: Is there a better instruction? This generates a
                // double vector, then only uses half of the result.
                int element_bytes = element_bits / 8;
                Value *packed = call_intrin_cast(
                    native2_ty,
                    INTRINSIC_128B(vdealvdd),
                    {ab_i1, ab_i0, ConstantInt::get(i32_t, -element_bytes)});
                llvm::Intrinsic::ID intrin = start == 0 ? INTRINSIC_128B(lo) : INTRINSIC_128B(hi);
                ret_i = call_intrin_cast(native_ty, intrin, {packed});
            } else {
                return CodeGen_Posix::shuffle_vectors(a, b, indices);
            }
            if (i + native_elements > result_elements) {
                // This is the last vector, and it has a few extra
                // elements. Slice it down.
                ret_i = slice_vector(ret_i, 0, result_elements - i);
            }
            ret.push_back(ret_i);
        }
        return concat_vectors(ret);
    }

    // Use a general delta operation.
    return vdelta(concat_vectors({a, b}), indices);
}

Value *CodeGen_Hexagon::vlut256(Value *lut, Value *idx, int min_index,
                                int max_index) {
    llvm::Type *lut_ty = lut->getType();
    llvm::Type *idx_ty = idx->getType();

    internal_assert(isa<VectorType>(lut_ty));
    internal_assert(isa<VectorType>(idx_ty));
    internal_assert(idx_ty->getScalarSizeInBits() == 8);
    internal_assert(min_index >= 0);
    internal_assert(max_index < 256);

    llvm::Intrinsic::ID vlut, vlut_acc, vshuff;
    if (lut_ty->getScalarSizeInBits() == 8) {
        // We can use vlut32.
        vlut = INTRINSIC_128B(vlutvvb);
        vlut_acc = INTRINSIC_128B(vlutvvb_oracc);
        vshuff = INTRINSIC_128B(vshuffb);
    } else {
        // We can use vlut16.
        vlut = INTRINSIC_128B(vlutvwh);
        vlut_acc = INTRINSIC_128B(vlutvwh_oracc);
        vshuff = INTRINSIC_128B(vshuffh);
    }

    // There are two dimensions in which we need to slice up the
    // inputs. First, if the index is larger than a native vector, we
    // need to slice up the operation into native vectors of
    // indices. Second, the LUT may need to be broken into several
    // stages, and that may need to be further broken up into select
    // operations.

    // Split up the LUT into native vectors, using the max_index to
    // indicate how many we need.
    max_index =
        std::min(max_index, get_vector_num_elements(lut_ty) - 1);
    int native_idx_elements = native_vector_bits() / 8;
    int native_lut_elements =
        native_vector_bits() / lut_ty->getScalarSizeInBits();

    // The vlut instructions work on pairs of LUTs interleaved, with
    // each lut containing lut_slice_elements. We need to interleave
    // pairs of the native LUTs to make a full set of native LUTs.
    vector<Value *> lut_slices;
    for (int i = 0; i <= max_index; i += native_lut_elements) {
        Value *lut_slice = slice_vector(lut, i, native_lut_elements);
        lut_slice = call_intrin_cast(lut_slice->getType(), vshuff,
                                     {lut_slice});
        lut_slices.push_back(lut_slice);
    }
    internal_assert(!lut_slices.empty());

    llvm::Type *native_result_ty = get_vector_type(
        get_vector_element_type(lut_ty), native_idx_elements);

    // The result will have the same number of elements as idx.
    int idx_elements = get_vector_num_elements(idx_ty);

    // Each LUT has 1 pair of even/odd mask values for HVX 64, 2 for
    // HVX 128.  We may not need all of the passes, if the LUT has
    // fewer than half of the elements in an HVX 128 vector.
    constexpr int lut_passes = 2;

    vector<Value *> result;
    for (int i = 0; i < idx_elements; i += native_idx_elements) {
        Value *idx_i = slice_vector(idx, i, native_idx_elements);

        if (lut_ty->getScalarSizeInBits() == 16) {
            // vlut16 deinterleaves its output. We can either
            // interleave the result, or the indices.  It's slightly
            // cheaper to interleave the indices (they are single
            // vectors, vs. the result which is a double vector), and
            // if the indices are constant (which is true for boundary
            // conditions) this should get lifted out of any loops.
            idx_i = call_intrin_cast(
                idx_i->getType(),
                INTRINSIC_128B(vshuffb), {idx_i});
        }

        Value *result_i = nullptr;
        for (int j = 0; j < static_cast<int>(lut_slices.size()); j++) {
            for (int k = 0; k < lut_passes; k++) {
                int pass_index = lut_passes * j + k;
                Value *mask[2] = {
                    ConstantInt::get(i32_t, 2 * pass_index + 0),
                    ConstantInt::get(i32_t, 2 * pass_index + 1),
                };
                if (result_i == nullptr) {
                    // The first native LUT, use vlut.
                    result_i = call_intrin_cast(native_result_ty, vlut,
                                                {idx_i, lut_slices[j], mask[0]});
                    result_i = call_intrin_cast(native_result_ty, vlut_acc,
                                                {result_i, idx_i, lut_slices[j], mask[1]});
                } else if (max_index >= pass_index * native_lut_elements / lut_passes) {
                    // Not the first native LUT, accumulate the LUT
                    // with the previous result.
                    for (Value *v : mask) {
                        result_i = call_intrin_cast(native_result_ty, vlut_acc,
                                                    {result_i, idx_i, lut_slices[j], v});
                    }
                }
            }
        }

        result.push_back(result_i);
    }
    return slice_vector(concat_vectors(result), 0, idx_elements);
}

bool is_power_of_two(int x) {
    return (x & (x - 1)) == 0;
}

// vdelta and vrdelta are instructions that take an input vector and
// pass it through a network made up of levels. Each element x at each
// level i can either take the element from the previous level at the
// same position x, or take the element from the previous level at y,
// where y is x with the bit at position i flipped. This forms a
// butterfly network. vdelta and vrdelta have the same structure,
// except the ordering of the levels is flipped.

// Find a descriptor of the path between x1 and x2. To find the path
// between element x1 and element x2, the algorithm is the same for
// both vdelta and vrdelta. To get from x1 to x2, we need to take the
// switch path at level i if bit i of x1 and x2 are not the same. The
// path is an integer where the bit at position i indicates the switch
// that jumps by i elements should be on.
int generate_delta_path(int x1, int x2) {
    int result = 0;
    for (int delta = 1; x1 != x2; delta *= 2) {
        if ((x1 & delta) != (x2 & delta)) {
            result |= delta;
        }
        x1 &= ~delta;
        x2 &= ~delta;
    }
    return result;
}

// Generate the switch descriptors for a vdelta or vrdelta
// instruction. To do this, we need to generate the switch descriptors
// of each output to input path, and then make sure that none of the
// switches need conflicting settings.
bool generate_vdelta(const std::vector<int> &indices, bool reverse,
                     std::vector<int> &switches) {
    int width = (int)indices.size();
    internal_assert(is_power_of_two(width));
    switches.resize(width);

    // For each switch bit, we have a bit indicating whether we
    // already care about the switch position.
    std::vector<int> switches_used(switches.size());
    std::fill(switches.begin(), switches.end(), 0);
    std::fill(switches_used.begin(), switches_used.end(), 0);

    for (int out = 0; out < width; out++) {
        int in = indices[out];
        if (in == -1) {
            // We don't care what the output is at this index.
            continue;
        }
        int path = generate_delta_path(out, in);
        int x = out;
        // Follow the path backwards, setting the switches we need as
        // we go. This is the only place where vdelta and vrdelta
        // differ. For vdelta, we start with the small jumps, vrdelta
        // starts with the large jumps.
        int start = reverse ? (1 << 30) : 1;
        for (int delta = start; path != 0;
             delta = reverse ? delta / 2 : delta * 2) {
            int switch_state = path & delta;
            if ((switches_used[x] & delta) != 0) {
                // This switch is already set...
                if ((switches[x] & delta) != switch_state) {
                    // ... and it is set to the wrong thing. We can't represent this
                    // shuffle.
                    return false;
                }
            } else {
                // This switch is not already set, set it to the value we want, and mark
                // it used.
                switches_used[x] |= delta;
                switches[x] |= switch_state;
            }
            // Update our position in the network.
            if (switch_state) {
                x ^= delta;
            }
            path &= ~delta;
        }
    }
    return true;
}

// Try generating vdelta/vrdelta before falling back to vlut.
Value *CodeGen_Hexagon::vdelta(Value *lut, const vector<int> &indices) {
    llvm::Type *lut_ty = lut->getType();
    int lut_elements = get_vector_num_elements(lut_ty);
    llvm::Type *element_ty = get_vector_element_type(lut_ty);
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements =
        native_vector_bits() / element_ty->getScalarSizeInBits();
    int result_elements = indices.size();

    if (element_bits == 1) {
        // If this is a vector of booleans, convert it to a vector of ints,
        // do the shuffle, and convert back.
        llvm::Type *new_lut_ty = get_vector_type(i8_t, lut_elements);
        Value *i8_lut = builder->CreateIntCast(lut, new_lut_ty, true);
        Value *result = vdelta(i8_lut, indices);
        return builder->CreateIntCast(result, lut_ty, true);
    } else if (element_bits != 8) {
        // If the input is not a vector of 8 bit elements, replicate the
        // indices and cast the LUT.
        int replicate = element_bits / 8;
        internal_assert(replicate != 0);
        llvm::Type *new_lut_ty = get_vector_type(i8_t, lut_elements * replicate);
        Value *i8_lut = builder->CreateBitCast(lut, new_lut_ty);
        vector<int> i8_indices(indices.size() * replicate);
        for (size_t i = 0; i < indices.size(); i++) {
            for (int j = 0; j < replicate; j++) {
                i8_indices[i * replicate + j] = indices[i] * replicate + j;
            }
        }
        Value *result = vdelta(i8_lut, i8_indices);
        llvm::Type *result_ty = get_vector_type(get_vector_element_type(lut_ty), indices.size());
        return builder->CreateBitCast(result, result_ty);
    }

    // We can only use vdelta to produce a single native vector at a
    // time. Break the input into native vector length shuffles.
    if (result_elements != native_elements) {
        vector<llvm::Value *> ret;
        for (int i = 0; i < result_elements; i += native_elements) {
            vector<int> indices_i(native_elements);
            for (int j = 0; j < native_elements; j++) {
                if (i + j < result_elements) {
                    indices_i[j] = indices[i + j];
                } else {
                    indices_i[j] = -1;
                }
            }
            Value *ret_i = vdelta(lut, indices_i);
            if (result_elements - i < native_elements) {
                // This was a fractional vector at the end, slice the part we want.
                ret_i = slice_vector(ret_i, 0, result_elements - i);
            }
            ret.push_back(ret_i);
        }
        return concat_vectors(ret);
    }

    internal_assert(result_elements == native_elements);

    // We can only use vdelta to shuffle a single native vector of
    // input. If we have more than one, we need to break it into
    // multiple vdelta operations, and combine them with select.
    if (lut_elements != native_elements) {
        Value *ret = nullptr;
        for (int i = 0; i < lut_elements; i += native_elements) {
            Value *lut_i = slice_vector(lut, i, native_elements);
            vector<int> indices_i(native_elements);
            vector<Constant *> mask(native_elements);
            bool all_used = true;
            bool none_used = true;
            for (int j = 0; j < native_elements; j++) {
                int idx = indices[j] - i;
                if (0 <= idx && idx < native_elements) {
                    indices_i[j] = idx;
                    mask[j] = ConstantInt::get(i1_t, 1);
                    none_used = false;
                } else {
                    indices_i[j] = -1;
                    mask[j] = ConstantInt::get(i1_t, 0);
                    all_used = false;
                }
            }
            Value *ret_i = vdelta(lut_i, indices_i);
            if (all_used || ret == nullptr) {
                // If the mask is all ones, or this is the first result, we don't need
                // to preserve past results.
                ret = ret_i;
            } else if (!none_used) {
                // Create a condition value for which elements of the range are valid
                // for this index.
                ret = builder->CreateSelect(ConstantVector::get(mask), ret_i, ret);
            }
        }
        return ret;
    }

    // We now have a single native vector to native vector shuffle. Try
    // Generating a vdelta or vrdelta.
    for (bool reverse : {false, true}) {
        std::vector<int> switches;
        if (generate_vdelta(indices, reverse, switches)) {
            vector<Constant *> control_elements(switches.size());
            for (int i = 0; i < (int)switches.size(); i++) {
                control_elements[i] = ConstantInt::get(i8_t, switches[i]);
            }
            Value *control = ConstantVector::get(control_elements);
            llvm::Intrinsic::ID vdelta = reverse ? INTRINSIC_128B(vrdelta) : INTRINSIC_128B(vdelta);
            return call_intrin_cast(lut_ty, vdelta, {lut, control});
        }
    }

    // TODO: If the above fails, we might be able to use a vdelta and
    // vrdelta instruction together to implement the shuffle.
    // TODO: If the vdelta results are sparsely used, it might be
    // better to use vlut.
    return vlut(lut, indices);
}

Value *CodeGen_Hexagon::create_vector(llvm::Type *ty, int val) {
    llvm::Type *scalar_ty = ty->getScalarType();
    Constant *value = ConstantInt::get(scalar_ty, val);
    return get_splat(get_vector_num_elements(ty), value);
}

Value *CodeGen_Hexagon::vlut(Value *lut, Value *idx, int min_index, int max_index) {
    const unsigned idx_elem_size = idx->getType()->getScalarSizeInBits();
    internal_assert(idx_elem_size <= 16)
        << "Index element for lookup tables must be <= 16 bits in size.\n";
    llvm::Type *lut_ty = lut->getType();
    llvm::Type *result_ty = get_vector_type(get_vector_element_type(lut_ty),
                                            get_vector_num_elements(idx->getType()));
    const unsigned idx_elems = get_vector_num_elements(idx->getType());

    // Construct a new index with 16-bit elements.
    unsigned idx16_elems = idx_elems;
    Value *idx16 = (idx_elem_size == 8) ?
                       call_intrin(get_vector_type(i16_t, idx_elems),
                                   "halide.hexagon.unpack.vub", {idx}) :
                       idx;

    const int replicate = lut_ty->getScalarSizeInBits() / 16;
    if (replicate > 1) {
        // Replicate the LUT indices and use vlut16.
        // For LUT32: create two indices:
        //   - 2 * index
        //   - 2 * index + 1
        // Interleave the two index vectors and use vlut16 with the new index
        // vector.
        vector<Value *> indices;
        Value *replicate_val = ConstantInt::get(i8_t, replicate);
        for (int i = 0; i < replicate; i++) {
            Value *pos = ConstantInt::get(idx16->getType(), i);
            indices.emplace_back(call_intrin(idx16->getType(),
                                             "halide.hexagon.add_mul.vh.vh.b",
                                             {pos, idx16, replicate_val}));
        }
        idx16 = interleave_vectors(indices);
        idx16_elems *= replicate;
        min_index = min_index * replicate;
        max_index = (max_index + 1) * replicate - 1;
        internal_assert(max_index <= 32676)
            << "Index range for lookup table must be <= 32676\n";
        lut_ty = get_vector_type(i16_t,
                                 get_vector_num_elements(lut_ty) * replicate);
        lut = builder->CreateBitCast(lut, lut_ty);
    }

    llvm::Type *i8x_t = get_vector_type(i8_t, idx16_elems);
    llvm::Type *i16x_t = get_vector_type(i16_t, idx16_elems);
    Value *minus_one = create_vector(i16x_t, -1);

    // If we can do this with one vlut, do it now.
    if (max_index < 256) {
        // If the idx already had 8 bit elements and no replication was needed,
        // we can use idx else we need to pack idx16.
        Value *idx8 = (idx_elem_size == 16 || idx_elems != idx16_elems) ?
                          call_intrin(i8x_t, "halide.hexagon.pack.vh", {idx16}) :
                          idx;
        Value *result_val = vlut256(lut, idx8, min_index, max_index);
        return builder->CreateBitCast(result_val, result_ty);
    }
    // We need to break the index up into ranges of up to 256, and select
    // the ranges together after using vlut on each range. This vector
    // contains the result of each range, and a condition vector
    // indicating whether the result should be used.
    vector<std::pair<Value *, Value *>> ranges;
    for (int min_index_i = 0; min_index_i < max_index; min_index_i += 256) {
        // Make a vector of the indices shifted such that the min of
        // this range is at 0. Use 16-bit indices for this.
        Value *min_index_i_val = create_vector(i16x_t, min_index_i);
        Value *indices = builder->CreateSub(idx16, min_index_i_val);

        // Create a condition value for which elements of the range are valid
        // for this index.
        Value *use_index = builder->CreateICmpSGT(indices, minus_one);

        // After we've eliminated the invalid elements, we can
        // truncate to 8 bits, as vlut requires.
        indices = call_intrin(i8x_t, "halide.hexagon.pack.vh", {indices});

        int range_extent_i = std::min(max_index - min_index_i, 255);
        Value *range_i = vlut256(slice_vector(lut, min_index_i, range_extent_i),
                                 indices, 0, range_extent_i);
        ranges.emplace_back(range_i, use_index);
    }

    // TODO: This could be reduced hierarchically instead of in
    // order. However, this requires the condition for the select to be
    // quite tricky.
    Value *result = ranges[0].first;
    for (size_t i = 1; i < ranges.size(); i++) {
        result = builder->CreateSelect(ranges[i].second, ranges[i].first, result);
    }
    return builder->CreateBitCast(result, result_ty);
}

Value *CodeGen_Hexagon::vlut(Value *lut, const vector<int> &indices) {
    // TODO: We can take advantage of the fact that we know the
    // indices at compile time to implement a few
    // optimizations. First, we can avoid running the vlut
    // instructions for ranges of the LUT for which we know we don't
    // have any indices. This wil happen often for strided
    // ramps. Second, we can do the shuffling of the indices necessary
    // at compile time.
    vector<Constant *> llvm_indices;
    llvm_indices.reserve(indices.size());
    int min_index = get_vector_num_elements(lut->getType());
    int max_index = 0;
    for (int i : indices) {
        if (i != -1) {
            min_index = std::min(min_index, i);
            max_index = std::max(max_index, i);
        }
        llvm_indices.push_back(ConstantInt::get(i16_t, i));
    }

    // We use i16 indices because we can't support LUTs with more than
    // 32k elements anyways without massive stack spilling (the LUT
    // must fit in registers), and it costs some runtime performance
    // due to the conversion to 8 bit. This is also crazy and should
    // never happen.
    internal_assert(max_index < std::numeric_limits<int16_t>::max())
        << "vlut of more than 32k elements not supported \n";

    return vlut(lut, ConstantVector::get(llvm_indices), min_index, max_index);
}

Value *CodeGen_Hexagon::call_intrin(Type result_type, const string &name,
                                    vector<Expr> args, bool maybe) {
    llvm::Function *fn = module->getFunction(name);
    if (maybe && !fn) {
        return nullptr;
    }
    internal_assert(fn) << "Function '" << name << "' not found\n";
    if (get_vector_num_elements(fn->getReturnType()) * 2 <=
        result_type.lanes()) {
        // We have fewer than half as many lanes in our intrinsic as
        // we have in the call. Check to see if a double vector
        // version of this intrinsic exists.
        llvm::Function *fn2 = module->getFunction(name + ".dv");
        if (fn2) {
            fn = fn2;
        }
    }
    function_does_not_access_memory(fn);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    return CodeGen_Posix::call_intrin(result_type, get_vector_num_elements(fn->getReturnType()),
                                      fn, std::move(args));
}

Value *CodeGen_Hexagon::call_intrin(llvm::Type *result_type, const string &name,
                                    vector<Value *> args, bool maybe) {
    llvm::Function *fn = module->getFunction(name);
    if (maybe && !fn) {
        return nullptr;
    }
    internal_assert(fn) << "Function '" << name << "' not found\n";
    if (get_vector_num_elements(fn->getReturnType()) * 2 <=
        get_vector_num_elements(result_type)) {
        // We have fewer than half as many lanes in our intrinsic as
        // we have in the call. Check to see if a double vector
        // version of this intrinsic exists.
        llvm::Function *fn2 = module->getFunction(name + ".dv");
        if (fn2) {
            fn = fn2;
        }
    }
    function_does_not_access_memory(fn);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    return CodeGen_Posix::call_intrin(result_type, get_vector_num_elements(fn->getReturnType()),
                                      fn, std::move(args));
}

string CodeGen_Hexagon::mcpu_target() const {
    if (target.has_feature(Halide::Target::HVX_v68)) {
        return "hexagonv68";
    } else if (target.has_feature(Halide::Target::HVX_v66)) {
        return "hexagonv66";
    } else if (target.has_feature(Halide::Target::HVX_v65)) {
        return "hexagonv65";
    } else {
        return "hexagonv62";
    }
}

string CodeGen_Hexagon::mcpu_tune() const {
    return mcpu_target();
}

string CodeGen_Hexagon::mattrs() const {
    std::vector<std::string> attrs = {
        "+hvx-length128b",
        "+long-calls",
    };
    if (target.has_feature(Target::HVX)) {
        attrs.push_back("+hvxv" + std::to_string(isa_version));
    }
    return join_strings(attrs, ",");
}

bool CodeGen_Hexagon::use_soft_float_abi() const {
    return false;
}

int CodeGen_Hexagon::native_vector_bits() const {
    return 128 * 8;
}

Expr maybe_scalar(Expr x) {
    const Broadcast *xb = x.as<Broadcast>();
    if (xb) {
        return xb->value;
    } else {
        return x;
    }
}

void CodeGen_Hexagon::visit(const Mul *op) {
    if (op->type.is_vector()) {
        value =
            call_intrin(op->type, "halide.hexagon.mul" + type_suffix(op->a, op->b),
                        {op->a, op->b}, true /*maybe*/);
        if (value) {
            return;
        }

        // Hexagon has mostly widening multiplies. Try to find a
        // widening multiply we can use.
        // TODO: It would probably be better to just define a bunch of
        // mul.*.* functions in the runtime HVX modules so the above
        // implementation can be used unconditionally.
        value =
            call_intrin(op->type, "halide.hexagon.mpy" + type_suffix(op->a, op->b),
                        {op->a, op->b}, true /*maybe*/);
        if (value) {
            // We found a widening op, we need to narrow back
            // down. The widening multiply deinterleaved the result,
            // but the trunc operation reinterleaves.
            Type wide = op->type.widen();
            value = call_intrin(llvm_type_of(op->type),
                                "halide.hexagon.trunc" + type_suffix(wide, false),
                                {value});
            return;
        }

        // v68 has vector support for single-precision float.
        if (target.has_feature(Halide::Target::HVX_v68) &&
            op->type.is_float() && op->type.bits() == 32) {
            CodeGen_Posix::visit(op);
            return;
        }
        internal_error << "Unhandled HVX multiply " << op->a.type() << "*"
                       << op->b.type() << "\n"
                       << Expr(op) << "\n";
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Call *op) {
    internal_assert(op->is_extern() || op->is_intrinsic())
        << "Can only codegen extern calls and intrinsics\n";

    // Map Halide functions to Hexagon intrinsics, plus a boolean
    // indicating if the intrinsic has signed variants or not.
    static std::map<string, std::pair<string, bool>> functions = {
        {Call::get_intrinsic_name(Call::absd), {"halide.hexagon.absd", true}},
        {Call::get_intrinsic_name(Call::halving_add), {"halide.hexagon.avg", true}},
        {Call::get_intrinsic_name(Call::rounding_halving_add), {"halide.hexagon.avg_rnd", true}},
        {Call::get_intrinsic_name(Call::halving_sub), {"halide.hexagon.navg", true}},
        {Call::get_intrinsic_name(Call::saturating_add), {"halide.hexagon.sat_add", true}},
        {Call::get_intrinsic_name(Call::saturating_sub), {"halide.hexagon.sat_sub", true}},
    };

    if (is_native_interleave(op)) {
        internal_assert(
            op->type.lanes() % (native_vector_bits() * 2 / op->type.bits()) == 0);
    }

    if (starts_with(op->name, "halide.hexagon.")) {
        // Handle all of the intrinsics we generated in
        // hexagon_optimize.  I'm not sure why this is different than
        // letting it fall through to CodeGen_LLVM.
        value = call_intrin(op->type, op->name, op->args);
        return;
    }

    if (op->type.is_vector()) {
        auto i = functions.find(op->name);
        if (i != functions.end()) {
            string intrin = i->second.first + type_suffix(op->args, i->second.second);
            value = call_intrin(op->type, intrin, op->args, true /*maybe*/);
            if (value) {
                return;
            }
        } else if (op->is_intrinsic(Call::shift_left) ||
                   op->is_intrinsic(Call::shift_right)) {
            internal_assert(op->args.size() == 2);
            string instr = op->is_intrinsic(Call::shift_left) ? "halide.hexagon.shl" : "halide.hexagon.shr";
            Expr b = maybe_scalar(op->args[1]);
            // Make b signed. Shifts are only well defined if this wouldn't overflow.
            b = cast(b.type().with_code(Type::Int), b);
            value = call_intrin(op->type,
                                instr + type_suffix(op->args[0], b),
                                {op->args[0], b});
            return;
        } else if (op->is_intrinsic(Call::dynamic_shuffle)) {
            internal_assert(op->args.size() == 4);
            auto min_index = as_const_int(op->args[2]);
            auto max_index = as_const_int(op->args[3]);
            internal_assert(min_index && max_index);
            Value *lut = codegen(op->args[0]);
            Value *idx = codegen(op->args[1]);
            value = vlut(lut, idx, *min_index, *max_index);
            return;
        } else if (op->is_intrinsic(Call::abs)) {
            internal_assert(op->args.size() == 1);
            Type ty = op->args[0].type();
            if ((ty.is_vector() && ty.is_int())) {
                if (ty.bits() != 8 || is_hvx_v65_or_later()) {
                    value = call_intrin(op->type,
                                        "halide.hexagon.abs" + type_suffix(op->args[0]),
                                        op->args);
                    return;
                }
            }
        } else if (op->is_intrinsic(Call::cast_mask)) {
            internal_error
                << "cast_mask should already have been handled in HexagonOptimize\n";
        }
    }

    if (op->is_intrinsic(Call::prefetch)) {
        internal_assert((op->args.size() == 4) || (op->args.size() == 6))
            << "Hexagon only supports 1D or 2D prefetch\n";

        const int elem_size = op->type.bytes();
        const Expr &base_address = op->args[0];
        const Expr &base_offset = op->args[1];
        const Expr &extent0 = op->args[2];
        const Expr &stride0 = op->args[3];

        Expr width_bytes = extent0 * stride0 * elem_size;
        Expr height, stride_bytes;
        if (op->args.size() == 6) {
            const Expr &extent1 = op->args[4];
            const Expr &stride1 = op->args[5];
            height = extent1;
            stride_bytes = stride1 * elem_size;
        } else {
            height = 1;
            stride_bytes = 1;
        }

        vector<llvm::Value *> args;
        args.push_back(codegen_buffer_pointer(codegen(base_address), op->type, base_offset));
        args.push_back(codegen(width_bytes));
        args.push_back(codegen(height));
        args.push_back(codegen(stride_bytes));

        llvm::Function *prefetch_fn = module->getFunction("_halide_prefetch_2d");
        internal_assert(prefetch_fn);

        // The first argument is a pointer, which has type i8*. We
        // need to cast the argument, which might be a pointer to a
        // different type.
        llvm::Type *ptr_type = prefetch_fn->getFunctionType()->params()[0];
        args[0] = builder->CreateBitCast(args[0], ptr_type);

        builder->CreateCall(prefetch_fn, args);

        value = codegen(cast(op->type, 0));
        return;
    }

    if (op->is_intrinsic(Call::hvx_gather)) {
        internal_assert(op->args.size() == 5);
        internal_assert(op->type.bits() == 16 || op->type.bits() == 32);
        int index_lanes = op->type.lanes();
        int intrin_lanes = native_vector_bits() / op->type.bits();

        string name = "halide.hexagon.vgather";
        name += (op->type.bits() == 16) ? ".h.h" : ".w.w";
        llvm::Function *fn = module->getFunction(name);

        Value *dst_buffer = codegen(op->args[0]);
        Value *src_ptr = codegen(op->args[2]);
        Value *size = codegen(op->args[3]);
        Value *index = codegen(op->args[4]);

        // Cut up the indices into appropriately-sized pieces.
        for (int start = 0; start < index_lanes; start += intrin_lanes) {
            vector<Value *> args;
            Value *new_index = slice_vector(index, start, intrin_lanes);
            args.push_back(dst_buffer);
            args.push_back(codegen(op->args[1] + start));
            args.push_back(src_ptr);
            args.push_back(size);
            args.push_back(new_index);
            value = builder->CreateCall(fn, args);
        }
        return;
    } else if (op->is_intrinsic(Call::hvx_scatter) ||
               op->is_intrinsic(Call::hvx_scatter_acc)) {
        internal_assert(op->args.size() == 4);
        internal_assert(op->type.bits() == 16 || op->type.bits() == 32);
        int index_lanes = op->type.lanes();
        int intrin_lanes = native_vector_bits() / op->type.bits();

        string name = "halide.hexagon.vscatter";
        name += op->is_intrinsic(Call::hvx_scatter_acc) ? "_acc" : "";
        name += (op->type.bits() == 16) ? ".h.h" : ".w.w";
        llvm::Function *fn = module->getFunction(name);

        Value *src_ptr = codegen(op->args[0]);
        Value *size = codegen(op->args[1]);
        Value *index = codegen(op->args[2]);
        Value *val = codegen(op->args[3]);

        Value *args[4];
        args[0] = src_ptr;
        args[1] = size;
        // Cut up the indices into appropriately-sized pieces.
        for (int start = 0; start < index_lanes; start += intrin_lanes) {
            args[2] = slice_vector(index, start, intrin_lanes);
            args[3] = slice_vector(val, start, intrin_lanes);
            value = builder->CreateCall(fn, args);
        }
        return;
    } else if (op->is_intrinsic(Call::hvx_scatter_release)) {
        internal_assert(op->args.size() == 1);
        Value *ptr = codegen(op->args[0]);
        llvm::Function *fn = module->getFunction("halide.hexagon.scatter.release");
        value = builder->CreateCall(fn, {ptr});
        return;
    } else if (op->is_intrinsic(Call::sorted_avg) && op->type.is_vector() &&
               ((op->type.is_uint() &&
                 (op->type.bits() == 8 || op->type.bits() == 16)) ||
                (op->type.is_int() &&
                 (op->type.bits() == 16 || op->type.bits() == 32)))) {
        value = codegen(Call::make(
            op->type, "halide.hexagon.avg" + type_suffix(op->args[0], op->args[1]),
            {op->args[0], op->args[1]}, Call::PureExtern));
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Max *op) {
    if (op->type.is_vector()) {
        value =
            call_intrin(op->type, "halide.hexagon.max" + type_suffix(op->a, op->b),
                        {op->a, op->b}, true /*maybe*/);
        if (!value) {
            Expr equiv = Select::make(op->a > op->b, op->a, op->b);
            equiv = common_subexpression_elimination(equiv);
            value = codegen(equiv);
        }
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Min *op) {
    if (op->type.is_vector()) {
        value =
            call_intrin(op->type, "halide.hexagon.min" + type_suffix(op->a, op->b),
                        {op->a, op->b}, true /*maybe*/);
        if (!value) {
            Expr equiv = Select::make(op->a > op->b, op->b, op->a);
            equiv = common_subexpression_elimination(equiv);
            value = codegen(equiv);
        }
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Select *op) {
    const Broadcast *b = op->condition.as<Broadcast>();
    if (op->type.is_vector() && b && b->type.is_scalar()) {
        // Implement scalar conditions on vector values with if-then-else.
        value = codegen(Call::make(op->type, Call::if_then_else,
                                   {b->value, op->true_value, op->false_value},
                                   Call::PureIntrinsic));
    } else if (op->type.is_vector() && op->type.is_bool()) {
        // Lower selects on bools to bit math
        std::string cond_name = unique_name('c');
        Expr cond = Variable::make(op->condition.type(), cond_name);
        Expr equiv = Let::make(cond_name, op->condition,
                               (op->true_value & cond) | (op->false_value & ~cond));
        value = codegen(equiv);
    } else {
        CodeGen_Posix::visit(op);
    }
}

Value *CodeGen_Hexagon::codegen_cache_allocation_size(
    const std::string &name, Type type,
    const std::vector<Expr> &extents, int padding) {
    // Compute size from list of extents checking for overflow.

    Expr overflow = make_zero(UInt(32));
    Expr total_size = make_const(UInt(32), type.lanes() * type.bytes());

    // We'll multiply all the extents into the 32-bit value
    // total_size. We'll also track (total_size >> 24) as a 32-bit
    // value to check for overflow as we go. The loop invariant will
    // be that either the overflow Expr is non-zero, or total_size_hi
    // only occupies the bottom 8-bits. Overflow could be more simply
    // checked for using division, but that's slower at runtime. This
    // method generates much better assembly.
    Expr total_size_hi = make_zero(UInt(32));

    Expr low_mask = make_const(UInt(32), (uint32_t)(0xfffff));
    for (const auto &extent : extents) {
        Expr next_extent = cast(UInt(32), extent);

        // Update total_size >> 24. This math can't overflow due to
        // the loop invariant:
        total_size_hi *= next_extent;
        // Deal with carry from the low bits. Still can't overflow.
        total_size_hi += ((total_size & low_mask) * next_extent) >> 24;

        // Update total_size. This may overflow.
        total_size *= next_extent;

        // We can check for overflow by asserting that total_size_hi
        // is still an 8-bit number.
        overflow = overflow | (total_size_hi >> 24);
    }
    int padding_bytes = padding * type.bytes();
    overflow = overflow | (total_size + padding_bytes < total_size);
    total_size += padding_bytes;

    Expr max_size = make_const(UInt(32), target.maximum_buffer_size());
    Expr size_check = (overflow == 0) && (total_size <= max_size);

    // For constant-sized allocations this check should simplify away.
    size_check = common_subexpression_elimination(simplify(size_check));
    if (!is_const_one(size_check)) {
        create_assertion(
            codegen(size_check),
            Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                       {name, Cast::make(UInt(64), total_size),
                        Cast::make(UInt(64), max_size)},
                       Call::Extern));
    }

    total_size = simplify(total_size);
    return codegen(total_size);
}

void CodeGen_Hexagon::visit(const Allocate *alloc) {
    if (sym_exists(alloc->name)) {
        user_error << "Can't have two different buffers with the same name: "
                   << alloc->name << "\n";
    }

    if (alloc->memory_type == MemoryType::LockedCache) {
        // We are not allowing Customized memory allocation for Locked Cache
        user_assert(!alloc->new_expr.defined())
            << "Custom Expression not allowed for Memory Type Locked Cache\n";

        Value *llvm_size = nullptr;
        int32_t constant_bytes =
            Allocate::constant_allocation_size(alloc->extents, alloc->name);
        if (constant_bytes > 0) {
            constant_bytes *= alloc->type.bytes();
            llvm_size = codegen(Expr(constant_bytes));
        } else {
            llvm_size = codegen_cache_allocation_size(alloc->name, alloc->type,
                                                      alloc->extents, alloc->padding);
        }

        // Only allocate memory if the condition is true, otherwise 0.
        Value *llvm_condition = codegen(alloc->condition);
        if (llvm_size != nullptr) {
            llvm_size = builder->CreateSelect(
                llvm_condition, llvm_size, ConstantInt::get(llvm_size->getType(), 0));
        }

        Allocation allocation;
        allocation.constant_bytes = constant_bytes;
        allocation.stack_bytes = 0;
        allocation.type = alloc->type;
        allocation.ptr = nullptr;
        allocation.destructor = nullptr;
        allocation.destructor_function = nullptr;
        allocation.name = alloc->name;

        // Call Halide_Locked_Cache_Alloc
        llvm::Function *alloc_fn =
            module->getFunction("halide_locked_cache_malloc");
        internal_assert(alloc_fn)
            << "Could not find halide_locked_cache_malloc in module\n";

        llvm::Function::arg_iterator arg_iter = alloc_fn->arg_begin();
        ++arg_iter;  // skip the user context *
        llvm_size = builder->CreateIntCast(llvm_size, arg_iter->getType(), false);

        debug(4) << "Creating call to halide_locked_cache_malloc for allocation "
                 << alloc->name << " of size " << alloc->type.bytes();
        for (const Expr &e : alloc->extents) {
            debug(4) << " x " << e;
        }
        debug(4) << "\n";
        Value *args[2] = {get_user_context(), llvm_size};

        Value *call = builder->CreateCall(alloc_fn, args);
        allocation.ptr = call;

        // Assert that the allocation worked.
        Value *check = builder->CreateIsNotNull(allocation.ptr);
        if (llvm_size) {
            Value *zero_size = builder->CreateIsNull(llvm_size);
            check = builder->CreateOr(check, zero_size);
        }
        create_assertion(check, Call::make(Int(32), "halide_error_out_of_memory",
                                           std::vector<Expr>(), Call::Extern));

        std::string free_function_string;
        // Register a destructor for this allocation.
        if (alloc->free_function.empty()) {
            free_function_string = "halide_locked_cache_free";
        }
        llvm::Function *free_fn = module->getFunction(free_function_string);
        internal_assert(free_fn)
            << "Could not find " << alloc->free_function << " in module.\n";
        allocation.destructor =
            register_destructor(free_fn, allocation.ptr, OnError);
        allocation.destructor_function = free_fn;

        // Push the allocation base pointer onto the symbol table
        debug(3) << "Pushing allocation called " << alloc->name
                 << " onto the symbol table\n";
        allocations.push(alloc->name, allocation);

        sym_push(alloc->name, allocation.ptr);

        codegen(alloc->body);

        // If there was no early free, free it now.
        if (const Allocation *alloc_obj = allocations.find(alloc->name)) {
            internal_assert(alloc_obj->destructor);
            trigger_destructor(alloc_obj->destructor_function, alloc_obj->destructor);

            allocations.pop(alloc->name);
            sym_pop(alloc->name);
        }
    } else if (alloc->memory_type == MemoryType::VTCM &&
               !alloc->new_expr.defined()) {
        if (!is_hvx_v65_or_later()) {
            user_error << "VTCM store_in requires HVX_v65 or later.\n";
        }
        // Calculate size of allocation.
        Expr size = alloc->type.bytes();
        for (const auto &extent : alloc->extents) {
            size *= extent;
        }
        size += alloc->padding * alloc->type.bytes();
        Expr new_expr =
            Call::make(Handle(), "halide_vtcm_malloc", {size}, Call::Extern);
        string free_function = "halide_vtcm_free";
        Stmt new_alloc = Allocate::make(
            alloc->name, alloc->type, alloc->memory_type, alloc->extents,
            alloc->condition, alloc->body, new_expr, free_function, alloc->padding);
        new_alloc.accept(this);
    } else {
        // For all other memory types
        CodeGen_Posix::visit(alloc);
    }
}

}  // namespace

std::unique_ptr<CodeGen_Posix> new_CodeGen_Hexagon(const Target &target) {
    return std::make_unique<CodeGen_Hexagon>(target);
}

#else  // WITH_HEXAGON

std::unique_ptr<CodeGen_Posix> new_CodeGen_Hexagon(const Target &target) {
    user_error << "hexagon not enabled for this build of Halide.\n";
    return nullptr;
}

#endif  // WITH_HEXAGON

}  // namespace Internal
}  // namespace Halide
