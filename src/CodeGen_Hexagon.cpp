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
#define HEXAGON_SINGLE_MODE_VECTOR_SIZE_IN_BITS 64 * 8
#define CPICK(c128, c64) (B128 ? c128 : c64)
#define WPICK(w128, w64) (B128 ? w128 : w64)
#define IPICK(i64) (B128 ? i64##_128B : i64)
#define UINT_8_MAX UInt(8).imax()
#define UINT_16_MAX UInt(16).imax()
#define INT_8_MAX Int(8).imax()
#define INT_16_MAX Int(16).imax()

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
  std::vector<Pattern> casts, varith, averages, combiners, vbitwise, multiplies;

namespace {
Expr sat_h_ub(Expr A) {
  return max(min(A, 255), 0);
}
Expr sat_w_h(Expr A) {
  return max(min(A, 32767), -32768);
}

Expr bitwiseOr(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_or, {A, B},
                              Internal::Call::Intrinsic);
}
Expr bitwiseAnd(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_and, {A, B},
                              Internal::Call::Intrinsic);
}
Expr bitwiseXor(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_xor, {A, B},
                              Internal::Call::Intrinsic);
}
#ifdef THESE_ARE_UNUSED
Expr bitwiseNot(Expr A) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_not, {A},
                              Internal::Call::Intrinsic);
}
Expr shiftLeft(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::shift_left, {A, B},
                              Internal::Call::Intrinsic);
}
#endif
Expr u8_(Expr E) {
  return cast (UInt(8, E.type().width), E);
}
Expr i8_(Expr E) {
  return cast (Int(8, E.type().width), E);
}Expr u16_(Expr E) {
  return cast (UInt(16, E.type().width), E);
}
Expr i16_(Expr E) {
  return cast (Int(16, E.type().width), E);
}
Expr u32_(Expr E) {
  return cast (UInt(32, E.type().width), E);
}
Expr i32_(Expr E) {
  return cast (Int(32, E.type().width), E);
}
}


