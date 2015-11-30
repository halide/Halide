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
#include "IntegerDivisionTable.h"
#include "IRPrinter.h"

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

#define UINT_8_MAX UInt(8).max()
#define UINT_8_MIN UInt(8).min()
#define UINT_16_MAX UInt(16).max()
#define UINT_16_MIN UInt(16).min()
#define INT_8_MAX Int(8).max()
#define INT_8_MIN Int(8).min()
#define INT_16_MAX Int(16).max()
#define INT_16_MIN Int(16).min()

#define UINT_8_IMAX 255
#define UINT_8_IMIN 0
#define UINT_16_IMAX 65535
#define UINT_16_IMIN 0
#define INT_8_IMAX 127
#define INT_8_IMIN -128
#define INT_16_IMAX 32767
#define INT_16_IMIN -32768

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

Expr shiftRight(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::shift_right, {A, B},
                              Internal::Call::Intrinsic);
}
#endif

Expr u8_(Expr E) {
  return cast (UInt(8, E.type().lanes()), E);
}
Expr i8_(Expr E) {
  return cast (Int(8, E.type().lanes()), E);
}Expr u16_(Expr E) {
  return cast (UInt(16, E.type().lanes()), E);
}
Expr i16_(Expr E) {
  return cast (Int(16, E.type().lanes()), E);
}
Expr u32_(Expr E) {
  return cast (UInt(32, E.type().lanes()), E);
}
Expr i32_(Expr E) {
  return cast (Int(32, E.type().lanes()), E);
}
}


