#include <iostream>
#include <mutex>
#include <sstream>

#include "AlignLoads.h"
#include "CSE.h"
#include "CodeGen_Hexagon.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "EliminateBoolVectors.h"
#include "HexagonOptimize.h"
#include "IREquality.h"
#include "IRMatch.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "LICM.h"
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

// LLVM Hexagon HVX intrinsics are broken up into 64B and 128B versions, for example,
// llvm::Intrinsic::hexagon_V6_vaddh and llvm::Intrinsic::hexagon_V6_vaddh_128B. This
// macro selects the 64B or 128B mode depending on the value of is_128B. There's a
// further dirty hack here: these intrinsics aren't defined in LLVM older than 3.9. To
// avoid needing to #ifdef random patches of code, we just replace all LLVM intrinsics
// with not_intrinsic.
#ifdef WITH_HEXAGON

#define IPICK(is_128B, i64) (is_128B ? i64##_128B : i64)
#else
#define IPICK(is_128B, i64) (is_128B ? Intrinsic::not_intrinsic : Intrinsic::not_intrinsic)
#endif

CodeGen_Hexagon::CodeGen_Hexagon(Target t) : CodeGen_Posix(t) {
#if !(WITH_HEXAGON)
    user_error << "hexagon not enabled for this build of Halide.\n";
#endif
#if LLVM_VERSION < 50
    user_assert(!t.has_feature(Target::HVX_v62))
        << "llvm 5.0 or later is required for Hexagon v62.\n";
    user_assert(!t.has_feature(Target::HVX_v65))
        << "llvm 5.0 or later is required for Hexagon v65.\n";
    user_assert(!t.has_feature(Target::HVX_v66))
        << "llvm 5.0 or later is required for Hexagon v66.\n";
#endif
    user_assert(llvm_Hexagon_enabled) << "llvm build not configured with Hexagon target enabled.\n";
}

std::unique_ptr<llvm::Module> CodeGen_Hexagon::compile(const Module &module) {
    auto llvm_module = CodeGen_Posix::compile(module);

    // TODO: This should be set on the module itself, or some other
    // safer way to pass this through to the target specific lowering
    // passes. We set the option here (after the base class'
    // implementation of compile) because it is the last
    // Hexagon-specific code to run prior to invoking the target
    // specific lowering in LLVM, minimizing the chances of the wrong
    // flag being set for the wrong module.
    static std::once_flag set_options_once;
    std::call_once(set_options_once, []() {
        cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_ARGS",
                                    "Halide HVX internal compiler\n");

        std::vector<const char *> options = {
            "halide-hvx-be",
            // Don't put small objects into .data sections, it causes
            // issues with position independent code.
            "-hexagon-small-data-threshold=0"
        };
        cl::ParseCommandLineOptions(options.size(), options.data());
    });

    if (module.target().features_all_of({Halide::Target::HVX_128, Halide::Target::HVX_64})) {
        user_error << "Both HVX_64 and HVX_128 set at same time\n";
    }

    return llvm_module;
}

namespace {

Stmt call_halide_qurt_hvx_lock(const Target &target) {
    Expr hvx_mode = target.has_feature(Target::HVX_128) ? 128 : 64;
    Expr hvx_lock = Call::make(Int(32), "halide_qurt_hvx_lock", {hvx_mode}, Call::Extern);
    string hvx_lock_result_name = unique_name("hvx_lock_result");
    Expr hvx_lock_result_var = Variable::make(Int(32), hvx_lock_result_name);
    Stmt check_hvx_lock = LetStmt::make(hvx_lock_result_name, hvx_lock,
                                        AssertStmt::make(EQ::make(hvx_lock_result_var, 0), hvx_lock_result_var));
    return check_hvx_lock;
}
Stmt call_halide_qurt_hvx_unlock() {
    Expr hvx_unlock = Call::make(Int(32), "halide_qurt_hvx_unlock", {}, Call::Extern);
    string hvx_unlock_result_name = unique_name("hvx_unlock_result");
    Expr hvx_unlock_result_var = Variable::make(Int(32), hvx_unlock_result_name);
    Stmt check_hvx_unlock = LetStmt::make(hvx_unlock_result_name, hvx_unlock,
                                          AssertStmt::make(EQ::make(hvx_unlock_result_var, 0), hvx_unlock_result_var));
    return check_hvx_unlock;
}
// Wrap the stmt in a call to qurt_hvx_lock, calling qurt_hvx_unlock
// as a destructor if successful.
Stmt acquire_hvx_context(Stmt stmt, const Target &target) {
    // Modify the stmt to add a call to halide_qurt_hvx_lock, and
    // register a destructor to call halide_qurt_hvx_unlock.
    Stmt check_hvx_lock = call_halide_qurt_hvx_lock(target);
    Expr dummy_obj = reinterpret(Handle(), cast<uint64_t>(1));
    Expr hvx_unlock = Call::make(Int(32), Call::register_destructor,
                                 {Expr("halide_qurt_hvx_unlock_as_destructor"), dummy_obj}, Call::Intrinsic);

    stmt = Block::make(Evaluate::make(hvx_unlock), stmt);
    stmt = Block::make(check_hvx_lock, stmt);
    return stmt;
}
bool is_dense_ramp(Expr x) {
    const Ramp *r = x.as<Ramp>();
    if (!r) return false;

    return is_one(r->stride);
}

// In Hexagon, we assume that we can read one vector past the end of
// buffers. Using this assumption, this mutator replaces vector
// predicated dense loads with scalar predicated dense loads.
class SloppyUnpredicateLoads : public IRMutator2 {
    Expr visit(const Load *op) override {
        // Don't handle loads with without predicates, scalar predicates, or non-dense ramps.
        if (is_one(op->predicate) || op->predicate.as<Broadcast>() || !is_dense_ramp(op->index)) {
            return IRMutator2::visit(op);
        }

        Expr predicate = mutate(op->predicate);
        Expr index = mutate(op->index);

        // Make the predicate into a scalar that is true if any of the lanes are true.
        Expr condition = Shuffle::make({predicate}, {0});
        for (int i = 1; i < op->type.lanes(); i++) {
            condition = condition || Shuffle::make({predicate}, {i});
        }
        predicate = Broadcast::make(condition, predicate.type().lanes());

        return Load::make(op->type, op->name, index, op->image, op->param, predicate);
    }