CodeGen_Hexagon::CodeGen_Hexagon(Target t)
  : CodeGen_Posix(t),
    wild_i32(Variable::make(Int(32), "*")),
    wild_u32(Variable::make(UInt(32), "*")),
    wild_i16(Variable::make(Int(16), "*")),
    wild_u16(Variable::make(UInt(16), "*")) {
  bool B128 = t.has_feature(Halide::Target::HVX_DOUBLE);
  casts.push_back(Pattern(cast(UInt(16, CPICK(128,64)),
                                        WPICK(wild_u8x128,wild_u8x64)),
                                        IPICK(Intrinsic::hexagon_V6_vzb)));
  casts.push_back(Pattern(cast(UInt(32, CPICK(64,32)),
                                        WPICK(wild_u16x64,wild_u16x32)),
                                        IPICK(Intrinsic::hexagon_V6_vzh)));
  casts.push_back(Pattern(cast(Int(16, CPICK(128,64)),
                                       WPICK(wild_i8x128,wild_i8x64)),
                                       IPICK(Intrinsic::hexagon_V6_vsb)));
  casts.push_back(Pattern(cast(Int(32, CPICK(64,32)),
                                       WPICK(wild_i16x64,wild_i16x32)),
                                       IPICK(Intrinsic::hexagon_V6_vsh)));

  // "shift_left (x, 8)" is converted to x*256 by Simplify.cpp
  combiners.push_back(
    Pattern(bitwiseOr(sat_h_ub(WPICK(wild_i16x64,wild_i16x32)),
                     (sat_h_ub(WPICK(wild_i16x64,wild_i16x32)) * 256)),
                      IPICK(Intrinsic::hexagon_V6_vsathub), Pattern::Simple,
                      true));
  combiners.push_back(
    Pattern(bitwiseOr((sat_h_ub(WPICK(wild_i16x64,wild_i16x32)) * 256),
                       sat_h_ub(WPICK(wild_i16x64,wild_i16x32))),
                       IPICK(Intrinsic::hexagon_V6_vsathub), Pattern::Simple,
                       false));
  combiners.push_back(
    Pattern(bitwiseOr(sat_w_h(WPICK(wild_i32x32,wild_i32x16)),
                     (sat_w_h(WPICK(wild_i32x32,wild_i32x16)) * 65536)),
                     IPICK(Intrinsic::hexagon_V6_vsatwh), Pattern::Simple,
                     true));
  combiners.push_back(
    Pattern(bitwiseOr((sat_w_h(WPICK(wild_i32x32,wild_i32x16)) * 65536),
                       sat_w_h(WPICK(wild_i32x32,wild_i32x16))),
                       IPICK(Intrinsic::hexagon_V6_vsatwh), Pattern::Simple,
                       false));
  combiners.push_back(Pattern(abs(WPICK(wild_u8x128,wild_u8x64) -
                                  WPICK(wild_u8x128,wild_u8x64)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffub)));
  combiners.push_back(Pattern(abs(WPICK(wild_u16x64,wild_u16x32) -
                                  WPICK(wild_u16x64,wild_u16x32)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffuh)));
  combiners.push_back(Pattern(abs(WPICK(wild_i16x64,wild_i16x32) -
                                  WPICK(wild_i16x64,wild_i16x32)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffh)));
  combiners.push_back(Pattern(abs(WPICK(wild_i32x32,wild_i32x16) -
                                  WPICK(wild_i32x32,wild_i32x16)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffw)));

  // Our bitwise operations are all type agnostic; all they need are vectors
  // of 64 bytes (single mode) or 128 bytes (double mode). Over 4 types -
  // unsigned bytes, signed and unsigned half-word, and signed word, we have
  // 12 such patterns for each operation. But, we'll stick to only like types
  // here.
  vbitwise.push_back(Pattern(bitwiseAnd(WPICK(wild_u8x128,wild_u8x64),
                                        WPICK(wild_u8x128,wild_u8x64)),
                                        IPICK(Intrinsic::hexagon_V6_vand)));
  vbitwise.push_back(Pattern(bitwiseAnd(WPICK(wild_i16x64,wild_i16x32),
                                        WPICK(wild_i16x64,wild_i16x32)),
                                        IPICK(Intrinsic::hexagon_V6_vand)));
  vbitwise.push_back(Pattern(bitwiseAnd(WPICK(wild_u16x64,wild_u16x32),
                                        WPICK(wild_u16x64,wild_u16x32)),
                                        IPICK(Intrinsic::hexagon_V6_vand)));
  vbitwise.push_back(Pattern(bitwiseAnd(WPICK(wild_i32x32,wild_i32x16),
                                        WPICK(wild_i32x32,wild_i32x16)),
                                        IPICK(Intrinsic::hexagon_V6_vand)));

  vbitwise.push_back(Pattern(bitwiseXor(WPICK(wild_u8x128,wild_u8x64),
                                        WPICK(wild_u8x128,wild_u8x64)),
                                        IPICK(Intrinsic::hexagon_V6_vxor)));
  vbitwise.push_back(Pattern(bitwiseXor(WPICK(wild_i16x64,wild_i16x32),
                                        WPICK(wild_i16x64,wild_i16x32)),
                                        IPICK(Intrinsic::hexagon_V6_vxor)));
  vbitwise.push_back(Pattern(bitwiseXor(WPICK(wild_u16x64,wild_u16x32),
                                        WPICK(wild_u16x64,wild_u16x32)),
                                        IPICK(Intrinsic::hexagon_V6_vxor)));
  vbitwise.push_back(Pattern(bitwiseXor(WPICK(wild_i32x32,wild_i32x16),
                                        WPICK(wild_i32x32,wild_i32x16)),
                                        IPICK(Intrinsic::hexagon_V6_vxor)));

  vbitwise.push_back(Pattern(bitwiseOr(WPICK(wild_u8x128,wild_u8x64),
                                       WPICK(wild_u8x128,wild_u8x64)),
                                       IPICK(Intrinsic::hexagon_V6_vor)));
  vbitwise.push_back(Pattern(bitwiseOr(WPICK(wild_i16x64,wild_i16x32),
                                       WPICK(wild_i16x64,wild_i16x32)),
                                       IPICK(Intrinsic::hexagon_V6_vor)));
  vbitwise.push_back(Pattern(bitwiseOr(WPICK(wild_u16x64,wild_u16x32),
                                       WPICK(wild_u16x64,wild_u16x32)),
                                       IPICK(Intrinsic::hexagon_V6_vor)));
  vbitwise.push_back(Pattern(bitwiseOr(WPICK(wild_i32x32,wild_i32x16),
                                       WPICK(wild_i32x32,wild_i32x16)),
                                       IPICK(Intrinsic::hexagon_V6_vor)));

  // "Add"
  // Byte Vectors
  varith.push_back(Pattern(WPICK(wild_i8x128,wild_i8x64) +
                           WPICK(wild_i8x128,wild_i8x64),
                           IPICK(Intrinsic::hexagon_V6_vaddb)));
  varith.push_back(Pattern(WPICK(wild_u8x128,wild_u8x64) +
                           WPICK(wild_u8x128,wild_u8x64),
                           IPICK(Intrinsic::hexagon_V6_vaddubsat)));
  // Half Vectors
  varith.push_back(Pattern(WPICK(wild_i16x64,wild_i16x32) +
                           WPICK(wild_i16x64,wild_i16x32),
                           IPICK(Intrinsic::hexagon_V6_vaddh)));
  varith.push_back(Pattern(WPICK(wild_u16x64,wild_u16x32) +
                           WPICK(wild_u16x64,wild_u16x32),
                           IPICK(Intrinsic::hexagon_V6_vadduhsat)));
  // Word Vectors.
  varith.push_back(Pattern(WPICK(wild_i32x32,wild_i32x16) +
                           WPICK(wild_i32x32,wild_i32x16),
                           IPICK(Intrinsic::hexagon_V6_vaddw)));
  // Double Vectors
  // Byte Double Vectors
  varith.push_back(Pattern(WPICK(wild_i8x256,wild_i8x128) +
                           WPICK(wild_i8x256,wild_i8x128),
                           IPICK(Intrinsic::hexagon_V6_vaddb_dv)));
  varith.push_back(Pattern(WPICK(wild_u8x256,wild_u8x128) +
                           WPICK(wild_u8x256,wild_u8x128),
                           IPICK(Intrinsic::hexagon_V6_vaddubsat_dv)));
  // Half Double Vectors
  varith.push_back(Pattern(WPICK(wild_i16x128,wild_i16x64) +
                           WPICK(wild_i16x128,wild_i16x64),
                           IPICK(Intrinsic::hexagon_V6_vaddh_dv)));
  varith.push_back(Pattern(WPICK(wild_u16x128,wild_u16x64) +
                           WPICK(wild_u16x128,wild_u16x64),
                           IPICK(Intrinsic::hexagon_V6_vadduhsat_dv)));
  // Word Double Vectors.
  varith.push_back(Pattern(WPICK(wild_i32x64,wild_i32x32) +
                           WPICK(wild_i32x64,wild_i32x32),
                           IPICK(Intrinsic::hexagon_V6_vaddw_dv)));


  // "Sub"
  // Byte Vectors
  varith.push_back(Pattern(WPICK(wild_i8x128,wild_i8x64) +
                           WPICK(wild_i8x128,wild_i8x64),
                           IPICK(Intrinsic::hexagon_V6_vsubb)));
  varith.push_back(Pattern(WPICK(wild_u8x128,wild_u8x64) +
                           WPICK(wild_u8x128,wild_u8x64),
                           IPICK(Intrinsic::hexagon_V6_vsububsat)));
  // Half Vectors
  varith.push_back(Pattern(WPICK(wild_i16x64,wild_i16x32) +
                           WPICK(wild_i16x64,wild_i16x32),
                           IPICK(Intrinsic::hexagon_V6_vsubh)));
  varith.push_back(Pattern(WPICK(wild_u16x64,wild_u16x32) +
                           WPICK(wild_u16x64,wild_u16x32),
                           IPICK(Intrinsic::hexagon_V6_vsubuhsat)));
  // Word Vectors.
  varith.push_back(Pattern(WPICK(wild_i32x32,wild_i32x16) +
                           WPICK(wild_i32x32,wild_i32x16),
                           IPICK(Intrinsic::hexagon_V6_vsubw)));
  // Double Vectors
  // Byte Double Vectors
  varith.push_back(Pattern(WPICK(wild_i8x256,wild_i8x128) +
                           WPICK(wild_i8x256,wild_i8x128),
                           IPICK(Intrinsic::hexagon_V6_vsubb_dv)));
  varith.push_back(Pattern(WPICK(wild_u8x256,wild_u8x128) +
                           WPICK(wild_u8x256,wild_u8x128),
                           IPICK(Intrinsic::hexagon_V6_vsububsat_dv)));
  // Half Double Vectors
  varith.push_back(Pattern(WPICK(wild_i16x128,wild_i16x64) +
                           WPICK(wild_i16x128,wild_i16x64),
                           IPICK(Intrinsic::hexagon_V6_vsubh_dv)));
  varith.push_back(Pattern(WPICK(wild_u16x128,wild_u16x64) +
                           WPICK(wild_u16x128,wild_u16x64),
                           IPICK(Intrinsic::hexagon_V6_vsubuhsat_dv)));
  // Word Double Vectors.
  varith.push_back(Pattern(WPICK(wild_i32x64,wild_i32x32) +
                           WPICK(wild_i32x64,wild_i32x32),
                           IPICK(Intrinsic::hexagon_V6_vsubw_dv)));

  // "Max"
  varith.push_back(Pattern(max(WPICK(wild_u8x128,wild_u8x64),
                               WPICK(wild_u8x128,wild_u8x64)),
                               IPICK(Intrinsic::hexagon_V6_vmaxub)));
  varith.push_back(Pattern(max(WPICK(wild_i16x64,wild_i16x32),
                               WPICK(wild_i16x64,wild_i16x32)),
                               IPICK(Intrinsic::hexagon_V6_vmaxh)));
  varith.push_back(Pattern(max(WPICK(wild_u16x64,wild_u16x32),
                               WPICK(wild_u16x64,wild_u16x32)),
                               IPICK(Intrinsic::hexagon_V6_vmaxuh)));
  varith.push_back(Pattern(max(WPICK(wild_i32x32,wild_i32x16),
                               WPICK(wild_i32x32,wild_i32x16)),
                               IPICK(Intrinsic::hexagon_V6_vmaxw)));
  // "Min"
  varith.push_back(Pattern(min(WPICK(wild_u8x128,wild_u8x64),
                               WPICK(wild_u8x128,wild_u8x64)),
                               IPICK(Intrinsic::hexagon_V6_vminub)));
  varith.push_back(Pattern(min(WPICK(wild_i16x64,wild_i16x32),
                               WPICK(wild_i16x64,wild_i16x32)),
                               IPICK(Intrinsic::hexagon_V6_vminh)));
  varith.push_back(Pattern(min(WPICK(wild_u16x64,wild_u16x32),
                               WPICK(wild_u16x64,wild_u16x32)),
                               IPICK(Intrinsic::hexagon_V6_vminuh)));
  varith.push_back(Pattern(min(WPICK(wild_i32x32,wild_i32x16),
                               WPICK(wild_i32x32,wild_i32x16)),
                               IPICK(Intrinsic::hexagon_V6_vminw)));

  averages.push_back(Pattern(((WPICK(wild_u8x128,wild_u8x64) +
                               WPICK(wild_u8x128,wild_u8x64))/2),
                               IPICK(Intrinsic::hexagon_V6_vavgub)));
  averages.push_back(Pattern(((WPICK(wild_u8x128,wild_u8x64) -
                               WPICK(wild_u8x128,wild_u8x64))/2),
                               IPICK(Intrinsic::hexagon_V6_vnavgub)));
  averages.push_back(Pattern(((WPICK(wild_u16x64,wild_u16x32) +
                               WPICK(wild_u16x64,wild_u16x32))/2),
                               IPICK(Intrinsic::hexagon_V6_vavguh)));
  averages.push_back(Pattern(((WPICK(wild_i16x64,wild_i16x32) +
                               WPICK(wild_i16x64,wild_i16x32))/2),
                               IPICK(Intrinsic::hexagon_V6_vavgh)));
  averages.push_back(Pattern(((WPICK(wild_i16x64,wild_i16x32) -
                               WPICK(wild_i16x64,wild_i16x32))/2),
                               IPICK(Intrinsic::hexagon_V6_vnavgh)));
  averages.push_back(Pattern(((WPICK(wild_i32x32,wild_i32x16) +
                               WPICK(wild_i32x32,wild_i32x16))/2),
                               IPICK(Intrinsic::hexagon_V6_vavgw)));
  averages.push_back(Pattern(((WPICK(wild_i32x32,wild_i32x16) -
                               WPICK(wild_i32x32,wild_i32x16))/2),
                               IPICK(Intrinsic::hexagon_V6_vnavgw)));

  multiplies.push_back(Pattern(u16_(WPICK(wild_u8x128,wild_u8x64) *
                                    WPICK(wild_u8x128,wild_u8x64)),
                                    IPICK(Intrinsic::hexagon_V6_vmpyubv)));
  multiplies.push_back(Pattern(i16_(WPICK(wild_i8x128,wild_i8x64) *
                                    WPICK(wild_i8x128,wild_i8x64)),
                                    IPICK(Intrinsic::hexagon_V6_vmpybv)));
  multiplies.push_back(Pattern(u32_(WPICK(wild_u16x64,wild_u16x32) *
                                    WPICK(wild_u16x64,wild_u16x32)),
                                    IPICK(Intrinsic::hexagon_V6_vmpyuhv)));
  multiplies.push_back(Pattern(i32_(WPICK(wild_i16x64,wild_i16x32) *
                                    WPICK(wild_i16x64,wild_i16x32)),
                                    IPICK(Intrinsic::hexagon_V6_vmpyhv)));
  multiplies.push_back(Pattern(WPICK(wild_i16x64,wild_i16x32) *
                               WPICK(wild_i16x64,wild_i16x32),
                               IPICK(Intrinsic::hexagon_V6_vmpyih)));

}
llvm::Value *
CodeGen_Hexagon::CallLLVMIntrinsic(llvm::Function *F,
                               std::vector<Value *> &Ops) {
  unsigned I;
  llvm::FunctionType *FType = F->getFunctionType();
  internal_assert(FType->getNumParams() == Ops.size());
  for (I = 0; I < FType->getNumParams(); ++I) {
    llvm::Type *T = FType->getParamType(I);
    if (T != Ops[I]->getType()) {
      Ops[I] = builder->CreateBitCast(Ops[I], T);
    }
  }
  return builder->CreateCall(F, Ops);
}
llvm::Value *
CodeGen_Hexagon::convertValueType(llvm::Value *V, llvm::Type *T) {
  if (T != V->getType())
    return builder->CreateBitCast(V, T);
  else
    return V;
}
void
CodeGen_Hexagon::getHighAndLowVectors(llvm::Value *DoubleVec,
                                      std::vector<llvm::Value *> &Res) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Value *> Ops;
  Ops.push_back(DoubleVec);
  Value *Hi =
    CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                      IPICK(Intrinsic::hexagon_V6_hi)), Ops);
  Value *Lo =
    CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                      IPICK(Intrinsic::hexagon_V6_lo)), Ops);
  Res.push_back(Hi);
  Res.push_back(Lo);
}
llvm::Value *
CodeGen_Hexagon::concatVectors(Value *High, Value *Low) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Value *>Ops;
  Ops.push_back(High);
  Ops.push_back(Low);
  Value *CombineCall =
    CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                      IPICK(Intrinsic::hexagon_V6_vcombine)), Ops);
  return CombineCall;
}
void
CodeGen_Hexagon::slice_into_halves(Expr a, std::vector<Expr> &Res) {
  if(!a.type().is_vector())
    return;
  Expr A_low, A_high;
  Type t = a.type();
  Type NewT = Type(t.code, t.bits, t.width/2);
  int NumElements = NewT.width;
  const Broadcast *B = a.as<Broadcast>();
  if (B) {
    A_low = Broadcast::make(B->value, NewT.width);
    A_high = Broadcast::make(B->value, NewT.width);
  } else {
    std::vector<Expr> ShuffleArgsALow, ShuffleArgsAHigh;
    ShuffleArgsALow.push_back(a);
    ShuffleArgsAHigh.push_back(a);
    for (int i = 0; i < NumElements; ++i) {
      ShuffleArgsALow.push_back(i);
      ShuffleArgsAHigh.push_back(i + NumElements);
    }
    A_low = Call::make(NewT, Call::shuffle_vector, ShuffleArgsALow,
                       Call::Intrinsic);
    A_high = Call::make(NewT, Call::shuffle_vector, ShuffleArgsAHigh,
                        Call::Intrinsic);
  }
  Res.push_back(A_low);
  Res.push_back(A_high);
  return;
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
  if (target.has_feature(Halide::Target::HVX_DOUBLE)) {
    debug(1) << "128 Byte mode\n";
    return 128*8;
  } else {
    debug(1) << "64 Byte mode\n";
    return 64*8;
  }
}

