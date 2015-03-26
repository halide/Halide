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

#define HEXAGON_SINGLE_MODE_VECTOR_SIZE 64
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
    bool InvertOperands;
    Pattern() {}
    Pattern(Expr p, llvm::Intrinsic::ID id, PatternType t = Simple,
            bool Invert = false) : pattern(p), ID(id), type(t),
                                   InvertOperands(Invert) {}
  };
  std::vector<Pattern> casts, varith, averages, combiners, vbitwise;

namespace {
Expr sat_h_ub(Expr A) {
  return max(min(A, 255), 0);
}
Expr sat_w_h(Expr A) {
  return max(min(A, 32767), -32768);
}

Expr bitwiseOr(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_or, vec(A, B),
                              Internal::Call::Intrinsic);
}
Expr bitwiseAnd(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_and, vec(A, B),
                              Internal::Call::Intrinsic);
}
Expr bitwiseXor(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_xor, vec(A, B),
                              Internal::Call::Intrinsic);
}
Expr bitwiseNot(Expr A) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_not, vec(A),
                              Internal::Call::Intrinsic);
}
Expr shiftLeft(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::shift_left, vec(A, B),
                              Internal::Call::Intrinsic);
}
}
CodeGen_Hexagon::CodeGen_Hexagon(Target t) : CodeGen_Posix(t) {
  casts.push_back(Pattern(cast(UInt(16, 64), wild_u8x64),
                          Intrinsic::hexagon_V6_vzb));
  casts.push_back(Pattern(cast(UInt(32, 32), wild_u16x32),
                          Intrinsic::hexagon_V6_vzh));
  casts.push_back(Pattern(cast(Int(16, 64), wild_i8x64),
                          Intrinsic::hexagon_V6_vsb));
  casts.push_back(Pattern(cast(Int(32, 32), wild_i16x32),
                          Intrinsic::hexagon_V6_vsh));

  // "shift_left (x, 8)" is converted to x*256 by Simplify.cpp
  combiners.push_back(Pattern(bitwiseOr(sat_h_ub(wild_i16x32),
                                        (sat_h_ub(wild_i16x32) * 256)),
                              Intrinsic::hexagon_V6_vsathub, Pattern::Simple,
                              true));
  combiners.push_back(Pattern(bitwiseOr((sat_h_ub(wild_i16x32) * 256),
                                        sat_h_ub(wild_i16x32)),
                              Intrinsic::hexagon_V6_vsathub, Pattern::Simple,
                              false));
  combiners.push_back(Pattern(bitwiseOr(sat_w_h(wild_i32x16),
                                        (sat_w_h(wild_i32x16) * 65536)),
                              Intrinsic::hexagon_V6_vsatwh, Pattern::Simple,
                              true));
  combiners.push_back(Pattern(bitwiseOr((sat_w_h(wild_i32x16) * 65536),
                                        sat_w_h(wild_i32x16)),
                              Intrinsic::hexagon_V6_vsatwh, Pattern::Simple,
                              false));
  // Our bitwise operations are all type agnostic; all they need are vectors
  // of 64 bytes (single mode) or 128 bytes (double mode). Over 4 types -
  // unsigned bytes, signed and unsigned half-word, and signed word, we have
  // 12 such patterns for each operation. But, we'll stick to only like types
  // here.
  vbitwise.push_back(Pattern(bitwiseAnd(wild_u8x64, wild_u8x64),
                             Intrinsic::hexagon_V6_vand));
  vbitwise.push_back(Pattern(bitwiseAnd(wild_i16x32, wild_i16x32),
                             Intrinsic::hexagon_V6_vand));
  vbitwise.push_back(Pattern(bitwiseAnd(wild_u16x32, wild_u16x32),
                             Intrinsic::hexagon_V6_vand));
  vbitwise.push_back(Pattern(bitwiseAnd(wild_i32x16, wild_i32x16),
                             Intrinsic::hexagon_V6_vand));

  vbitwise.push_back(Pattern(bitwiseXor(wild_u8x64, wild_u8x64),
                             Intrinsic::hexagon_V6_vxor));
  vbitwise.push_back(Pattern(bitwiseXor(wild_i16x32, wild_i16x32),
                             Intrinsic::hexagon_V6_vxor));
  vbitwise.push_back(Pattern(bitwiseXor(wild_u16x32, wild_u16x32),
                             Intrinsic::hexagon_V6_vxor));
  vbitwise.push_back(Pattern(bitwiseXor(wild_i32x16, wild_i32x16),
                             Intrinsic::hexagon_V6_vxor));

  vbitwise.push_back(Pattern(bitwiseOr(wild_u8x64, wild_u8x64),
                             Intrinsic::hexagon_V6_vor));
  vbitwise.push_back(Pattern(bitwiseOr(wild_i16x32, wild_i16x32),
                             Intrinsic::hexagon_V6_vor));
  vbitwise.push_back(Pattern(bitwiseOr(wild_u16x32, wild_u16x32),
                             Intrinsic::hexagon_V6_vor));
  vbitwise.push_back(Pattern(bitwiseOr(wild_i32x16, wild_i32x16),
                             Intrinsic::hexagon_V6_vor));

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

#if 0
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
#endif

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

int CodeGen_Hexagon::native_vector_bits() const {
  return 64*8;
  // will need 128*8 at some point
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

// Attempt to cast an expression to a smaller type while provably not
// losing information. If it can't be done, return an undefined Expr.

Expr lossless_cast(Type t, Expr e) {
    if (t == e.type()) {
        return e;
    } else if (t.can_represent(e.type())) {
        return cast(t, e);
    }

    if (const Cast *c = e.as<Cast>()) {
        if (t == c->value.type()) {
            return c->value;
        } else {
            return lossless_cast(t, c->value);
        }
    }

    if (const Broadcast *b = e.as<Broadcast>()) {
        Expr v = lossless_cast(t.element_of(), b->value);
        if (v.defined()) {
            return Broadcast::make(v, b->width);
        } else {
            return Expr();
        }
    }

    if (const IntImm *i = e.as<IntImm>()) {
        int x = int_cast_constant(t, i->value);
        if (x == i->value) {
            return cast(t, e);
        } else {
            return Expr();
        }
    }

    return Expr();
}
bool checkVMPAVectorLoadPair(const Load *LoadA, const Load*LoadC) {
  if (!LoadA || !LoadC)
    return false;
  debug(4) << "**checkVMPAVectorLoadPair**\n";
  debug(4) << "LoadA = " << LoadA->name << "[" << LoadA->index << "]\n";
  debug(4) << "LoadC = " << LoadC->name << "[" << LoadC->index << "]\n";
  const Ramp *RampA = LoadA->index.as<Ramp>();
  const Ramp *RampC = LoadC->index.as<Ramp>();
  if (!RampA  || !RampC) {
    debug(4) << "checkVMPAVectorLoadPair: Not both Ramps\n";
    return false;
  }
  const IntImm *StrideA = RampA->stride.as<IntImm>();
  const IntImm *StrideC = RampC->stride.as<IntImm>();

  if (StrideA->value != 2 || StrideC->value != 2) {
    debug(4) << "checkVMPAVectorLoadPair: Not all strides are 2\n";
    return false;
  }

  if (RampA->width != 64 || RampC->width != 64) {
      debug(4) << "checkVMPAVectorLoadPair: Not all widths are 64\n";
      return false;
  }

  Expr BaseA = RampA->base;
  Expr BaseC = RampC->base;

  Expr DiffCA = simplify(BaseC - BaseA);
  debug (4) << "checkVMPAVectorLoadPair: BaseA = " << BaseA << "\n";
  debug (4) << "checkVMPAVectorLoadPair: BaseC = " << BaseC << "\n";
  debug (4) << "checkVMPAVectorLoadPair: DiffCA = " << DiffCA << "\n";
  if (is_one(DiffCA))
    return true;
  return false;
}
bool checkVMPAOperandCombinations(vector<Expr> &matches) {
  internal_assert(matches.size() == 4);
  // We accept only two combinations for now.
  // All four vector loads.
  // Two vector loads and two broadcasts.
  // Check if all four are loads
  int I;
  debug(4) << "**checkVMPAOperandCombinations**\n";
  for (I = 0; I < 4; ++I)
    debug(4) << "matches[" << I << "] = " << matches[I] << "\n";

  const Load *LoadA = matches[0].as<Load>();
  const Load *LoadB = matches[1].as<Load>();
  const Load *LoadC = matches[2].as<Load>();
  const Load *LoadD = matches[3].as<Load>();
  if (LoadA && LoadB && LoadC && LoadD) {
    if (((LoadA->name != LoadC->name) && (LoadA->name != LoadD->name)) ||
        ((LoadB->name != LoadC->name) && (LoadB->name != LoadD->name))) {
      debug(4) <<
        "checkVMPAOperandCombinations: All 4 loads, but not exactly"
        " two pairs of arrays\n";
      return false;
    }
    if (LoadA->name == LoadD->name){
      std::swap(matches[2], matches[3]);
      std::swap(LoadC, LoadD);
    }
    return checkVMPAVectorLoadPair(LoadA, LoadC) &&
      checkVMPAVectorLoadPair(LoadB, LoadD);
  } else {
    // Theoretically we can deal with all of them not being vector loads
    // (Hint: Think 4 broadcasts), but this is rare, so now we will deal
    // only with 2 broadcasts and 2 loads to catch this case for example
    // 3*f(2x) + 7*f(2x+1);
    const Load *InterleavedLoad1, *InterleavedLoad2;
    if (!LoadA && LoadB) {
      const Broadcast *BroadcastA = matches[0].as<Broadcast>();
      if (!BroadcastA) {
        debug(4) << "checkVMPAOperandCombinations: A is neither a vector load"
          " nor a broadcast\n";
        return false;
      }
      InterleavedLoad1 = LoadB;
    } else if (LoadA && !LoadB) {
      const Broadcast *BroadcastB = matches[1].as<Broadcast>();
      if (!BroadcastB) {
        debug(4) << "checkVMPAOperandCombinations: B is neither a vector load"
          " nor a broadcast\n";
        return false;
      }
      InterleavedLoad1 = LoadA;
      std::swap(matches[0], matches[1]);
    }
    if (!LoadC && LoadD) {
      const Broadcast *BroadcastC = matches[2].as<Broadcast>();
      if (!BroadcastC) {
        debug(4) << "checkVMPAOperandCombinations: C is neither a vector load"
          " nor a broadcast\n";
        return false;
      }
      InterleavedLoad2 = LoadD;
    } else if (LoadC && !LoadD) {
      const Broadcast *BroadcastD = matches[3].as<Broadcast>();
      if (!BroadcastD) {
        debug(4) << "checkVMPAOperandCombinations: D is neither a vector load"
          " nor a broadcast\n";
        return false;
      }
      InterleavedLoad2 = LoadC;
      std::swap(matches[2], matches[3]);
    }
    if (InterleavedLoad1->name != InterleavedLoad2->name)
      return false;
    // Todo: Should we be checking the broadcasts?
    return checkVMPAVectorLoadPair(InterleavedLoad1, InterleavedLoad2);
  }
  return false;
}
bool CodeGen_Hexagon::shouldUseVMPA(const Add *op, std::vector<Value *> &LLVMValues){
  Expr pattern;
  // pattern = (cast(Int(16, 64), wild_u8x64) * cast(Int(16, 64), wild_u8x64)) +
  //   (cast(Int(16, 64), wild_u8x64) * cast(Int(16, 64), wild_u8x64));
  pattern = (wild_i16x64 * wild_i16x64) + (wild_i16x64 * wild_i16x64);
  vector<Expr> matches;
  if (expr_match(pattern, op, matches)) {
    debug(4) << "Pattern matched\n";
    internal_assert(matches.size() == 4);
    matches[0] = lossless_cast(UInt(8, 64), matches[0]);
    matches[1] = lossless_cast(UInt(8, 64), matches[1]);
    matches[2] = lossless_cast(UInt(8, 64), matches[2]);
    matches[3] = lossless_cast(UInt(8, 64), matches[3]);
    if (!matches[0].defined() || !matches[1].defined() ||
        !matches[2].defined() || !matches[3].defined())
      return false;
    if (!checkVMPAOperandCombinations(matches))
      return false;

    std::vector<Expr> vecA, vecB;
    vecA.push_back(matches[0]);
    vecA.push_back(matches[2]);
    vecB.push_back(matches[1]);
    vecB.push_back(matches[3]);
    Value *a = interleave_vectors(Int(8, 128), vecA);
    Value *b = interleave_vectors(Int(8, 128), vecB);
    LLVMValues.push_back(a);
    LLVMValues.push_back(b);
    return true;
  }
  return false;
}
void CodeGen_Hexagon::visit(const Add *op) {
  debug(4) << "HexagonCodegen: " << op->type << ", " << op->a << " + " << op->b << "\n";
  std::vector<Value *>LLVMValues;
  if (shouldUseVMPA(op, LLVMValues)) {
    internal_assert (LLVMValues.size() == 2);
    Value *Lt = LLVMValues[0];
    Value *Rt = LLVMValues[1];
    llvm::Function *F = Intrinsic::getDeclaration(module, Intrinsic::hexagon_V6_vmpabuuv);
    llvm::FunctionType *FType = F->getFunctionType();
    llvm::Type *T0 = FType->getParamType(0);
    llvm::Type *T1 = FType->getParamType(1);
    if (T0 != Lt->getType()) {
      Lt = builder->CreateBitCast(Lt, T0);
    }
    if (T1 != Rt->getType())
      Rt = builder->CreateBitCast(Rt, T1);

    Halide::Type DestType = op->type;
    llvm::Type *DestLLVMType = llvm_type_of(DestType);
    Value *Call = builder->CreateCall2(F, Lt, Rt);
    if (DestLLVMType != Call->getType())
      value = builder->CreateBitCast(Call, DestLLVMType);
    else
      value = Call;
    debug(4) << "Generating vmpa\n";
    return;
  }

  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen_Posix::visit(op);
  return;
}

void CodeGen_Hexagon::visit(const Div *op) {
  value = emitBinaryOp(op, averages);
  if (!value)
    CodeGen_Posix::visit(op);
  return;
}

void CodeGen_Hexagon::visit(const Sub *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen_Posix::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Max *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen_Posix::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Min *op) {
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen_Posix::visit(op);
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
  CodeGen_Posix::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Call *op) {
  vector<Expr> matches;
  std::cerr << "Op is\n" << op << "\n";
  for (size_t I = 0; I < combiners.size(); ++I) {
    const Pattern &P = combiners[I];
    if (expr_match(P.pattern, op, matches)) {
      Intrinsic::ID ID = P.ID;
      bool InvertOperands = P.InvertOperands;
      llvm::Function *F = Intrinsic::getDeclaration(module, ID);
      llvm::FunctionType *FType = F->getFunctionType();
      size_t NumMatches = matches.size();
      internal_assert(NumMatches == 2);
      internal_assert(FType->getNumParams() == NumMatches);
      Value *Op0 = codegen(matches[0]);
      Value *Op1 = codegen(matches[1]);
      llvm::Type *T0 = FType->getParamType(0);
      llvm::Type *T1 = FType->getParamType(1);
      Halide::Type DestType = op->type;
      llvm::Type *DestLLVMType = llvm_type_of(DestType);
      if (T0 != Op0->getType()) {
        Op0 = builder->CreateBitCast(Op0, T0);
      }
      if (T1 != Op1->getType()) {
        Op1 = builder->CreateBitCast(Op1, T1);
      }
      Value *Call;
      if (InvertOperands)
        Call = builder->CreateCall2(F, Op1, Op0);
      else
        Call = builder->CreateCall2(F, Op0, Op1);
      value = builder->CreateBitCast(Call, DestLLVMType);
      return;
    }
  }
  value = emitBinaryOp(op, vbitwise);
  if (!value) {
    if (op->name == Call::bitwise_not) {
      if (op->type.is_vector() &&
          ((op->type.bytes() * op->type.width) ==
           HEXAGON_SINGLE_MODE_VECTOR_SIZE)) {
        llvm::Function *F =
          Intrinsic::getDeclaration(module,
                                    Intrinsic::hexagon_V6_vnot);
        llvm::FunctionType *FType = F->getFunctionType();
        llvm::Type *T0 = FType->getParamType(0);
        Value *Op0 = codegen(op->args[0]);
        if (T0 != Op0->getType()) {
          Op0 = builder->CreateBitCast(Op0, T0);
        }
        Halide::Type DestType = op->type;
        llvm::Type *DestLLVMType = llvm_type_of(DestType);
        Value *Call = builder->CreateCall(F, Op0);
        if (DestLLVMType != Call->getType())
          value = builder->CreateBitCast(Call, DestLLVMType);
        else
          value = Call;
        return;
      }

    }
    CodeGen_Posix::visit(op);
  }
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
    CodeGen_Posix::visit(op);
  }
}}