    using IRMutator2::visit;
};

Stmt sloppy_unpredicate_loads(Stmt s) {
    return SloppyUnpredicateLoads().mutate(s);
}

class InjectHVXLocks : public IRMutator2 {
public:
    InjectHVXLocks(const Target &t) : target(t) {
        uses_hvx_var = Variable::make(Bool(), "uses_hvx");
    }
    bool uses_hvx = false;
private:
    Expr uses_hvx_var;
    using IRMutator2::visit;
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
    Stmt visit(const For *op) {
        if (op->for_type == ForType::Parallel) {
            bool old_uses_hvx = uses_hvx;
            uses_hvx = false;

            Stmt body = mutate(op->body);
            Stmt s;
            if (uses_hvx) {
                body = acquire_hvx_context(body, target);
                body = substitute("uses_hvx", true, body);
                Stmt new_for = For::make(op->name, op->min, op->extent,
                                         op->for_type, op->device_api, body);
                Stmt prolog = IfThenElse::make(uses_hvx_var,
                                               call_halide_qurt_hvx_unlock());
                Stmt epilog = IfThenElse::make(uses_hvx_var,
                                               call_halide_qurt_hvx_lock(target));
                s = Block::make({prolog, new_for, epilog});
                debug(4) << "Wrapping prolog & epilog around par loop\n" << s << "\n";
            } else {
                // We do not substitute false for "uses_hvx" into the body as we do in the true
                // case because we want to defer that to an enclosing scope. The logic is that
                // in case this scope doesn't use_hvx (we are here in the else because of that)
                // then an enclosing scope might. However, substituting false for "uses_hvx"
                // at this stage will remove the prolog and epilog checks that will be needed
                // as the enclosing scope uses hvx. This is exhibited by the following code
                // structure
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
                s = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);
            }

            uses_hvx = old_uses_hvx;
            return s;

        }
        return IRMutator2::visit(op);
    }
    Expr visit(const Variable *op) {
        uses_hvx = uses_hvx || op->type.is_vector();
        return op;
    }
    Expr visit(const Ramp *op) {
        uses_hvx = uses_hvx || op->type.is_vector();
        return op;
    }
    Expr visit(const Broadcast *op) {
        uses_hvx = uses_hvx || op->lanes > 1;
        return op;
    }
    Expr visit(const Call *op) {
        uses_hvx = uses_hvx || op->type.is_vector();
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

}// namespace

void CodeGen_Hexagon::compile_func(const LoweredFunc &f,
                                   const string &simple_name, const string &extern_name) {
    CodeGen_Posix::begin_func(f.linkage, simple_name, extern_name, f.args);

    Stmt body = f.body;

    debug(1) << "Unpredicating loads and stores...\n";
    // Before running unpredicate_loads_stores, replace dense vector
    // predicated loads with sloppy scalarized predicates.
    body = sloppy_unpredicate_loads(body);
    body = unpredicate_loads_stores(body);
    debug(2) << "Lowering after unpredicating loads/stores:\n" << body << "\n\n";

    debug(1) << "Optimizing shuffles...\n";
    // vlut always indexes 64 bytes of the LUT at a time, even in 128 byte mode.
    const int lut_alignment = 64;
    body = optimize_hexagon_shuffles(body, lut_alignment);
    debug(2) << "Lowering after optimizing shuffles:\n" << body << "\n\n";

    // Generating vtmpy before CSE and align_loads makes it easier to match
    // patterns for vtmpy.
    #if 0
    // TODO(aankit): Re-enable this after fixing complexity issue.
    debug(1) << "Generating vtmpy...\n";
    body = vtmpy_generator(body);
    debug(2) << "Lowering after generating vtmpy:\n" << body << "\n\n";
    #endif

    debug(1) << "Aligning loads for HVX....\n";
    body = align_loads(body, target.natural_vector_size(Int(8)), alignment_info);
    body = common_subexpression_elimination(body);
    // Don't simplify here, otherwise it will re-collapse the loads we
    // want to carry across loop iterations.
    debug(2) << "Lowering after aligning loads:\n" << body << "\n\n";

    debug(1) << "Carrying values across loop iterations...\n";
    // Use at most 16 vector registers for carrying values.
    body = loop_carry(body, 16);
    body = simplify(body);
    debug(2) << "Lowering after forwarding stores:\n" << body << "\n\n";

    // We can't deal with bool vectors, convert them to integer vectors.
    debug(1) << "Eliminating boolean vectors from Hexagon code...\n";
    body = eliminate_bool_vectors(body);
    debug(2) << "Lowering after eliminating boolean vectors: " << body << "\n\n";

    // Optimize the IR for Hexagon.
    debug(1) << "Optimizing Hexagon instructions...\n";
    body = optimize_hexagon_instructions(body, target);

    debug(1) << "Adding calls to qurt_hvx_lock, if necessary...\n";
    body = inject_hvx_lock_unlock(body, target);

    debug(1) << "Hexagon function body:\n";
    debug(1) << body << "\n";

    body.accept(this);

    CodeGen_Posix::end_func(f.args);
}

void CodeGen_Hexagon::init_module() {
    CodeGen_Posix::init_module();

    bool is_128B = target.has_feature(Halide::Target::HVX_128);

    Type i8 = Int(8);
    Type i16 = Int(16);
    Type i32 = Int(32);
    Type u8 = UInt(8);
    Type u16 = UInt(16);
    Type u32 = UInt(32);

    // Define vectors that are 1x and 2x the Hexagon HVX width.
    Type i8v1 = i8.with_lanes(native_vector_bits() / 8);
    Type i16v1 = i16.with_lanes(native_vector_bits() / 16);
    Type i32v1 = i32.with_lanes(native_vector_bits() / 32);
    Type u8v1 = u8.with_lanes(native_vector_bits() / 8);
    Type u16v1 = u16.with_lanes(native_vector_bits() / 16);
    Type u32v1 = u32.with_lanes(native_vector_bits() / 32);

    Type i8v2 = i8v1.with_lanes(i8v1.lanes() * 2);
    Type i16v2 = i16v1.with_lanes(i16v1.lanes() * 2);
    Type i32v2 = i32v1.with_lanes(i32v1.lanes() * 2);
    Type u8v2 = u8v1.with_lanes(u8v1.lanes() * 2);
    Type u16v2 = u16v1.with_lanes(u16v1.lanes() * 2);
    Type u32v2 = u32v1.with_lanes(u32v1.lanes() * 2);

    // LLVM's HVX vector intrinsics don't include the type of the
    // operands, they all operate on vectors of 32 bit integers. To make
    // it easier to generate code, we define wrapper intrinsics with
    // the correct type (plus the necessary bitcasts).
    struct HvxIntrinsic {
        enum {
            BroadcastScalarsToWords = 1 << 0,  // Some intrinsics need scalar arguments broadcasted up to 32 bits.
        };
        Intrinsic::ID id;
        Type ret_type;
        const char *name;
        vector<Type> arg_types;
        int flags;
    };
    vector<HvxIntrinsic> intrinsic_wrappers = {
        // Zero/sign extension:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vzb), u16v2,  "zxt.vub", {u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vzh), u32v2,  "zxt.vuh", {u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsb), i16v2,  "sxt.vb",  {i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsh), i32v2,  "sxt.vh",  {i16v1} },

        // Similar to zxt/sxt, but without deinterleaving the result.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vunpackub), u16v2, "unpack.vub", {u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vunpackuh), u32v2, "unpack.vuh", {u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vunpackb),  i16v2, "unpack.vb",  {i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vunpackh),  i32v2, "unpack.vh",  {i16v1} },

        // Truncation:
        // (Yes, there really are two fs in the b versions, and 1 f in
        // the h versions.)
        { IPICK(is_128B, Intrinsic::hexagon_V6_vshuffeb), i8v1,  "trunc.vh",  {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vshufeh),  i16v1, "trunc.vw",  {i32v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vshuffob), i8v1,  "trunclo.vh",  {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vshufoh),  i16v1, "trunclo.vw",  {i32v2} },

        // Downcast with saturation:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsathub),  u8v1,  "trunc_satub.vh",  {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsatwh),   i16v1, "trunc_sath.vw",   {i32v2} },
#if LLVM_VERSION >= 50
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsatuwuh), u16v1, "trunc_satuh.vuw",   {u32v2} },    // v62 or later
#endif

        { IPICK(is_128B, Intrinsic::hexagon_V6_vroundhub), u8v1,  "trunc_satub_rnd.vh", {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vroundhb),  i8v1,  "trunc_satb_rnd.vh",  {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vroundwuh), u16v1, "trunc_satuh_rnd.vw", {i32v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vroundwh),  i16v1, "trunc_sath_rnd.vw",  {i32v2} },

        // vpack does not interleave its input.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackhub_sat), u8v1,  "pack_satub.vh", {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackwuh_sat), u16v1, "pack_satuh.vw", {i32v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackhb_sat),  i8v1,  "pack_satb.vh",  {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackwh_sat),  i16v1, "pack_sath.vw",  {i32v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackeb),      i8v1,  "pack.vh",       {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackeh),      i16v1, "pack.vw",       {i32v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackob),      i8v1,  "packhi.vh",     {i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpackoh),      i16v1, "packhi.vw",     {i32v2} },


        // Adds/subtracts:
        // Note that we just use signed arithmetic for unsigned
        // operands, because it works with two's complement arithmetic.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddb),     i8v1,  "add.vb.vb",     {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddh),     i16v1, "add.vh.vh",     {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddw),     i32v1, "add.vw.vw",     {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddb_dv),  i8v2,  "add.vb.vb.dv",  {i8v2,  i8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddh_dv),  i16v2, "add.vh.vh.dv",  {i16v2, i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddw_dv),  i32v2, "add.vw.vw.dv",  {i32v2, i32v2} },

        // Widening adds. There are other instructions that add two vub and two vuh but do not widen.
        // To differentiate those from the widening ones, we encode the return type in the name here.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddubh), u16v2, "add_vuh.vub.vub", {u8v1, u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddhw), i32v2, "add_vw.vh.vh", {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduhw), u32v2, "add_vuw.vuh.vuh", {u16v1, u16v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubb),     i8v1,  "sub.vb.vb",     {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubh),     i16v1, "sub.vh.vh",     {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubw),     i32v1, "sub.vw.vw",     {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubb_dv),  i8v2,  "sub.vb.vb.dv",  {i8v2,  i8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubh_dv),  i16v2, "sub.vh.vh.dv",  {i16v2, i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubw_dv),  i32v2, "sub.vw.vw.dv",  {i32v2, i32v2} },

        // Widening subtracts. There are other instructions that subtact two vub and two vuh but do not widen.
        // To differentiate those from the widening ones, we encode the return type in the name here.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsububh), u16v2, "sub_vuh.vub.vub", {u8v1, u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsububh), i16v2, "sub_vh.vub.vub", {u8v1, u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubhw), i32v2, "sub_vw.vh.vh", {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubuhw), u32v2, "sub_vuw.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubuhw), i32v2, "sub_vw.vuh.vuh", {u16v1, u16v1} },


