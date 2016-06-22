#include <iostream>
#include <sstream>

#include "LLVM_Headers.h"
#include "CodeGen_Hexagon.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Target.h"
#include "Debug.h"
#include "Util.h"
#include "Simplify.h"
#include "IRPrinter.h"
#include "EliminateBoolVectors.h"
#include "HexagonOptimize.h"
#include "AlignLoads.h"
#include "CSE.h"
#include "LoopCarry.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

using namespace llvm;

// LLVM Hexagon HVX intrinsics are broken up into 64B and 128B versions, for example,
// llvm::Intrinsic::hexagon_V6_vaddh and llvm::Intrinsic::hexagon_V6_vaddh_128B. This
// macro selects the 64B or 128B mode depending on the value of is_128B. There's a
// further dirty hack here: these intrinsics aren't defined in LLVM older than 3.9. To
// avoid needing to #ifdef random patches of code, we just replace all LLVM intrinsics
// with not_intrinsic.
#ifdef WITH_HEXAGON
#if LLVM_VERSION < 39
#error "Hexagon target requires LLVM version 3.9 or later."
#endif

#define IPICK(is_128B, i64) (is_128B ? i64##_128B : i64)
#else
#define IPICK(is_128B, i64) (is_128B ? Intrinsic::not_intrinsic : Intrinsic::not_intrinsic)
#endif

CodeGen_Hexagon::CodeGen_Hexagon(Target t) : CodeGen_Posix(t) {
#if !(WITH_HEXAGON)
    user_error << "hexagon not enabled for this build of Halide.\n";
#endif
    user_assert(llvm_Hexagon_enabled) << "llvm build not configured with Hexagon target enabled.\n";
}

std::unique_ptr<llvm::Module> CodeGen_Hexagon::compile(const Module &module) {
    auto llvm_module = CodeGen_Posix::compile(module);
    static bool options_processed = false;

    // TODO: This should be set on the module itself, or some other
    // safer way to pass this through to the target specific lowering
    // passes. We set the option here (after the base class'
    // implementation of compile) because it is the last
    // Hexagon-specific code to run prior to invoking the target
    // specific lowering in LLVM, minimizing the chances of the wrong
    // flag being set for the wrong module.
    if (!options_processed) {
        cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_ARGS",
                                    "Halide HVX internal compiler\n");

        std::vector<const char *> options = {
            // Don't put small objects into .data sections, it causes
            // issues with position independent code.
            "-hexagon-small-data-threshold=0"
        };
        cl::ParseCommandLineOptions(options.size(), options.data());
    }
    options_processed = true;

    if (module.target().features_all_of({Halide::Target::HVX_128, Halide::Target::HVX_64})) {
        user_error << "Both HVX_64 and HVX_128 set at same time\n";
    }

    return llvm_module;
}

namespace {

// A piece of IR uses HVX if it contains any vector type producing IR
// nodes.
class UsesHvx : public IRVisitor {
private:
    using IRVisitor::visit;
    void visit(const Variable *op) {
        uses_hvx = uses_hvx || op->type.is_vector();
    }
    void visit(const Ramp *op) {
        uses_hvx = uses_hvx || op->type.is_vector();
    }
    void visit(const Broadcast *op) {
        uses_hvx = uses_hvx || op->lanes > 1;
    }
    void visit(const Call *op) {
        uses_hvx = uses_hvx || op->type.is_vector();
    }

public:
    bool uses_hvx = false;
};

bool uses_hvx(Stmt s) {
    UsesHvx uses;
    s.accept(&uses);
    return uses.uses_hvx;
}

// Wrap the stmt in a call to qurt_hvx_lock, calling qurt_hvx_unlock
// as a destructor if successful.
Stmt acquire_hvx_context(Stmt stmt, const Target &target) {
    // Modify the stmt to add a call to halide_qurt_hvx_lock, and
    // register a destructor to call halide_qurt_hvx_unlock.
    Expr hvx_mode = target.has_feature(Target::HVX_128) ? 128 : 64;
    Expr hvx_lock = Call::make(Int(32), "halide_qurt_hvx_lock", {hvx_mode}, Call::Extern);
    string hvx_lock_result_name = unique_name("hvx_lock_result");
    Expr hvx_lock_result_var = Variable::make(Int(32), hvx_lock_result_name);
    Stmt check_hvx_lock = LetStmt::make(hvx_lock_result_name, hvx_lock,
                                        AssertStmt::make(EQ::make(hvx_lock_result_var, 0), hvx_lock_result_var));

    Expr dummy_obj = reinterpret(Handle(), cast<uint64_t>(1));
    Expr hvx_unlock = Call::make(Int(32), Call::register_destructor,
                                 {Expr("halide_qurt_hvx_unlock_as_destructor"), dummy_obj}, Call::Intrinsic);

    stmt = Block::make(Evaluate::make(hvx_unlock), stmt);
    stmt = Block::make(check_hvx_lock, stmt);
    return stmt;
}

}  // namespace

