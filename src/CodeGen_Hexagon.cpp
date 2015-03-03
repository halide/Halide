#include <iostream>
#include <sstream>

#include "CodeGen_Hexagon.h"
#include "IROperator.h"
#include "IRMatch.h"
#include "IREquality.h"
#include "Debug.h"
#include "Util.h"
#include "Simplify.h"
#include "IntegerDivisionTable.h"
#include "IRPrinter.h"
#include "LLVM_Headers.h"

// Native client llvm relies on global flags to control sandboxing on
// arm, because they expect you to be coming from the command line.
#ifdef WITH_NATIVE_CLIENT
#if LLVM_VERSION < 34
#include <llvm/Support/CommandLine.h>
namespace llvm {
extern cl::opt<bool> FlagSfiData,
    FlagSfiLoad,
    FlagSfiStore,
    FlagSfiStack,
    FlagSfiBranch,
    FlagSfiDisableCP,
    FlagSfiZeroMask;
}
extern llvm::cl::opt<bool> ReserveR9;
#endif
#endif

namespace Halide {
namespace Internal {

using std::vector;
using std::string;
using std::ostringstream;
using std::pair;
using std::make_pair;

using namespace llvm;

  /** Various patterns to peephole match against */
  struct Pattern {

    Expr pattern;
    //        enum PatternType {Simple = 0, LeftShift, RightShift, NarrowArgs};
    enum PatternType {Simple = 0, LeftShift, RightShift, NarrowArgs};
    Intrinsic::ID ID;
    PatternType type;
    Pattern() {}
    Pattern(Expr p, llvm::Intrinsic::ID id, PatternType t = Simple) : pattern(p), ID(id), type(t) {}
  };
  std::vector<Pattern> casts, varith, averages;

CodeGen_Hexagon::CodeGen_Hexagon(Target t) : CodeGen_Posix(t) {
  casts.push_back(Pattern(cast(UInt(16, 64), wild_u8x64),
                          Intrinsic::hexagon_V6_vzb));
  casts.push_back(Pattern(cast(UInt(32, 32), wild_u16x32),
                          Intrinsic::hexagon_V6_vzh));
  casts.push_back(Pattern(cast(Int(16, 64), wild_i8x64),
                          Intrinsic::hexagon_V6_vsb));
  casts.push_back(Pattern(cast(Int(32, 32), wild_i16x32),
                          Intrinsic::hexagon_V6_vsh));
  // "Add"
  // Byte Vectors
  varith.push_back(Pattern(wild_i8x64 + wild_i8x64,
                           Intrinsic::hexagon_V6_vaddb));
  varith.push_back(Pattern(wild_u8x64 + wild_u8x64,
                           Intrinsic::hexagon_V6_vaddubsat));
  // Half Vectors
  varith.push_back(Pattern(wild_i16x32 + wild_i16x32,
                           Intrinsic::hexagon_V6_vaddh));
  varith.push_back(Pattern(wild_u16x32 + wild_u16x32,
                           Intrinsic::hexagon_V6_vadduhsat));
  // Word Vectors.
  varith.push_back(Pattern(wild_i32x16 + wild_i32x16,
                           Intrinsic::hexagon_V6_vaddw));
  // Double Vectors
  // Byte Double Vectors
  varith.push_back(Pattern(wild_i8x128 + wild_i8x128,
                           Intrinsic::hexagon_V6_vaddb_dv));
  varith.push_back(Pattern(wild_u8x128 + wild_u8x128,
                           Intrinsic::hexagon_V6_vaddubsat_dv));
  // Half Double Vectors
  varith.push_back(Pattern(wild_i16x64 + wild_i16x64,
                           Intrinsic::hexagon_V6_vaddh_dv));
  varith.push_back(Pattern(wild_u16x64 + wild_u16x64,
                           Intrinsic::hexagon_V6_vadduhsat_dv));
  // Word Double Vectors.
  varith.push_back(Pattern(wild_i32x32 + wild_i32x32,
                           Intrinsic::hexagon_V6_vaddw_dv));

  // "Sub"
  // Byte Vectors
  varith.push_back(Pattern(wild_i8x64 - wild_i8x64,
                           Intrinsic::hexagon_V6_vsubb));
  varith.push_back(Pattern(wild_u8x64 - wild_u8x64,
                           Intrinsic::hexagon_V6_vsububsat));
  // Half Vectors
  varith.push_back(Pattern(wild_i16x32 - wild_i16x32,
                           Intrinsic::hexagon_V6_vsubh));
  varith.push_back(Pattern(wild_u16x32 - wild_u16x32,
                           Intrinsic::hexagon_V6_vsubuhsat));
  // Word Vectors.
  varith.push_back(Pattern(wild_i32x16 - wild_i32x16,
                           Intrinsic::hexagon_V6_vsubw));
  // Double Vectors
  // Byte Double Vectors
  varith.push_back(Pattern(wild_i8x128 - wild_i8x128,
                           Intrinsic::hexagon_V6_vsubb_dv));
  varith.push_back(Pattern(wild_u8x128 - wild_u8x128,
                           Intrinsic::hexagon_V6_vsububsat_dv));
  // Half Double Vectors
  varith.push_back(Pattern(wild_i16x64 - wild_i16x64,
                           Intrinsic::hexagon_V6_vsubh_dv));
  varith.push_back(Pattern(wild_u16x64 - wild_u16x64,
                           Intrinsic::hexagon_V6_vsubuhsat_dv));
  // Word Double Vectors.
  varith.push_back(Pattern(wild_i32x32 - wild_i32x32,
                           Intrinsic::hexagon_V6_vsubw_dv));

  // "Max"
  varith.push_back(Pattern(max(wild_u8x64, wild_u8x64),
                           Intrinsic::hexagon_V6_vmaxub));
  varith.push_back(Pattern(max(wild_i16x32, wild_i16x32),
                           Intrinsic::hexagon_V6_vmaxh));
  varith.push_back(Pattern(max(wild_u16x32, wild_u16x32),
                           Intrinsic::hexagon_V6_vmaxuh));
  varith.push_back(Pattern(max(wild_i32x16, wild_i32x16),
                           Intrinsic::hexagon_V6_vmaxw));
  // "Min"
  varith.push_back(Pattern(min(wild_u8x64, wild_u8x64),
                           Intrinsic::hexagon_V6_vminub));
  varith.push_back(Pattern(min(wild_i16x32, wild_i16x32),
                           Intrinsic::hexagon_V6_vminh));
  varith.push_back(Pattern(min(wild_u16x32, wild_u16x32),
                           Intrinsic::hexagon_V6_vminuh));
  varith.push_back(Pattern(min(wild_i32x16, wild_i32x16),
                           Intrinsic::hexagon_V6_vminw));


  averages.push_back(Pattern(((wild_u8x64 + wild_u8x64)/2),
                             Intrinsic::hexagon_V6_vavgub));
  averages.push_back(Pattern(((wild_u8x64 - wild_u8x64)/2),
                             Intrinsic::hexagon_V6_vnavgub));
  averages.push_back(Pattern(((wild_u16x32 + wild_u16x32)/2),
                             Intrinsic::hexagon_V6_vavguh));
  averages.push_back(Pattern(((wild_i16x32 + wild_i16x32)/2),
                             Intrinsic::hexagon_V6_vavgh));
  averages.push_back(Pattern(((wild_i16x32 - wild_i16x32)/2),
                             Intrinsic::hexagon_V6_vnavgh));
  averages.push_back(Pattern(((wild_i32x16 + wild_i32x16)/2),
                             Intrinsic::hexagon_V6_vavgw));
  averages.push_back(Pattern(((wild_i32x16 - wild_i32x16)/2),
                             Intrinsic::hexagon_V6_vnavgw));
}

void CodeGen_Hexagon::compile(Stmt stmt, string name,
                          const vector<Argument> &args,
                          const vector<Buffer> &images_to_embed) {

    init_module();

    module = get_initial_module_for_target(target, context);

    // Fix the target triple.
    user_warning << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    llvm::Triple triple = get_target_triple();
    module->setTargetTriple(triple.str());

    user_warning << "Target triple of initial module: " << module->getTargetTriple() << "\n";

    cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_ARGS",
                                "Halide HVX internal compiler\n");