        // Adds/subtract of unsigned values with saturation.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddubsat),    u8v1,  "satub_add.vub.vub",    {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduhsat),    u16v1, "satuh_add.vuh.vuh",    {u16v1, u16v1} },
#if LLVM_VERSION >= 50
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduwsat),    u32v1, "satuw_add.vuw.vuw",    {u32v1, u32v1} },  // v62 or later
#endif
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddhsat),     i16v1, "sath_add.vh.vh",       {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddwsat),     i32v1, "satw_add.vw.vw",       {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddubsat_dv), u8v2,  "satub_add.vub.vub.dv", {u8v2,  u8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduhsat_dv), u16v2, "satuh_add.vuh.vuh.dv", {u16v2, u16v2} },
#if LLVM_VERSION >= 50
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduwsat_dv), u32v2, "satuw_add.vuw.vuw.dv", {u32v2, u32v2} },  // v62 or later
#endif
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddhsat_dv),  i16v2, "sath_add.vh.vh.dv",    {i16v2, i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddwsat_dv),  i32v2, "satw_add.vw.vw.dv",    {i32v2, i32v2} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vsububsat),    u8v1,  "satub_sub.vub.vub",    {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubuhsat),    u16v1, "satuh_sub.vuh.vuh",    {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubhsat),     i16v1, "sath_sub.vh.vh",       {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubwsat),     i32v1, "satw_sub.vw.vw",       {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsububsat_dv), u8v2,  "satub_sub.vub.vub.dv", {u8v2,  u8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubuhsat_dv), u16v2, "satuh_sub.vuh.vuh.dv", {u16v2, u16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubhsat_dv),  i16v2, "sath_sub.vh.vh.dv",    {i16v2, i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubwsat_dv),  i32v2, "satw_sub.vw.vw.dv",    {i32v2, i32v2} },

        // Absolute value:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vabsh),   u16v1, "abs.vh", {i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vabsw),   u32v1, "abs.vw", {i32v1} },

        // Absolute difference:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vabsdiffub),  u8v1,  "absd.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vabsdiffuh),  u16v1, "absd.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vabsdiffh),   u16v1, "absd.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vabsdiffw),   u32v1, "absd.vw.vw",   {i32v1, i32v1} },

        // Averaging:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavgub), u8v1,  "avg.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavguh), u16v1, "avg.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavgh),  i16v1, "avg.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavgw),  i32v1, "avg.vw.vw",   {i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vavgubrnd), u8v1,  "avg_rnd.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavguhrnd), u16v1, "avg_rnd.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavghrnd),  i16v1, "avg_rnd.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vavgwrnd),  i32v1, "avg_rnd.vw.vw",   {i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vnavgub), i8v1,  "navg.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnavgh),  i16v1, "navg.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnavgw),  i32v1, "navg.vw.vw",   {i32v1, i32v1} },

        // Non-widening multiplication:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyih),  i16v1, "mul.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyihb), i16v1, "mul.vh.b",    {i16v1, i8}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyiwh), i32v1, "mul.vw.h",    {i32v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyiwb), i32v1, "mul.vw.b",    {i32v1, i8}, HvxIntrinsic::BroadcastScalarsToWords },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyih_acc),  i16v1, "add_mul.vh.vh.vh",   {i16v1, i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyihb_acc), i16v1, "add_mul.vh.vh.b",    {i16v1, i16v1, i8}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyiwh_acc), i32v1, "add_mul.vw.vw.h",    {i32v1, i32v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyiwb_acc), i32v1, "add_mul.vw.vw.b",    {i32v1, i32v1, i8}, HvxIntrinsic::BroadcastScalarsToWords },

        // Widening vector multiplication:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyubv), u16v2, "mpy.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyuhv), u32v2, "mpy.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpybv),  i16v2, "mpy.vb.vb",   {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhv),  i32v2, "mpy.vh.vh",   {i16v1, i16v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyubv_acc), u16v2, "add_mpy.vuh.vub.vub", {u16v2, u8v1, u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyuhv_acc), u32v2, "add_mpy.vuw.vuh.vuh", {u32v2, u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpybv_acc),  i16v2, "add_mpy.vh.vb.vb",    {i16v2, i8v1, i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhv_acc),  i32v2, "add_mpy.vw.vh.vh",    {i32v2, i16v1, i16v1} },

        // Inconsistencies: both are vector instructions despite the
        // missing 'v', and the signedness is indeed swapped.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpybusv), i16v2, "mpy.vub.vb",  {u8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhus),  i32v2, "mpy.vh.vuh",  {i16v1, u16v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpybusv_acc), i16v2, "add_mpy.vh.vub.vb",  {i16v2, u8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhus_acc),  i32v2, "add_mpy.vw.vh.vuh",  {i32v2, i16v1, u16v1} },

        // Widening scalar multiplication:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyub),  u16v2, "mpy.vub.ub",  {u8v1,  u8}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyuh),  u32v2, "mpy.vuh.uh",  {u16v1, u16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyh),   i32v2, "mpy.vh.h",    {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpybus), i16v2, "mpy.vub.b",   {u8v1,  i8}, HvxIntrinsic::BroadcastScalarsToWords },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyub_acc),   u16v2, "add_mpy.vuh.vub.ub",   {u16v2, u8v1,  u8}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyuh_acc),   u32v2, "add_mpy.vuw.vuh.uh",   {u32v2, u16v1, u16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpybus_acc),  i16v2, "add_mpy.vh.vub.b",     {i16v2, u8v1,  i8}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhsat_acc), i32v2, "satw_add_mpy.vw.vh.h", {i32v2, i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },

        // Widening vector multiplication, with horizontal reduction.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpyubv),  u32v1, "add_4mpy.vub.vub", {u8v1, u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpybv),   i32v1, "add_4mpy.vb.vb",   {i8v1, i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpybusv), i32v1, "add_4mpy.vub.vb",  {i8v1, i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpyubv_acc),  u32v1, "acc_add_4mpy.vuw.vub.vub", {u32v1, u8v1, u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpybv_acc),   i32v1, "acc_add_4mpy.vw.vb.vb",   {i32v1, i8v1, i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpybusv_acc), i32v1, "acc_add_4mpy.vw.vub.vb",  {i32v1, i8v1, i8v1} },

        // Widening scalar multiplication, with horizontal reduction.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vdmpybus), i16v1, "add_2mpy.vub.b", {u8v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vdmpyhb),  i32v1, "add_2mpy.vh.b",  {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vdmpybus_acc), i16v1, "acc_add_2mpy.vh.vub.b", {i16v1, u8v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vdmpyhb_acc),  i32v1, "acc_add_2mpy.vw.vh.b",  {i32v1, i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },

        // TODO: There are also saturating versions of vdmpy.

        // TODO: These don't generate correctly because the vectors
        // aren't interleaved correctly.
        //{ IPICK(is_128B, Intrinsic::hexagon_V6_vdmpybus_dv), i16v2, "add_2mpy.vub.b.dv", {u8v2, i32} },
        //{ IPICK(is_128B, Intrinsic::hexagon_V6_vdmpyhb_dv), i32v2, "add_2mpy.vh.b.dv", {i16v2, i32} },
        //{ IPICK(is_128B, Intrinsic::hexagon_V6_vdmpybus_dv_acc), i16v2, "acc_add_2mpy.vh.vub.b.dv", {i16v2, u8v2, i32} },
        //{ IPICK(is_128B, Intrinsic::hexagon_V6_vdmpyhb_dv_acc), i32v2, "acc_add_2mpy.vw.vh.b.dv", {i32v2, i16v2, i32} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpybus), i32v1, "add_4mpy.vub.b",  {u8v1, i32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpyub),  u32v1, "add_4mpy.vub.ub", {u8v1, u32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpybus_acc), i32v1, "acc_add_4mpy.vw.vub.b",  {i32v1, u8v1, i32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vrmpyub_acc),  u32v1, "acc_add_4mpy.vuw.vub.ub", {u32v1, u8v1, u32} },

        // Multiply keep high half, with multiplication by 2.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhvsrs), i16v1, "trunc_satw_mpy2_rnd.vh.vh", {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhss), i16v1, "trunc_satw_mpy2.vh.h", {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmpyhsrs), i16v1, "trunc_satw_mpy2_rnd.vh.h", {i16v1, i16}, HvxIntrinsic::BroadcastScalarsToWords },

        // Select/conditionals. Conditions are always signed integer
        // vectors (so widening sign extends).
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmux), i8v1,  "mux.vb.vb",  {i8v1,  i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmux), i16v1, "mux.vh.vh",  {i16v1, i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmux), i32v1, "mux.vw.vw",  {i32v1, i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_veqb), i8v1,  "eq.vb.vb",  {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_veqh), i16v1, "eq.vh.vh",  {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_veqw), i32v1, "eq.vw.vw",  {i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vgtub), i8v1,  "gt.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vgtuh), i16v1, "gt.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vgtuw), i32v1, "gt.vuw.vuw", {u32v1, u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vgtb),  i8v1,  "gt.vb.vb",   {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vgth),  i16v1, "gt.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vgtw),  i32v1, "gt.vw.vw",   {i32v1, i32v1} },

        // Min/max:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmaxub), u8v1,  "max.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmaxuh), u16v1, "max.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmaxh),  i16v1, "max.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vmaxw),  i32v1, "max.vw.vw",   {i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vminub), u8v1,  "min.vub.vub", {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vminuh), u16v1, "min.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vminh),  i16v1, "min.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vminw),  i32v1, "min.vw.vw",   {i32v1, i32v1} },

        // Shifts
        // We map arithmetic and logical shifts to just "shr", depending on type.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vlsrhv), u16v1, "shr.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vlsrwv), u32v1, "shr.vuw.vuw", {u32v1, u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrhv), i16v1, "shr.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrwv), i32v1, "shr.vw.vw",   {i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslhv), u16v1, "shl.vuh.vuh", {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslwv), u32v1, "shl.vuw.vuw", {u32v1, u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslhv), i16v1, "shl.vh.vh",   {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslwv), i32v1, "shl.vw.vw",   {i32v1, i32v1} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vlsrh),  u16v1, "shr.vuh.uh", {u16v1, u16} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vlsrw),  u32v1, "shr.vuw.uw", {u32v1, u32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrh),  i16v1, "shr.vh.h",   {i16v1, i16} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrw),  i32v1, "shr.vw.w",   {i32v1, i32} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslh),  u16v1, "shl.vuh.uh", {u16v1, u16} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslw),  u32v1, "shl.vuw.uw", {u32v1, u32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslh),  i16v1, "shl.vh.h",   {i16v1, i16} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslw),  i32v1, "shl.vw.w",   {i32v1, i32} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrw_acc), i32v1, "add_shr.vw.vw.w", {i32v1, i32v1, i32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaslw_acc), i32v1, "add_shl.vw.vw.w", {i32v1, i32v1, i32} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrwh), i16v1, "trunc_shr.vw.w",   {i32v2, i32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrhubsat), u8v1, "trunc_satub_shr.vh.h",  {i16v2, i16} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrwuhsat), u16v1, "trunc_satuh_shr.vw.w", {i32v2, i32} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vasrwhsat),  i16v1, "trunc_sath_shr.vw.w",  {i32v2, i32} },

        // Bitwise operators
        { IPICK(is_128B, Intrinsic::hexagon_V6_vand),  u8v1,  "and.vb.vb",  {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vand),  u16v1, "and.vh.vh",  {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vand),  u32v1, "and.vw.vw",  {u32v1, u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vor),   u8v1,  "or.vb.vb",   {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vor),   u16v1, "or.vh.vh",   {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vor),   u32v1, "or.vw.vw",   {u32v1, u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vxor),  u8v1,  "xor.vb.vb",  {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vxor),  u16v1, "xor.vh.vh",  {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vxor),  u32v1, "xor.vw.vw",  {u32v1, u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnot),  u8v1,  "not.vb",     {u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnot),  u16v1, "not.vh",     {u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnot),  u32v1, "not.vw",     {u32v1} },

        // Broadcasts