void CodeGen_Hexagon::compile_func(const LoweredFunc &f,
                                   const string &simple_name, const string &extern_name) {
    CodeGen_Posix::begin_func(f.linkage, simple_name, extern_name, f.args);

    Stmt body = f.body;

    debug(1) << "Optimizing shuffles...\n";
    body = optimize_hexagon_shuffles(body);
    debug(2) << "Lowering after optimizing shuffles:\n" << body << "\n\n";

    debug(1) << "Aligning loads for HVX....\n";
    body = align_loads(body, target.natural_vector_size(Int(8)));
    body = common_subexpression_elimination(body);
    body = simplify(body);
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
    body = optimize_hexagon_instructions(body);

    if (uses_hvx(body)) {
        debug(1) << "Adding calls to qurt_hvx_lock...\n";
        body = acquire_hvx_context(body, target);
    }

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
    HvxIntrinsic intrinsic_wrappers[] = {
        // Zero/sign extension:
        { IPICK(is_128B, Intrinsic::hexagon_V6_vzb), u16v2,  "zxt.vub", {u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vzh), u32v2,  "zxt.vuh", {u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsb), i16v2,  "sxt.vb",  {i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsh), i32v2,  "sxt.vh",  {i16v1} },

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

        // Adds/subtracts:
        // Note that we just use signed arithmetic for unsigned
        // operands, because it works with two's complement arithmetic.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddb),     i8v1,  "add.vb.vb",     {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddh),     i16v1, "add.vh.vh",     {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddw),     i32v1, "add.vw.vw",     {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddb_dv),  i8v2,  "add.vb.vb.dv",  {i8v2,  i8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddh_dv),  i16v2, "add.vh.vh.dv",  {i16v2, i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddw_dv),  i32v2, "add.vw.vw.dv",  {i32v2, i32v2} },

        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubb),     i8v1,  "sub.vb.vb",     {i8v1,  i8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubh),     i16v1, "sub.vh.vh",     {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubw),     i32v1, "sub.vw.vw",     {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubb_dv),  i8v2,  "sub.vb.vb.dv",  {i8v2,  i8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubh_dv),  i16v2, "sub.vh.vh.dv",  {i16v2, i16v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vsubw_dv),  i32v2, "sub.vw.vw.dv",  {i32v2, i32v2} },


        // Adds/subtract of unsigned values with saturation.
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddubsat),    u8v1,  "satub_add.vub.vub",    {u8v1,  u8v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduhsat),    u16v1, "satuh_add.vuh.vuh",    {u16v1, u16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddhsat),     i16v1, "sath_add.vh.vh",       {i16v1, i16v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddwsat),     i32v1, "satw_add.vw.vw",       {i32v1, i32v1} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vaddubsat_dv), u8v2,  "satub_add.vub.vub.dv", {u8v2,  u8v2} },
        { IPICK(is_128B, Intrinsic::hexagon_V6_vadduhsat_dv), u16v2, "satuh_add.vuh.vuh.dv", {u16v2, u16v2} },
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
                } else {
                    args[i] = builder->CreateIntCast(args[i], arg_ty, arg_types[i].is_int());
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
        // so we use vlut if we can. This is actually pretty general, it might be
        // useful for other interleavings, though the LUT grows impractically
        // large quite quickly.
        if (element_bits == 8 || element_bits == 16) {
            Value *lut = concat_vectors(v);

            std::vector<int> indices;
            for (unsigned i = 0; i < v_ty->getVectorNumElements(); i++) {
                for (size_t j = 0; j < v.size(); j++) {
                    indices.push_back(j * v_ty->getVectorNumElements() + i);
                }
            }

            return vlut(lut, indices);
        }
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
    int element_bits = element_ty->getScalarSizeInBits();
    int native_elements = native_vector_bits() / element_bits;
    llvm::Type *native_ty = llvm::VectorType::get(element_ty, native_elements);
    llvm::Type *native2_ty = llvm::VectorType::get(element_ty, native_elements*2);

    int result_elements = static_cast<int>(indices.size());
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
        } else if (max < 256) {
            // This is something else and the indices fit in 8 bits, use a vlut.
            return vlut(concat_vectors({a, b}), indices);
        }
        return CodeGen_Posix::shuffle_vectors(a, b, indices);
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
    // implemented here.

    if (element_bits <= 16 && max < 256) {
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

    // Each LUT has 2 mask values for HVX 64, 4 mask values for HVX 128.
    int lut_passes = is_128B ? 4 : 2;

    vector<Value *> result;
    for (int i = 0; i < idx_elements; i += native_idx_elements) {
        Value *idx_i = slice_vector(idx, i, native_idx_elements);

        Value *result_i = nullptr;
        for (int j = 0; j < static_cast<int>(lut_slices.size()); j++) {
            for (int k = 0; k < lut_passes; k++) {
                Value *mask = ConstantInt::get(i32_t, lut_passes*j + k);
                if (result_i == nullptr) {
                    // The first native LUT, use vlut.
                    result_i = call_intrin_cast(native_result_ty, vlut_id,
                                                {idx_i, lut_slices[j], mask});
                } else {
                    // Not the first native LUT, accumulate the LUT
                    // with the previous result.
                    result_i = call_intrin_cast(native_result_ty, vlut_acc_id,
                                                {result_i, idx_i, lut_slices[j], mask});
                }
            }
        }

        if (native_result_ty->getScalarSizeInBits() == 16) {
            // If we used vlut16, the result is a deinterleaved double
            // vector. Reinterleave it.
            // TODO: We might be able to do this to the indices
            // instead of the result. However, I think that requires a
            // non-native vector width deinterleave, so it's probably
            // not faster, except where the indices are compile time
            // constants.
            result_i = call_intrin(native_result_ty, "halide.hexagon.interleave.vh", {result_i});
        }

        result.push_back(result_i);
    }

    return slice_vector(concat_vectors(result), 0, idx_elements);
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

        ranges.push_back(std::make_pair(range_i, use_index));
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
    if (target.has_feature(Halide::Target::HVX_v62)) {
        return "hexagonv62";
    } else {
        return "hexagonv60";
    }
}