#ifdef THESE_ARE_UNUSED
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
#endif

static bool
isLargeVector(Type t, int vec_bits) {
  return t.is_vector() &&
    (t.bits * t.width) > (2 * vec_bits);
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
// Check to see if LoadA and LoadB are vectors of 'Width' elements and
// that they form interleaved loads of even and odd elements
//. i.e they are of the form.
// LoadA = A[ramp(base, 2, Width)]
// LoadB = A[ramp(base+1, 2, Width)]
bool checkInterleavedLoadPair(const Load *LoadA, const Load*LoadC, int Width) {
  if (!LoadA || !LoadC)
    return false;
  debug(4) << "HexCG: checkInterleavedLoadPair\n";
  debug(4) << "LoadA = " << LoadA->name << "[" << LoadA->index << "]\n";
  debug(4) << "LoadC = " << LoadC->name << "[" << LoadC->index << "]\n";
  const Ramp *RampA = LoadA->index.as<Ramp>();
  const Ramp *RampC = LoadC->index.as<Ramp>();
  if (!RampA  || !RampC) {
    debug(4) << "checkInterleavedLoadPair: Not both Ramps\n";
    return false;
  }
  const IntImm *StrideA = RampA->stride.as<IntImm>();
  const IntImm *StrideC = RampC->stride.as<IntImm>();

  if (StrideA->value != 2 || StrideC->value != 2) {
    debug(4) << "checkInterleavedLoadPair: Not all strides are 2\n";
    return false;
  }

  if (RampA->width != Width || RampC->width != Width) {
      debug(4) << "checkInterleavedLoadPair: Not all widths are 64\n";
      return false;
  }

  Expr BaseA = RampA->base;
  Expr BaseC = RampC->base;

  Expr DiffCA = simplify(BaseC - BaseA);
  debug (4) << "checkInterleavedLoadPair: BaseA = " << BaseA << "\n";
  debug (4) << "checkInterleavedLoadPair: BaseC = " << BaseC << "\n";
  debug (4) << "checkInterleavedLoadPair: DiffCA = " << DiffCA << "\n";
  if (is_one(DiffCA))
    return true;
  return false;
}
// Check to see if the elements of the 'matches' form a dot product
// of interleaved values, i.e, they are this for example
// matches[0]*matches[1] + matches[2]*matches[3]
// where matches[0] and matches[2] are interleaved loads
// i.e matches[0] is A[ramp(base, 2, Width)]
// and matches[2] is A[ramp(base+1, 2, Width)]
// Similarly, matches[1] is B[ramp(base, 2, Width)]
// and matches[3] is B[ramp(base+1, 2, Width)]
bool checkTwoWayDotProductOperandsCombinations(vector<Expr> &matches,
                                               int Width) {
  internal_assert(matches.size() == 4);
  // We accept only two combinations for now.
  // All four vector loads.
  // Two vector loads and two broadcasts.
  // Check if all four are loads
  int I;
  debug(4) << "HexCG: checkTwoWayDotProductOperandsCombinations\n";
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
        "checkTwoWayDotProductOperandsCombinations: All 4 loads, but not"
        " exactly two pairs of arrays\n";
      return false;
    }
    if (LoadA->name == LoadD->name){
      std::swap(matches[2], matches[3]);
      std::swap(LoadC, LoadD);
    }
    return checkInterleavedLoadPair(LoadA, LoadC, Width) &&
      checkInterleavedLoadPair(LoadB, LoadD, Width);
  } else {
    // Theoretically we can deal with all of them not being vector loads
    // (Hint: Think 4 broadcasts), but this is rare, so now we will deal
    // only with 2 broadcasts and 2 loads to catch this case for example
    // 3*f(2x) + 7*f(2x+1);
    const Load *InterleavedLoad1 = NULL, *InterleavedLoad2 = NULL;
    if (!LoadA && LoadB) {
      const Broadcast *BroadcastA = matches[0].as<Broadcast>();
      if (!BroadcastA) {
        debug(4) << "checkTwoWayDotProductOperandsCombinations: A is neither a"
          "vector load  nor a broadcast\n";
        return false;
      }
      InterleavedLoad1 = LoadB;
    } else if (LoadA && !LoadB) {
      const Broadcast *BroadcastB = matches[1].as<Broadcast>();
      if (!BroadcastB) {
        debug(4) << "checkTwoWayDotProductOperandsCombinations: B is neither a"
          " vector laod nor a broadcast\n";
        return false;
      }
      InterleavedLoad1 = LoadA;
      std::swap(matches[0], matches[1]);
    }
    if (!LoadC && LoadD) {
      const Broadcast *BroadcastC = matches[2].as<Broadcast>();
      if (!BroadcastC) {
        debug(4) << "checkTwoWayDotProductOperandsCombinations: C is neither a"
          " vector load nor a broadcast\n";
        return false;
      }
      InterleavedLoad2 = LoadD;
    } else if (LoadC && !LoadD) {
      const Broadcast *BroadcastD = matches[3].as<Broadcast>();
      if (!BroadcastD) {
        debug(4) << "checkTwoWayDotProductOperandsCombinations: D is neither a"
          "vector load nor a broadcast\n";
        return false;
      }
      InterleavedLoad2 = LoadC;
      std::swap(matches[2], matches[3]);
    }
    if (InterleavedLoad1->name != InterleavedLoad2->name)
      return false;
    // Todo: Should we be checking the broadcasts?
    return checkInterleavedLoadPair(InterleavedLoad1, InterleavedLoad2, Width);
  }
  return false;
}
bool CodeGen_Hexagon::shouldUseVDMPY(const Add *op,
                                     std::vector<Value *> &LLVMValues){
  Expr pattern;
  int I;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  pattern = WPICK( ((wild_i32x32 * wild_i32x32) + (wild_i32x32 * wild_i32x32)),
                   ((wild_i32x16 * wild_i32x16) + (wild_i32x16 * wild_i32x16)));
  vector<Expr> matches;
  if (expr_match(pattern, op, matches)) {
    internal_assert(matches.size() == 4);
    debug(4) << "HexCG: shouldUseVDMPY\n";
    for (I = 0; I < 4; ++I)
      debug(4) << "matches[" << I << "] = " << matches[I] << "\n";

    matches[0] = lossless_cast(Int(16, CPICK(32,16)), matches[0]);
    matches[1] = lossless_cast(Int(16, CPICK(32,16)), matches[1]);
    matches[2] = lossless_cast(Int(16, CPICK(32,16)), matches[2]);
    matches[3] = lossless_cast(Int(16, CPICK(32,16)), matches[3]);
    if (!matches[0].defined() || !matches[1].defined() ||
        !matches[2].defined() || !matches[3].defined())
      return false;
    if (!checkTwoWayDotProductOperandsCombinations(matches, CPICK(32,16)))
      return false;

    std::vector<Expr> vecA, vecB;
    vecA.push_back(matches[0]);
    vecA.push_back(matches[2]);
    vecB.push_back(matches[1]);
    vecB.push_back(matches[3]);
    Value *a = interleave_vectors(Int(16, CPICK(64,32)), vecA);
    Value *b = interleave_vectors(Int(16, CPICK(64,32)), vecB);
    LLVMValues.push_back(a);
    LLVMValues.push_back(b);
    return true;

  }
  return false;
}
bool CodeGen_Hexagon::shouldUseVMPA(const Add *op,
                                    std::vector<Value *> &LLVMValues){
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  Expr pattern;
  // pattern = (cast(Int(16, 64), wild_u8x64) * cast(Int(16, 64), wild_u8x64)) +
  //   (cast(Int(16, 64), wild_u8x64) * cast(Int(16, 64), wild_u8x64));
  pattern =
     WPICK(((wild_i16x128 * wild_i16x128) + (wild_i16x128 * wild_i16x128)),
           ((wild_i16x64 * wild_i16x64) + (wild_i16x64 * wild_i16x64)));
  vector<Expr> matches;
  if (expr_match(pattern, op, matches)) {
    debug(4) << "Pattern matched\n";
    internal_assert(matches.size() == 4);
    matches[0] = lossless_cast(UInt(8, CPICK(128,64)), matches[0]);
    matches[1] = lossless_cast(UInt(8, CPICK(128,64)), matches[1]);
    matches[2] = lossless_cast(UInt(8, CPICK(128,64)), matches[2]);
    matches[3] = lossless_cast(UInt(8, CPICK(128,64)), matches[3]);
    if (!matches[0].defined() || !matches[1].defined() ||
        !matches[2].defined() || !matches[3].defined())
      return false;
    if (!checkTwoWayDotProductOperandsCombinations(matches, CPICK(128,64)))
      return false;

    std::vector<Expr> vecA, vecB;
    vecA.push_back(matches[0]);
    vecA.push_back(matches[2]);
    vecB.push_back(matches[1]);
    vecB.push_back(matches[3]);
    Value *a = interleave_vectors(Int(8, CPICK(256,128)), vecA);
    Value *b = interleave_vectors(Int(8, CPICK(256,128)), vecB);
    LLVMValues.push_back(a);
    LLVMValues.push_back(b);
    return true;
  }
  return false;
}
void CodeGen_Hexagon::visit(const Add *op) {
  debug(4) << "HexagonCodegen: " << op->type << ", " << op->a
           << " + " << op->b << "\n";
  if (isLargeVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  std::vector<Value *>LLVMValues;
  if (shouldUseVMPA(op, LLVMValues)) {
    internal_assert (LLVMValues.size() == 2);
    Value *Lt = LLVMValues[0];
    Value *Rt = LLVMValues[1];
    llvm::Function *F =
      Intrinsic::getDeclaration(module, Intrinsic::hexagon_V6_vmpabuuv);
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
  if (shouldUseVDMPY(op, LLVMValues)) {
    internal_assert (LLVMValues.size() == 2);
    Value *Lt = LLVMValues[0];
    Value *Rt = LLVMValues[1];
    llvm::Function *F =
      Intrinsic::getDeclaration(module, Intrinsic::hexagon_V6_vdmpyhvsat);
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
    debug(4) << "HexagonCodegen: Generating Vd32.w=vdmpy(Vu32.h,Rt32.h):sat\n";
    return;
  }
  value = emitBinaryOp(op, varith);
  if (!value)
    CodeGen_Posix::visit(op);
  return;
}
  // Handle types greater than double vectors.
  // Types handled:
  // 1. u32x64 + u32x64 (Single Mode)
  // 2. i32x64 + i32x64 (Single Mode)
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Add *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  Patterns.push_back(WPICK(wild_u32x128, wild_u32x64)
                     + WPICK(wild_u32x128, wild_u32x64));
  Patterns.push_back(WPICK(wild_i32x128, wild_i32x64)
                     + WPICK(wild_i32x128, wild_i32x64));
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // There are two ways of doing this.
      // The first is to use slice_into_halves to create smaller
      // vectors that are still Halide IR of type u32x32 or i32x32.
      // We then codegen u32x32 + u32x32 for the lower and higher
      // parts and then put the result together using concat_vectors.
      // However, slice_into_halves creates shuffle_vector that
      // need to be lowered. Instead, we codegen the two u32x64/i32x64
      // operands of the add and then put them together using concat_vectors.
      Value *Op0 = codegen(matches[0]);
      Value *Op1 = codegen(matches[1]);
      int bytes_in_vector = (native_vector_bits() / 8);
      int VectorSize = (2 * bytes_in_vector)/4;
      // We now have a u32x64 vector, i.e. 2 vector register pairs.
      Value *EvenRegPairOp0 = slice_vector(Op0, 0, VectorSize);
      Value *OddRegPairOp0 = slice_vector(Op0, VectorSize, VectorSize);
      Value *EvenRegPairOp1 = slice_vector(Op1, 0, VectorSize);
      Value *OddRegPairOp1 = slice_vector(Op1, VectorSize, VectorSize);

      std::vector<Value *> Ops;
      Ops.push_back(EvenRegPairOp0);
      Ops.push_back(EvenRegPairOp1);
      Intrinsic::ID IntrinsID = IPICK(Intrinsic::hexagon_V6_vaddw_dv);
      Value *EvenLanes = CallLLVMIntrinsic(Intrinsic::
                                           getDeclaration(module, IntrinsID),
                                           Ops);
      Ops.clear();
      Ops.push_back(OddRegPairOp0);
      Ops.push_back(OddRegPairOp1);
      Value *OddLanes = CallLLVMIntrinsic(Intrinsic::
                                          getDeclaration(module, IntrinsID),
                                          Ops);
      Ops.clear();
      Ops.push_back(EvenLanes);
      Ops.push_back(OddLanes);
      Value *Result = concat_vectors(Ops);
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }
  return NULL;
}
  // Handle types greater than double vectors.
  // Also, only handles the case when the divisor is a power of 2.
  // Types handled:
  // 1. u32x64 (Single Mode)
  // 2. i32x64 (Single Mode)
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Div *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  Expr wild_u32x64_bc = Broadcast::make(wild_u32, 64);
  Expr wild_i32x64_bc = Broadcast::make(wild_i32, 64);
  Expr wild_u32x128_bc = Broadcast::make(wild_u32, 128);
  Expr wild_i32x128_bc = Broadcast::make(wild_i32, 128);

  Patterns.push_back(WPICK(wild_u32x128, wild_u32x64)
                     / WPICK(wild_u32x128_bc, wild_u32x64_bc));
  Patterns.push_back(WPICK(wild_i32x128, wild_i32x64)
                     / WPICK(wild_i32x128_bc, wild_i32x64_bc));

  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      int rt_shift_by = 0;
      if (is_const_power_of_two_integer(matches[1], &rt_shift_by)) {
        std::vector<Expr> VectorRegisterPairs;
        std::vector<Expr> VectorRegisters;
        // matches[0] is a vector of type u32x64 or i32x64.
        // 1. Slice it into halves, so we get two register pairs.
        slice_into_halves(matches[0], VectorRegisterPairs);
        // 2. Slice the first register pair that should form the
        //    even elements.
        slice_into_halves(VectorRegisterPairs[0], VectorRegisters);
        int num_words_in_vector = (native_vector_bits() / 8) / 4;
        Expr Divisor = Broadcast::make(matches[1], num_words_in_vector);
        // 3. Operate on each pair and then use hexagon_V6_combine
        //    to get u32x32 or i32x32 result.
        Value *LowEvenRegister = codegen(VectorRegisters[0]/Divisor);
        Value *HighEvenRegister = codegen(VectorRegisters[1]/Divisor);
        Value *EvenRegisterPair = concatVectors(HighEvenRegister,
                                                LowEvenRegister);
        VectorRegisters.clear();
        // 4. Slice the other half to get the lower lanes in two vector
        //    registers.
        slice_into_halves(VectorRegisterPairs[1], VectorRegisters);
        // 5. Operate on each pair and then use hexagon_V6_combine
        //    to get u32x32 or i32x32 result.
        Value *LowOddRegister = codegen(VectorRegisters[0]/Divisor);
        Value *HighOddRegister = codegen(VectorRegisters[1]/Divisor);
        Value *OddRegisterPair = concatVectors(HighOddRegister,
                                               LowOddRegister);
        std::vector<Value *> Ops;
        Ops.push_back(EvenRegisterPair);
        Ops.push_back(OddRegisterPair);
        // 6. Concatenate the two pairs to get a u32x64 or i32x64 result.
        Value *Result = concat_vectors(Ops);
        return convertValueType(Result, llvm_type_of(op->type));
      }
    }
  }
  return NULL;
}
void CodeGen_Hexagon::visit(const Div *op) {
  debug(1) << "HexCG: " << op->type <<  "visit(Div)\n";
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  value = emitBinaryOp(op, averages);
  if (!value) {
    if (isLargeVector(op->type, native_vector_bits()))
      value = handleLargeVectors(op);
    else {
      // If the Divisor is a multiple of 2, simply generate right shifts.
      std::vector<Pattern> Patterns;
      std::vector<Expr> matches;
      int WordsInVector  = native_vector_bits() / 32;
      int HalfWordsInVector = WordsInVector * 2;
      Patterns.push_back(Pattern(WPICK(wild_u32x32, wild_u32x16)
                                 / Broadcast::make(wild_u32, WordsInVector),
                                 IPICK(Intrinsic::hexagon_V6_vlsrw)));
      Patterns.push_back(Pattern(WPICK(wild_u16x64, wild_u16x32)
                                 / Broadcast::make(wild_u16, HalfWordsInVector),
                                 IPICK(Intrinsic::hexagon_V6_vlsrh)));
      Patterns.push_back(Pattern(WPICK(wild_i32x32, wild_i32x16)
                                 / Broadcast::make(wild_i32, WordsInVector),
                                 IPICK(Intrinsic::hexagon_V6_vasrw)));
      Patterns.push_back(Pattern(WPICK(wild_i16x64, wild_i16x32)
                                 / Broadcast::make(wild_i16, HalfWordsInVector),
                                 IPICK(Intrinsic::hexagon_V6_vasrh)));
      for (size_t I = 0; I < Patterns.size(); ++I) {
        const Pattern &P = Patterns[I];
        if (expr_match(P.pattern, op, matches)) {
          int rt_shift_by = 0;
          if (is_const_power_of_two_integer(matches[1], &rt_shift_by)) {
            Value *Vector = codegen(matches[0]);
            Value *ShiftBy = codegen(rt_shift_by);
            Intrinsic::ID IntrinsID = P.ID;
            std::vector<Value *> Ops;
            Ops.push_back(Vector);
            Ops.push_back(ShiftBy);
            Value *Result =
              CallLLVMIntrinsic(Intrinsic::getDeclaration(module, IntrinsID),
                                Ops);
            value = convertValueType(Result, llvm_type_of(op->type));
            return;
          }
        }
      }
    }
  }
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
static
bool isWideningVectorCast(const Cast *op) {
  Type t = op->type;
  Type e_type = op->value.type();
  return (t.is_vector() && e_type.is_vector() &&
          t.bits > e_type.bits);
}
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Cast *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  if (isWideningVectorCast(op)) {
    std::vector<Pattern> Patterns;
    std::vector<Expr> matches;
    Patterns.push_back(Pattern(cast(UInt(32, CPICK(128, 64)),
                                    WPICK(wild_u8x128, wild_u8x64)),
                               IPICK(Intrinsic::hexagon_V6_vzh)));
    Patterns.push_back(Pattern(cast(Int(32, CPICK(128, 64)),
                                    WPICK(wild_i8x128, wild_i8x64)),
                               IPICK(Intrinsic::hexagon_V6_vsh)));
    for (size_t I = 0; I < Patterns.size(); ++I) {
        const Pattern &P = Patterns[I];
        if (expr_match(P.pattern, op, matches)) {
          // You have a vector that is u8x64 in matches. Extend this to u32x64.
          debug(4) << "HexCG::" << op->type <<
            "handleLargeVectors(const Cast *)\n";
          debug(4) << "HexCG::First widening to 16bit elements\n";
          Intrinsic::ID IntrinsID = P.ID;
          Type NewT = Type(op->type.code, 16, op->type.width);
          Value *DoubleVector = codegen(cast(NewT, op->value));
          // We now have a vector is that u16x64/i16x64. Get the lower half of
          // this vector which contains the events elements of A and extend that
          // to 32 bits. Similarly, deal with the upper half of the double
          // vector.
          std::vector<Value *> Ops;
          debug(4) << "HexCG::" << "Widening lower vector reg elements(even)"
            " elements to 32 bit\n";
          getHighAndLowVectors(DoubleVector, Ops);
          Value *HiVecReg = Ops[0];
          Value *LowVecReg = Ops[1];

          Ops.clear();
          Ops.push_back(LowVecReg);
          Value *EvenRegisterPair =
            CallLLVMIntrinsic(Intrinsic::
                              getDeclaration(module,
                                             IntrinsID), Ops);
          debug(4) << "HexCG::" << "Widening higher vector reg elements(odd)"
            " elements to 32 bit\n";
          Ops.clear();
          Ops.push_back(HiVecReg);
          Value *OddRegisterPair =
            CallLLVMIntrinsic(Intrinsic::
                              getDeclaration(module,
                                             IntrinsID), Ops);
          Ops.clear();
          Ops.push_back(EvenRegisterPair);
          Ops.push_back(OddRegisterPair);
          // Note: At this point we are returning a concatenated vector of type
          // u32x64 or i32x64. This is essentially an illegal type for the
          // Hexagon HVX LLVM backend. However, we expect to break this
          // down before it is stored or whenever these are computed say by
          // a mul node, we expect the visitor to break them down and compose
          // them again.
          debug(4) << "HexCG::" << "Concatenating the two vectors\n";
          Value *V = concat_vectors(Ops);
          return convertValueType(V, llvm_type_of(op->type));
        }
    }
  } else {
    // Look for saturate & pack
    std::vector<Pattern> Patterns;
    std::vector<Expr> matches;
    Patterns.push_back(Pattern(u8_(min(WPICK(wild_u32x128, wild_u32x64),
                                       UINT_8_MAX)),
                               IPICK(Intrinsic::hexagon_V6_vsathub)));
    // Fixme: PDB: Shouldn't the signed version have a max in the pattern too?
    Patterns.push_back(Pattern(i8_(min(WPICK(wild_i32x128, wild_i32x64),
                                       INT_8_MAX)),
                               IPICK(Intrinsic::hexagon_V6_vsathub)));

    for (size_t I = 0; I < Patterns.size(); ++I) {
      const Pattern &P = Patterns[I];
      if (expr_match(P.pattern, op, matches)) {
        internal_assert(matches.size() == 1);
        Type FirstStepType = Type(op->type.code, 16, op->type.width);
        Value *FirstStep = codegen(cast(FirstStepType,
                                        (min(matches[0],
                                             FirstStepType.imax()))));
        std::vector<Value *> Ops;
        Intrinsic::ID IntrinsID = P.ID;
        // Ops[0] is the higher vectors and Ops[1] the lower.
        getHighAndLowVectors(FirstStep, Ops);
        Value *V = CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                                                               IntrinsID), Ops);
        return convertValueType(V, llvm_type_of(op->type));
      }
    }
    // This lowers the first step of u32x64->u8x64, which is a two step
    // saturate and pack. This first step converts a u32x64/i32x64 into u16x64/
    // i16x64
    Patterns.clear();
    matches.clear();
    Patterns.push_back(Pattern(u16_(min(WPICK(wild_u32x128, wild_u32x64),
                                        UINT_16_MAX)),
                               IPICK(Intrinsic::hexagon_V6_vsatwh)));
    Patterns.push_back(Pattern(i16_(min(WPICK(wild_i32x128, wild_i32x64),
                                        INT_16_MAX)),
                               IPICK(Intrinsic::hexagon_V6_vsatwh)));
    for (size_t I = 0; I < Patterns.size(); ++I) {
      const Pattern &P = Patterns[I];
      if (expr_match(P.pattern, op, matches)) {
        std::vector <Value *> Ops;
        Value *Vector = codegen(matches[0]);
        Intrinsic::ID IntrinsID = P.ID;
        int bytes_in_vector = native_vector_bits() / 8;
        int VectorSize = (2 * bytes_in_vector)/4;
        // We now have a u32x64 vector, i.e. 2 vector register pairs.
        Value *EvenRegPair = slice_vector(Vector, 0, VectorSize);
        Value *OddRegPair = slice_vector(Vector, VectorSize, VectorSize);
        // We now have the lower register pair in EvenRegPair.
        getHighAndLowVectors(EvenRegPair, Ops);
        // TODO: For v61 use hexagon_V6_vsathuwuh
        Value *EvenHalf =
          CallLLVMIntrinsic(Intrinsic::
                            getDeclaration(module, IntrinsID), Ops);
        Ops.clear();
        getHighAndLowVectors(OddRegPair, Ops);
        Value *OddHalf =
          CallLLVMIntrinsic(Intrinsic::
                            getDeclaration(module, IntrinsID), Ops);

        // EvenHalf & OddHalf are each one vector wide.
        Value *Result = concatVectors(OddHalf, EvenHalf);
        return convertValueType(Result, llvm_type_of(op->type));
      }
    }
  }
  return NULL;
}
void CodeGen_Hexagon::visit(const Cast *op) {
  vector<Expr> matches;
  debug(1) << "HexCG: " << op->type << ", " << "visit(Cast)\n";
  if (isWideningVectorCast(op)) {
      // ******** Part 1: Up casts (widening) ***************
      // Two step extensions.
      // i8x64 -> i32x64 <to do>
      // u8x64 -> u32x64
      if (isLargeVector(op->type, native_vector_bits())) {
        value = handleLargeVectors(op);
        if (value)
          return;
      }
      matches.clear();
      // Try to generate the following casts.
      // Vdd32.uh=vzxt(Vu32.ub) i.e. u8x64 -> u16x64
      // Vdd32.uw=vzxt(Vu32.uh) i.e. u16x32 -> u32x32
      // Vdd32.h=vsxt(Vu32.b) i.e. i8x64 -> i16x64
      // Vdd32.w=vsxt(Vu32.h) i.e. i16x32 -> i32x32
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
      matches.clear();

      // Vdd32.uh=vmpy(Vu32.ub,Vv32.ub)
      // Vdd32.h=vmpy(Vu32.b,Vv32.b)
      // Vdd32.uw=vmpy(Vu32.uh,Vv32.uh)
      // Vdd32.w=vmpy(Vu32.h,Vv32.h)
      // Vd32.h=vmpyi(Vu32.h,Vv32.h)
      for (size_t I = 0; I < multiplies.size(); ++I) {
        const Pattern &P = multiplies[I];
        if (expr_match(P.pattern, op, matches)) {
          internal_assert(matches.size() == 2);
          Intrinsic::ID ID = P.ID;
          llvm::Function *F = Intrinsic::getDeclaration(module, ID);
          llvm::FunctionType *FType = F->getFunctionType();
          bool InvertOperands = P.InvertOperands;
          Value *Lt = codegen(matches[0]);
          Value *Rt = codegen(matches[1]);
          if (InvertOperands)
            std::swap(Lt, Rt);

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
          return;
        }
      }
      // ******** End Part 1: Up casts (widening) ***************
    } else {
      // ******** Part 2: Down casts (Narrowing)* ***************
      // Two step downcasts.
      // std::vector<Expr> Patterns;
      //  Patterns.push_back(u8_(min(wild_u32x64, 255)));
    if (isLargeVector(op->value.type(), native_vector_bits())) {
        value = handleLargeVectors(op);
        if (value)
          return;
      }
      // Lets look for saturate and pack.
    bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
    std::vector<Pattern> SatAndPack;
    SatAndPack.push_back(Pattern(u8_(min(WPICK(wild_u16x128, wild_u16x64),
                                         255)),
                                 IPICK(Intrinsic::hexagon_V6_vsathub)));
    SatAndPack.push_back(Pattern(u8_(min(WPICK(wild_i16x128, wild_i16x64),
                                         255)),
                                 Intrinsic::hexagon_V6_vsathub));
    matches.clear();
    for (size_t I = 0; I < SatAndPack.size(); ++I) {
      const Pattern &P = SatAndPack[I];
      if (expr_match(P.pattern, op, matches)) {
        std::vector<Value *> Ops;
        Value *DoubleVector = codegen(matches[0]);
        getHighAndLowVectors(DoubleVector, Ops);
        Intrinsic::ID ID = P.ID;
        Value *SatAndPackInst =
          CallLLVMIntrinsic(Intrinsic::getDeclaration(module,ID), Ops);
        value = convertValueType(SatAndPackInst, llvm_type_of(op->type));
        return;
      }
    }
  }
  // ******** End Part 2: Down casts (Narrowing)* ***************

  CodeGen_Posix::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Call *op) {
  vector<Expr> matches;
  debug(1) << "HexCG: " << op->type << ", " << "visit(Call)\n";
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

  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  int VecSize = HEXAGON_SINGLE_MODE_VECTOR_SIZE;
  if (B128) VecSize *= 2;

  value = emitBinaryOp(op, vbitwise);
  if (!value) {
    if (op->name == Call::bitwise_not) {
      if (op->type.is_vector() &&
          ((op->type.bytes() * op->type.width) == VecSize)) {
        llvm::Function *F =
          Intrinsic::getDeclaration(module, IPICK(Intrinsic::hexagon_V6_vnot));
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
    } else if (op->name == Call::abs) {
      if (op->type.is_vector() &&
          ((op->type.bytes() * op->type.width) == 2 * VecSize)) {
        // vector sized absdiff should have been covered by the look up table
        // "combiners".
        std::vector<Pattern> doubleAbsDiff;

        doubleAbsDiff.push_back(Pattern(
          abs(WPICK(wild_u8x256 - wild_u8x256,wild_u8x128 - wild_u8x128)),
              IPICK(Intrinsic::hexagon_V6_vabsdiffub)));
        doubleAbsDiff.push_back(Pattern(
          abs(WPICK(wild_u16x128 - wild_u16x128,wild_u16x64 - wild_u16x64)),
              IPICK(Intrinsic::hexagon_V6_vabsdiffuh)));
        doubleAbsDiff.push_back(Pattern(
          abs(WPICK(wild_i16x128 - wild_i16x128,wild_i16x64 - wild_i16x64)),
              IPICK(Intrinsic::hexagon_V6_vabsdiffh)));
        doubleAbsDiff.push_back(Pattern(
          abs(WPICK(wild_i32x64 - wild_i32x64,wild_i32x32 - wild_i32x32)),
              IPICK(Intrinsic::hexagon_V6_vabsdiffw)));

        matches.clear();
        for (size_t I = 0; I < doubleAbsDiff.size(); ++I) {
          const Pattern &P = doubleAbsDiff[I];
          if (expr_match(P.pattern, op, matches)) {
            internal_assert(matches.size() == 2);
            Value *DoubleVector0 = codegen(matches[0]);
            Value *DoubleVector1 = codegen(matches[1]);
            std::vector<Value *> Ops0;
            std::vector<Value *> Ops1;
            getHighAndLowVectors(DoubleVector0, Ops0);
            getHighAndLowVectors(DoubleVector1, Ops1);
            internal_assert(Ops0.size() == 2);
            internal_assert(Ops1.size() == 2);
            std::swap(Ops0[0], Ops1[1]);
            // Now Ops0 has both the low vectors and
            // Ops1 has the high vectors.
            Intrinsic::ID ID = P.ID;
            Value *LowRes =
              CallLLVMIntrinsic(Intrinsic::getDeclaration(module, ID), Ops0);
            Value *HighRes =
              CallLLVMIntrinsic(Intrinsic::getDeclaration(module, ID), Ops1);
            Value *Result = concatVectors(HighRes, LowRes);
            value = convertValueType(Result, llvm_type_of(op->type));
            return;
          }
        }
      }
    }
    CodeGen_Posix::visit(op);
  }
}
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Mul *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  Expr wild_u32x64_bc = Broadcast::make(wild_u32, 64);
  Expr wild_i32x64_bc = Broadcast::make(wild_i32, 64);
  Expr wild_u32x128_bc = Broadcast::make(wild_u32, 128);
  Expr wild_i32x128_bc = Broadcast::make(wild_i32, 128);
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  Patterns.push_back(WPICK(wild_u32x128, wild_u32x64) *
                     WPICK(wild_u32x128_bc, wild_u32x64_bc));
  Patterns.push_back(WPICK(wild_i32x128, wild_i32x64) *
                     WPICK(wild_i32x128_bc, wild_i32x64_bc));
  Patterns.push_back(WPICK(wild_u32x128_bc, wild_u32x64_bc)
                     * WPICK(wild_u32x128, wild_u32x64));
  Patterns.push_back(WPICK(wild_i32x128_bc, wild_i32x64_bc)
                     * WPICK(wild_i32x128, wild_i32x64));

  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      Expr Vector, Other;
      // 1. Slice the two operands into halves, so we get four register pairs.
      // One of them is a broadcast. Make it half the width.
      if (matches[0].type().is_vector()) {
        Vector = matches[0];
        Other = op->b;
      } else {
        Vector = matches[1];
        Other = op->a;
      }
      slice_into_halves(Vector, VectorRegisterPairsA);
      slice_into_halves(Other, VectorRegisterPairsB);
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(A_low * B_low);
      Value *OddLanes = codegen(A_high * B_high);
      std::vector<Value *>Ops;
      Ops.push_back(EvenLanes);
      Ops.push_back(OddLanes);
      Value *Result = concat_vectors(Ops);
      return convertValueType(Result, llvm_type_of(op->type));    }
  }
  return NULL;
}
void CodeGen_Hexagon:: visit(const Mul *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  int VecSize = HEXAGON_SINGLE_MODE_VECTOR_SIZE;
  if (B128) VecSize *= 2;
  debug(1) << "HexCG: " << op->type << ", " << "visit(Mul)\n";
  if (isLargeVector(op->type, native_vector_bits())) {
    // Consider such Halide code.
    //  ImageParam input(type_of<uint8_t>(), 2);
    //  Func input_32("input_32");
    //  input_32(x, y) = cast<uint32_t>(input(x, y));
    //  Halide::Func rows("rows");
    //  rows(x, y) = (input_32(x,y)) + (10*input_32(x+1,y))
    // + (45 * input_32(x+2,y))  + (120 * input_32(x+3,y)) +
    //  rows.vectorize(x, 64);
    // The multiplication operations here are of type u32x64.
    // These need to be broken down into 4 u32x16 multiplies and then the result
    // needs to composed together.
    value = handleLargeVectors(op);
    if (value)
      return;
  }

  value = emitBinaryOp(op, multiplies);
  if (!value) {
    // There is a good chance we are dealing with
    // vector by scalar kind of multiply instructions.
    Expr A = op->a;

    if (A.type().is_vector() &&
          ((A.type().bytes() * A.type().width) ==
           2*VecSize)) {
      // If it is twice the hexagon vector width, then try
      // splitting into two vector multiplies.
      debug (4) << "HexCG: visit(Const Mul *op) ** \n";
      debug (4) << "op->a:" << op->a << "\n";
      debug (4) << "op->b:" << op->b << "\n";
      int types[] = {16, 32};
      std::vector<Expr> Patterns;
      for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        int HexagonVectorPairSizeInBits = 2 * native_vector_bits();
        Expr WildUCard = Variable::make(UInt(types[i]), "*");
        int Width = HexagonVectorPairSizeInBits / types[i];
        Expr WildUBroadcast = Broadcast::make(WildUCard, Width);
        Expr WildUIntVector = Variable::make(UInt(types[i], Width), "*");

        Expr WildICard = Variable::make(Int(types[i]), "*");
        Expr WildIBroadcast = Broadcast::make(WildICard, Width);
        Expr WildIntVector = Variable::make(Int(types[i], Width), "*");

        Patterns.push_back(WildUIntVector * WildUBroadcast);
        Patterns.push_back(WildUBroadcast * WildUIntVector);
        Patterns.push_back(WildIntVector * WildIBroadcast);
        Patterns.push_back(WildIBroadcast * WildIntVector);
      }
      std::vector<Expr> matches;
      Expr Vec, Other;
      for (size_t I = 0; I < Patterns.size(); ++I) {
        const Expr P = Patterns[I];
        if (expr_match(P, op, matches)) {
          //__builtin_HEXAGON_V6_vmpyhss, __builtin_HEXAGON_V6_hi
          debug (4)<< "HexCG: Going to generate __builtin_HEXAGON_V6_vmpyhss\n";
          // Exactly one of them definitely has to be a vector.
          if (matches[0].type().is_vector()) {
            Vec = matches[0];
            Other = matches[1];
          } else {
            Vec = matches[1];
            Other = matches[0];
          }
          debug(4) << "vector " << Vec << "\n";
          debug(4) << "Broadcast " << Other << "\n";
          const IntImm *Imm = Other.as<IntImm>();
          if (!Imm) {
            const Cast *C = Other.as<Cast>();
            if (C) {
              Imm = C->value.as<IntImm>();
            }
          }
          if (Imm) {
            int ImmValue = Imm->value;
            int ScalarValue = 0;
            Intrinsic::ID IntrinsID = (Intrinsic::ID) 0;
            if (Vec.type().bits == 16
                && ImmValue <= UInt(8).imax()) {
              int A = ImmValue & 0xFF;
              int B = A | (A << 8);
              ScalarValue = B | (B << 16);
              IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyihb);
            } else if (Vec.type().bits == 32) {
              if (ImmValue <= UInt(8).imax()) {
                int A = ImmValue & 0xFF;
                int B = A | (A << 8);
                ScalarValue = B | (B << 16);
                IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyiwb);
              } else if (ImmValue <= UInt(16).imax()) {
                ScalarValue = ImmValue & 0xFFFF;
                IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyiwh);
              } else
                internal_error <<
                  "Cannot deal with an Imm value greater than 16"
                  "bits in generating vmpyi\n";
            } else
              internal_error << "Unhandled case in visit(Mul *)\n";
            Expr ScalarImmExpr = IntImm::make(ScalarValue);
            Value *Scalar = codegen(ScalarImmExpr);
            Value *VectorOp = codegen(Vec);
            std::vector<Value *> Ops;
            debug(4) << "HexCG: Generating vmpyhss\n";

            getHighAndLowVectors(VectorOp, Ops);
            Value *HiCall = Ops[0];  //Odd elements.
            Value *LoCall = Ops[1];  //Even elements
            Ops.clear();
            Ops.push_back(HiCall);
            Ops.push_back(Scalar);
            Value *Call1 =  //Odd elements
              CallLLVMIntrinsic(Intrinsic::
                                getDeclaration(module, IntrinsID), Ops);
            Ops.clear();
            Ops.push_back(LoCall);
            Ops.push_back(Scalar);
            Value *Call2 =        // Even elements.
              CallLLVMIntrinsic(Intrinsic::
                                getDeclaration(module, IntrinsID), Ops);
            Ops.clear();
            Ops.push_back(Call1);
            Ops.push_back(Call2);
            IntrinsID = IPICK(Intrinsic::hexagon_V6_vcombine);
            Value *CombineCall =
              CallLLVMIntrinsic(Intrinsic::
                                getDeclaration(module, IntrinsID), Ops);
            Halide::Type DestType = op->type;
            llvm::Type *DestLLVMType = llvm_type_of(DestType);
            if (DestLLVMType != CombineCall->getType())
                value = builder->CreateBitCast(CombineCall, DestLLVMType);
            else
              value = CombineCall;
            return;
          }
        }
      }
    }
  }
  if (!value) {
    CodeGen_Posix::visit(op);
    return;
  }

}
void CodeGen_Hexagon::visit(const Broadcast *op) {
    bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

    //    int Width = op->width;
    Expr WildI32 = Variable::make(Int(32), "*");
    Expr PatternMatch = Broadcast::make(WildI32, CPICK(32,16));
    vector<Expr> Matches;
    if (expr_match(PatternMatch, op, Matches)) {
        //    if (Width == 16) {
      Intrinsic::ID ID = IPICK(Intrinsic::hexagon_V6_lvsplatw);
      llvm::Function *F = Intrinsic::getDeclaration(module, ID);
      Value *Op1 = codegen(op->value);
      value = builder->CreateCall(F, Op1);
      return;
    }
    CodeGen_Posix::visit(op);
}

}}