#if LLVM_VERSION >= 50
        { IPICK(is_128B, Intrinsic::hexagon_V6_lvsplatb), u8v1,   "splat_v62.b", {u8}  },   // v62 or later
        { IPICK(is_128B, Intrinsic::hexagon_V6_lvsplath), u16v1,  "splat_v62.h", {u16} },   // v62 or later
#endif
        { IPICK(is_128B, Intrinsic::hexagon_V6_lvsplatw), u32v1,  "splat.w", {u32} },

        // Bit counting
        { IPICK(is_128B, Intrinsic::hexagon_V6_vcl0h), u16v1, "clz.vh", {u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vcl0w), u32v1, "clz.vw", {u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnormamth), u16v1, "cls.vh", {u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vnormamtw), u32v1, "cls.vw", {u32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vpopcounth), u16v1, "popcount.vh", {u16v1} },
        // TODO: If we need it, we could implement a popcountw in the
        // runtime module that uses popcounth, and horizontally add
        // each pair of lanes.
    };
    // TODO: Many variants of the above functions are missing. They
    // need to be implemented in the runtime module, or via
    // fall-through to CodeGen_LLVM.
    for (HvxIntrinsic &i : intrinsic_wrappers) {
        define_hvx_intrinsic(i.id, i.ret_type, i.name, i.arg_types,
                             i.flags & HvxIntrinsic::BroadcastScalarsToWords);
    }
}

llvm::Function *CodeGen_Hexagon::define_hvx_intrinsic(int id, Type ret_ty, const string &name,
                                                      const vector<Type> &arg_types, bool broadcast_scalar_word) {
    internal_assert(id != Intrinsic::not_intrinsic);
    // Get the real intrinsic.
    llvm::Function *intrin = Intrinsic::getDeclaration(module.get(), (llvm::Intrinsic::ID)id);
    return define_hvx_intrinsic(intrin, ret_ty, name, arg_types, broadcast_scalar_word);
}

llvm::Function *CodeGen_Hexagon::define_hvx_intrinsic(llvm::Function *intrin, Type ret_ty, const string &name,
                                                      vector<Type> arg_types, bool broadcast_scalar_word) {
    internal_assert(intrin) << "Null definition for intrinsic '" << name << "'\n";
    llvm::FunctionType *intrin_ty = intrin->getFunctionType();

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
    llvm::BasicBlock *block = llvm::BasicBlock::Create(module->getContext(), "entry", wrapper);
    IRBuilderBase::InsertPoint here = builder->saveIP();
    builder->SetInsertPoint(block);

    vector<Value *> args;
    for (Value &arg : wrapper->args()) {
        args.push_back(&arg);
    }

    if (args.size() + 1 == intrin_ty->getNumParams()) {
        // This intrinsic needs the first argument split into the high and low vectors.
        Value *dv = args[0];
        int vec_lanes = native_vector_bits()/arg_types[0].bits();
        Value *low = slice_vector(dv, 0, vec_lanes);
        Value *high = slice_vector(dv, vec_lanes, vec_lanes);

        args[0] = high;
        args.insert(args.begin() + 1, low);

        Type split_type = arg_types.front().with_lanes(arg_types.front().lanes() / 2);
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
                    // We know it is a scalar type. We can have 8 bit, 16 bit or 32 bit types only.
                    unsigned bits = arg_types[i].bits();
                    switch(bits) {
                    case 8:
                        fn = module->getFunction("halide.hexagon.dup4.b");
                        break;
                    case 16:
                        fn = module->getFunction("halide.hexagon.dup2.h");
                        break;
                    default:
                        internal_error << "unhandled broadcast_scalar_word in define_hvx_intrinsic";
                    }
                    args[i] = builder->CreateCall(fn, { args[i] });
                } else if (args[i]->getType()->isIntegerTy()) {
                    args[i] = builder->CreateIntCast(args[i], arg_ty, arg_types[i].is_int());
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
    } else if (isa<UndefValue>(v)) {
        return UndefValue::get(ty);
    } else if (v->getType() != ty) {
        v = builder->CreateBitCast(v, ty);
    }
    return v;
}

Value *CodeGen_Hexagon::call_intrin_cast(llvm::Type *ret_ty,
                                         llvm::Function *F,
                                         vector<Value *> Ops) {
    llvm::FunctionType *FType = F->getFunctionType();
    internal_assert(FType->getNumParams() == Ops.size());
    for (unsigned I = 0; I < FType->getNumParams(); ++I) {
        Ops[I] = create_bitcast(Ops[I], FType->getParamType(I));
    }
    Value *ret = builder->CreateCall(F, Ops);
    return create_bitcast(ret, ret_ty);
}

Value *CodeGen_Hexagon::call_intrin_cast(llvm::Type *ret_ty,
                                         int id,
                                         vector<Value *> Ops) {
    llvm::Function *intrin = Intrinsic::getDeclaration(module.get(), (llvm::Intrinsic::ID)id);
    return call_intrin_cast(ret_ty, intrin, Ops);
}