    // Pass to the generic codegen
    CodeGen::compile(stmt, name, args, images_to_embed);

    // Optimize
    CodeGen::optimize_module();
}

llvm::Triple CodeGen_Hexagon::get_target_triple() const {
    llvm::Triple triple;
    triple.setVendor(llvm::Triple::UnknownVendor);
    triple.setArch(llvm::Triple::hexagon);
    triple.setObjectFormat(llvm::Triple::ELF);
    return triple;
}

string CodeGen_Hexagon::mcpu() const {
  return "hexagonv60";
}

string CodeGen_Hexagon::mattrs() const {
  return "+hvx";
}

bool CodeGen_Hexagon::use_soft_float_abi() const {
  return false;
}


static bool canUseVadd(const Add *op) {
  const Ramp *RampA = op->a.as<Ramp>();
  const Ramp *RampB = op->b.as<Ramp>();
  if (RampA && RampB)
    return true;
  if (!RampA && RampB) {
    const Broadcast *BroadcastA = op->a.as<Broadcast>();
    return BroadcastA != NULL;
  } else  if (RampA && !RampB) {
    const Broadcast *BroadcastB = op->b.as<Broadcast>();
    return BroadcastB != NULL;
  } else {
    const Broadcast *BroadcastA = op->a.as<Broadcast>();
    const Broadcast *BroadcastB = op->b.as<Broadcast>();
    if (BroadcastA && BroadcastB)
      return true;
  }
  return false;
}

llvm::Value *CodeGen_Hexagon::emitBinaryOp(const BaseExprNode *op,
                                           std::vector<Pattern> &Patterns) {
  vector<Expr> matches;
  for (size_t I = 0; I < Patterns.size(); ++I) {
    const Pattern &P = Patterns[I];
    if (expr_match(P.pattern, op, matches)) {
        Intrinsic::ID ID = P.ID;
        bool BitCastNeeded = false;
        llvm::Type *BitCastBackTo;
        llvm::Function *F = Intrinsic::getDeclaration(module, ID);
        llvm::FunctionType *FType = F->getFunctionType();
        Value *Lt = codegen(matches[0]);
        Value *Rt = codegen(matches[1]);
        llvm::Type *T0 = FType->getParamType(0);
        llvm::Type *T1 = FType->getParamType(1);
        if (T0 != Lt->getType()) {
          BitCastBackTo = Lt->getType();
          Lt = builder->CreateBitCast(Lt, T0);
          BitCastNeeded = true;
        }
        if (T1 != Rt->getType())
          Rt = builder->CreateBitCast(Rt, T1);
        Value *Call = builder->CreateCall2(F, Lt, Rt);
        if (BitCastNeeded)
          return builder->CreateBitCast(Call, BitCastBackTo);
        else
          return Call;
      }
  }
  return NULL;
}
void CodeGen_Hexagon::visit(const Add *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen::visit(op);
  return;
}

void CodeGen_Hexagon::visit(const Div *op) {
  value = emitBinaryOp(op, averages);
  if (!value)
    CodeGen::visit(op);
  return;
}

void CodeGen_Hexagon::visit(const Sub *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Max *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Min *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Cast *op) {
  vector<Expr> matches;
  for (size_t I = 0; I < casts.size(); ++I) {
    const Pattern &P = casts[I];
    if (expr_match(P.pattern, op, matches)) {
        Intrinsic::ID ID = P.ID;
        llvm::Function *F = Intrinsic::getDeclaration(module, ID);
        llvm::FunctionType *FType = F->getFunctionType();
        Value *Op0 = codegen(matches[0]);
        const Cast *C = P.pattern.as<Cast>();
        internal_assert (C);
        internal_assert(FType->getNumParams() == 1);
        Halide::Type DestType = C->type;
        llvm::Type *DestLLVMType = llvm_type_of(DestType);
        llvm::Type *T0 = FType->getParamType(0);
        if (T0 != Op0->getType()) {
          Op0 = builder->CreateBitCast(Op0, T0);
        }
        Value *Call = builder->CreateCall(F, Op0);
        value = builder->CreateBitCast(Call, DestLLVMType);
        return;
      }
  }
  CodeGen::visit(op);
  return;
}
  void CodeGen_Hexagon::visit(const Broadcast *op) {
    //    int Width = op->width;
    Expr WildI32 = Variable::make(Int(32), "*");
    Expr PatternMatch = Broadcast::make(WildI32, 16);
    vector<Expr> Matches;
    if (expr_match(PatternMatch, op, Matches)) {
        //    if (Width == 16) {
      Intrinsic::ID ID = Intrinsic::hexagon_V6_lvsplatw;
      llvm::Function *F = Intrinsic::getDeclaration(module, ID);
      Value *Op1 = codegen(op->value);
      value = builder->CreateCall(F, Op1);
      return;
    }
    CodeGen::visit(op);
  }
}}