CodeGen_Hexagon::CodeGen_Hexagon(Target t)
  : CodeGen_Posix(t),
    wild_i32(Variable::make(Int(32), "*")),
    wild_u32(Variable::make(UInt(32), "*")),
    wild_i16(Variable::make(Int(16), "*")),
    wild_u16(Variable::make(UInt(16), "*")),
    wild_i8(Variable::make(Int(8), "*")),
    wild_u8(Variable::make(UInt(8), "*")) {
  bool B128 = t.has_feature(Halide::Target::HVX_DOUBLE);
  casts.push_back(Pattern(cast(UInt(16, CPICK(128,64)),
                               WPICK(wild_u8x128,wild_u8x64)),
                          IPICK(Intrinsic::hexagon_V6_vzb)));
  casts.push_back(Pattern(cast(UInt(16, CPICK(128,64)),
                               WPICK(wild_i8x128,wild_i8x64)),
                          IPICK(Intrinsic::hexagon_V6_vzb)));
  casts.push_back(Pattern(cast(Int(16, CPICK(128,64)),
                               WPICK(wild_u8x128,wild_u8x64)),
                          IPICK(Intrinsic::hexagon_V6_vzb)));
  casts.push_back(Pattern(cast(UInt(32, CPICK(64,32)),
                               WPICK(wild_u16x64,wild_u16x32)),
                          IPICK(Intrinsic::hexagon_V6_vzh)));
  casts.push_back(Pattern(cast(Int(32, CPICK(64,32)),
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
  combiners.push_back(Pattern(absd(WPICK(wild_u8x128,wild_u8x64),
                                  WPICK(wild_u8x128,wild_u8x64)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffub)));
  combiners.push_back(Pattern(absd(WPICK(wild_u16x64,wild_u16x32),
                                  WPICK(wild_u16x64,wild_u16x32)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffuh)));
  combiners.push_back(Pattern(absd(WPICK(wild_i16x64,wild_i16x32),
                                  WPICK(wild_i16x64,wild_i16x32)),
                                  IPICK(Intrinsic::hexagon_V6_vabsdiffh)));
  combiners.push_back(Pattern(absd(WPICK(wild_i32x32,wild_i32x16),
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
  if (t.has_feature(Halide::Target::HVX_V62)) {
    varith.push_back(Pattern(WPICK(wild_u32x32,wild_u32x16) +
                           WPICK(wild_u32x32,wild_u32x16),
                           IPICK(Intrinsic::hexagon_V6_vadduwsat)));
  } else {
    // Note: no 32-bit saturating unsigned add in V60, use vaddw
    varith.push_back(Pattern(WPICK(wild_u32x32,wild_u32x16) +
                           WPICK(wild_u32x32,wild_u32x16),
                           IPICK(Intrinsic::hexagon_V6_vaddw)));
  }

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
  if (t.has_feature(Halide::Target::HVX_V62)) {
    varith.push_back(Pattern(WPICK(wild_u32x64,wild_u32x32) +
                           WPICK(wild_u32x64,wild_u32x32),
                           IPICK(Intrinsic::hexagon_V6_vadduwsat_dv)));
  } else {
    // Note: no 32-bit saturating unsigned add in V60, use vaddw
    varith.push_back(Pattern(WPICK(wild_u32x64,wild_u32x32) +
                           WPICK(wild_u32x64,wild_u32x32),
                           IPICK(Intrinsic::hexagon_V6_vaddw_dv)));
  }


  // "Sub"
  // Byte Vectors
  varith.push_back(Pattern(WPICK(wild_i8x128,wild_i8x64) -
                           WPICK(wild_i8x128,wild_i8x64),
                           IPICK(Intrinsic::hexagon_V6_vsubb)));
  varith.push_back(Pattern(WPICK(wild_u8x128,wild_u8x64) -
                           WPICK(wild_u8x128,wild_u8x64),
                           IPICK(Intrinsic::hexagon_V6_vsububsat)));
  // Half Vectors
  varith.push_back(Pattern(WPICK(wild_i16x64,wild_i16x32) -
                           WPICK(wild_i16x64,wild_i16x32),
                           IPICK(Intrinsic::hexagon_V6_vsubh)));
  varith.push_back(Pattern(WPICK(wild_u16x64,wild_u16x32) -
                           WPICK(wild_u16x64,wild_u16x32),
                           IPICK(Intrinsic::hexagon_V6_vsubuhsat)));
  // Word Vectors.
  varith.push_back(Pattern(WPICK(wild_i32x32,wild_i32x16) -
                           WPICK(wild_i32x32,wild_i32x16),
                           IPICK(Intrinsic::hexagon_V6_vsubw)));
  // Double Vectors
  // Byte Double Vectors
  varith.push_back(Pattern(WPICK(wild_i8x256,wild_i8x128) -
                           WPICK(wild_i8x256,wild_i8x128),
                           IPICK(Intrinsic::hexagon_V6_vsubb_dv)));
  varith.push_back(Pattern(WPICK(wild_u8x256,wild_u8x128) -
                           WPICK(wild_u8x256,wild_u8x128),
                           IPICK(Intrinsic::hexagon_V6_vsububsat_dv)));
  // Half Double Vectors
  varith.push_back(Pattern(WPICK(wild_i16x128,wild_i16x64) -
                           WPICK(wild_i16x128,wild_i16x64),
                           IPICK(Intrinsic::hexagon_V6_vsubh_dv)));
  varith.push_back(Pattern(WPICK(wild_u16x128,wild_u16x64) -
                           WPICK(wild_u16x128,wild_u16x64),
                           IPICK(Intrinsic::hexagon_V6_vsubuhsat_dv)));
  // Word Double Vectors.
  varith.push_back(Pattern(WPICK(wild_i32x64,wild_i32x32) -
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
  Type NewT = Type(t.code(), t.bits(), t.lanes()/2);
  int NumElements = NewT.lanes();
  const Broadcast *B = a.as<Broadcast>();
  if (B) {
    A_low = Broadcast::make(B->value, NewT.lanes());
    A_high = Broadcast::make(B->value, NewT.lanes());
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
  if (target.has_feature(Halide::Target::HVX_V62))
    return "hexagonv62";
  else
    return "hexagonv60";
}

string CodeGen_Hexagon::mattrs() const {
  return "+hvx";
}

bool CodeGen_Hexagon::use_soft_float_abi() const {
  return false;
}
static bool EmittedOnce = false;
int CodeGen_Hexagon::native_vector_bits() const {
  bool DoTrace = ! EmittedOnce;
  EmittedOnce = true;
  if (target.has_feature(Halide::Target::HVX_DOUBLE)) {
    if (DoTrace) debug(1) << "128 Byte mode\n";
    return 128*8;
  } else {
    if (DoTrace) debug(1) << "64 Byte mode\n";
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

#ifdef HEX_CG_ASSERTS
  static bool enableVecAsserts = true;
#else
  static bool enableVecAsserts = false;
#endif

static int show_chk = -1;

// checkVectorOp
//
// Check to see if the op has a vector type.
// If so, report an error or warning (as selected) .
// This routine is to be called before calling the CodeGen_Posix visitor
// to detect when we are scalarizing a vector operation.
static void
checkVectorOp(const Expr op, string msg) {
  if (show_chk < 0) { // Not yet initialized.
    #ifdef _WIN32
    char chk[128];
    size_t read = 0;
    getenv_s(&read, chk, "HEX_CHK");
    if (read)
    #else
    if (char *chk = getenv("HEX_CHK"))
    #endif
    {
      show_chk = atoi(chk);
    }
  }
  if (op.type().is_vector()) {
    debug(1) << "VEC-EXPECT " << msg ;
    if (enableVecAsserts)
      internal_error << "VEC-EXPECT " << msg;
    if (show_chk > 0) {
      user_warning << "Unsupported vector op: "
                   << op.type() << " " << msg
                   << "  " << op << "\n";
    }
  }
}
static bool isDblVector(Type t, int vec_bits) {
  return t.is_vector() && (
    ((t.bits() * t.lanes()) == (2 * vec_bits)));
}
static bool isQuadVector(Type t, int vec_bits) {
  return t.is_vector() && (
    ((t.bits() * t.lanes()) == (4 * vec_bits)));
}
static bool isDblOrQuadVector(Type t, int vec_bits) {
  return t.is_vector() && (
    ((t.bits() * t.lanes()) == (2 * vec_bits)) ||
    ((t.bits() * t.lanes()) == (4 * vec_bits)));
}
static bool
isLargerThanVector(Type t, int vec_bits) {
  return t.is_vector() &&
    (t.bits() * t.lanes()) > vec_bits;
}
static bool
isLargerThanDblVector(Type t, int vec_bits) {
  return t.is_vector() &&
    (t.bits() * t.lanes()) > (2 * vec_bits);
}
static bool
isValidHexagonVector(Type t, int vec_bits) {
  return t.is_vector() &&
    ((t.bits() * t.lanes()) == vec_bits);
}
static bool
isValidHexagonVectorPair(Type t, int vec_bits) {
  return t.is_vector() &&
    ((t.bits() * t.lanes()) == 2*vec_bits);
}
int CodeGen_Hexagon::bytes_in_vector() const {
  return native_vector_bits() / 8;
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

Expr lossless_cast_cmp(Type t, Expr e) {
  const EQ *eq = e.as<EQ>();
  const NE *ne = e.as<NE>();
  const LT *lt = e.as<LT>();
  const LE *le = e.as<LE>();
  const GT *gt = e.as<GT>();
  const GE *ge = e.as<GE>();
  Expr a = eq ? eq->a : (ne ? ne->a : (lt ? lt->a : (gt ? gt->a : (ge ? ge->a :
                                                                   Expr()))));
  Expr b = eq ? eq->b : (ne ? ne->b : (lt ? lt->b : (gt ? gt->b : (ge ? ge->b :
                                                                   Expr()))));
  if (!a.defined() || !b.defined())
    return Expr();
  a = lossless_cast(t, a);
  b = lossless_cast(t, b);
  if (!a.defined() || !b.defined())
    return Expr();
  if (eq)
    return EQ::make(a, b);
  else if (ne)
    return NE::make(a, b);
  else if (lt)
    return LT::make(a, b);
  else if (le)
    return LE::make(a, b);
  else if (gt)
    return GT::make(a, b);
  else if (ge)
    return GE::make(a, b);
  else
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

  if (RampA->lanes != Width || RampC->lanes != Width) {
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
bool CodeGen_Hexagon::possiblyGenerateVMPAAccumulate(const Add *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern> Patterns;

  // Convert A:zxt(a:u8x64) + B:zxt(bu8x64) + C:i16x64 ->
  // C += (vcombine(a,b).ub, 0x01010101)
  //
  // PDB: FIXME: Is it ok to accumulate into C? For instance, if C is needed
  // in another iteration, then C will need to be reloaded, right? Consider this
  // Halide code.
  //   ImageParam g(type_of<uint8_t>(), 2);
  //   Halide::Func f, g_16;
  //   g_16(x, y) = cast<int16_t> g(x, y);
  //   f(x, y) = g_16(x, y-1) + g_16(x, y) + g_16(x, y+1)
  // In this case the vectors for g_16(x, y) and g_16(x, y+1) can be reused,
  // but if we accumulate into either one of the two, then reuse is not
  // possible.

  Patterns.push_back(Pattern(WPICK((wild_i16x128 + wild_i16x128 + wild_i16x128),
                                   (wild_i16x64 + wild_i16x64 + wild_i16x64)),
                             IPICK(Intrinsic::hexagon_V6_vmpabus_acc)));
  Patterns.push_back(Pattern(WPICK((wild_i32x64 + wild_i32x64 + wild_i32x64),
                                   (wild_i32x32 + wild_i32x32 + wild_i32x32)),
                             IPICK(Intrinsic::hexagon_V6_vmpahb_acc)));
  vector<Expr> matches;
  for (size_t I = 0; I < Patterns.size(); ++I) {
    const Pattern &P = Patterns[I];
    if (expr_match(P.pattern, op, matches)) {
      internal_assert(matches.size() == 3);
      Expr Op0, Op1, Op2;
      Expr Acc, OpA, OpB;
      Op0 = lossless_cast(UInt(8, CPICK(128, 64)), matches[0]);
      Op1 = lossless_cast(UInt(8, CPICK(128, 64)), matches[1]);
      Op2 = lossless_cast(UInt(8, CPICK(128, 64)), matches[2]);
      if (Op0.defined() && Op1.defined() && !Op2.defined()) {
        Acc = matches[2]; OpA = Op0; OpB = Op1;
      } else if (Op0.defined() && !Op1.defined() && Op2.defined()) {
        Acc = matches[1]; OpA = Op0; OpB = Op2;
      } else if (!Op0.defined() && Op1.defined() && Op2.defined()) {
        Acc = matches[0]; OpA = Op1; OpB = Op2;
      } else if (Op0.defined() && Op1.defined() && Op2.defined()) {
        Acc = matches[0]; OpA = Op1; OpB = Op2;
      } else
        return false;
      Value *Accumulator = codegen(Acc);
      Value *Operand0 = codegen(OpA);
      Value *Operand1 = codegen(OpB);
      Value *Multiplicand = concatVectors(Operand0, Operand1);
      int ScalarValue = 0x01010101;
      Expr ScalarImmExpr = IntImm::make(Int(32), ScalarValue);
      Value *Scalar = codegen(ScalarImmExpr);
      std::vector<Value *> Ops;
      Ops.push_back(Accumulator);
      Ops.push_back(Multiplicand);
      Ops.push_back(Scalar);
      Intrinsic::ID IntrinsID = P.ID;
      Value *Result = CallLLVMIntrinsic(Intrinsic::
                                        getDeclaration(module, IntrinsID),
                                        Ops);
      value =  convertValueType(Result, llvm_type_of(op->type));
      return true;
    }
  }
  return false;
}
void CodeGen_Hexagon::visit(const Add *op) {
  debug(4) << "HexagonCodegen: " << op->type << ", " << op->a
           << " + " << op->b << "\n";
  if (isLargerThanDblVector(op->type, native_vector_bits())) {
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
  // See if you can generate Vdd.h += vmpa(Vuu.ub, Rt.b)
  if(possiblyGenerateVMPAAccumulate(op)) {
    return;
  }
  if (!value)
    value = emitBinaryOp(op, varith);
  if (!value &&
      isLargerThanVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  if (!value) {
    checkVectorOp(op, "in visit(Add *)\n");
    CodeGen_Posix::visit(op);
  }
  return;
}

llvm::Value *
CodeGen_Hexagon::possiblyCodeGenWideningMultiplySatRndSat(const Div *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Expr> Patterns, matches;
  int num_hw_pair = (bytes_in_vector() / 2) * 2; // num half words in a pair.
  int num_w_quad = (bytes_in_vector() / 4) * 4;
  Patterns.push_back(((WPICK(wild_i32x128, wild_i32x64) *
                      WPICK(wild_i32x128, wild_i32x64)) + (1 << 14))
                     / Broadcast::make(wild_i32, num_w_quad));
  Patterns.push_back(((WPICK(wild_i16x128, wild_i16x64) *
                      WPICK(wild_i16x128, wild_i16x64)) + (1 << 14))
                     / Broadcast::make(wild_i16, num_hw_pair));
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      Type t = matches[0].type();
      if (t.bits() == 32) {
        Type narrow = Type(t.code(), (t.bits()/2), t.lanes());
        matches[0] = lossless_cast(narrow, matches[0]);
        matches[1] = lossless_cast(narrow, matches[1]);
        if (!matches[0].defined() || !matches[1].defined())
          return NULL;
      }
      int rt_shift_by = 0;
      if (is_const_power_of_two_integer(matches[2], &rt_shift_by)
          && rt_shift_by == 15) {
        Intrinsic::ID IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyhvsrs);
        std::vector<Value *> Ops, OpsA, OpsB;
        Value *DoubleVecA = codegen(matches[0]);
        Value *DoubleVecB = codegen(matches[1]);
        getHighAndLowVectors(DoubleVecA, OpsA);
        getHighAndLowVectors(DoubleVecB, OpsB);
        Ops.push_back(OpsA[0]);
        Ops.push_back(OpsB[0]);
        Value *HighRes = CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                                                                     IntrinsID),
                                           Ops);
        Ops.clear();
        Ops.push_back(OpsA[1]);
        Ops.push_back(OpsB[1]);
        Value *LowRes = CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                                                                    IntrinsID),
                                           Ops);
        Ops.clear();
        Ops.push_back(LowRes);
        Ops.push_back(HighRes);
        if (t.bits() != 32)
          return convertValueType(concat_vectors(Ops), llvm_type_of(op->type));
        else
          return convertValueType(concat_vectors(Ops),
                                  llvm_type_of(matches[0].type()));
      } else
        return NULL;
    }
  }
  return NULL;
}
void CodeGen_Hexagon::visit(const Div *op) {
  debug(1) << "HexCG: " << op->type <<  ", visit(Div)\n";
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  value = emitBinaryOp(op, averages);
  if (!value) {
    std::vector<Pattern> Patterns;
    std::vector<Expr> matches;
    if (isLargerThanDblVector(op->type, native_vector_bits()))
      value = handleLargeVectors(op);
    else if (isValidHexagonVectorPair(op->type, native_vector_bits())) {
      if (possiblyCodeGenWideningMultiplySatRndSat(op))
        return;
    }
    else {
      // If the Divisor is a power of 2, simply generate right shifts.
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

  if (!value &&
      isLargerThanVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  if (!value) {
    checkVectorOp(op, "in visit(Div *)\n");
    CodeGen_Posix::visit(op);
  }
  return;
}
void CodeGen_Hexagon::visit(const Sub *op) {
  if (isLargerThanDblVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  value = emitBinaryOp(op, varith);
  if (!value &&
      isLargerThanVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  if (!value) {
    checkVectorOp(op, "in visit(Sub *)\n");
    CodeGen_Posix::visit(op);
  }
  return;
}
void CodeGen_Hexagon::visit(const Max *op) {
  if (isLargerThanVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  value = emitBinaryOp(op, varith);
  if (!value) {
    checkVectorOp(op, "in visit(Max *)\n");
    CodeGen_Posix::visit(op);
  }
  return;
}
void CodeGen_Hexagon::visit(const Min *op) {
  if (isLargerThanVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  value = emitBinaryOp(op, varith);
  if (!value) {
    checkVectorOp(op, "in visit(Min *)\n");
    CodeGen_Posix::visit(op);
  }
  return;
}

/////////////////////////////////////////////////////////////////////////////
// Start of handleLargeVectors

// Handle Add on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Add *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  // 4096-bit vector + vector
  Patterns.push_back(wild_u32x128 + wild_u32x128);
  Patterns.push_back(wild_i32x128 + wild_i32x128);
  Patterns.push_back(wild_u16x256 + wild_u16x256);
  Patterns.push_back(wild_i16x256 + wild_i16x256);
  Patterns.push_back(wild_u8x512  + wild_u8x512);
  Patterns.push_back(wild_i8x512  + wild_i8x512);

  // 2048-bit vector + vector
  Patterns.push_back(wild_u32x64  + wild_u32x64);
  Patterns.push_back(wild_i32x64  + wild_i32x64);
  Patterns.push_back(wild_u16x128 + wild_u16x128);
  Patterns.push_back(wild_i16x128 + wild_i16x128);
  Patterns.push_back(wild_u8x256  + wild_u8x256);
  Patterns.push_back(wild_i8x256  + wild_i8x256);

  // 1024-bit vector + vector
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(wild_u32x32 + wild_u32x32);
    Patterns.push_back(wild_i32x32 + wild_i32x32);
    Patterns.push_back(wild_u16x64 + wild_u16x64);
    Patterns.push_back(wild_i16x64 + wild_i16x64);
    Patterns.push_back(wild_u8x128 + wild_u8x128);
    Patterns.push_back(wild_i8x128 + wild_i8x128);
  }

  debug(4) << "HexCG: " << op->type <<  ", handleLargeVectors (Add)\n";
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      slice_into_halves(matches[0], VectorRegisterPairsA);
      slice_into_halves(matches[1], VectorRegisterPairsB);

      // 2. Operate on the halves
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(A_low + B_low);
      Value *OddLanes = codegen(A_high + B_high);

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(OddLanes, EvenLanes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(EvenLanes);
        Ops.push_back(OddLanes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }

  return NULL;
}

// Handle Sub on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Sub *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  // 4096-bit vector - vector
  Patterns.push_back(wild_u32x128 - wild_u32x128);
  Patterns.push_back(wild_i32x128 - wild_i32x128);
  Patterns.push_back(wild_u16x256 - wild_u16x256);
  Patterns.push_back(wild_i16x256 - wild_i16x256);
  Patterns.push_back(wild_u8x512  - wild_u8x512);
  Patterns.push_back(wild_i8x512  - wild_i8x512);

  // 2048-bit vector - vector
  Patterns.push_back(wild_u32x64  - wild_u32x64);
  Patterns.push_back(wild_i32x64  - wild_i32x64);
  Patterns.push_back(wild_u16x128 - wild_u16x128);
  Patterns.push_back(wild_i16x128 - wild_i16x128);
  Patterns.push_back(wild_u8x256  - wild_u8x256);
  Patterns.push_back(wild_i8x256  - wild_i8x256);

  // 1024-bit vector - vector
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(wild_u32x32 - wild_u32x32);
    Patterns.push_back(wild_i32x32 - wild_i32x32);
    Patterns.push_back(wild_u16x64 - wild_u16x64);
    Patterns.push_back(wild_i16x64 - wild_i16x64);
    Patterns.push_back(wild_u8x128 - wild_u8x128);
    Patterns.push_back(wild_i8x128 - wild_i8x128);
  }

  debug(4) << "HexCG: " << op->type <<  ", handleLargeVectors (Add)\n";
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      slice_into_halves(matches[0], VectorRegisterPairsA);
      slice_into_halves(matches[1], VectorRegisterPairsB);

      // 2. Operate on the halves
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(A_low - B_low);
      Value *OddLanes = codegen(A_high - B_high);

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(OddLanes, EvenLanes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(EvenLanes);
        Ops.push_back(OddLanes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }

  return NULL;
}

// Handle Min on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Min *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  // 4096-bit min(vector, vector)
  Patterns.push_back(min(wild_u32x128, wild_u32x128));
  Patterns.push_back(min(wild_i32x128, wild_i32x128));
  Patterns.push_back(min(wild_u16x256, wild_u16x256));
  Patterns.push_back(min(wild_i16x256, wild_i16x256));
  Patterns.push_back(min(wild_u8x512,  wild_u8x512));
  Patterns.push_back(min(wild_i8x512,  wild_i8x512));

  // 2048-bit min(vector, vector)
  Patterns.push_back(min(wild_u32x64,  wild_u32x64));
  Patterns.push_back(min(wild_i32x64,  wild_i32x64));
  Patterns.push_back(min(wild_u16x128, wild_u16x128));
  Patterns.push_back(min(wild_i16x128, wild_i16x128));
  Patterns.push_back(min(wild_u8x256,  wild_u8x256));
  Patterns.push_back(min(wild_i8x256,  wild_i8x256));

  // 1024-bit min(vector, vector)
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(min(wild_u32x32, wild_u32x32));
    Patterns.push_back(min(wild_i32x32, wild_i32x32));
    Patterns.push_back(min(wild_u16x64, wild_u16x64));
    Patterns.push_back(min(wild_i16x64, wild_i16x64));
    Patterns.push_back(min(wild_u8x128, wild_u8x128));
    Patterns.push_back(min(wild_i8x128, wild_i8x128));
  }

  debug(4) << "HexCG: " << op->type <<  ", handleLargeVectors (Min)\n";
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      slice_into_halves(matches[0], VectorRegisterPairsA);
      slice_into_halves(matches[1], VectorRegisterPairsB);

      // 2. Operate on the halves
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(min(A_low, B_low));
      Value *OddLanes = codegen(min(A_high, B_high));

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(OddLanes, EvenLanes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(EvenLanes);
        Ops.push_back(OddLanes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }

  return NULL;
}

// Handle Max on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Max *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  // 4096-bit max(vector, vector)
  Patterns.push_back(max(wild_u32x128, wild_u32x128));
  Patterns.push_back(max(wild_i32x128, wild_i32x128));
  Patterns.push_back(max(wild_u16x256, wild_u16x256));
  Patterns.push_back(max(wild_i16x256, wild_i16x256));
  Patterns.push_back(max(wild_u8x512,  wild_u8x512));
  Patterns.push_back(max(wild_i8x512,  wild_i8x512));

  // 2048-bit max(vector, vector)
  Patterns.push_back(max(wild_u32x64,  wild_u32x64));
  Patterns.push_back(max(wild_i32x64,  wild_i32x64));
  Patterns.push_back(max(wild_u16x128, wild_u16x128));
  Patterns.push_back(max(wild_i16x128, wild_i16x128));
  Patterns.push_back(max(wild_u8x256,  wild_u8x256));
  Patterns.push_back(max(wild_i8x256,  wild_i8x256));

  // 1024-bit max(vector, vector)
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(max(wild_u32x32, wild_u32x32));
    Patterns.push_back(max(wild_i32x32, wild_i32x32));
    Patterns.push_back(max(wild_u16x64, wild_u16x64));
    Patterns.push_back(max(wild_i16x64, wild_i16x64));
    Patterns.push_back(max(wild_u8x128, wild_u8x128));
    Patterns.push_back(max(wild_i8x128, wild_i8x128));
  }

  debug(4) << "HexCG: " << op->type <<  ", handleLargeVectors (Max)\n";
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      slice_into_halves(matches[0], VectorRegisterPairsA);
      slice_into_halves(matches[1], VectorRegisterPairsB);

      // 2. Operate on the halves
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(max(A_low, B_low));
      Value *OddLanes = codegen(max(A_high, B_high));

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(OddLanes, EvenLanes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(EvenLanes);
        Ops.push_back(OddLanes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }

  return NULL;
}

// Handle Mul on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Mul *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  // 4096-bit vector x vector
  Patterns.push_back(wild_u32x128 * wild_u32x128);
  Patterns.push_back(wild_i32x128 * wild_i32x128);
  Patterns.push_back(wild_u16x256 * wild_u16x256);
  Patterns.push_back(wild_i16x256 * wild_i16x256);
  Patterns.push_back(wild_u8x512  * wild_u8x512);
  Patterns.push_back(wild_i8x512  * wild_i8x512);

  // 2048-bit vector x vector
  Patterns.push_back(wild_u32x64  * wild_u32x64);
  Patterns.push_back(wild_i32x64  * wild_i32x64);
  // FIXME: DJP: avoid slice/shuffles when: (see sobel)
  //   Unsupported type for vector multiply (uint16x128 * uint16x128 = uint16x128)
  // when sliced results in shuffles and:
  //   Unsupported type for vector multiply (uint16x64 * uint16x64 = uint16x64)
  if (op->type.bits() != 16)
     Patterns.push_back(wild_u16x128 * wild_u16x128);
  Patterns.push_back(wild_i16x128 * wild_i16x128);
  Patterns.push_back(wild_u8x256  * wild_u8x256);
  Patterns.push_back(wild_i8x256  * wild_i8x256);

  // 1024-bit vector x vector
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(wild_u32x32 * wild_u32x32);
    Patterns.push_back(wild_i32x32 * wild_i32x32);
    Patterns.push_back(wild_u16x64 * wild_u16x64);
    Patterns.push_back(wild_i16x64 * wild_i16x64);
    Patterns.push_back(wild_u8x128 * wild_u8x128);
    Patterns.push_back(wild_i8x128 * wild_i8x128);
  }

  debug(4) << "HexCG: " << op->type <<  ", handleLargeVectorVectors (Mul)\n";
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      slice_into_halves(matches[0], VectorRegisterPairsA);
      slice_into_halves(matches[1], VectorRegisterPairsB);

      // 2. Operate on the halves
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(A_low * B_low);
      Value *OddLanes = codegen(A_high * B_high);

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(OddLanes, EvenLanes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(EvenLanes);
        Ops.push_back(OddLanes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }
  return NULL;
}

// Handle Div on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Div *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

  // 4096-bit vector / vector
  Patterns.push_back(wild_u32x128 / wild_u32x128);
  Patterns.push_back(wild_i32x128 / wild_i32x128);
  Patterns.push_back(wild_u16x256 / wild_u16x256);
  Patterns.push_back(wild_i16x256 / wild_i16x256);
  Patterns.push_back(wild_u8x512  / wild_u8x512);
  Patterns.push_back(wild_i8x512  / wild_i8x512);

  // 2048-bit vector / vector
  Patterns.push_back(wild_u32x64  / wild_u32x64);
  Patterns.push_back(wild_i32x64  / wild_i32x64);
  Patterns.push_back(wild_u16x128 / wild_u16x128);
  Patterns.push_back(wild_i16x128 / wild_i16x128);
  Patterns.push_back(wild_u8x256  / wild_u8x256);
  Patterns.push_back(wild_i8x256  / wild_i8x256);

  // 1024-bit vector / vector
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(wild_u32x32 / wild_u32x32);
    Patterns.push_back(wild_i32x32 / wild_i32x32);
    Patterns.push_back(wild_u16x64 / wild_u16x64);
    Patterns.push_back(wild_i16x64 / wild_i16x64);
    Patterns.push_back(wild_u8x128 / wild_u8x128);
    Patterns.push_back(wild_i8x128 / wild_i8x128);
  }

  debug(4) << "HexCG: " << op->type <<  ", handleLargeVectors (Div)\n";
  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      slice_into_halves(matches[0], VectorRegisterPairsA);
      slice_into_halves(matches[1], VectorRegisterPairsB);

      // 2. Operate on the halves
      Expr A_low = VectorRegisterPairsA[0];
      Expr A_high = VectorRegisterPairsA[1];
      Expr B_low = VectorRegisterPairsB[0];
      Expr B_high = VectorRegisterPairsB[1];
      Value *EvenLanes = codegen(A_low / B_low);
      Value *OddLanes = codegen(A_high / B_high);

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(OddLanes, EvenLanes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(EvenLanes);
        Ops.push_back(OddLanes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }

  return NULL;
}

// Handle Cast on types greater than a single vector
static
bool isWideningVectorCast(const Cast *op) {
  Type t = op->type;
  Type e_type = op->value.type();
  return (t.is_vector() && e_type.is_vector() &&
          t.bits() > e_type.bits());
}
static
bool isSameSizeVectorCast(const Cast *op) {
  Type t = op->type;
  Type e_type = op->value.type();
  return (t.is_vector() && e_type.is_vector() &&
          t.bits() == e_type.bits());
}
static
bool isNarrowingVectorCast(const Cast *op) {
  Type t = op->type;
  Type e_type = op->value.type();
  return (t.is_vector() && e_type.is_vector() &&
          t.bits() < e_type.bits());
}
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Cast *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  if (isWideningVectorCast(op)) {
    std::vector<Pattern> Patterns;
    debug(4) << "Entered handleLargeVectors (Cast)\n";
    std::vector<Expr> matches;

    // 8-bit -> 16-bit (2x widening)
    Patterns.push_back(Pattern(cast(UInt(16, CPICK(128, 64)),
                                    WPICK(wild_u8x128, wild_u8x64)),
                               IPICK(Intrinsic::hexagon_V6_vzb)));
    Patterns.push_back(Pattern(cast(UInt(16, CPICK(128, 64)),
                                    WPICK(wild_i8x128, wild_i8x64)),
                               IPICK(Intrinsic::hexagon_V6_vzb)));

    Patterns.push_back(Pattern(cast(Int(16, CPICK(128, 64)),
                                    WPICK(wild_u8x128, wild_u8x64)),
                               IPICK(Intrinsic::hexagon_V6_vzb)));
    Patterns.push_back(Pattern(cast(Int(16, CPICK(128, 64)),
                                    WPICK(wild_i8x128, wild_i8x64)),
                               IPICK(Intrinsic::hexagon_V6_vsb)));

    // 16-bit -> 32-bit (2x widening)
    Patterns.push_back(Pattern(cast(UInt(32, CPICK(128, 64)),
                                    WPICK(wild_u16x128, wild_u16x64)),
                               IPICK(Intrinsic::hexagon_V6_vzh)));
    Patterns.push_back(Pattern(cast(UInt(32, CPICK(128, 64)),
                                    WPICK(wild_i16x128, wild_i16x64)),
                               IPICK(Intrinsic::hexagon_V6_vzh)));

    Patterns.push_back(Pattern(cast(Int(32, CPICK(128, 64)),
                                    WPICK(wild_u16x128, wild_u16x64)),
                               IPICK(Intrinsic::hexagon_V6_vzh)));
    Patterns.push_back(Pattern(cast(Int(32, CPICK(128, 64)),
                                    WPICK(wild_i16x128, wild_i16x64)),
                               IPICK(Intrinsic::hexagon_V6_vsh)));

    // 8-bit -> 32-bit (4x widening)
    // Note: listed intrinsic is the second step (16->32bit) widening
    Patterns.push_back(Pattern(cast(UInt(32, CPICK(128, 64)),
                                    WPICK(wild_u8x128, wild_u8x64)),
                               IPICK(Intrinsic::hexagon_V6_vzh)));
    Patterns.push_back(Pattern(cast(UInt(32, CPICK(128, 64)),
                                    WPICK(wild_i8x128, wild_i8x64)),
                               IPICK(Intrinsic::hexagon_V6_vzh)));

    Patterns.push_back(Pattern(cast(Int(32, CPICK(128, 64)),
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
            " handleLargeVectors(const Cast *)\n";
          Intrinsic::ID IntrinsID = P.ID;

          Value *DoubleVector = NULL;
          // First, check to see if we are widening 4x.
          if ((op->value.type().bits() == 8) && (op->type.bits() == 32))
          {
            debug(4) << "HexCG::First widening 2x to 16bit elements\n";
            Type NewT = Type(op->type.code(), 16, op->type.lanes());
            DoubleVector = codegen(cast(NewT, op->value));
          } else {
            DoubleVector = codegen(op->value);
          }

          // We now have a vector is that u16x64/i16x64. Get the lower half of
          // this vector which contains the events elements of A and extend that
          // to 32 bits. Similarly, deal with the upper half of the double
          // vector.
          std::vector<Value *> Ops;
          getHighAndLowVectors(DoubleVector, Ops);
          Value *HiVecReg = Ops[0];
          Value *LowVecReg = Ops[1];

          debug(4) << "HexCG::" << "Widening lower vector reg elements(even)"
            " elements to 32 bit\n";
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
                                       UINT_8_IMAX)),
                               IPICK(Intrinsic::hexagon_V6_vsathub)));
    // Due to aggressive simplification introduced by the patch below
    // the max operator is removed when saturating a signed value down
    // to an unsigned value.
    // commit 23c461ce6cc50e22becbd38e6217c7e9e9b4dd98
    //  Author: Andrew Adams <andrew.b.adams@gmail.com>
    //  Date:   Mon Jun 16 15:38:13 2014 -0700
    // Weaken some simplification rules due to horrible compile times.
    Patterns.push_back(Pattern(u8_(min(WPICK(wild_i32x128, wild_i32x64),
                                       UINT_8_IMAX)),
                               IPICK(Intrinsic::hexagon_V6_vsathub)));
    // Fixme: PDB: Shouldn't the signed version have a max in the pattern too?
    // Yes it should. So adding it now.
    Patterns.push_back(Pattern(i8_(max(min(WPICK(wild_i32x128, wild_i32x64),
                                           INT_8_IMAX), INT_8_IMIN)),
                               Intrinsic::not_intrinsic));
    Patterns.push_back(Pattern(u8_(max(min(WPICK(wild_i32x128, wild_i32x64),
                                           UINT_8_IMAX), UINT_8_IMIN)),
                               IPICK(Intrinsic::hexagon_V6_vsathub)));

    for (size_t I = 0; I < Patterns.size(); ++I) {
      const Pattern &P = Patterns[I];
      if (expr_match(P.pattern, op, matches)) {
        // At the present moment we barf when saturating to an i8 value.
        // Once we fix this issue, make sure to get rid of the error
        // below in the else part as well.
        if (P.ID == Intrinsic::not_intrinsic) {
          user_error << "Saturate and packing not supported when downcasting"
            " words to signed chars\n";
        } else {
          internal_assert(matches.size() == 1);
          bool operand_type_signed = matches[0].type().is_int();
          Type FirstStepType = Type(matches[0].type().code(), 16, op->type.lanes());
          Value *FirstStep = NULL;
          if (operand_type_signed) {
            if (op->type.is_int()) {
              // i32->i8.
              // This is not supported currently.
              user_error << "Saturate and packing not supported when"
                "downcasting words to signed chars\n";
            } else {
              // i32->u8.
              const Div *d = matches[0].as<Div>();
              if (d) {
                FirstStep = possiblyCodeGenWideningMultiplySatRndSat(d);
              }
              // FirstSteType is int16
              if (!FirstStep) {
                FirstStep = codegen(cast(FirstStepType,
                                         max(min(matches[0], INT_16_IMAX),
                                             INT_16_IMIN)));
              }
            }
          } else {
            // u32->u8
            // FirstStepType is uint16.
            FirstStep = codegen(cast(FirstStepType,
                                     (min(matches[0],
                                          UINT_16_IMAX))));
          }
          std::vector<Value *> Ops;
          Intrinsic::ID IntrinsID = P.ID;

          // Ops[0] is the higher vectors and Ops[1] the lower.
          getHighAndLowVectors(FirstStep, Ops);
          Value *V = CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                                                                 IntrinsID),
                                       Ops);
          return convertValueType(V, llvm_type_of(op->type));
        }
      }
    }
    {
      // Truncate and pack.
      Patterns.clear();
      matches.clear();

      Patterns.push_back(Pattern(u8_(WPICK(wild_u32x128, wild_u32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      Patterns.push_back(Pattern(i8_(WPICK(wild_u32x128, wild_u32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      Patterns.push_back(Pattern(u8_(WPICK(wild_i32x128, wild_i32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      Patterns.push_back(Pattern(i8_(WPICK(wild_i32x128, wild_i32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      for (size_t I = 0; I < Patterns.size(); ++I) {
        const Pattern &P = Patterns[I];
        if (expr_match(P.pattern, op, matches)) {
          Type FirstStepType = Type(matches[0].type().code(), 16, op->type.lanes());
          Value *FirstStep = codegen(cast(FirstStepType, matches[0]));
          std::vector<Value *> Ops;
          Intrinsic::ID IntrinsID = P.ID;
          getHighAndLowVectors(FirstStep, Ops);
          Value * V = CallLLVMIntrinsic(Intrinsic::getDeclaration(module,
                                                                  IntrinsID),
                                        Ops);
          return convertValueType(V, llvm_type_of(op->type));
        }
      }
    }
    {
      // This lowers the first step of u32x64->u8x64 with saturation, which is a
      // two step saturate and pack. This first step converts a u32x64/i32x64
      // into u16x64/i16x64
      Patterns.clear();
      matches.clear();
      if (target.has_feature(Halide::Target::HVX_V62))
        Patterns.push_back(Pattern(u16_(min(WPICK(wild_u32x128, wild_u32x64),
                                            UINT_16_IMAX)),
                                   IPICK(Intrinsic::hexagon_V6_vsatuwuh)));
      else
        Patterns.push_back(Pattern(u16_(min(WPICK(wild_u32x128, wild_u32x64),
                                            UINT_16_IMAX)),
                                   IPICK(Intrinsic::hexagon_V6_vsatwh)));

      Patterns.push_back(Pattern(i16_(max(min(WPICK(wild_i32x128, wild_i32x64),
                                              INT_16_IMAX), INT_16_IMIN)),
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
    {
      // This lowers the first step of u32x64->u8x64 with truncation, which is a
      // two step saturate and pack. This first step converts a u32x64/i32x64
      // into u16x64/i16x64
      Patterns.clear();
      matches.clear();

      Patterns.push_back(Pattern(u16_(WPICK(wild_u32x128, wild_u32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      Patterns.push_back(Pattern(i16_(WPICK(wild_u32x128, wild_u32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      Patterns.push_back(Pattern(u16_(WPICK(wild_i32x128, wild_i32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      Patterns.push_back(Pattern(i16_(WPICK(wild_i32x128, wild_i32x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
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
  }
  return NULL;
}
// End of handleLargeVectors
/////////////////////////////////////////////////////////////////////////////

void CodeGen_Hexagon::visit(const Cast *op) {
  vector<Expr> matches;
  debug(1) << "HexCG: " << op->type << " <- " << op->value.type() << ", " << "visit(Cast)\n";
  if (isWideningVectorCast(op)) {
      // ******** Part 1: Up casts (widening) ***************
      // Two step extensions.
      // i8x64 -> i32x64 <to do>
      // u8x64 -> u32x64
      if (isLargerThanDblVector(op->type, native_vector_bits())) {
        value = handleLargeVectors(op);
        if (value)
          return;
      }
      matches.clear();
      // vmpy - vector multiply - vector by vector.
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
      // ******** End Part 1: Up casts (widening) ***************
    } else {
      // ******** Part 2: Down casts (Narrowing)* ***************
      // Two step downcasts.
      // std::vector<Expr> Patterns;
      //  Patterns.push_back(u8_(min(wild_u32x64, 255)));
    if (isLargerThanDblVector(op->value.type(), native_vector_bits())) {
        value = handleLargeVectors(op);
        if (value)
          return;
      }
    bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
    // Looking for shuffles
    {
      std::vector<Pattern> Shuffles;
      // Halide has a bad habit of converting "a >> 8" to "a/256"
      Expr TwoFiveSix_u = u16_(256);
      Expr TwoFiveSix_i = i16_(256);
      Expr Divisor_u = u32_(65536);
      Expr Divisor_i = i32_(65536);
      // Vd.b = shuffo(Vu.b, Vv.b)
      Shuffles.push_back(Pattern(u8_(WPICK(wild_u16x128, wild_u16x64) /
                                     Broadcast::make(TwoFiveSix_u,
                                                     CPICK(128, 64))),
                                 IPICK(Intrinsic::hexagon_V6_vshuffob)));
      Shuffles.push_back(Pattern(i8_(WPICK(wild_u16x128, wild_u16x64) /
                                     Broadcast::make(TwoFiveSix_u,
                                                     CPICK(128, 64))),
                                 IPICK(Intrinsic::hexagon_V6_vshuffob)));
      Shuffles.push_back(Pattern(u8_(WPICK(wild_i16x128, wild_i16x64) /
                                     Broadcast::make(TwoFiveSix_i,
                                                     CPICK(128, 64))),
                                 IPICK(Intrinsic::hexagon_V6_vshuffob)));
      Shuffles.push_back(Pattern(i8_(WPICK(wild_i16x128, wild_i16x64) /
                                     Broadcast::make(TwoFiveSix_i,
                                                     CPICK(128, 64))),
                                 IPICK(Intrinsic::hexagon_V6_vshuffob)));
      // Vd.h = shuffo(Vu.h, Vv.h)
      Shuffles.push_back(Pattern(u16_(WPICK(wild_u32x64, wild_u32x32) /
                                     Broadcast::make(Divisor_u,
                                                     CPICK(64, 32))),
                                 IPICK(Intrinsic::hexagon_V6_vshufoh)));
      Shuffles.push_back(Pattern(i16_(WPICK(wild_u32x64, wild_u32x32) /
                                     Broadcast::make(Divisor_u,
                                                     CPICK(64, 32))),
                                 IPICK(Intrinsic::hexagon_V6_vshufoh)));
      Shuffles.push_back(Pattern(u16_(WPICK(wild_i32x64, wild_i32x32) /
                                     Broadcast::make(Divisor_i,
                                                     CPICK(64, 32))),
                                 IPICK(Intrinsic::hexagon_V6_vshufoh)));
      Shuffles.push_back(Pattern(i16_(WPICK(wild_i32x64, wild_i32x32) /
                                     Broadcast::make(Divisor_i,
                                                     CPICK(64, 32))),
                                 IPICK(Intrinsic::hexagon_V6_vshufoh)));

      matches.clear();
      for (size_t I = 0; I < Shuffles.size(); ++I) {
        const Pattern &P = Shuffles[I];
        if (expr_match(P.pattern, op, matches)) {
          std::vector<Value *> Ops;
          Value *DoubleVector = codegen(matches[0]);
          getHighAndLowVectors(DoubleVector, Ops);
          Intrinsic::ID ID = P.ID;
          Value *ShuffleInst =
            CallLLVMIntrinsic(Intrinsic::getDeclaration(module, ID), Ops);
          value = convertValueType(ShuffleInst, llvm_type_of(op->type));
          return;
        }
      }
    }

    // Lets look for saturate and pack.
    std::vector<Pattern> SatAndPack;
    matches.clear();
    int WrdsInVP  = 2 * (native_vector_bits() / 32);
    int HWrdsInVP = WrdsInVP * 2 ;
    SatAndPack.push_back(Pattern(u8_(max(min(WPICK(wild_i16x128, wild_i16x64)
                                             >> Broadcast::make(wild_i16,
                                                                HWrdsInVP),
                                             UINT_8_IMAX), UINT_8_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrhubsat)));
    SatAndPack.push_back(Pattern(i8_(max(min(WPICK(wild_i16x128, wild_i16x64)
                                             >> Broadcast::make(wild_i16,
                                                                HWrdsInVP),
                                             INT_8_IMAX), INT_8_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrhbrndsat)));
    SatAndPack.push_back(Pattern(i16_(max(min(WPICK(wild_i32x64, wild_i32x32)
                                              >> Broadcast::make(wild_i32,
                                                                 WrdsInVP),
                                              INT_16_IMAX), INT_16_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrwhsat)));
    SatAndPack.push_back(Pattern(u16_(max(min(WPICK(wild_i32x64, wild_i32x32)
                                              >> Broadcast::make(wild_i32,
                                                                 WrdsInVP),
                                              UINT_16_IMAX), UINT_16_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrwuhsat)));

    for (size_t I = 0; I < SatAndPack.size(); ++I) {
      const Pattern &P = SatAndPack[I];
      if (expr_match(P.pattern, op, matches)) {
        std::vector<Value *> Ops;
        Intrinsic::ID IntrinsID = P.ID;
        Value *DoubleVector = codegen(matches[0]);
        Value *ShiftOperand = codegen(matches[1]);
        getHighAndLowVectors(DoubleVector, Ops);
        Ops.push_back(ShiftOperand);
        Value *SatAndPackInst =
          CallLLVMIntrinsic(Intrinsic::getDeclaration(module, IntrinsID), Ops);
        value = convertValueType(SatAndPackInst, llvm_type_of(op->type));
        return;
      }
    }
    // when saturating a signed value to an unsigned value, we see "max"
    // unlike saturating an unsinged value to a smaller unsinged value when
    // the max(Expr, 0) part is redundant.
    SatAndPack.push_back(Pattern(u8_(max(min(WPICK(wild_i16x128, wild_i16x64)
                                             / Broadcast::make(wild_i16,
                                                               HWrdsInVP),
                                             UINT_8_IMAX), UINT_8_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrhubsat)));
    SatAndPack.push_back(Pattern(i8_(max(min(WPICK(wild_i16x128, wild_i16x64)
                                             / Broadcast::make(wild_i16,
                                                               HWrdsInVP),
                                             INT_8_IMAX), INT_8_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrhbrndsat)));
    SatAndPack.push_back(Pattern(i16_(max(min(WPICK(wild_i32x64, wild_i32x32)
                                              / Broadcast::make(wild_i32,
                                                                WrdsInVP),
                                              INT_16_IMAX), INT_16_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrwhsat)));
    SatAndPack.push_back(Pattern(u16_(max(min(WPICK(wild_i32x64, wild_i32x32)
                                              / Broadcast::make(wild_i32,
                                                                WrdsInVP),
                                              UINT_16_IMAX), UINT_16_IMIN)),
                                 IPICK(Intrinsic::hexagon_V6_vasrwuhsat)));

    for (size_t I = 0; I < SatAndPack.size(); ++I) {
      const Pattern &P = SatAndPack[I];
      if (expr_match(P.pattern, op, matches)) {
          int rt_shift_by = 0;
          if (is_const_power_of_two_integer(matches[1], &rt_shift_by)) {
            Value *DoubleVector = codegen(matches[0]);
            Value *ShiftBy = codegen(rt_shift_by);
            Intrinsic::ID IntrinsID = P.ID;
            std::vector<Value *> Ops;
            getHighAndLowVectors(DoubleVector, Ops);
            Ops.push_back(ShiftBy);
            Value *Result =
              CallLLVMIntrinsic(Intrinsic::getDeclaration(module, IntrinsID),
                                Ops);
            value = convertValueType(Result, llvm_type_of(op->type));
            return;
          }
      }
    }
    SatAndPack.clear();
    matches.clear();
    SatAndPack.push_back(Pattern(u8_(min(WPICK(wild_u16x128, wild_u16x64),
                                         255)),
                                 IPICK(Intrinsic::hexagon_V6_vsathub)));
    SatAndPack.push_back(Pattern(u8_(max(min(WPICK(wild_i16x128, wild_i16x64),
                                             255), 0)),
                                 IPICK(Intrinsic::hexagon_V6_vsathub)));
    SatAndPack.push_back(Pattern(i8_(max(min(WPICK(wild_i16x128, wild_i16x64),
                                             127), -128)),
                                 Intrinsic::not_intrinsic));
    // -128 when implicitly coerced to uint16 is 65408.
    SatAndPack.push_back(Pattern(i8_(max(min(WPICK(wild_u16x128, wild_u16x64),
                                             127), 65408 /*uint16(-128)*/)),
                                 Intrinsic::not_intrinsic));
    for (size_t I = 0; I < SatAndPack.size(); ++I) {
      const Pattern &P = SatAndPack[I];
      if (expr_match(P.pattern, op, matches)) {
        std::vector<Value *> Ops;
        Value *DoubleVector = codegen(matches[0]);
        getHighAndLowVectors(DoubleVector, Ops);
        Intrinsic::ID ID = P.ID;
        if (ID == Intrinsic::not_intrinsic) {
          user_error << "Saturate and packing not supported when downcasting"
            " shorts (signed and unsigned) to signed chars\n";
        } else {
          Value *SatAndPackInst =
            CallLLVMIntrinsic(Intrinsic::getDeclaration(module,ID), Ops);
          value = convertValueType(SatAndPackInst, llvm_type_of(op->type));
          return;
        }
      }
    }
    {
      std::vector<Pattern> Shuffles;
      // Vd.b = shuffe(Vu.b, Vv.b)
      Shuffles.push_back(Pattern(u8_(WPICK(wild_u16x128, wild_u16x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      Shuffles.push_back(Pattern(i8_(WPICK(wild_u16x128, wild_u16x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      Shuffles.push_back(Pattern(u8_(WPICK(wild_i16x128, wild_i16x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));
      Shuffles.push_back(Pattern(i8_(WPICK(wild_i16x128, wild_i16x64)),
                                 IPICK(Intrinsic::hexagon_V6_vshuffeb)));

      // Vd.h = shuffe(Vu.h, Vv.h)
      Shuffles.push_back(Pattern(u16_(WPICK(wild_u32x64, wild_u32x32)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      Shuffles.push_back(Pattern(i16_(WPICK(wild_u32x64, wild_u32x32)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      Shuffles.push_back(Pattern(u16_(WPICK(wild_i32x64, wild_i32x32)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      Shuffles.push_back(Pattern(i16_(WPICK(wild_i32x64, wild_i32x32)),
                                 IPICK(Intrinsic::hexagon_V6_vshufeh)));
      matches.clear();
      for (size_t I = 0; I < Shuffles.size(); ++I) {
        const Pattern &P = Shuffles[I];
        if (expr_match(P.pattern, op, matches)) {
          std::vector<Value *> Ops;
          Value *DoubleVector = codegen(matches[0]);
          getHighAndLowVectors(DoubleVector, Ops);
          Intrinsic::ID ID = P.ID;
          Value *ShuffleInst =
            CallLLVMIntrinsic(Intrinsic::getDeclaration(module, ID), Ops);
          value = convertValueType(ShuffleInst, llvm_type_of(op->type));
          return;
        }
      }
    }
  }
  // ******** End Part 2: Down casts (Narrowing)* ***************

  ostringstream msgbuf;
  msgbuf << "<- " << op->value.type();
  string msg = msgbuf.str() + " in visit(Cast *)\n";
  checkVectorOp(op, msg);
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
          ((op->type.bytes() * op->type.lanes()) == VecSize)) {
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
    } else if (op->name == Call::absd) {
      if (op->type.is_vector() &&
          ((op->type.bytes() * op->type.lanes()) == 2 * VecSize)) {
        // vector sized absdiff should have been covered by the look up table
        // "combiners".
        std::vector<Pattern> doubleAbsDiff;

        doubleAbsDiff.push_back(Pattern(absd(WPICK(wild_u8x256, wild_u8x128),
                                             WPICK(wild_u8x256, wild_u8x128)),
                                        IPICK(Intrinsic::
                                              hexagon_V6_vabsdiffub)));
        doubleAbsDiff.push_back(Pattern(absd(WPICK(wild_u16x128, wild_u16x64),
                                             WPICK(wild_u16x128, wild_u16x64)),
                                        IPICK(Intrinsic::
                                              hexagon_V6_vabsdiffuh)));
        doubleAbsDiff.push_back(Pattern(absd(WPICK(wild_i16x128, wild_i16x64),
                                             WPICK(wild_i16x128, wild_i16x64)),
                                        IPICK(Intrinsic::
                                              hexagon_V6_vabsdiffh)));
        doubleAbsDiff.push_back(Pattern(absd(WPICK(wild_i32x64 ,wild_i32x32),
                                             WPICK(wild_i32x64 ,wild_i32x32)),
                                        IPICK(Intrinsic::
                                              hexagon_V6_vabsdiffw)));

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
    // Suppress warning on the calls where it is OK to use CodeGen_Posix
    if ((op->name != Call::interleave_vectors) &&
        (op->name != Call::shuffle_vector)) {
      checkVectorOp(op, "in visit(Call *)\n");
    }
    CodeGen_Posix::visit(op);
  }
}
bool CodeGen_Hexagon::possiblyCodeGenWideningMultiply(const Mul *op) {
  const Broadcast *bc_a = op->a.as<Broadcast>();
  const Broadcast *bc_b = op->b.as<Broadcast>();
  if (!bc_a && !bc_b)
    // both cannot be broadcasts because simplify should have reduced them.
    return false;
  // So we know at least one is a broadcast.
  //   Vdd.h=vmpy(Vu.ub,Rt.b)
  //   Vdd.w=vmpy(Vu.h,Rt.h)
  //   Vdd.uh=vmpy(Vu.ub,Rt.ub)
  //   Vdd.uw=vmpy(Vu.uh,Rt.uh)
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern>Patterns;
  std::vector<Expr> matches;
  std::vector<Value *> Ops;
  Expr Vec, BC, bc_value;;
  Intrinsic::ID IntrinsID;
  //   Deal with these first.
  //   Vdd.h=vmpy(Vu.ub,Rt.b)
  //   Vdd.w=vmpy(Vu.h,Rt.h)
  Patterns.push_back(Pattern(WPICK(wild_i16x128, wild_i16x64) *
                             WPICK(wild_i16x128, wild_i16x64),
                             IPICK(Intrinsic::hexagon_V6_vmpybus)));
  Patterns.push_back(Pattern(WPICK(wild_i32x64, wild_i32x32) *
                             WPICK(wild_i32x64, wild_i32x32),
                             IPICK(Intrinsic::hexagon_V6_vmpyh)));
  for (size_t I = 0; I < Patterns.size(); ++I) {
    const Pattern &P = Patterns[I];
    if (expr_match(P.pattern, op, matches)) {
      if (bc_a) {
        BC = matches[0];
        Vec = matches[1];
        bc_value = bc_a->value;
      } else {
        Vec = matches[0];
        BC = matches[1];
        bc_value = bc_b->value;
      }
      Type t_vec, t_bc;
      if (Vec.type().bits() == 16) {
        t_vec = UInt(8, Vec.type().lanes());
        t_bc = Int(8, Vec.type().lanes());
      }
      else if (Vec.type().bits() == 32) {
        t_vec = Int(16, Vec.type().lanes());
        t_bc = Int(16, Vec.type().lanes());
      }
      Vec  = lossless_cast(t_vec, Vec);
      BC = lossless_cast(t_bc, BC);
      IntrinsID = P.ID;
      break;
    }
  }
  if (!Vec.defined()) {
    Patterns.clear();
    matches.clear();
    Ops.clear();
    //   Vdd.uh=vmpy(Vu.ub,Rt.ub)
    //   Vdd.uw=vmpy(Vu.uh,Rt.uh)
    Patterns.push_back(Pattern(WPICK(wild_u16x128, wild_u16x64) *
                               WPICK(wild_u16x128, wild_u16x64),
                               IPICK(Intrinsic::hexagon_V6_vmpyub)));
    Patterns.push_back(Pattern(WPICK(wild_u32x64, wild_u32x32) *
                               WPICK(wild_u32x64, wild_u32x32),
                               IPICK(Intrinsic::hexagon_V6_vmpyuh)));
    for (size_t I = 0; I < Patterns.size(); ++I) {
      const Pattern &P = Patterns[I];
      if (expr_match(P.pattern, op, matches)) {
        if (bc_a) {
          BC = matches[0];
          Vec = matches[1];
          bc_value = bc_a->value;
        } else {
          Vec = matches[0];
          BC = matches[1];
          bc_value = bc_b->value;
        }
        Type t_vec, t_bc;
        if (Vec.type().bits() == 16) {
          t_vec = UInt(8, Vec.type().lanes());
          t_bc = UInt(8, Vec.type().lanes());
        }
        else if (Vec.type().bits() == 32) {
          t_vec = UInt(16, Vec.type().lanes());
          t_bc = UInt(16, Vec.type().lanes());
        }
        Vec  = lossless_cast(t_vec, Vec);
        BC = lossless_cast(t_bc, BC);
        IntrinsID = P.ID;
        break;
      }
    }
  }
  if (!Vec.defined() || !BC.defined())
    return false;
  if (!is_const(BC))
    return false;
  const int64_t *ImmValue_p = as_const_int(BC);
  const uint64_t *UImmValue_p = as_const_uint(BC);
  if (!ImmValue_p && !UImmValue_p)
    return false;
  int64_t ImmValue = ImmValue_p ? *ImmValue_p :
    (int64_t) *UImmValue_p;

  Value *Vector = codegen(Vec);
  //   Vdd.h=vmpy(Vu.ub,Rt.b)
  //   Vdd.w=vmpy(Vu.h,Rt.h)
  int ScalarValue = 0;
  if (Vec.type().bits() == 8) {
    int A = ImmValue & 0xFF;
    int B = A | (A << 8);
    ScalarValue = B | (B << 16);
  } else {
    int A = ImmValue & 0xFFFF;
    ScalarValue = A | (A << 16);
  }
  Value *Scalar = codegen(ScalarValue);
  Ops.push_back(Vector);
  Ops.push_back(Scalar);
  Value *Vmpy =
    CallLLVMIntrinsic(Intrinsic::getDeclaration(module, IntrinsID), Ops);
  value = convertValueType(Vmpy, llvm_type_of(op->type));
  return true;
}
void CodeGen_Hexagon::visit(const Mul *op) {
  if (!op->a.type().is_vector() && !op->b.type().is_vector()) {
    // scalar x scalar multiply
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  int VecSize = HEXAGON_SINGLE_MODE_VECTOR_SIZE;
  if (B128) VecSize *= 2;
  debug(1) << "HexCG: " << op->type << ", " << "visit(Mul)\n";
  if (isLargerThanDblVector(op->type, native_vector_bits())) {
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
  if (!value) {
    // There is a good chance we are dealing with
    // vector by scalar kind of multiply instructions.
    Expr A = op->a;

    if (A.type().is_vector() &&
          ((A.type().bytes() * A.type().lanes()) ==
           2*VecSize)) {
      // If it is twice the hexagon vector width, then try
      // splitting into two vector multiplies.
      debug (4) << "HexCG: visit(Const Mul *op) ** \n";
      debug (4) << "op->a:" << op->a << "\n";
      debug (4) << "op->b:" << op->b << "\n";
      // Before we generate vector by scalar multiplies check if we can
      // use widening multiplies
      if (possiblyCodeGenWideningMultiply(op))
        return;
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
          debug(4) << "Other bits size : " << Other.type().bits() << "\n";
          if (is_const(Other)) {
            const int64_t *ImmValue_p = as_const_int(Other);
            const uint64_t *UImmValue_p = as_const_uint(Other);
            if (ImmValue_p || UImmValue_p) {
              int64_t ImmValue = ImmValue_p ? *ImmValue_p :
                (int64_t) *UImmValue_p;
              unsigned int ScalarValue = 0;
              Intrinsic::ID IntrinsID = (Intrinsic::ID) 0;
              if (Vec.type().bits() == 16
                  && ImmValue <=  INT_8_IMAX) {
                unsigned int A = ImmValue & 0xFF;
                unsigned int B = A | (A << 8);
                ScalarValue = B | (B << 16);
                IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyihb);
              } else if (Vec.type().bits() == 32) {
                if (ImmValue <= INT_8_IMAX) {
                  unsigned int A = ImmValue & 0xFF;
                  unsigned int B = A | (A << 8);
                  ScalarValue = B | (B << 16);
                  IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyiwb);
                } else if (ImmValue <= INT_16_IMAX) {
                  unsigned int A = ImmValue & 0xFFFF;
                  ScalarValue = (A << 16) | A;
                  IntrinsID = IPICK(Intrinsic::hexagon_V6_vmpyiwh);
                } else
                  internal_error <<
                    "Cannot deal with an Imm value greater than 16"
                    "bits in generating vmpyi\n";
              } else
                checkVectorOp(op, "in visit(Mul *) Vector Scalar Imm\n");
              Expr ScalarImmExpr = IntImm::make(Int(32), ScalarValue);
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
              // It is Vector x Vector
            } else  if (matches[0].type().is_vector() && matches[1].type().is_vector()) {
              checkVectorOp(op, "in visit(Mul *) Vector x Vector\n");
              internal_error << "in visit(Mul *) Vector x Vector\n";
            }
          }
        } // expr_match
      }
      checkVectorOp(op, "in visit(Mul *) Vector x Vector no match)\n");
      debug(4) << "HexCG: FAILED to generate a  vector multiply.\n";
    }
  }
  value = emitBinaryOp(op, multiplies);
  if (!value &&
      isLargerThanVector(op->type, native_vector_bits())) {
    value = handleLargeVectors(op);
    if (value)
      return;
  }
  if (!value) {
    user_warning << "Unsupported type for vector multiply ("
                 << op->a.type()
                 << " * "
                 << op->b.type()
                 << " = "
                 << op->type
                 << ")\n";
    checkVectorOp(op, "in visit(Mul *)\n");
    CodeGen_Posix::visit(op);
    return;
  }
}
void CodeGen_Hexagon::visit(const Broadcast *op) {
    bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);

    int Width = op->lanes;
    bool match32 = false;
    bool match16 = false;
    bool match8  = false;
    vector<Expr> Matches;

    int width_32 = CPICK(32,16);
    int width_16 = CPICK(64,32);
    int width_8  = CPICK(128,64);

    // Look for supported broadcasts.
    Expr match_i32 = Broadcast::make(wild_i32, 0);
    Expr match_u32 = Broadcast::make(wild_u32, 0);
    match32 = expr_match(match_i32, op, Matches) ||
              expr_match(match_u32, op, Matches);
    if (!match32) {
      Expr match_i16 = Broadcast::make(wild_i16, 0);
      Expr match_u16 = Broadcast::make(wild_u16, 0);
      match16 = expr_match(match_i16, op, Matches) ||
                expr_match(match_u16, op, Matches);
    }
    if (!match32 && !match16) {
      Expr match_i8 = Broadcast::make(wild_i8, 0);
      Expr match_u8 = Broadcast::make(wild_u8, 0);
      match8 = expr_match(match_i8, op, Matches) ||
               expr_match(match_u8, op, Matches);
    }

    // Select the correct intrinsic & broadcast value preparation.
    Intrinsic::ID ID = (Intrinsic::ID) 0;
    bool zext_wordsize = false;
    bool fill_wordsize = false;
    bool zero_bcast = false;
    size_t NumOps   = 0;
    int WidthOps = 0;

    if (match32 || match16 || match8) {
      if (is_zero(op->value)) {
        ID = IPICK(Intrinsic::hexagon_V6_vd0);
        zero_bcast = true;
      } else if (target.has_feature(Halide::Target::HVX_V62)) {
        if (match8) {
          ID = IPICK(Intrinsic::hexagon_V6_lvsplatb);
          zext_wordsize = true;
        } else if (match16) {
          ID = IPICK(Intrinsic::hexagon_V6_lvsplath);
          zext_wordsize = true;
        } else if (match32) {
          ID = IPICK(Intrinsic::hexagon_V6_lvsplatw);
        }
      } else {
        ID = IPICK(Intrinsic::hexagon_V6_lvsplatw);
        if (match8 || match16) {
          zext_wordsize = true;
          fill_wordsize = true;
        }
      }

      if (match32) {
        WidthOps = width_32;
      } else if (match16) {
        WidthOps = width_16;
      } else if (match8) {
        WidthOps = width_8;
      }
      NumOps = Width / WidthOps;
    }

    if (ID == (Intrinsic::ID) 0) {  // Didn't find a matching intrinsic.
      user_warning << "Unsupported type for vector broadcast ("
                   << op->type
                   << (match32 ? " m32" : "")
                   << (match16 ? " m16" : "")
                   << (match8  ? " m8"  : "")
                   << " WidthOps:" << WidthOps
                   << " NumOps:" << NumOps
                   << ")\n";
      checkVectorOp(op, "Unhandled type in visit(Broadcast *)\n");
      CodeGen_Posix::visit(op);
      return;
    }

    if (Width % WidthOps) {  // Check for integer multiple of vector.
      user_warning << "Width not a supported multiple for vector broadcast ("
                   << op->type
                   << (match32 ? " m32" : "")
                   << (match16 ? " m16" : "")
                   << (match8  ? " m8"  : "")
                   << " WidthOps:" << WidthOps
                   << " NumOps:" << NumOps
                   << ")\n";
      checkVectorOp(op, "Unhandled width in visit(Broadcast *)\n");
      CodeGen_Posix::visit(op);
      return;
    }

    // Generate the broadcast code.
    debug(4) << "HexCG: Matched vector broadcast ("
                 << op->type
                 << (match32 ? " m32" : "")
                 << (match16 ? " m16" : "")
                 << (match8  ? " m8"  : "")
                 << " WidthOps:" << WidthOps
                 << " NumOps:" << NumOps
                 << ")\n";

    Value *splatval = NULL;
    if (!zero_bcast) {
      if (zext_wordsize) {   // Widen splat value to 32bit word.
        splatval = builder->CreateZExt(codegen(op->value), llvm_type_of(UInt(32)));
      } else {
        splatval = codegen(op->value);
      }

      if (fill_wordsize) {   // Replicate smaller bitsize within 32bit word.
        if (match16 || match8) {  // Stage 1: shift left by 16 & or.
          Constant *shift = ConstantInt::get(llvm_type_of(UInt(32)), 16);
          Value *tempsh = builder->CreateShl(splatval, shift);
          Value *tempor = builder->CreateOr(tempsh, splatval);
          splatval = tempor;
        }
        if (match8) {             // Stage 2: shift left by 8 & or.
          Constant *shift = ConstantInt::get(llvm_type_of(UInt(32)), 8);
          Value *tempsh = builder->CreateShl(splatval, shift);
          Value *tempor = builder->CreateOr(tempsh, splatval);
          splatval = tempor;
        }
      }
    }

    std::vector<Value *> ResVec;
    for (size_t numop = 0; numop < NumOps; ++numop) {
      llvm::Function *F = Intrinsic::getDeclaration(module, ID);
      std::vector<Value *> Ops;
      if (splatval) {
         Ops.push_back(splatval);
      }
      Value *ResOne = CallLLVMIntrinsic(F, Ops);
      ResVec.push_back(ResOne);
    }

    value = concat_vectors(ResVec);
    value = convertValueType(value, llvm_type_of(op->type));
}
void CodeGen_Hexagon::visit(const Load *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  if (op->type.is_vector() && isValidHexagonVector(op->type,
                                                   native_vector_bits())) {

    bool possibly_misaligned = (might_be_misaligned.find(op->name) != might_be_misaligned.end());
    const Ramp *ramp = op->index.as<Ramp>();
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
    if (ramp && stride && stride->value == 1) {
      int width = ramp->lanes;
      ModulusRemainder mod_rem = getAlignmentInfo(ramp->base);
      int alignment_required = CPICK(128, 64);
      if (width != alignment_required) {
        // This will happen only under two cases.
        // 1. We are loading a partial vector.
        // 2. We are loading a type other than u8 or i8.
        // FIXME: PDB: Relax the second condition.
        CodeGen_Posix::visit(op);
        return;
      }
      if (mod_rem.modulus == 1 &&
          mod_rem.remainder == 0) {
        // We know nothing about alignment. Just fall back upon vanilla codegen.
        CodeGen_Posix::visit(op);
        return;
      }
      if (!possibly_misaligned && mod_rem.modulus == alignment_required) {
        if (mod_rem.remainder == 0) {
          // This is a perfectly aligned address. Vanilla codegen can deal with
          // this.
          CodeGen_Posix::visit(op);
          return;
        } else {
          Expr base = ramp->base;
          const Add *add = base.as<Add>();
          const IntImm *b = add->b.as<IntImm>();
          // We can generate a combination of two vmems (aligned) followed by
          // a valign/vlalign if The base is like
          // 1. (aligned_expr + const)
          // In the former case, for double vector mode, we will have
          // mod_rem.modulus == alignment_required and
          // mod_rem.remainder == vector_width + const
          if (!b) {
            CodeGen_Posix::visit(op);
            return;
          }
          int offset = b->value;
          if (mod_rem.remainder != ((bytes_in_vector() + offset) % bytes_in_vector())) {
            CodeGen_Posix::visit(op);
            return;
          }
          int bytes_off = std::abs(offset) * (op->type.bits() / 8);
          Expr base_low = offset > 0 ? add->a : simplify(add->a - width);
          Expr base_high = offset > 0 ? simplify(add->a + width) : add->a;
          Expr ramp_low = Ramp::make(base_low, 1, width);
          Expr ramp_high = Ramp::make(base_high, 1, width);
          Expr load_low = Load::make(op->type, op->name, ramp_low, op->image,
                                     op->param);
          Expr load_high = Load::make(op->type, op->name, ramp_high, op->image,
                                      op->param);
          Value *vec_low = codegen(load_low);
          Value *vec_high = codegen(load_high);

          Intrinsic::ID IntrinsID = (Intrinsic::ID) 0;
          std::vector<Value *> Ops;
          if (offset > 0) {
            Value *Scalar;
            if (bytes_off < 7) {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_valignbi);
              Expr ScalarImmExpr = IntImm::make(Int(32), bytes_off);
              Scalar = codegen(ScalarImmExpr);
            }
            else {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_valignb);
              // FIXME: PDB: Is this correct? Should this require a register
              // transfer.
              Scalar = codegen(bytes_off);
            }
            Ops.push_back(vec_high);
            Ops.push_back(vec_low);
            Ops.push_back(Scalar);
            Value *valign =
              CallLLVMIntrinsic(Intrinsic::
                                getDeclaration(module, IntrinsID), Ops);
            value = convertValueType(valign, llvm_type_of(op->type));
            return;
          } else {
            Value *Scalar;
            if (bytes_off < 7) {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_vlalignbi);
              Expr ScalarImmExpr = IntImm::make(Int(32), bytes_off);
              Scalar = codegen(ScalarImmExpr);
            }
            else {
              IntrinsID = IPICK(Intrinsic::hexagon_V6_vlalignb);
              // FIXME: PDB: Is this correct? Should this require a register
              // transfer.
             Scalar = codegen(bytes_off);
            }
            Ops.push_back(vec_high);
            Ops.push_back(vec_low);
            Ops.push_back(Scalar);
            Value *valign =
              CallLLVMIntrinsic(Intrinsic::
                                getDeclaration(module, IntrinsID), Ops);
            value = convertValueType(valign, llvm_type_of(op->type));
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
  // Sometimes, when we are dealing with vector selects of vector pairs,
  // it is possible to do the select as vectors and then up-cast the
  // result.
  // For e.g.
  // vector_pair_result = select(x128(s0) < x128(s1), cast<int16_t>(v0),
  //                                                  cast<int16_t>(v1))
  // where:
  //       s0, s1 are 1 byte scalars.
  //       v0 and v1 are vectors of 64 byte (single mode) or 128 bytes
  //       (double mode)
bool CodeGen_Hexagon::possiblyCodeGenNarrowerType(const Select *op) {
  // At this point, we call possiblyCodeGenNarrowerType only for vector
  // pairs and when the condition is also a vector pair.

  Expr cond = op->condition;
  Expr true_value = op->true_value;
  Expr false_value = op->false_value;
  Type t = op->type;
  Type narrow = Type(t.code(), (t.bits()/2), t.lanes());
  Expr n_cond = lossless_cast_cmp(narrow, cond);
  Expr n_tv = lossless_cast(narrow, true_value);
  Expr n_fv = lossless_cast(narrow, false_value);
  if (n_cond.defined() && n_tv.defined() && n_fv.defined()) {
    value = codegen(Cast::make(t, Select::make(n_cond, n_tv, n_fv)));
    return true;
  }
  return false;
}
void CodeGen_Hexagon::visit(const Select *op) {
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  Expr char_cmp_vector =  Variable::make(Bool(CPICK(128, 64)), "*");
  Expr short_cmp_vector =  Variable::make(Bool(CPICK(64, 32)), "*");
  Expr word_cmp_vector =  Variable::make(Bool(CPICK(32, 16)), "*");
  Expr char_dbl_cmp_vector =  Variable::make(Bool(CPICK(128*2, 64*2)), "*");
  Expr short_dbl_cmp_vector =  Variable::make(Bool(CPICK(64*2, 32*2)), "*");
  Expr word_dbl_cmp_vector =  Variable::make(Bool(CPICK(32*2, 16*2)), "*");

  if (op->condition.type().is_vector()) {
      if (isValidHexagonVectorPair(op->type, native_vector_bits())) {
        if (possiblyCodeGenNarrowerType(op))
          return;
        std::vector<Expr> PairSelects;
        std::vector<Expr> matches;
        int PredVectorSize = CPICK(1024, 512);
        PairSelects.push_back(select(char_dbl_cmp_vector,
                                     WPICK(wild_i8x256, wild_i8x128),
                                     WPICK(wild_i8x256, wild_i8x128)));
        PairSelects.push_back(select(char_dbl_cmp_vector,
                                     WPICK(wild_u8x256, wild_u8x128),
                                     WPICK(wild_u8x256, wild_u8x128)));
        PairSelects.push_back(select(short_dbl_cmp_vector,
                                     WPICK(wild_i16x128, wild_i16x64),
                                     WPICK(wild_i16x128, wild_i16x64)));
        PairSelects.push_back(select(short_dbl_cmp_vector,
                                     WPICK(wild_u16x128, wild_u16x64),
                                     WPICK(wild_u16x128, wild_u16x64)));
        PairSelects.push_back(select(word_dbl_cmp_vector,
                                     WPICK(wild_i32x64, wild_i32x32),
                                     WPICK(wild_i32x64, wild_i32x32)));
        PairSelects.push_back(select(word_dbl_cmp_vector,
                                     WPICK(wild_u32x64, wild_u32x32),
                                     WPICK(wild_u32x64, wild_u32x32)));
        for (size_t I = 0; I < PairSelects.size(); ++I) {
          const Expr P = PairSelects[I];
          if (expr_match(P, op, matches)) {
            // 1. Codegen the condition. Since we are dealing with vector pairs,
            // the condition will be of type 2048i1(128B mode) or
            // 1024i1(64B mode).
            Value *Cond = codegen(op->condition);
            // 2. Slice the condition into halves.
            // PDB. Cond should be the result of an HVX intrinsics. We put pairs
            // together using concat_vectors in all our comparison visitors.
            // This means we don't use vcombine there, or in other words, we use
            // shuffle_vector to concatenate the two vectors. This means the
            // following slice_vectors that end up inserting "shuffle_vectors"
            // and the shuffle_vectors inserted as part of codegen for cond
            // should get optimized away.
            Value *LowCond = slice_vector(Cond, 0, PredVectorSize);
            Value *HighCond = slice_vector(Cond, PredVectorSize,
                                           PredVectorSize);
            // 3. Codegen the double vectors.
            Value *DblVecA = codegen(matches[1]);
            Value *DblVecB = codegen(matches[2]);
            std::vector<Value *> OpsA, OpsB;
            // 4. Slice them into halves or vector registers.
            getHighAndLowVectors(DblVecA, OpsA);
            getHighAndLowVectors(DblVecB, OpsB);
            std::vector<Value *> Ops;
            Ops.push_back(HighCond);
            Ops.push_back(OpsA[0]);
            Ops.push_back(OpsB[0]);
            Intrinsic::ID ID = IPICK(Intrinsic::hexagon_V6_vmux);
            Value *HighMux = CallLLVMIntrinsic(Intrinsic::
                                               getDeclaration(module, ID),
                                               Ops);
            Ops.clear();
            Ops.push_back(LowCond);
            Ops.push_back(OpsA[1]);
            Ops.push_back(OpsB[1]);
            Value *LowMux = CallLLVMIntrinsic(Intrinsic::
                                               getDeclaration(module, ID),
                                               Ops);
            Ops.clear();
            Ops.push_back(LowMux);
            Ops.push_back(HighMux);
            value = convertValueType(concat_vectors(Ops),
                                     llvm_type_of(op->type));
            return;
          }
        }
      }
      else if (isValidHexagonVector(op->type, native_vector_bits())) {
        std::vector<Expr> Selects;
        std::vector<Expr> matches;
        Selects.push_back(select(char_cmp_vector,
                                     WPICK(wild_i8x128, wild_i8x64),
                                     WPICK(wild_i8x128, wild_i8x64)));
        Selects.push_back(select(char_cmp_vector,
                                     WPICK(wild_u8x128, wild_u8x64),
                                     WPICK(wild_u8x128, wild_u8x64)));
        Selects.push_back(select(short_cmp_vector,
                                     WPICK(wild_i16x64, wild_i16x32),
                                     WPICK(wild_i16x64, wild_i16x32)));
        Selects.push_back(select(short_cmp_vector,
                                     WPICK(wild_u16x64, wild_u16x32),
                                     WPICK(wild_u16x64, wild_u16x32)));
        Selects.push_back(select(word_cmp_vector,
                                     WPICK(wild_i32x32, wild_i32x16),
                                     WPICK(wild_i32x32, wild_i32x16)));
        Selects.push_back(select(word_cmp_vector,
                                     WPICK(wild_u32x32, wild_u32x16),
                                     WPICK(wild_u32x32, wild_u32x16)));
        for (size_t I = 0; I < Selects.size(); ++I) {
          const Expr P = Selects[I];
          if (expr_match(P, op, matches)) {
            // 1. Codegen the condition. Since we are dealing with vector pairs,
            // the condition will be of type 2048i1(128B mode) or
            // 1024i1(64B mode).
            Value *Cond = codegen(op->condition);
            // 2. Codegen the vectors.
            Value *VecA = codegen(matches[1]);
            Value *VecB = codegen(matches[2]);
            // 3. Generate the vmux intrinsic.
            std::vector<Value *> Ops;
            Ops.push_back(Cond);
            Ops.push_back(VecA);
            Ops.push_back(VecB);
            Intrinsic::ID ID = IPICK(Intrinsic::hexagon_V6_vmux);
            Value *Mux = CallLLVMIntrinsic(Intrinsic::
                                               getDeclaration(module, ID),
                                               Ops);
            value = convertValueType(Mux, llvm_type_of(op->type));
            return;
          }
        }
      }
  }
  if (!value) {
    checkVectorOp(op, "in visit(Select *)\n");
    CodeGen_Posix::visit(op);
  }
}
llvm::Value *
CodeGen_Hexagon::compare(llvm::Value *a, llvm::Value *b,
                         llvm::Function *F) {

  std::vector<Value*> Ops;
  Ops.push_back(a);
  Ops.push_back(b);
  Value *Cmp = CallLLVMIntrinsic(F, Ops);
  return Cmp;
}
llvm::Value *
CodeGen_Hexagon::negate(llvm::Value *a) {
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  Intrinsic::ID PredNot = IPICK(Intrinsic::hexagon_V6_pred_not);
  llvm::Function *PredNotF = Intrinsic::getDeclaration(module, PredNot);
  std::vector<Value*> Ops;
  Ops.push_back(a);
  Value *Cmp = CallLLVMIntrinsic(PredNotF, Ops);
  return Cmp;
}
llvm::Value*
CodeGen_Hexagon::generate_vector_comparison(const BaseExprNode *op,
                                          std::vector<Pattern> &VecPairCompares,
                                          std::vector<Pattern> &VecCompares,
                                          bool invert_ops, bool negate_after) {
  std::vector<Expr> matches;
  for (size_t I = 0; I < VecPairCompares.size(); ++I) {
    const Pattern &P = VecPairCompares[I];
    if (expr_match(P.pattern, op, matches)) {
      Value *DblVecA = codegen(matches[0]);
      Value *DblVecB = codegen(matches[1]);
      Intrinsic::ID IntrinsID = P.ID;
      std::vector<Value *> OpsA, OpsB;
      std::vector<Value *> Ops;
      getHighAndLowVectors(DblVecA, OpsA);
      getHighAndLowVectors(DblVecB, OpsB);
      // OpsA[0] == HighVector;
      // OpsA[1] = LowVector;
      // a <= b is !(a > b)
      llvm::Function *CmpF = Intrinsic::getDeclaration(module, IntrinsID);
      Value *LowCmp, *HighCmp;
      if (invert_ops) {
       LowCmp = compare(OpsB[0], OpsA[0], CmpF);
       HighCmp = compare(OpsB[1], OpsA[1], CmpF);
      } else {
       LowCmp = compare(OpsA[0], OpsB[0], CmpF);
       HighCmp = compare(OpsA[1], OpsB[1], CmpF);
      }
      if (negate_after) {
        LowCmp = negate(LowCmp);
        HighCmp = negate(HighCmp);
      }
      Ops.clear();
      Ops.push_back(LowCmp);
      Ops.push_back(HighCmp);
      // Do not change type back to llvm_type_of(op->type);
      return concat_vectors(Ops);
     }
  }
  for (size_t I = 0; I < VecCompares.size(); ++I) {
    const Pattern &P = VecCompares[I];
    if (expr_match(P.pattern, op, matches)) {
      Value *VecA = codegen(matches[0]);
      Value *VecB = codegen(matches[1]);
      Intrinsic::ID IntrinsID = P.ID;
      // a <= b is !(a > b)
      llvm::Function *CmpF = Intrinsic::getDeclaration(module, IntrinsID);
      Value *Cmp;
      if (invert_ops)
        Cmp = compare(VecB, VecA, CmpF);
      else
        Cmp =  compare(VecA, VecB, CmpF);
      if (negate_after)
        Cmp = negate(Cmp);
      // Do not change type back to llvm_type_of(op->type);
      return Cmp;
    }
  }
  return NULL;
}

void CodeGen_Hexagon::visit(const LE *op) {
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.push_back(Pattern(WPICK(wild_i8x256, wild_i8x128) <=
                                    WPICK(wild_i8x256, wild_i8x128),
                                    IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u8x256, wild_u8x128) <=
                                    WPICK(wild_u8x256, wild_u8x128),
                                    IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i16x128, wild_i16x64) <=
                                    WPICK(wild_i16x128, wild_i16x64),
                                    IPICK(Intrinsic::hexagon_V6_vgth)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u16x128, wild_u16x64) <=
                                    WPICK(wild_u16x128, wild_u16x64),
                                    IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i32x64, wild_i32x32) <=
                                    WPICK(wild_i32x64, wild_i32x32),
                                    IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u32x64, wild_u32x32) <=
                                    WPICK(wild_u32x64, wild_u32x32),
                                    IPICK(Intrinsic::hexagon_V6_vgtuw)));
  std::vector<Pattern> VecCompares;
  VecCompares.push_back(Pattern(WPICK(wild_i8x128, wild_i8x64) <=
                                WPICK(wild_i8x128, wild_i8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecCompares.push_back(Pattern(WPICK(wild_u8x128, wild_u8x64) <=
                                WPICK(wild_u8x128, wild_u8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecCompares.push_back(Pattern(WPICK(wild_i16x64, wild_i16x32) <=
                                WPICK(wild_i16x64, wild_i16x32),
                                IPICK(Intrinsic::hexagon_V6_vgth)));
  VecCompares.push_back(Pattern(WPICK(wild_u16x64, wild_u16x32) <=
                                WPICK(wild_u16x64, wild_u16x32),
                                IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecCompares.push_back(Pattern(WPICK(wild_i32x32, wild_i32x16) <=
                                WPICK(wild_i32x32, wild_i32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecCompares.push_back(Pattern(WPICK(wild_u32x32, wild_u32x16) <=
                                WPICK(wild_u32x32, wild_u32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtuw)));

  value = generate_vector_comparison(op,VecPairCompares, VecCompares, false,
                                     true);

  if (!value) {
    checkVectorOp(op, "in visit(LE *)\n");
    CodeGen_Posix::visit(op);
  }
}
void CodeGen_Hexagon::visit(const LT *op) {
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.push_back(Pattern(WPICK(wild_i8x256, wild_i8x128) <
                                    WPICK(wild_i8x256, wild_i8x128),
                                    IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u8x256, wild_u8x128) <
                                    WPICK(wild_u8x256, wild_u8x128),
                                    IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i16x128, wild_i16x64) <
                                    WPICK(wild_i16x128, wild_i16x64),
                                    IPICK(Intrinsic::hexagon_V6_vgth)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u16x128, wild_u16x64) <
                                    WPICK(wild_u16x128, wild_u16x64),
                                    IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i32x64, wild_i32x32) <
                                    WPICK(wild_i32x64, wild_i32x32),
                                    IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u32x64, wild_u32x32) <
                                    WPICK(wild_u32x64, wild_u32x32),
                                    IPICK(Intrinsic::hexagon_V6_vgtuw)));
  std::vector<Pattern> VecCompares;
  VecCompares.push_back(Pattern(WPICK(wild_i8x128, wild_i8x64) <
                                WPICK(wild_i8x128, wild_i8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecCompares.push_back(Pattern(WPICK(wild_u8x128, wild_u8x64) <
                                WPICK(wild_u8x128, wild_u8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecCompares.push_back(Pattern(WPICK(wild_i16x64, wild_i16x32) <
                                WPICK(wild_i16x64, wild_i16x32),
                                IPICK(Intrinsic::hexagon_V6_vgth)));
  VecCompares.push_back(Pattern(WPICK(wild_u16x64, wild_u16x32) <
                                WPICK(wild_u16x64, wild_u16x32),
                                IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecCompares.push_back(Pattern(WPICK(wild_i32x32, wild_i32x16) <
                                WPICK(wild_i32x32, wild_i32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecCompares.push_back(Pattern(WPICK(wild_u32x32, wild_u32x16) <
                                WPICK(wild_u32x32, wild_u32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtuw)));

  value = generate_vector_comparison(op,VecPairCompares, VecCompares, true,
                                     false);

  if (!value) {
    checkVectorOp(op, "in visit(LT *)\n");
    CodeGen_Posix::visit(op);
  }
}
void CodeGen_Hexagon::visit(const NE *op) {
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.push_back(Pattern(WPICK(wild_i8x256, wild_i8x128) !=
                                    WPICK(wild_i8x256, wild_i8x128),
                                    IPICK(Intrinsic::hexagon_V6_veqb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u8x256, wild_u8x128) !=
                                    WPICK(wild_u8x256, wild_u8x128),
                                    IPICK(Intrinsic::hexagon_V6_veqb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i16x128, wild_i16x64) !=
                                    WPICK(wild_i16x128, wild_i16x64),
                                    IPICK(Intrinsic::hexagon_V6_veqh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u16x128, wild_u16x64) !=
                                    WPICK(wild_u16x128, wild_u16x64),
                                    IPICK(Intrinsic::hexagon_V6_veqh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i32x64, wild_i32x32) !=
                                    WPICK(wild_i32x64, wild_i32x32),
                                    IPICK(Intrinsic::hexagon_V6_veqw)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u32x64, wild_u32x32) !=
                                    WPICK(wild_u32x64, wild_u32x32),
                                    IPICK(Intrinsic::hexagon_V6_veqw)));
  std::vector<Pattern> VecCompares;
  VecCompares.push_back(Pattern(WPICK(wild_i8x128, wild_i8x64) !=
                                WPICK(wild_i8x128, wild_i8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecCompares.push_back(Pattern(WPICK(wild_u8x128, wild_u8x64) !=
                                WPICK(wild_u8x128, wild_u8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecCompares.push_back(Pattern(WPICK(wild_i16x64, wild_i16x32) !=
                                WPICK(wild_i16x64, wild_i16x32),
                                IPICK(Intrinsic::hexagon_V6_vgth)));
  VecCompares.push_back(Pattern(WPICK(wild_u16x64, wild_u16x32) !=
                                WPICK(wild_u16x64, wild_u16x32),
                                IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecCompares.push_back(Pattern(WPICK(wild_i32x32, wild_i32x16) !=
                                WPICK(wild_i32x32, wild_i32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecCompares.push_back(Pattern(WPICK(wild_u32x32, wild_u32x16) !=
                                WPICK(wild_u32x32, wild_u32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtuw)));

  value = generate_vector_comparison(op,VecPairCompares, VecCompares, false,
                                     true);

  if (!value) {
    checkVectorOp(op, "in visit(NE *)\n");
    CodeGen_Posix::visit(op);
  }
}
void CodeGen_Hexagon::visit(const GT *op) {
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.push_back(Pattern(WPICK(wild_i8x256, wild_i8x128) >
                                    WPICK(wild_i8x256, wild_i8x128),
                                    IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u8x256, wild_u8x128) >
                                    WPICK(wild_u8x256, wild_u8x128),
                                    IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i16x128, wild_i16x64) >
                                    WPICK(wild_i16x128, wild_i16x64),
                                    IPICK(Intrinsic::hexagon_V6_vgth)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u16x128, wild_u16x64) >
                                    WPICK(wild_u16x128, wild_u16x64),
                                    IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i32x64, wild_i32x32) >
                                    WPICK(wild_i32x64, wild_i32x32),
                                    IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u32x64, wild_u32x32) >
                                    WPICK(wild_u32x64, wild_u32x32),
                                    IPICK(Intrinsic::hexagon_V6_vgtuw)));
  std::vector<Pattern> VecCompares;
  VecCompares.push_back(Pattern(WPICK(wild_i8x128, wild_i8x64) >
                                WPICK(wild_i8x128, wild_i8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtb)));
  VecCompares.push_back(Pattern(WPICK(wild_u8x128, wild_u8x64) >
                                WPICK(wild_u8x128, wild_u8x64),
                                IPICK(Intrinsic::hexagon_V6_vgtub)));
  VecCompares.push_back(Pattern(WPICK(wild_i16x64, wild_i16x32) >
                                WPICK(wild_i16x64, wild_i16x32),
                                IPICK(Intrinsic::hexagon_V6_vgth)));
  VecCompares.push_back(Pattern(WPICK(wild_u16x64, wild_u16x32) >
                                WPICK(wild_u16x64, wild_u16x32),
                                IPICK(Intrinsic::hexagon_V6_vgtuh)));
  VecCompares.push_back(Pattern(WPICK(wild_i32x32, wild_i32x16) >
                                WPICK(wild_i32x32, wild_i32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtw)));
  VecCompares.push_back(Pattern(WPICK(wild_u32x32, wild_u32x16) >
                                WPICK(wild_u32x32, wild_u32x16),
                                IPICK(Intrinsic::hexagon_V6_vgtuw)));

  value = generate_vector_comparison(op,VecPairCompares, VecCompares, false,
                                     false);

  if (!value) {
    checkVectorOp(op, "in visit(GT *)\n");
    CodeGen_Posix::visit(op);
  }
}
void CodeGen_Hexagon::visit(const EQ *op) {
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_DOUBLE);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.push_back(Pattern(WPICK(wild_i8x256, wild_i8x128) ==
                                    WPICK(wild_i8x256, wild_i8x128),
                                    IPICK(Intrinsic::hexagon_V6_veqb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u8x256, wild_u8x128) ==
                                    WPICK(wild_u8x256, wild_u8x128),
                                    IPICK(Intrinsic::hexagon_V6_veqb)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i16x128, wild_i16x64) ==
                                    WPICK(wild_i16x128, wild_i16x64),
                                    IPICK(Intrinsic::hexagon_V6_veqh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u16x128, wild_u16x64) ==
                                    WPICK(wild_u16x128, wild_u16x64),
                                    IPICK(Intrinsic::hexagon_V6_veqh)));
  VecPairCompares.push_back(Pattern(WPICK(wild_i32x64, wild_i32x32) ==
                                    WPICK(wild_i32x64, wild_i32x32),
                                    IPICK(Intrinsic::hexagon_V6_veqw)));
  VecPairCompares.push_back(Pattern(WPICK(wild_u32x64, wild_u32x32) ==
                                    WPICK(wild_u32x64, wild_u32x32),
                                    IPICK(Intrinsic::hexagon_V6_veqw)));
  std::vector<Pattern> VecCompares;
  VecCompares.push_back(Pattern(WPICK(wild_i8x128, wild_i8x64) ==
                                WPICK(wild_i8x128, wild_i8x64),
                                IPICK(Intrinsic::hexagon_V6_veqb)));
  VecCompares.push_back(Pattern(WPICK(wild_u8x128, wild_u8x64) ==
                                WPICK(wild_u8x128, wild_u8x64),
                                IPICK(Intrinsic::hexagon_V6_veqb)));
  VecCompares.push_back(Pattern(WPICK(wild_i16x64, wild_i16x32) ==
                                WPICK(wild_i16x64, wild_i16x32),
                                IPICK(Intrinsic::hexagon_V6_veqh)));
  VecCompares.push_back(Pattern(WPICK(wild_u16x64, wild_u16x32) ==
                                WPICK(wild_u16x64, wild_u16x32),
                                IPICK(Intrinsic::hexagon_V6_veqh)));
  VecCompares.push_back(Pattern(WPICK(wild_i32x32, wild_i32x16) ==
                                WPICK(wild_i32x32, wild_i32x16),
                                IPICK(Intrinsic::hexagon_V6_veqw)));
  VecCompares.push_back(Pattern(WPICK(wild_u32x32, wild_u32x16) ==
                                WPICK(wild_u32x32, wild_u32x16),
                                IPICK(Intrinsic::hexagon_V6_veqw)));

  value = generate_vector_comparison(op,VecPairCompares, VecCompares, false,
                                     false);

  if (!value) {
    checkVectorOp(op, "in visit(EQ *)\n");
    CodeGen_Posix::visit(op);
  }
}
}}