Value *CodeGen_Hexagon::interleave_vectors(const vector<llvm::Value *> &v) {
    bool is_128B = target.has_feature(Halide::Target::HVX_128);
    llvm::Type *v_ty = v[0]->getType();
    llvm::Type *element_ty = v_ty->getVectorElementType();
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements = native_vector_bits()/element_ty->getScalarSizeInBits();
    int result_elements = v_ty->getVectorNumElements()*v.size();
    if (v.size() == 2) {
        // Interleaving two vectors.
        Value *a = v[0];
        Value *b = v[1];

        if (result_elements == native_elements && (element_bits == 8 || element_bits == 16)) {
            llvm::Type *native_ty = llvm::VectorType::get(element_ty, native_elements);
            // This is an interleave of two half native vectors, use
            // vshuff.
            Intrinsic::ID vshuff =
                element_bits == 8 ? IPICK(is_128B, Intrinsic::hexagon_V6_vshuffb) : IPICK(is_128B, Intrinsic::hexagon_V6_vshuffh);
            return call_intrin_cast(native_ty, vshuff, {concat_vectors({a, b})});
        } else {
            // Break them into native vectors, use vshuffvdd, and
            // concatenate the shuffled results.
            llvm::Type *native2_ty = llvm::VectorType::get(element_ty, native_elements*2);
            Value *bytes = codegen(-static_cast<int>(element_bits/8));
            vector<Value *> ret;
            for (int i = 0; i < result_elements/2; i += native_elements) {
                Value *a_i = slice_vector(a, i, native_elements);
                Value *b_i = slice_vector(b, i, native_elements);
                Value *ret_i = call_intrin_cast(native2_ty,
                                                IPICK(is_128B, Intrinsic::hexagon_V6_vshuffvdd),
                                                {b_i, a_i, bytes});
                if ((i + native_elements)*2 > result_elements) {
                    // This is the last vector, and it has some extra
                    // elements. Slice it down.
                    ret_i = slice_vector(ret_i, 0, (i + native_elements)*2 - result_elements);
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
        for (unsigned i = 0; i < v_ty->getVectorNumElements(); i++) {
            for (size_t j = 0; j < v.size(); j++) {
                indices.push_back(j * v_ty->getVectorNumElements() + i);
            }
        }

        return vdelta(lut, indices);
    }
    return CodeGen_Posix::interleave_vectors(v);
}

namespace {

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
    stride = dy/dx;
    start = indices[x0] - stride*x0;

    // Verify that all of the non-undef elements are part of the strided ramp.
    for (int i = 0; i < size; i++) {
        if (indices[i] != -1 && indices[i] != start + i*stride) {
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

}  // namespace

Value *CodeGen_Hexagon::shuffle_vectors(Value *a, Value *b,
                                        const vector<int> &indices) {
    llvm::Type *a_ty = a->getType();
    llvm::Type *b_ty = b->getType();
    internal_assert(a_ty == b_ty);

    bool is_128B = target.has_feature(Halide::Target::HVX_128);
    int a_elements = static_cast<int>(a_ty->getVectorNumElements());
    int b_elements = static_cast<int>(b_ty->getVectorNumElements());

    llvm::Type *element_ty = a->getType()->getVectorElementType();
    internal_assert(element_ty);
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements = native_vector_bits() / element_bits;
    llvm::Type *native_ty = llvm::VectorType::get(element_ty, native_elements);
    llvm::Type *native2_ty = llvm::VectorType::get(element_ty, native_elements*2);

    int result_elements = static_cast<int>(indices.size());
    internal_assert(result_elements > 0);
    llvm::Type *result_ty = VectorType::get(element_ty, result_elements);

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
            if (i != -1) i -= a_elements;
        }
        return shuffle_vectors(b, UndefValue::get(b->getType()), shifted_indices);
    }

    // Try to rewrite shuffles that only access the elements of a.
    int max = *std::max_element(indices.begin(), indices.end());
    if (max < a_elements) {
        BitCastInst *a_cast = dyn_cast<BitCastInst>(a);
        CallInst *a_call = dyn_cast<CallInst>(a_cast ? a_cast->getOperand(0) : a);
        llvm::Function *vcombine =
            Intrinsic::getDeclaration(module.get(), IPICK(is_128B, Intrinsic::hexagon_V6_vcombine));
        if (a_call && a_call->getCalledFunction() == vcombine) {
            // Rewrite shuffle(vcombine(a, b), x) to shuffle(a, b)
            return shuffle_vectors(
                create_bitcast(a_call->getArgOperand(1), native_ty),
                create_bitcast(a_call->getArgOperand(0), native_ty),
                indices);
        } else if (ShuffleVectorInst *a_shuffle = dyn_cast<ShuffleVectorInst>(a)) {
            bool is_identity = true;
            for (int i = 0; i < a_elements; i++) {
                int mask_i = a_shuffle->getMaskValue(i);
                is_identity = is_identity && (mask_i == i || mask_i == -1);
            }
            if (is_identity) {
                return shuffle_vectors(
                    a_shuffle->getOperand(0),
                    a_shuffle->getOperand(1),
                    indices);
            }
        }
    }

    // Try to rewrite shuffles of (maybe strided) ramps.
    int start = 0, stride = 0;
    if (!is_strided_ramp(indices, start, stride)) {
        if (is_concat_or_slice(indices) || element_bits > 16) {
            // Let LLVM handle concat or slices.
            return CodeGen_Posix::shuffle_vectors(a, b, indices);
        }
        return vlut(concat_vectors({a, b}), indices);
    }

    if (stride == 1) {
        if (result_ty == native2_ty && a_ty == native_ty && b_ty == native_ty) {
            // This is a concatenation of a and b, where a and b are
            // native vectors. Use vcombine.
            internal_assert(start == 0);
            return call_intrin_cast(native2_ty, IPICK(is_128B, Intrinsic::hexagon_V6_vcombine), {b, a});
        }
        if (result_ty == native_ty && a_ty == native2_ty && max < a_elements) {
            // Extract a and b from a double vector.
            b = call_intrin_cast(native_ty, IPICK(is_128B, Intrinsic::hexagon_V6_hi), {a});
            a = call_intrin_cast(native_ty, IPICK(is_128B, Intrinsic::hexagon_V6_lo), {a});
            a_ty = a->getType();
            b_ty = b->getType();
            a_elements = a_ty->getVectorNumElements();
            b_elements = b_ty->getVectorNumElements();
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
            Intrinsic::ID intrin_id = IPICK(is_128B, Intrinsic::hexagon_V6_valignb);
            // v(l)align is a bit more efficient if the offset fits in
            // 3 bits, so if the offset is with in 3 bits from the
            // high end, use vlalign instead.
            if (bytes_off <= 7) {
                intrin_id = IPICK(is_128B, Intrinsic::hexagon_V6_valignbi);
            } else if (reverse_bytes <= 7) {
                intrin_id = IPICK(is_128B, Intrinsic::hexagon_V6_vlalignbi);
                bytes_off = reverse_bytes;
            }
            return call_intrin_cast(native_ty, intrin_id, {b, a, codegen(bytes_off)});
        }
        return CodeGen_Posix::shuffle_vectors(a, b, indices);
    } else if (stride == 2 && result_elements*2 == a_elements + b_elements) {
        internal_assert(start == 0 || start == 1);
        // For stride 2 shuffles, we can use vpack or vdeal.
        // It's hard to use call_intrin here. We'll just slice and
        // concat manually.
        Value *ab = max < a_elements ? a : concat_vectors({a, b});
        vector<Value *> ret;
        for (int i = 0; i < result_elements; i += native_elements) {
            Value *ab_i0 = slice_vector(ab, i*2, native_elements);
            Value *ab_i1 = slice_vector(ab, i*2 + native_elements, native_elements);
            Value *ret_i;
            if (element_bits == 8) {
                Intrinsic::ID intrin =
                    start == 0 ? IPICK(is_128B, Intrinsic::hexagon_V6_vpackeb) : IPICK(is_128B, Intrinsic::hexagon_V6_vpackob);
                ret_i = call_intrin_cast(native_ty, intrin, {ab_i1, ab_i0});
            } else if (element_bits == 16) {
                Intrinsic::ID intrin =
                    start == 0 ? IPICK(is_128B, Intrinsic::hexagon_V6_vpackeh) : IPICK(is_128B, Intrinsic::hexagon_V6_vpackoh);
                ret_i = call_intrin_cast(native_ty, intrin, {ab_i1, ab_i0});
            } else if (element_bits%8 == 0) {
                // Need to use vdealw, followed by lo/hi.
                // TODO: Is there a better instruction? This generates a
                // double vector, then only uses half of the result.
                int element_bytes = element_bits / 8;
                Value *packed = call_intrin_cast(native2_ty,
                                                 IPICK(is_128B, Intrinsic::hexagon_V6_vdealvdd),
                                                 {ab_i1, ab_i0, ConstantInt::get(i32_t, -element_bytes)});
                Intrinsic::ID intrin =
                    start == 0 ? IPICK(is_128B, Intrinsic::hexagon_V6_lo) : IPICK(is_128B, Intrinsic::hexagon_V6_hi);
                ret_i = call_intrin_cast(native_ty, intrin, {packed});
            } else {
                return CodeGen_Posix::shuffle_vectors(a, b, indices);
            }
            if (i + native_elements > result_elements) {
                // This is the last vector, and it has a few extra
                // elements. Slice it down.
                ret_i = slice_vector(ret_i, 0, i + native_elements - result_elements);
            }
            ret.push_back(ret_i);
        }
        return concat_vectors(ret);
    }

    // TODO: There are more HVX permute instructions that could be
    // implemented here, such as vdelta/vrdelta.

    if (element_bits <= 16) {
        return vlut(concat_vectors({a, b}), indices);
    } else {
        return CodeGen_Posix::shuffle_vectors(a, b, indices);
    }
}

Value *CodeGen_Hexagon::vlut(Value *lut, Value *idx, int min_index, int max_index) {
    bool is_128B = target.has_feature(Halide::Target::HVX_128);
    llvm::Type *lut_ty = lut->getType();
    llvm::Type *idx_ty = idx->getType();

    internal_assert(isa<VectorType>(lut_ty));
    internal_assert(isa<VectorType>(idx_ty));
    internal_assert(idx_ty->getScalarSizeInBits() == 8);
    internal_assert(min_index >= 0);
    internal_assert(max_index <= 255);

    Intrinsic::ID vlut_id = Intrinsic::not_intrinsic;
    Intrinsic::ID vlut_acc_id = Intrinsic::not_intrinsic;
    Intrinsic::ID vshuff_id = Intrinsic::not_intrinsic;
    if (lut_ty->getScalarSizeInBits() == 8) {
        // We can use vlut32.
        vlut_id = IPICK(is_128B, Intrinsic::hexagon_V6_vlutvvb);
        vlut_acc_id = IPICK(is_128B, Intrinsic::hexagon_V6_vlutvvb_oracc);
        vshuff_id = IPICK(is_128B, Intrinsic::hexagon_V6_vshuffb);
    } else {
        // We can use vlut16. If the LUT has greater than 16 bit
        // elements, we replicate the LUT indices.
        int replicate = lut_ty->getScalarSizeInBits() / 16;
        if (replicate > 1) {
            // TODO: Reinterpret this as a LUT lookup of 16 bit entries.
            internal_error << "LUT with greater than 16 bit entries not implemented.\n";
        }
        vlut_id = IPICK(is_128B, Intrinsic::hexagon_V6_vlutvwh);
        vlut_acc_id = IPICK(is_128B, Intrinsic::hexagon_V6_vlutvwh_oracc);
        vshuff_id = IPICK(is_128B, Intrinsic::hexagon_V6_vshuffh);
    }

    // There are two dimensions in which we need to slice up the
    // inputs. First, if the index is larger than a native vector, we
    // need to slice up the operation into native vectors of
    // indices. Second, the LUT may need to be broken into several
    // stages, and that may need to be further broken up into vmux
    // operations.

    // Split up the LUT into native vectors, using the max_index to
    // indicate how many we need.
    max_index = std::min(max_index, static_cast<int>(lut_ty->getVectorNumElements()) - 1);
    int native_idx_elements = native_vector_bits()/8;
    int native_lut_elements = native_vector_bits()/lut_ty->getScalarSizeInBits();

    // The vlut instructions work on pairs of LUTs interleaved, with
    // each lut containing lut_slice_elements. We need to interleave
    // pairs of the native LUTs to make a full set of native LUTs.
    vector<Value *> lut_slices;
    for (int i = 0; i <= max_index; i += native_lut_elements) {
        Value *lut_slice = slice_vector(lut, i, native_lut_elements);
        lut_slice = call_intrin_cast(lut_slice->getType(), vshuff_id, {lut_slice});
        lut_slices.push_back(lut_slice);
    }
    internal_assert(!lut_slices.empty());

    llvm::Type *native_result_ty =
        llvm::VectorType::get(lut_ty->getVectorElementType(), native_idx_elements);

    // The result will have the same number of elements as idx.
    int idx_elements = idx_ty->getVectorNumElements();

    // Each LUT has 1 pair of even/odd mask values for HVX 64, 2 for
    // HVX 128.  We may not need all of the passes, if the LUT has
    // fewer than half of the elements in an HVX 128 vector.
    int lut_passes = is_128B ? 2 : 1;

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
            idx_i = call_intrin_cast(idx_i->getType(), IPICK(is_128B, Intrinsic::hexagon_V6_vshuffb), {idx_i});
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
                    result_i = call_intrin_cast(native_result_ty, vlut_id,
                                                {idx_i, lut_slices[j], mask[0]});
                    result_i = call_intrin_cast(native_result_ty, vlut_acc_id,
                                                {result_i, idx_i, lut_slices[j], mask[1]});
                } else if (max_index >= pass_index * native_lut_elements / lut_passes) {
                    // Not the first native LUT, accumulate the LUT
                    // with the previous result.
                    for (int m = 0; m < 2; m++) {
                        result_i = call_intrin_cast(native_result_ty, vlut_acc_id,
                                                    {result_i, idx_i, lut_slices[j], mask[m]});
                    }
                }
            }
        }

        result.push_back(result_i);
    }

    return slice_vector(concat_vectors(result), 0, idx_elements);
}