string CodeGen_Hexagon::mattrs() const {
    if (target.has_feature(Halide::Target::HVX_128)) {
        return "+hvx,+hvx-double";
    } else {
        return "+hvx";
    }
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
    internal_assert(op->call_type == Call::Extern ||
                    op->call_type == Call::Intrinsic ||
                    op->call_type == Call::PureExtern ||
                    op->call_type == Call::PureIntrinsic)
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
        }
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Broadcast *op) {
    if (op->lanes * op->type.bits() <= 32) {
        // If the result is not more than 32 bits, just use scalar code.
        CodeGen_Posix::visit(op);
    } else {
        // TODO: Use vd0?
        value = call_intrin(op->type,
                            "halide.hexagon.splat" + type_suffix(op->value, false),
                            {op->value});
    }
}

void CodeGen_Hexagon::visit(const Max *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.max" + type_suffix(op->a, op->b),
                            {op->a, op->b},
                            true /*maybe*/);
        if (value) return;
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Min *op) {
    if (op->type.is_vector()) {
        value = call_intrin(op->type,
                            "halide.hexagon.min" + type_suffix(op->a, op->b),
                            {op->a, op->b},
                            true /*maybe*/);
        if (value) return;
    }
    CodeGen_Posix::visit(op);
}

void CodeGen_Hexagon::visit(const Select *op) {
    if (op->type.is_vector() && op->condition.type().is_vector()) {
        // eliminate_bool_vectors has replaced all boolean vectors
        // with integer vectors of the appropriate size, and this
        // condition is of the form 'cond != 0'. We just need to grab
        // cond and use that as the operand for vmux.
        Expr cond = op->condition;
        const NE *cond_ne_0 = cond.as<NE>();
        if (cond_ne_0) {
            internal_assert(is_zero(cond_ne_0->b));
            cond = cond_ne_0->a;
        }
        Expr t = op->true_value;
        Expr f = op->false_value;
        value = call_intrin(op->type,
                            "halide.hexagon.mux" + type_suffix(t, f, false),
                            {cond, t, f});
    } else if (op->type.is_vector()) {
        // Implement scalar conditions with if-then-else.
        BasicBlock *true_bb = BasicBlock::Create(*context, "true_bb", function);
        BasicBlock *false_bb = BasicBlock::Create(*context, "false_bb", function);
        BasicBlock *after_bb = BasicBlock::Create(*context, "after_bb", function);
        builder->CreateCondBr(codegen(op->condition), true_bb, false_bb);

        builder->SetInsertPoint(true_bb);
        Value *true_value = codegen(op->true_value);
        builder->CreateBr(after_bb);
        // The true value might have jumped to a new block (e.g. if there was a
        // nested select). To generate a correct PHI node, grab the current
        // block.
        BasicBlock *true_pred = builder->GetInsertBlock();

        builder->SetInsertPoint(false_bb);
        Value *false_value = codegen(op->false_value);
        builder->CreateBr(after_bb);
        BasicBlock *false_pred = builder->GetInsertBlock();

        builder->SetInsertPoint(after_bb);
        PHINode *phi = builder->CreatePHI(true_value->getType(), 2);
        phi->addIncoming(true_value, true_pred);
        phi->addIncoming(false_value, false_pred);

        value = phi;
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

}}