bool is_power_of_two(int x) { return (x & (x - 1)) == 0; }

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
bool generate_vdelta(const std::vector<int> &indices, bool reverse, std::vector<int> &switches) {
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
        for (int delta = start; path != 0; delta = reverse ? delta / 2 : delta * 2) {
            int switch_state = path & delta;
            if ((switches_used[x] & delta) != 0) {
                // This switch is already set...
                if ((switches[x] & delta) != switch_state) {
                    // ... and it is set to the wrong thing. We can't represent this shuffle.
                    return false;
                }
            } else {
                // This switch is not already set, set it to the value we want, and mark it used.
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

Value *CodeGen_Hexagon::vdelta(Value *lut, const vector<int> &indices) {
    bool is_128B = target.has_feature(Halide::Target::HVX_128);
    llvm::Type *lut_ty = lut->getType();
    int lut_elements = lut_ty->getVectorNumElements();
    llvm::Type *element_ty = lut_ty->getVectorElementType();
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements = native_vector_bits()/element_ty->getScalarSizeInBits();
    int result_elements = indices.size();

    // If the input is not a vector of 8 bit elements, replicate the
    // indices and cast the LUT.
    if (element_bits != 8) {
        int replicate = element_bits / 8;
        assert(replicate != 0);
        llvm::Type *new_lut_ty = llvm::VectorType::get(i8_t, lut_elements * replicate);
        Value *i8_lut = builder->CreateBitCast(lut, new_lut_ty);
        vector<int> i8_indices(indices.size() * replicate);
        for (size_t i = 0; i < indices.size(); i++) {
            for (int j = 0; j < replicate; j++) {
                i8_indices[i * replicate + j] = indices[i] * replicate + j;
            }
        }
        Value *result = vdelta(i8_lut, i8_indices);
        result = builder->CreateBitCast(result, lut_ty);
        return result;
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

    assert(result_elements == native_elements);

    // We can only use vdelta to shuffle a single native vector of
    // input. If we have more than one, we need to break it into
    // multiple vdelta operations, and combine them with vmux.
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
                    mask[j] = ConstantInt::get(i8_t, 255);
                    none_used = false;
                } else {
                    indices_i[j] = -1;
                    mask[j] = ConstantInt::get(i8_t, 0);
                    all_used = false;
                }
            }
            Value *ret_i = vdelta(lut_i, indices_i);
            if (all_used || ret == nullptr) {
                // If the mask is all ones, or this is the first result, we don't need to preserve past results.
                ret = ret_i;
            } else if (!none_used) {
                // Create a condition value for which elements of the range are valid for this index.
                // We can't make a constant vector of <1024 x i1>, it crashes the Hexagon LLVM backend before LLVM version 6.0.
                Value *minus_one = codegen(make_const(UInt(8, mask.size()), 255));
                Value *hack_mask = call_intrin(lut_i->getType(), "halide.hexagon.eq.vb.vb", {ConstantVector::get(mask), minus_one});

                ret = call_intrin(lut_i->getType(),
                                  "halide.hexagon.mux.vb.vb",
                                  {hack_mask, ret_i, ret});
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
            Intrinsic::ID vdelta_id = IPICK(is_128B, reverse ? Intrinsic::hexagon_V6_vrdelta : Intrinsic::hexagon_V6_vdelta);
            return call_intrin_cast(lut_ty, vdelta_id, {lut, control});
        }
    }

    // TODO: If the above fails, we might be able to use a vdelta and
    // vrdelta instruction together to implement the shuffle.
    internal_error << "Unsupported vdelta operation.\n";

    // TODO: If the vdelta results are sparsely used, it might be
    // better to use vlut.
    return vlut(lut, indices);
}

Value *CodeGen_Hexagon::vlut(Value *lut, const vector<int> &indices) {
    // TODO: We can take advantage of the fact that we know the
    // indices at compile time to implement a few
    // optimizations. First, we can avoid running the vlut
    // instructions for ranges of the LUT for which we know we don't
    // have any indices. This wil happen often for strided
    // ramps. Second, we can do the shuffling of the indices necessary
    // at compile time.
    vector<Constant *>llvm_indices;
    llvm_indices.reserve(indices.size());
    int min_index = lut->getType()->getVectorNumElements();
    int max_index = 0;
    for (int i : indices) {
        if (i != -1) {
            min_index = std::min(min_index, i);
            max_index = std::max(max_index, i);
        }
        llvm_indices.push_back(ConstantInt::get(i8_t, i));
    }

    if (max_index <= 255) {
        // If we can do this with one vlut, do it now.
        return vlut(lut, ConstantVector::get(llvm_indices), min_index, max_index);
    }

    llvm::Type *i8x_t = VectorType::get(i8_t, indices.size());
    llvm::Type *i16x_t = VectorType::get(i16_t, indices.size());

    // We use i16 indices because we can't support LUTs with more than
    // 32k elements anyways without massive stack spilling (the LUT
    // must fit in registers), and it costs some runtime performance
    // due to the conversion to 8 bit. This is also crazy and should
    // never happen.
    internal_assert(max_index < std::numeric_limits<int16_t>::max())
        << "vlut of more than 32k elements not supported \n";

    // We need to break the index up into ranges of up to 256, and mux
    // the ranges together after using vlut on each range. This vector
    // contains the result of each range, and a condition vector
    // indicating whether the result should be used.
    vector<std::pair<Value *, Value *>> ranges;
    for (int min_index_i = 0; min_index_i < max_index; min_index_i += 256) {
        // Make a vector of the indices shifted such that the min of
        // this range is at 0.
        vector<Constant *> llvm_indices;
        llvm_indices.reserve(indices.size());
        for (int i : indices) {
            llvm_indices.push_back(ConstantInt::get(i16_t, i - min_index_i));
        }
        Value *llvm_index = ConstantVector::get(llvm_indices);

        // Create a condition value for which elements of the range are valid for this index.
        // We can't make a constant vector of <1024 x i1>, it crashes the Hexagon LLVM backend.
        Value *minus_one = codegen(make_const(UInt(16, indices.size()), -1));
        Value *use_index = call_intrin(i16x_t, "halide.hexagon.gt.vh.vh", {llvm_index, minus_one});

        // After we've eliminated the invalid elements, we can
        // truncate to 8 bits, as vlut requires.
        llvm_index = call_intrin(i8x_t, "halide.hexagon.pack.vh", {llvm_index});
        use_index = call_intrin(i8x_t, "halide.hexagon.pack.vh", {use_index});

        int range_extent_i = std::min(max_index - min_index_i, 255);
        Value *range_i = vlut(slice_vector(lut, min_index_i, range_extent_i), llvm_index, 0, range_extent_i);

        ranges.push_back({ range_i, use_index });
    }

    // TODO: This could be reduced hierarchically instead of in
    // order. However, this requires the condition for the mux to be
    // quite tricky.
    Value *result = ranges[0].first;
    llvm::Type *element_ty = result->getType()->getVectorElementType();
    string mux = "halide.hexagon.mux";
    switch (element_ty->getScalarSizeInBits()) {
    case 8: mux += ".vb.vb"; break;
    case 16: mux += ".vh.vh"; break;
    case 32: mux += ".vw.vw"; break;
    default: internal_error << "Cannot constant select vector of " << element_ty->getScalarSizeInBits() << "\n";
    }
    for (size_t i = 1; i < ranges.size(); i++) {
        result = call_intrin(result->getType(), mux, {ranges[i].second, ranges[i].first, result});
    }
    return result;
}

namespace {

string type_suffix(Type type, bool signed_variants = true) {
    string prefix = type.is_vector() ? ".v" : ".";
    if (type.is_int() || !signed_variants) {
        switch (type.bits()) {
        case 8: return prefix + "b";
        case 16: return prefix + "h";
        case 32: return prefix + "w";
        }
    } else if (type.is_uint()) {
        switch (type.bits()) {
        case 8: return prefix + "ub";
        case 16: return prefix + "uh";
        case 32: return prefix + "uw";
        }
    }
    internal_error << "Unsupported HVX type: " << type << "\n";
    return "";
}

string type_suffix(Expr a, bool signed_variants = true) {
    return type_suffix(a.type(), signed_variants);
}

string type_suffix(Expr a, Expr b, bool signed_variants = true) {
    return type_suffix(a, signed_variants) + type_suffix(b, signed_variants);
}

string type_suffix(const vector<Expr> &ops, bool signed_variants = true) {
    if (ops.empty()) return "";
    string suffix = type_suffix(ops.front(), signed_variants);
    for (size_t i = 1; i < ops.size(); i++) {
        suffix = suffix + type_suffix(ops[i], signed_variants);
    }
    return suffix;
}

}  // namespace

Value *CodeGen_Hexagon::call_intrin(Type result_type, const string &name,
                                    vector<Expr> args, bool maybe) {
    llvm::Function *fn = module->getFunction(name);
    if (maybe && !fn) return nullptr;
    internal_assert(fn) << "Function '" << name << "' not found\n";
    if (fn->getReturnType()->getVectorNumElements()*2 <= static_cast<unsigned>(result_type.lanes())) {
        // We have fewer than half as many lanes in our intrinsic as
        // we have in the call. Check to see if a double vector
        // version of this intrinsic exists.
        llvm::Function *fn2 = module->getFunction(name + ".dv");
        if (fn2) {
            fn = fn2;
        }
    }
    return call_intrin(result_type,
                       fn->getReturnType()->getVectorNumElements(),
                       fn->getName(),
                       args);
}

Value *CodeGen_Hexagon::call_intrin(llvm::Type *result_type, const string &name,
                                    vector<Value *> args, bool maybe) {
    llvm::Function *fn = module->getFunction(name);
    if (maybe && !fn) return nullptr;
    internal_assert(fn) << "Function '" << name << "' not found\n";
    if (fn->getReturnType()->getVectorNumElements()*2 <= result_type->getVectorNumElements()) {
        // We have fewer than half as many lanes in our intrinsic as
        // we have in the call. Check to see if a double vector
        // version of this intrinsic exists.
        llvm::Function *fn2 = module->getFunction(name + ".dv");
        if (fn2) {
            fn = fn2;
        }
    }
    return call_intrin(result_type,
                       fn->getReturnType()->getVectorNumElements(),
                       fn->getName(),
                       args);
}

string CodeGen_Hexagon::mcpu() const {
    if (target.has_feature(Halide::Target::HVX_v66)) {
        return "hexagonv66";
    } else if (target.has_feature(Halide::Target::HVX_v65)) {
        return "hexagonv65";
    } else if (target.has_feature(Halide::Target::HVX_v62)) {
        return "hexagonv62";
    } else {
        return "hexagonv60";
    }
}

string CodeGen_Hexagon::mattrs() const {
    std::stringstream attrs;
    if (target.has_feature(Halide::Target::HVX_128)) {
#if LLVM_VERSION < 60
        attrs << "+hvx-double";
#else
        attrs << "+hvx-length128b";
#endif
    } else {
#if LLVM_VERSION < 60
        attrs << "+hvx";
#else
        attrs << "+hvx-length64b";
#endif
    }
#if LLVM_VERSION >= 50
    attrs << ",+long-calls";
#else
    user_error << "LLVM version 5.0 or greater is required for the Hexagon backend";
#endif
    return attrs.str();
}

bool CodeGen_Hexagon::use_soft_float_abi() const {
    return false;
}

int CodeGen_Hexagon::native_vector_bits() const {
    if (target.has_feature(Halide::Target::HVX_128)) {
        return 128*8;
    } else {
        return 64*8;
    }
}

void CodeGen_Hexagon::visit(const Add *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.add" + type_suffix(op->a, op->b, false),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Sub *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.sub" + type_suffix(op->a, op->b, false),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

namespace {

Expr maybe_scalar(Expr x) {
    const Broadcast *xb = x.as<Broadcast>();
    if (xb) {
        return xb->value;
    } else {
        return x;
    }
}

}  // namespace

void CodeGen_Hexagon::visit(const Mul *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.mul" + type_suffix(op->a, op->b),
                            {op->a, op->b},
                            true /*maybe*/);
        if (value) return;

        // Hexagon has mostly widening multiplies. Try to find a
        // widening multiply we can use.
        // TODO: It would probably be better to just define a bunch of
        // mul.*.* functions in the runtime HVX modules so the above
        // implementation can be used unconditionally.
        value = call_intrin(op->type,
                            "halide.hexagon.mpy" + type_suffix(op->a, op->b),
                            {op->a, op->b},
                            true /*maybe*/);
        if (value) {
            // We found a widening op, we need to narrow back
            // down. The widening multiply deinterleaved the result,
            // but the trunc operation reinterleaves.
            Type wide = op->type.with_bits(op->type.bits()*2);
            value = call_intrin(llvm_type_of(op->type),
                                "halide.hexagon.trunc" + type_suffix(wide, false),
                                {value});
            return;
        }

        internal_error << "Unhandled HVX multiply "
                       << op->a.type() << "*" << op->b.type() << "\n"
                       << Expr(op) << "\n";
    } else {
        CodeGen_Posix::visit(op);
    }
}

Expr CodeGen_Hexagon::mulhi_shr(Expr a, Expr b, int shr) {
    Type ty = a.type();
    if (ty.is_vector() && (ty.bits() == 8 || ty.bits() == 16)) {
        Type wide_ty = ty.with_bits(ty.bits() * 2);

        // Generate a widening multiply.
        Expr p_wide = Call::make(wide_ty, "halide.hexagon.mpy" + type_suffix(a, b),
                                 {a, b}, Call::PureExtern);

        // Keep the high half (truncate the low half). This also
        // re-interleaves after mpy deinterleaved.
        Expr p = Call::make(ty, "halide.hexagon.trunclo" + type_suffix(p_wide, false),
                            {p_wide}, Call::PureExtern);

        // Apply the remaining shift.
        if (shr != 0) {
            p = p >> shr;
        }

        return p;
    } else {
        return CodeGen_Posix::mulhi_shr(a, b, shr);
    }
}

Expr CodeGen_Hexagon::sorted_avg(Expr a, Expr b) {
    Type ty = a.type();
    if (ty.is_vector() && ((ty.is_uint() && (ty.bits() == 8 || ty.bits() == 16)) ||
                           (ty.is_int() && (ty.bits() == 16 || ty.bits() == 32)))) {
        return Call::make(ty, "halide.hexagon.avg" + type_suffix(a, b),
                          {a, b}, Call::PureExtern);
    } else {
        return CodeGen_Posix::sorted_avg(a, b);
    }
}

void CodeGen_Hexagon::visit(const Div *op) {
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Cast *op) {
    // TODO: Do we need to handle same-sized vector casts before LLVM sees them?
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Call *op) {
    internal_assert(op->is_extern() || op->is_intrinsic())
        << "Can only codegen extern calls and intrinsics\n";

    // Map Halide functions to Hexagon intrinsics, plus a boolean
    // indicating if the intrinsic has signed variants or not.
    static std::map<string, std::pair<string, bool>> functions = {
        { Call::abs, { "halide.hexagon.abs", true } },
        { Call::absd, { "halide.hexagon.absd", true } },
        { Call::bitwise_and, { "halide.hexagon.and", false } },
        { Call::bitwise_or, { "halide.hexagon.or", false } },
        { Call::bitwise_xor, { "halide.hexagon.xor", false } },
        { Call::bitwise_not, { "halide.hexagon.not", false } },
        { Call::count_leading_zeros, { "halide.hexagon.clz", false } },
        { Call::popcount, { "halide.hexagon.popcount", false } },
    };

    if (is_native_interleave(op) || is_native_deinterleave(op)) {
        user_assert(op->type.lanes() % (native_vector_bits() * 2 / op->type.bits()) == 0)
            << "Interleave or deinterleave will result in miscompilation, "
            << "see https://github.com/halide/Halide/issues/1582\n" << Expr(op) << "\n";
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
            string intrin =
                i->second.first + type_suffix(op->args, i->second.second);
            value = call_intrin(op->type, intrin, op->args, true /*maybe*/);
            if (value) return;
        } else if (op->is_intrinsic(Call::shift_left) ||
                   op->is_intrinsic(Call::shift_right)) {

            internal_assert(op->args.size() == 2);
            string instr = op->is_intrinsic(Call::shift_left) ? "halide.hexagon.shl" : "halide.hexagon.shr";
            Expr b = maybe_scalar(op->args[1]);
            value = call_intrin(op->type,
                                instr + type_suffix(op->args[0], b),
                                {op->args[0], b});
            return;
        } else if (op->is_intrinsic("dynamic_shuffle")) {
            internal_assert(op->args.size() == 4);
            const int64_t *min_index = as_const_int(op->args[2]);
            const int64_t *max_index = as_const_int(op->args[3]);
            internal_assert(min_index && max_index);
            Value *lut = codegen(op->args[0]);
            Value *idx = codegen(op->args[1]);
            value = vlut(lut, idx, *min_index, *max_index);
            return;
        } else if (op->is_intrinsic(Call::select_mask)) {
            internal_assert(op->args.size() == 3);
            // eliminate_bool_vectors has replaced all boolean vectors
            // with integer vectors of the appropriate size, so we
            // just need to convert the select_mask intrinsic to a
            // hexagon mux intrinsic.
            value = call_intrin(op->type,
                                "halide.hexagon.mux" +
                                type_suffix(op->args[1], op->args[2], false),
                                op->args);
            return;
        } else if (op->is_intrinsic(Call::cast_mask)) {
            internal_error << "cast_mask should already have been handled in HexagonOptimize\n";
        }
    }

    if (op->is_intrinsic(Call::bool_to_mask)) {
        internal_assert(op->args.size() == 1);
        if (op->args[0].type().is_vector()) {
            // The argument is already a mask of the right width.
            op->args[0].accept(this);
        } else {
            // The argument is a scalar bool. Converting it to
            // all-ones or all-zeros is sufficient for HVX masks
            // (mux just looks at the LSB of each byte).
            Expr equiv = -Cast::make(op->type, op->args[0]);
            equiv.accept(this);
        }
        return;
    } else if (op->is_intrinsic(Call::extract_mask_element)) {
        internal_assert(op->args.size() == 2);
        const int64_t *index = as_const_int(op->args[1]);
        internal_assert(index);
        value = codegen(Cast::make(Bool(), Shuffle::make_extract_element(op->args[0], *index)));
        return;
    }

    if (op->is_intrinsic(Call::prefetch)) {
        internal_assert((op->args.size() == 4) || (op->args.size() == 6))
            << "Hexagon only supports 1D or 2D prefetch\n";

        vector<llvm::Value *> args;
        args.push_back(codegen_buffer_pointer(codegen(op->args[0]), op->type, op->args[1]));

        Expr extent_0_bytes = op->args[2] * op->args[3] * op->type.bytes();
        args.push_back(codegen(extent_0_bytes));

        llvm::Function *prefetch_fn = nullptr;
        if (op->args.size() == 4) { // 1D prefetch: {base, offset, extent0, stride0}
            prefetch_fn = module->getFunction("_halide_prefetch");
        } else { // 2D prefetch: {base, offset, extent0, stride0, extent1, stride1}
            prefetch_fn = module->getFunction("_halide_prefetch_2d");
            args.push_back(codegen(op->args[4]));
            Expr stride_1_bytes = op->args[5] * op->type.bytes();
            args.push_back(codegen(stride_1_bytes));
        }
        internal_assert(prefetch_fn);

        // The first argument is a pointer, which has type i8*. We
        // need to cast the argument, which might be a pointer to a
        // different type.
        llvm::Type *ptr_type = prefetch_fn->getFunctionType()->params()[0];
        args[0] = builder->CreateBitCast(args[0], ptr_type);

        value = builder->CreateCall(prefetch_fn, args);
        return;
    }

    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Broadcast *op) {
    if (op->lanes * op->type.bits() <= 32) {
        // If the result is not more than 32 bits, just use scalar code.
        CodeGen_Posix::visit(op);
    } else {
        // TODO: Use vd0?
        string v62orLater_suffix = "";
        if (target.features_any_of({Target::HVX_v62, Target::HVX_v65, Target::HVX_v66}) &&
            (op->value.type().bits() == 8 || op->value.type().bits() == 16))
            v62orLater_suffix = "_v62";

        value = call_intrin(op->type,
                            "halide.hexagon.splat" + v62orLater_suffix + type_suffix(op->value, false),
                            {op->value});
    }
}

void CodeGen_Hexagon::visit(const Max *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.max" + type_suffix(op->a, op->b),
                            {op->a, op->b},
                            true /*maybe*/);
        if (!value) {
            Expr equiv =
                Call::make(op->type, Call::select_mask, {op->a > op->b, op->a, op->b}, Call::PureIntrinsic);
            equiv = common_subexpression_elimination(equiv);
            value = codegen(equiv);
        }
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Min *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.min" + type_suffix(op->a, op->b),
                            {op->a, op->b},
                            true /*maybe*/);
        if (!value) {
            Expr equiv =
                Call::make(op->type, Call::select_mask, {op->a > op->b, op->b, op->a}, Call::PureIntrinsic);
            equiv = common_subexpression_elimination(equiv);
            value = codegen(equiv);
        }
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const Select *op) {
    internal_assert(op->condition.type().is_scalar()) << Expr(op) << "\n";

    if (op->type.is_vector()) {
        // Implement scalar conditions on vector values with if-then-else.
        value = codegen(Call::make(op->type, Call::if_then_else,
                                   {op->condition, op->true_value, op->false_value},
                                   Call::Intrinsic));
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const GT *op) {
    if (op->type.is_vector()) {
        value = call_intrin(eliminated_bool_type(op->type, op->a.type()),
                            "halide.hexagon.gt" + type_suffix(op->a, op->b),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const EQ *op) {
    if (op->type.is_vector()) {
        value = call_intrin(eliminated_bool_type(op->type, op->a.type()),
                            "halide.hexagon.eq" + type_suffix(op->a, op->b, false),
                            {op->a, op->b});
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const GE *op) {
    if (op->type.is_vector()) {
        Expr ge = Not::make(GT::make(op->b, op->a));
        ge = eliminate_bool_vectors(ge);
        ge.accept(this);
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const LE *op) {
    if (op->type.is_vector()) {
        Expr le = Not::make(GT::make(op->a, op->b));
        le = eliminate_bool_vectors(le);
        le.accept(this);
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const LT *op) {
    if (op->type.is_vector()) {
        Expr lt = GT::make(op->b, op->a);
        lt.accept(this);
    } else {
        CodeGen_Posix::visit(op);
    }
}

void CodeGen_Hexagon::visit(const NE *op) {
    if (op->type.is_vector()) {
        Expr eq = Not::make(EQ::make(op->a, op->b));
        eq = eliminate_bool_vectors(eq);
        eq.accept(this);
    } else {
        CodeGen_Posix::visit(op);
    }
}

Value *CodeGen_Hexagon::codegen_allocation_size(const std::string &name, Type type, const std::vector<Expr> &extents) {
    // Compute size from list of extents checking for overflow.

    Expr overflow = make_zero(UInt(64));
    Expr total_size = make_const(UInt(64), type.lanes() * type.bytes());

    // We'll multiply all the extents into the 64-bit value
    // total_size. We'll also track (total_size >> 32) as a 64-bit
    // value to check for overflow as we go. The loop invariant will
    // be that either the overflow Expr is non-zero, or total_size_hi
    // only occupies the bottom 32-bits. Overflow could be more simply
    // checked for using division, but that's slower at runtime. This
    // method generates much better assembly.
    Expr total_size_hi = make_zero(UInt(64));

    Expr low_mask = make_const(UInt(64), (uint64_t)(0xffffffff));
    for (size_t i = 0; i < extents.size(); i++) {
        Expr next_extent = cast(UInt(32), extents[i]);

        // Update total_size >> 32. This math can't overflow due to
        // the loop invariant:
        total_size_hi *= next_extent;
        // Deal with carry from the low bits. Still can't overflow.
        total_size_hi += ((total_size & low_mask) * next_extent) >> 32;

        // Update total_size. This may overflow.
        total_size *= next_extent;

        // We can check for overflow by asserting that total_size_hi
        // is still a 32-bit number.
        overflow = overflow | (total_size_hi >> 32);
    }

    Expr max_size = make_const(UInt(64), target.maximum_buffer_size());
    Expr size_check = (overflow == 0) && (total_size <= max_size);

    // For constant-sized allocations this check should simplify away.
    size_check = common_subexpression_elimination(simplify(size_check));
    if (!is_one(size_check)) {
        create_assertion(codegen(size_check),
                         Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                                    {name, total_size, max_size}, Call::Extern));
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
        user_assert(!alloc->new_expr.defined()) << "Custom Expression not allowed for Memory Type Locked Cache\n";

        Value *llvm_size = nullptr;
        int32_t constant_bytes = Allocate::constant_allocation_size(alloc->extents, alloc->name);
        if (constant_bytes > 0) {
            constant_bytes *= alloc->type.bytes();
            llvm_size = codegen(Expr(constant_bytes));
        } else {
            llvm_size = codegen_allocation_size(alloc->name, alloc->type, alloc->extents);
        }
 
        // Only allocate memory if the condition is true, otherwise 0.
        Value *llvm_condition = codegen(alloc->condition);
        if (llvm_size != nullptr) {
            llvm_size = builder->CreateSelect(llvm_condition,
                                          llvm_size,
                                          ConstantInt::get(llvm_size->getType(), 0));
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
        llvm::Function *alloc_fn = module->getFunction("halide_locked_cache_malloc");
        internal_assert(alloc_fn) << "Could not find halide_locked_cache_malloc in module\n";

        llvm::Function::arg_iterator arg_iter = alloc_fn->arg_begin();
        ++arg_iter;  // skip the user context *
        llvm_size = builder->CreateIntCast(llvm_size, arg_iter->getType(), false);

        debug(4) << "Creating call to halide_locked_cache_malloc for allocation " << alloc->name
                 << " of size " << alloc->type.bytes();
        for (Expr e : alloc->extents) {
            debug(4) << " x " << e;
        }
        debug(4) << "\n";
        Value *args[2] = { get_user_context(), llvm_size };

        Value *call = builder->CreateCall(alloc_fn, args);

        // Fix the type to avoid pointless bitcasts later
        call = builder->CreatePointerCast(call, llvm_type_of(alloc->type)->getPointerTo());
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
        internal_assert(free_fn) << "Could not find " << alloc->free_function << " in module.\n";
        allocation.destructor = register_destructor(free_fn, allocation.ptr, OnError);
        allocation.destructor_function = free_fn;

        // Push the allocation base pointer onto the symbol table
        debug(3) << "Pushing allocation called " << alloc->name << " onto the symbol table\n";
        allocations.push(alloc->name, allocation);

        sym_push(alloc->name, allocation.ptr);

        codegen(alloc->body);

        // If there was no early free, free it now.
        if (allocations.contains(alloc->name)) {
            Allocation alloc_obj = allocations.get(alloc->name);
            internal_assert(alloc_obj.destructor);
            trigger_destructor(alloc_obj.destructor_function, alloc_obj.destructor);

            allocations.pop(alloc->name);
            sym_pop(alloc->name);
        }
    } else {
        // For all other memory types
        CodeGen_Posix::visit(alloc);
    }
}

void CodeGen_Hexagon::visit(const Free *stmt) {
    Allocation alloc = allocations.get(stmt->name);

    internal_assert(alloc.destructor);
    trigger_destructor(alloc.destructor_function, alloc.destructor);

    allocations.pop(stmt->name);
    sym_pop(stmt->name);
}

}  // namespace Internal
}  // namespace Halide
