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

#define HEXAGON_SINGLE_MODE_VECTOR_SIZE 64
#define HEXAGON_SINGLE_MODE_VECTOR_SIZE_IN_BITS 64 * 8
#define CPICK(c128, c64) (B128 ? c128 : c64)
#define WPICK(w128, w64) (B128 ? w128 : w64)
#define IPICK(i64) (B128 ? i64##_128B : i64)

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
  std::vector<Pattern> casts, typecasts, varith, averages, combiners, vbitwise, multiplies;

namespace {
Expr sat_h_ub(Expr A) {
  return max(min(A, 255), 0);
}
Expr sat_w_h(Expr A) {
  return max(min(A, 32767), -32768);
}

Expr bitwiseOr(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_or, {A, B},
                              Internal::Call::PureIntrinsic);
}
Expr bitwiseAnd(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_and, {A, B},
                              Internal::Call::PureIntrinsic);
}
Expr bitwiseXor(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_xor, {A, B},
                              Internal::Call::PureIntrinsic);
}
Expr vavg_round(Expr A, Expr B) {
  Type wider = A.type().with_bits(A.type().bits() * 2);
  return cast(A.type(), (cast(wider, A) + cast(wider, B) + 1)/2);
}
Expr vavg(Expr A, Expr B) {
  Type wider = A.type().with_bits(A.type().bits() * 2);
  return cast(A.type(), (cast(wider, A) + cast(wider, B))/2);
}
Expr vnavg(Expr A, Expr B) {
  Type wider = A.type().with_bits(A.type().bits() * 2);
  return cast(A.type(), (cast(wider, A) - cast(wider, B))/2);
}
Expr sat_sub(Expr A, Expr B) {
  Type wider = Int(A.type().bits() * 2, A.type().lanes());
  Expr max_v = A.type().max();
  Expr min_v = A.type().min();
  return cast(A.type(), max(min(cast(wider, A) - cast(wider, B),
                                max_v), min_v));

}
Expr sat_add(Expr A, Expr B) {
  Type wider = Int(A.type().bits() * 2, A.type().lanes());
  Expr max_v = A.type().max();
  Expr min_v = A.type().min();
  return cast(A.type(), max(min(cast(wider, A) + cast(wider, B),
                                max_v), min_v));
}
#ifdef THESE_ARE_UNUSED
Expr bitwiseNot(Expr A) {
  return Internal::Call::make(A.type(), Internal::Call::bitwise_not, {A},
                              Internal::Call::PureIntrinsic);
}
Expr shiftLeft(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::shift_left, {A, B},
                              Internal::Call::PureIntrinsic);
}

Expr shiftRight(Expr A, Expr B) {
  return Internal::Call::make(A.type(), Internal::Call::shift_right, {A, B},
                              Internal::Call::PureIntrinsic);
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

const int min_i8 = -128, max_i8 = 127;
const int min_i16 = -32768, max_i16 = 32767;
const int min_i32 = 0x80000000, max_i32 = 0x7fffffff;
const int max_u8 = 255;
const int max_u16 = 65535;
Expr max_u32 = UInt(32).max();

Expr i32c(Expr e) { return i32_(clamp(e, min_i32, max_i32)); }
Expr u32c(Expr e) { return u32_(clamp(e, 0, max_u32)); }
Expr i16c(Expr e) { return i16_(clamp(e, min_i16, max_i16)); }
Expr u16c(Expr e) { return u16_(clamp(e, 0, max_u16)); }
Expr i8c(Expr e) { return i8_(clamp(e, min_i8, max_i8)); }
Expr u8c(Expr e) { return u8_(clamp(e, 0, max_u8)); }
}


CodeGen_Hexagon::CodeGen_Hexagon(Target t)
  : CodeGen_Posix(t),
    wild_i32(Variable::make(Int(32), "*")),
    wild_u32(Variable::make(UInt(32), "*")),
    wild_i16(Variable::make(Int(16), "*")),
    wild_u16(Variable::make(UInt(16), "*")),
    wild_i8(Variable::make(Int(8), "*")),
    wild_u8(Variable::make(UInt(8), "*")) {
  bool B128 = t.has_feature(Halide::Target::HVX_128);

  wild_u8xW = B128 ? wild_u8x128 : wild_u8x64;
  wild_i8xW = B128 ? wild_i8x128 : wild_i8x64;
  wild_u16xW = B128 ? wild_u16x64 : wild_u16x32;
  wild_i16xW = B128 ? wild_i16x64 : wild_i16x32;
  wild_u32xW = B128 ? wild_u32x32 : wild_u32x16;
  wild_i32xW = B128 ? wild_i32x32 : wild_i32x16;

  wild_u8x2W = B128 ? wild_u8x256 : wild_u8x128;
  wild_i8x2W = B128 ? wild_i8x256 : wild_i8x128;
  wild_u16x2W = B128 ? wild_u16x128 : wild_u16x64;
  wild_i16x2W = B128 ? wild_i16x128 : wild_i16x64;
  wild_u32x2W = B128 ? wild_u32x64 : wild_u32x32;
  wild_i32x2W = B128 ? wild_i32x64 : wild_i32x32;

  wild_u8x4W = B128 ? wild_u8x512 : wild_u8x256;
  wild_i8x4W = B128 ? wild_i8x512 : wild_i8x256;
  wild_u16x4W = B128 ? wild_u16x256 : wild_u16x128;
  wild_i16x4W = B128 ? wild_i16x256 : wild_i16x128;
  wild_u32x4W = B128 ? wild_u32x128 : wild_u32x64;
  wild_i32x4W = B128 ? wild_i32x128 : wild_i32x64;

  casts.emplace_back(cast(UInt(16, CPICK(128,64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vzb));
  casts.emplace_back(cast(UInt(16, CPICK(128,64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vzb));
  casts.emplace_back(cast(Int(16, CPICK(128,64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vzb));
  casts.emplace_back(cast(UInt(32, CPICK(64,32)), wild_u16xW), IPICK(Intrinsic::hexagon_V6_vzh));
  casts.emplace_back(cast(Int(32, CPICK(64,32)), wild_u16xW), IPICK(Intrinsic::hexagon_V6_vzh));
  casts.emplace_back(cast(Int(16, CPICK(128,64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vsb));
  casts.emplace_back(cast(Int(32, CPICK(64,32)), wild_i16xW), IPICK(Intrinsic::hexagon_V6_vsh));

  // same size typecasts
  typecasts.emplace_back(cast(Int(32, CPICK(32,16)), wild_u32xW), IPICK(Intrinsic::hexagon_V6_vassign));
  typecasts.emplace_back(cast(Int(16, CPICK(64,32)), wild_u16xW), IPICK(Intrinsic::hexagon_V6_vassign));
  typecasts.emplace_back(cast(Int(8, CPICK(128,64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vassign));
  typecasts.emplace_back(cast(UInt(32, CPICK(32,16)), wild_i32xW), IPICK(Intrinsic::hexagon_V6_vassign));
  typecasts.emplace_back(cast(UInt(16, CPICK(64,32)), wild_i16xW), IPICK(Intrinsic::hexagon_V6_vassign));
  typecasts.emplace_back(cast(UInt(8, CPICK(128,64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vassign));

  combiners.emplace_back(absd(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vabsdiffub));
  combiners.emplace_back(absd(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vabsdiffuh));
  combiners.emplace_back(absd(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vabsdiffh));
  combiners.emplace_back(absd(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vabsdiffw));

  // Our bitwise operations are all type agnostic; all they need are vectors
  // of 64 bytes (single mode) or 128 bytes (double mode). Over 4 types -
  // unsigned bytes, signed and unsigned half-word, and signed word, we have
  // 12 such patterns for each operation. But, we'll stick to only like types
  // here.
  vbitwise.emplace_back(bitwiseAnd(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vand));
  vbitwise.emplace_back(bitwiseAnd(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vand));
  vbitwise.emplace_back(bitwiseAnd(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vand));
  vbitwise.emplace_back(bitwiseAnd(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vand));

  vbitwise.emplace_back(bitwiseXor(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vxor));
  vbitwise.emplace_back(bitwiseXor(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vxor));
  vbitwise.emplace_back(bitwiseXor(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vxor));
  vbitwise.emplace_back(bitwiseXor(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vxor));

  vbitwise.emplace_back(bitwiseOr(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vor));
  vbitwise.emplace_back(bitwiseOr(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vor));
  vbitwise.emplace_back(bitwiseOr(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vor));
  vbitwise.emplace_back(bitwiseOr(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vor));

  // "Add"
  // Byte Vectors
  varith.emplace_back(wild_i8xW + wild_i8xW, IPICK(Intrinsic::hexagon_V6_vaddb));
  varith.emplace_back(wild_u8xW + wild_u8xW, IPICK(Intrinsic::hexagon_V6_vaddubsat));
  // Half Vectors
  varith.emplace_back(wild_i16xW + wild_i16xW, IPICK(Intrinsic::hexagon_V6_vaddh));
  varith.emplace_back(wild_u16xW + wild_u16xW, IPICK(Intrinsic::hexagon_V6_vaddh));
  // Word Vectors.
  varith.emplace_back(wild_i32xW + wild_i32xW, IPICK(Intrinsic::hexagon_V6_vaddw));
// Hexagon v62 feature.
#include <v62feat1.inc>
  {
    // Note: no 32-bit saturating unsigned add in V60, use vaddw
    varith.emplace_back(wild_u32xW + wild_u32xW, IPICK(Intrinsic::hexagon_V6_vaddw));
  }

  // Double Vectors
  // Byte Double Vectors
  varith.emplace_back(wild_i8x2W + wild_i8x2W, IPICK(Intrinsic::hexagon_V6_vaddb_dv));
  varith.emplace_back(wild_u8x2W + wild_u8x2W, IPICK(Intrinsic::hexagon_V6_vaddubsat_dv));
  // Half Double Vectors
  varith.emplace_back(wild_i16x2W + wild_i16x2W, IPICK(Intrinsic::hexagon_V6_vaddh_dv));
  varith.emplace_back(wild_u16x2W + wild_u16x2W, IPICK(Intrinsic::hexagon_V6_vadduhsat_dv));
  // Word Double Vectors.
  varith.emplace_back(wild_i32x2W + wild_i32x2W, IPICK(Intrinsic::hexagon_V6_vaddw_dv));
// Hexagon v62 feature
#include <v62feat2.inc>
  {
    // Note: no 32-bit saturating unsigned add in V60, use vaddw
    varith.emplace_back(wild_u32x2W + wild_u32x2W, IPICK(Intrinsic::hexagon_V6_vaddw_dv));
  }


  // "Sub"
  // Byte Vectors
  varith.emplace_back(wild_i8xW - wild_i8xW, IPICK(Intrinsic::hexagon_V6_vsubb));
  varith.emplace_back(wild_u8xW - wild_u8xW, IPICK(Intrinsic::hexagon_V6_vsububsat));
  // Half Vectors
  varith.emplace_back(wild_i16xW - wild_i16xW, IPICK(Intrinsic::hexagon_V6_vsubh));
  varith.emplace_back(wild_u16xW - wild_u16xW, IPICK(Intrinsic::hexagon_V6_vsubh));
  // Word Vectors.
  varith.emplace_back(wild_i32xW - wild_i32xW, IPICK(Intrinsic::hexagon_V6_vsubw));
  // Double Vectors
  // Byte Double Vectors
  varith.emplace_back(wild_i8x2W - wild_i8x2W, IPICK(Intrinsic::hexagon_V6_vsubb_dv));
  varith.emplace_back(wild_u8x2W - wild_u8x2W, IPICK(Intrinsic::hexagon_V6_vsububsat_dv));
  // Half Double Vectors
  varith.emplace_back(wild_i16x2W - wild_i16x2W, IPICK(Intrinsic::hexagon_V6_vsubh_dv));
  varith.emplace_back(wild_u16x2W - wild_u16x2W, IPICK(Intrinsic::hexagon_V6_vsubuhsat_dv));
  // Word Double Vectors.
  varith.emplace_back(wild_i32x2W - wild_i32x2W, IPICK(Intrinsic::hexagon_V6_vsubw_dv));

  // "Max"
  varith.emplace_back(max(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vmaxub));
  varith.emplace_back(max(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vmaxh));
  varith.emplace_back(max(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vmaxuh));
  varith.emplace_back(max(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vmaxw));
  // "Min"
  varith.emplace_back(min(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vminub));
  varith.emplace_back(min(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vminh));
  varith.emplace_back(min(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vminuh));
  varith.emplace_back(min(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vminw));

  averages.emplace_back(((wild_u8xW + wild_u8xW)/2), IPICK(Intrinsic::hexagon_V6_vavgub));
  averages.emplace_back(((wild_u8xW - wild_u8xW)/2), IPICK(Intrinsic::hexagon_V6_vnavgub));
  averages.emplace_back(((wild_u16xW + wild_u16xW)/2), IPICK(Intrinsic::hexagon_V6_vavguh));
  averages.emplace_back(((wild_i16xW + wild_i16xW)/2), IPICK(Intrinsic::hexagon_V6_vavgh));
  averages.emplace_back(((wild_i16xW - wild_i16xW)/2), IPICK(Intrinsic::hexagon_V6_vnavgh));
  averages.emplace_back(((wild_i32xW + wild_i32xW)/2), IPICK(Intrinsic::hexagon_V6_vavgw));
  averages.emplace_back(((wild_i32xW - wild_i32xW)/2), IPICK(Intrinsic::hexagon_V6_vnavgw));

  multiplies.emplace_back(u16_(wild_u8xW * wild_u8xW), IPICK(Intrinsic::hexagon_V6_vmpyubv));
  multiplies.emplace_back(i16_(wild_i8xW * wild_i8xW), IPICK(Intrinsic::hexagon_V6_vmpybv));
  multiplies.emplace_back(u32_(wild_u16xW * wild_u16xW), IPICK(Intrinsic::hexagon_V6_vmpyuhv));
  multiplies.emplace_back(i32_(wild_i16xW * wild_i16xW), IPICK(Intrinsic::hexagon_V6_vmpyhv));
  multiplies.emplace_back(wild_i16xW * wild_i16xW, IPICK(Intrinsic::hexagon_V6_vmpyih));

}

std::unique_ptr<llvm::Module> CodeGen_Hexagon::compile(const Module &module) {
    auto llvm_module = CodeGen_Posix::compile(module);
    static bool options_processed = false;

    // TODO: This should be set on the module itself, or some other
    // safer way to pass this through to the target specific lowering
    // passes. We set the option here (after the base class'
    // implementaiton of compile) because it is the last
    // Hexagon-specific code to run prior to invoking the target
    // specific lowering in LLVM, minimizing the chances of the wrong
    // flag being set for the wrong module.
    if (!options_processed) {
      cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_ARGS",
                                  "Halide HVX internal compiler\n");
      // We need to EnableQuIC for LLVM and Halide (Unrolling).
      char *s = strdup("HALIDE_LLVM_QUIC=-hexagon-small-data-threshold=0");
      ::putenv(s);
      cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_QUIC",
                                  "Halide HVX quic option\n");
    }
    options_processed = true;

    if (module.target().has_feature(Halide::Target::HVX_128)) {
        char *s = strdup("HALIDE_LLVM_INTERNAL=-enable-hexagon-hvx-double");
        ::putenv(s);
        cl::ParseEnvironmentOptions("halide-hvx-be", "HALIDE_LLVM_INTERNAL",
                                    "Halide HVX internal options\n");
        if (module.target().has_feature(Halide::Target::HVX_64))
                internal_error << "Both HVX_64 and HVX_128 set at same time\n";
    }
    return llvm_module;
}

llvm::Value *CodeGen_Hexagon::callLLVMIntrinsic(llvm::Function *F,
                                                std::vector<Value *> Ops) {
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
llvm::Value *CodeGen_Hexagon::callLLVMIntrinsic(llvm::Intrinsic::ID id,
                                                std::vector<Value *> Ops) {
    return callLLVMIntrinsic(Intrinsic::getDeclaration(module.get(), id), Ops);
}

llvm::Value *
CodeGen_Hexagon::convertValueType(llvm::Value *V, llvm::Type *T) {
  if (T != V->getType())
    return builder->CreateBitCast(V, T);
  else
    return V;
}
std::vector<Expr>
CodeGen_Hexagon::getHighAndLowVectors(Expr DoubleVec) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  int VecSize = HEXAGON_SINGLE_MODE_VECTOR_SIZE;
  if (B128) VecSize *= 2;
  Expr Hi, Lo;

  Type t = DoubleVec.type();
  Type NewT = Type(t.code(), t.bits(), t.lanes()/2);
  int NumElements = NewT.lanes();

  debug(4) << "HexCG: getHighAndLowVectors(Expr) "
           << "DoubleVec.type: " << t
           << " NewT: " << NewT << "\n";

  internal_assert(t.is_vector());
  internal_assert((t.bytes() * t.lanes()) == 2*VecSize);
  internal_assert(NewT.is_vector());
  internal_assert((NewT.bytes() * NumElements) == VecSize);

  const Broadcast *B = DoubleVec.as<Broadcast>();
  if (B) {
    Hi = Broadcast::make(B->value, NumElements);
    Lo = Broadcast::make(B->value, NumElements);
  } else {
    Hi = Call::make(NewT, Call::get_high_register, {DoubleVec},
                    Call::PureIntrinsic);
    Lo = Call::make(NewT, Call::get_low_register, {DoubleVec},
                    Call::PureIntrinsic);
  }
  return {Hi, Lo};
}
std::vector<Value *>
CodeGen_Hexagon::getHighAndLowVectors(llvm::Value *DoubleVec) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);

  debug(4) << "HexCG: getHighAndLowVectors(Value)\n";

  Value *Hi = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_hi), {DoubleVec});
  Value *Lo = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_lo), {DoubleVec});
  return {Hi, Lo};
}
llvm::Value *
CodeGen_Hexagon::concatVectors(Value *High, Value *Low) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);

  debug(4) << "HexCG: concatVectors(Value, Value)\n";

  Value *CombineCall = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_vcombine), {High, Low});
  return CombineCall;
}
std::vector<Expr>
CodeGen_Hexagon::slice_into_halves(Expr a) {
  if(!a.type().is_vector())
      return {};
  Expr A_low, A_high;
  Type t = a.type();
  Type NewT = Type(t.code(), t.bits(), t.lanes()/2);
  int NumElements = NewT.lanes();

  debug(4) << "HexCG: slice_into_halves(Expr) "
           << "a.type: " << t
           << " NewT: " << NewT << "\n";

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
                       Call::PureIntrinsic);
    A_high = Call::make(NewT, Call::shuffle_vector, ShuffleArgsAHigh,
                        Call::PureIntrinsic);
  }

  // use high/low ordering to match getHighAndLowVectors
  return {A_high, A_low};
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
  if (DoTrace)
    debug(1) << (target.has_feature(Halide::Target::HVX_V62)
             ? "V62\n" : "V60\n");
  if (target.has_feature(Halide::Target::HVX_128)) {
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
    debug(2) << "VEC-EXPECT " << msg ;
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
  for (const Pattern &P : Patterns) {
    if (expr_match(P.pattern, op, matches)) {
        Intrinsic::ID ID = P.ID;
        bool BitCastNeeded = false;
        llvm::Type *BitCastBackTo;
        llvm::Function *F = Intrinsic::getDeclaration(module.get(), ID);
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
        std::vector<Value *> Ops;
        Ops.push_back(Lt);
        Ops.push_back(Rt);
        Value *Call = builder->CreateCall(F, Ops);
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);

  pattern = (wild_i32xW * wild_i32xW) + (wild_i32xW * wild_i32xW);
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  Expr pattern;
  // pattern = (cast(Int(16, 64), wild_u8x64) * cast(Int(16, 64), wild_u8x64)) +
  //   (cast(Int(16, 64), wild_u8x64) * cast(Int(16, 64), wild_u8x64));
  pattern = (wild_i16x2W * wild_i16x2W) + (wild_i16x2W * wild_i16x2W);
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
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

  Patterns.emplace_back(wild_i16x2W + wild_i16x2W + wild_i16x2W,
                        IPICK(Intrinsic::hexagon_V6_vmpabus_acc));
  Patterns.emplace_back(wild_i32x2W + wild_i32x2W + wild_i32x2W,
                        IPICK(Intrinsic::hexagon_V6_vmpahb_acc));
  vector<Expr> matches;
  for (const Pattern &P : Patterns) {
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
      Value *Result = callLLVMIntrinsic(P.ID, {Accumulator, Multiplicand, Scalar});
      value =  convertValueType(Result, llvm_type_of(op->type));
      return true;
    }
  }
  return false;
}
void CodeGen_Hexagon::visit(const Add *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
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
      Intrinsic::getDeclaration(module.get(), IPICK(Intrinsic::hexagon_V6_vmpabuuv));
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
    std::vector<Value *> Ops;
    Ops.push_back(Lt);
    Ops.push_back(Rt);
    Value *Call = builder->CreateCall(F, Ops);
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
      Intrinsic::getDeclaration(module.get(), IPICK(Intrinsic::hexagon_V6_vdmpyhvsat));
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
    std::vector<Value *> Ops;
    Ops.push_back(Lt);
    Ops.push_back(Rt);
    Value *Call = builder->CreateCall(F, Ops);
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Expr> Patterns, matches;
  int num_hw_pair = (bytes_in_vector() / 2) * 2; // num half words in a pair.
  int num_w_quad = (bytes_in_vector() / 4) * 4;
  Patterns.push_back((wild_i32x4W * wild_i32x4W + (1 << 14))
                     / Broadcast::make(wild_i32, num_w_quad));
  Patterns.push_back((wild_i16x2W * wild_i16x2W + (1 << 14))
                     / Broadcast::make(wild_i16, num_hw_pair));
  for (const Expr &P : Patterns) {
    if (expr_match(P, op, matches)) {
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
        Value *DoubleVecA = codegen(matches[0]);
        Value *DoubleVecB = codegen(matches[1]);
        std::vector<Value *> OpsA = getHighAndLowVectors(DoubleVecA);
        std::vector<Value *> OpsB = getHighAndLowVectors(DoubleVecB);
        Value *HighRes = callLLVMIntrinsic(IntrinsID, {OpsA[0], OpsB[0]});
        Value *LowRes = callLLVMIntrinsic(IntrinsID, {OpsA[1], OpsB[1]});
        Value *Op = concat_vectors({LowRes, HighRes});
        if (t.bits() != 32)
          return convertValueType(Op, llvm_type_of(op->type));
        else
          return convertValueType(Op, llvm_type_of(matches[0].type()));
      } else
        return NULL;
    }
  }
  return NULL;
}
void CodeGen_Hexagon::visit(const Div *op) {
  debug(2) << "HexCG: " << op->type <<  ", visit(Div)\n";
  if (!op->type.is_vector()) {
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_128);
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
      Patterns.emplace_back(wild_u32xW / Broadcast::make(wild_u32, WordsInVector),
                            IPICK(Intrinsic::hexagon_V6_vlsrw));
      Patterns.emplace_back(wild_u16xW / Broadcast::make(wild_u16, HalfWordsInVector),
                            IPICK(Intrinsic::hexagon_V6_vlsrh));
      Patterns.emplace_back(wild_i32xW / Broadcast::make(wild_i32, WordsInVector),
                            IPICK(Intrinsic::hexagon_V6_vasrw));
      Patterns.emplace_back(wild_i16xW / Broadcast::make(wild_i16, HalfWordsInVector),
                            IPICK(Intrinsic::hexagon_V6_vasrh));
      for (const Pattern &P : Patterns) {
        if (expr_match(P.pattern, op, matches)) {
          int rt_shift_by = 0;
          if (is_const_power_of_two_integer(matches[1], &rt_shift_by)) {
            Value *Vector = codegen(matches[0]);
            Value *ShiftBy = codegen(rt_shift_by);
            Value *Result = callLLVMIntrinsic(P.ID, {Vector, ShiftBy});
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
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors (Add)\n";
  #define _OP(a,b)  a + b
  #include "CodeGen_Hexagon_LV.h"
}

// Handle Sub on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Sub *op) {
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors (Sub)\n";
  #define _OP(a,b)  a - b
  #include "CodeGen_Hexagon_LV.h"
}

// Handle Min on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Min *op) {
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors (Min)\n";
  #define _OP(a,b)  min(a, b)
  #include "CodeGen_Hexagon_LV.h"
}

// Handle Max on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Max *op) {
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors (Max)\n";
  #define _OP(a,b)  max(a, b)
  #include "CodeGen_Hexagon_LV.h"
}

// Handle Mul on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Mul *op) {
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors (Mul)\n";
  #define _OP(a,b)  a * b
  #include "CodeGen_Hexagon_LV.h"
}

// Handle Div on types greater than a single vector
llvm::Value *
CodeGen_Hexagon::handleLargeVectors(const Div *op) {
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors (Div)\n";
  #define _OP(a,b)  a / b
  #include "CodeGen_Hexagon_LV.h"
}

llvm::Value *
CodeGen_Hexagon::handleLargeVectors_absd(const Call *op) {
  debug(4) << "HexCG: " << op->type << ", handleLargeVectors_absd (Call)\n";
  #define _OP(a,b)  absd(a, b)
  #include "CodeGen_Hexagon_LV.h"
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  if (isWideningVectorCast(op)) {
    std::vector<Pattern> Patterns;
    debug(4) << "Entered handleLargeVectors (Cast)\n";
    std::vector<Expr> matches;

    // 8-bit -> 16-bit (2x widening)
    Patterns.emplace_back(cast(UInt(16, CPICK(128, 64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vzb));
    Patterns.emplace_back(cast(UInt(16, CPICK(128, 64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vzb));

    Patterns.emplace_back(cast(Int(16, CPICK(128, 64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vzb));
    Patterns.emplace_back(cast(Int(16, CPICK(128, 64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vsb));

    // 16-bit -> 32-bit (2x widening)
    Patterns.emplace_back(cast(UInt(32, CPICK(128, 64)), wild_u16x2W), IPICK(Intrinsic::hexagon_V6_vzh));
    Patterns.emplace_back(cast(UInt(32, CPICK(128, 64)), wild_i16x2W), IPICK(Intrinsic::hexagon_V6_vzh));

    Patterns.emplace_back(cast(Int(32, CPICK(128, 64)), wild_u16x2W), IPICK(Intrinsic::hexagon_V6_vzh));
    Patterns.emplace_back(cast(Int(32, CPICK(128, 64)), wild_i16x2W), IPICK(Intrinsic::hexagon_V6_vsh));

    // 8-bit -> 32-bit (4x widening)
    // Note: listed intrinsic is the second step (16->32bit) widening
    Patterns.emplace_back(cast(UInt(32, CPICK(128, 64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vzh));
    Patterns.emplace_back(cast(UInt(32, CPICK(128, 64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vzh));

    Patterns.emplace_back(cast(Int(32, CPICK(128, 64)), wild_u8xW), IPICK(Intrinsic::hexagon_V6_vzh));
    Patterns.emplace_back(cast(Int(32, CPICK(128, 64)), wild_i8xW), IPICK(Intrinsic::hexagon_V6_vsh));


    for (const Pattern &P : Patterns) {
        if (expr_match(P.pattern, op, matches)) {
          // You have a vector that is u8x64 in matches. Extend this to u32x64.
          debug(4) << "HexCG::" << op->type <<
            " handleLargeVectors(const Cast *)\n";

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
          std::vector<Value *> Ops = getHighAndLowVectors(DoubleVector);
          Value *HiVecReg = Ops[0];
          Value *LowVecReg = Ops[1];

          debug(4) << "HexCG::" << "Widening lower vector reg elements(even)"
            " elements to 32 bit\n";
          Value *EvenRegisterPair = callLLVMIntrinsic(P.ID, {LowVecReg});
          debug(4) << "HexCG::" << "Widening higher vector reg elements(odd)"
            " elements to 32 bit\n";
          Value *OddRegisterPair = callLLVMIntrinsic(P.ID, {HiVecReg});
          // Note: At this point we are returning a concatenated vector of type
          // u32x64 or i32x64. This is essentially an illegal type for the
          // Hexagon HVX LLVM backend. However, we expect to break this
          // down before it is stored or whenever these are computed say by
          // a mul node, we expect the visitor to break them down and compose
          // them again.
          debug(4) << "HexCG::" << "Concatenating the two vectors\n";
          Value *V = concat_vectors({EvenRegisterPair, OddRegisterPair});
          return convertValueType(V, llvm_type_of(op->type));
        }
    }
  } else {
    // Look for saturate & pack
    std::vector<Pattern> Patterns;
    std::vector<Expr> matches;
    Patterns.emplace_back(u8_(min(wild_u32x4W, UINT_8_IMAX)), IPICK(Intrinsic::hexagon_V6_vsathub));
    // Due to aggressive simplification introduced by the patch below
    // the max operator is removed when saturating a signed value down
    // to an unsigned value.
    // commit 23c461ce6cc50e22becbd38e6217c7e9e9b4dd98
    //  Author: Andrew Adams <andrew.b.adams@gmail.com>
    //  Date:   Mon Jun 16 15:38:13 2014 -0700
    // Weaken some simplification rules due to horrible compile times.
    Patterns.emplace_back(u8_(min(wild_i32x4W, UINT_8_IMAX)), IPICK(Intrinsic::hexagon_V6_vsathub));
    // Fixme: PDB: Shouldn't the signed version have a max in the pattern too?
    // Yes it should. So adding it now.
    Patterns.emplace_back(i8c(wild_i32x4W), Intrinsic::not_intrinsic);
    Patterns.emplace_back(u8c(wild_i32x4W), IPICK(Intrinsic::hexagon_V6_vsathub));

    for (const Pattern &P : Patterns) {
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
                FirstStep = codegen(i16c(matches[0]));
              }
            }
          } else {
            // u32->u8
            // FirstStepType is uint16.
            FirstStep = codegen(cast(FirstStepType,
                                     (min(matches[0],
                                          UINT_16_IMAX))));
          }
          Value *V = callLLVMIntrinsic(P.ID, getHighAndLowVectors(FirstStep));
          return convertValueType(V, llvm_type_of(op->type));
        }
      }
    }
    {
      // Truncate and pack.
      Patterns.clear();
      matches.clear();

      Patterns.emplace_back(u8_(wild_u32x4W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      Patterns.emplace_back(i8_(wild_u32x4W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      Patterns.emplace_back(u8_(wild_i32x4W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      Patterns.emplace_back(i8_(wild_i32x4W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      for (const Pattern & P : Patterns) {
        if (expr_match(P.pattern, op, matches)) {
          Type FirstStepType = Type(matches[0].type().code(), 16, op->type.lanes());
          Value *FirstStep = codegen(cast(FirstStepType, matches[0]));
          Value * V = callLLVMIntrinsic(P.ID, getHighAndLowVectors(FirstStep));
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
#include <v62feat3.inc>
      Patterns.emplace_back(u16_(min(wild_u32x4W, UINT_16_IMAX)),
                            IPICK(Intrinsic::hexagon_V6_vsatwh));

      Patterns.emplace_back(i16c(wild_i32x4W), IPICK(Intrinsic::hexagon_V6_vsatwh));
      for (const Pattern &P : Patterns) {
        if (expr_match(P.pattern, op, matches)) {
          Value *Vector = codegen(matches[0]);
          int bytes_in_vector = native_vector_bits() / 8;
          int VectorSize = (2 * bytes_in_vector)/4;
          // We now have a u32x64 vector, i.e. 2 vector register pairs.
          Value *EvenRegPair = slice_vector(Vector, 0, VectorSize);
          Value *OddRegPair = slice_vector(Vector, VectorSize, VectorSize);
          // We now have the lower register pair in EvenRegPair.
          // TODO: For v61 use hexagon_V6_vsathuwuh
          Value *EvenHalf = callLLVMIntrinsic(P.ID, getHighAndLowVectors(EvenRegPair));
          Value *OddHalf = callLLVMIntrinsic(P.ID, getHighAndLowVectors(OddRegPair));

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

      Patterns.emplace_back(u16_(wild_u32x4W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      Patterns.emplace_back(i16_(wild_u32x4W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      Patterns.emplace_back(u16_(wild_i32x4W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      Patterns.emplace_back(i16_(wild_i32x4W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      for (const Pattern &P : Patterns) {
        if (expr_match(P.pattern, op, matches)) {
          Value *Vector = codegen(matches[0]);
          int bytes_in_vector = native_vector_bits() / 8;
          int VectorSize = (2 * bytes_in_vector)/4;
          // We now have a u32x64 vector, i.e. 2 vector register pairs.
          Value *EvenRegPair = slice_vector(Vector, 0, VectorSize);
          Value *OddRegPair = slice_vector(Vector, VectorSize, VectorSize);
          // We now have the lower register pair in EvenRegPair.
          Value *EvenHalf = callLLVMIntrinsic(P.ID, getHighAndLowVectors(EvenRegPair));
          Value *OddHalf = callLLVMIntrinsic(P.ID, getHighAndLowVectors(OddRegPair));

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

bool CodeGen_Hexagon::possiblyCodeGenSaturatingArith(const Cast *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> patterns;
  vector<Expr> matches;
  patterns.emplace_back(sat_sub(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vsububsat));
  patterns.emplace_back(sat_add(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vaddubsat));

  patterns.emplace_back(sat_sub(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vsubuhsat));
  patterns.emplace_back(sat_sub(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vsubhsat));

  patterns.emplace_back(sat_add(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vadduhsat));
  patterns.emplace_back(sat_add(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vaddhsat));

  patterns.emplace_back(sat_sub(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vsubwsat));
  patterns.emplace_back(sat_add(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vaddwsat));
  value = emitBinaryOp(op, patterns);
  return value != NULL;
}
bool CodeGen_Hexagon::possiblyCodeGenVavg(const Cast *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> avgs;
  vector<Expr> matches;
  avgs.emplace_back(vavg(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vavgub));
  avgs.emplace_back(vavg_round(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vavgubrnd));
  avgs.emplace_back(vavg(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vavguh));
  avgs.emplace_back(vavg_round(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vavguhrnd));
  avgs.emplace_back(vavg(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vavgh));
  avgs.emplace_back(vavg_round(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vavghrnd));
  avgs.emplace_back(vavg(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vavgw));
  avgs.emplace_back(vavg_round(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vavgwrnd));

  avgs.emplace_back(vnavg(wild_u8xW, wild_u8xW), IPICK(Intrinsic::hexagon_V6_vnavgub));
  avgs.emplace_back(vnavg(wild_u16xW, wild_u16xW), IPICK(Intrinsic::hexagon_V6_vnavgh));
  avgs.emplace_back(vnavg(wild_i16xW, wild_i16xW), IPICK(Intrinsic::hexagon_V6_vnavgh));
  avgs.emplace_back(vnavg(wild_i32xW, wild_i32xW), IPICK(Intrinsic::hexagon_V6_vnavgw));
  matches.clear();
  for (const Pattern &P : avgs) {
    if (expr_match(P.pattern, op, matches)) {
      Value *Vec0 = codegen(matches[0]);
      Value *Vec1 = codegen(matches[1]);
      Value *VavgInst = callLLVMIntrinsic(P.ID, {Vec0, Vec1});
      value = convertValueType(VavgInst, llvm_type_of(op->type));
      return true;
    }
  }
  return false;
}
void CodeGen_Hexagon::visit(const Cast *op) {
  vector<Expr> matches;
  debug(2) << "HexCG: " << op->type << " <- " << op->value.type() << ", " << "visit(Cast)\n";
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
      for (const Pattern &P : multiplies) {
        if (expr_match(P.pattern, op, matches)) {
          internal_assert(matches.size() == 2);
          Intrinsic::ID ID = P.ID;
          llvm::Function *F = Intrinsic::getDeclaration(module.get(), ID);
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
          std::vector<Value *> Ops;
          Ops.push_back(Lt);
          Ops.push_back(Rt);
          Value *Call = builder->CreateCall(F, Ops);

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
      for (const Pattern &P : casts) {
        if (expr_match(P.pattern, op, matches)) {
          Intrinsic::ID ID = P.ID;
          llvm::Function *F = Intrinsic::getDeclaration(module.get(), ID);
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
  } else if (isNarrowingVectorCast(op)) {
      // ******** Part 2: Down casts (Narrowing) ****************
      // Two step downcasts.
      // std::vector<Expr> Patterns;
      //  Patterns.push_back(u8_(min(wild_u32x64, 255)));
    if (isLargerThanDblVector(op->value.type(), native_vector_bits())) {
        value = handleLargeVectors(op);
        if (value)
          return;
      }
    bool B128 = target.has_feature(Halide::Target::HVX_128);
    if (possiblyCodeGenVavg(op)) {
      return;
    }
    if (possiblyCodeGenSaturatingArith(op)) {
      return;
    }
    // Looking for shuffles
    {
      std::vector<Pattern> Shuffles;
      // Halide has a bad habit of converting "a >> 8" to "a/256"
      Expr TwoFiveSix_u = Broadcast::make(u16_(256), CPICK(128, 64));
      Expr TwoFiveSix_i = Broadcast::make(i16_(256), CPICK(128, 64));
      Expr Divisor_u = Broadcast::make(u32_(65536), CPICK(64, 32));
      Expr Divisor_i = Broadcast::make(i32_(65536), CPICK(64, 32));
      // Vd.b = shuffo(Vu.b, Vv.b)
      Shuffles.emplace_back(u8_(wild_u16x2W / TwoFiveSix_u), IPICK(Intrinsic::hexagon_V6_vshuffob));
      Shuffles.emplace_back(i8_(wild_u16x2W / TwoFiveSix_u), IPICK(Intrinsic::hexagon_V6_vshuffob));
      Shuffles.emplace_back(u8_(wild_i16x2W / TwoFiveSix_i), IPICK(Intrinsic::hexagon_V6_vshuffob));
      Shuffles.emplace_back(i8_(wild_i16x2W / TwoFiveSix_i), IPICK(Intrinsic::hexagon_V6_vshuffob));
      // Vd.h = shuffo(Vu.h, Vv.h)
      Shuffles.emplace_back(u16_(wild_u32x2W / Divisor_u), IPICK(Intrinsic::hexagon_V6_vshufoh));
      Shuffles.emplace_back(i16_(wild_u32x2W / Divisor_u), IPICK(Intrinsic::hexagon_V6_vshufoh));
      Shuffles.emplace_back(u16_(wild_i32x2W / Divisor_i), IPICK(Intrinsic::hexagon_V6_vshufoh));
      Shuffles.emplace_back(i16_(wild_i32x2W / Divisor_i), IPICK(Intrinsic::hexagon_V6_vshufoh));

      matches.clear();
      for (const Pattern &P : Shuffles) {
        if (expr_match(P.pattern, op, matches)) {
          Value *DoubleVector = codegen(matches[0]);
          Value *ShuffleInst = callLLVMIntrinsic(P.ID, getHighAndLowVectors(DoubleVector));
          value = convertValueType(ShuffleInst, llvm_type_of(op->type));
          return;
        }
      }
    }

    // Lets look for saturate and pack.
    std::vector<Pattern> SatAndPack;
    matches.clear();
    int WrdsInVP  = 2 * (native_vector_bits() / 32);
    int HWrdsInVP = WrdsInVP * 2;
    Expr wild_i16xHWrdsInVP = Broadcast::make(wild_i16, HWrdsInVP);
    Expr wild_i32xWrdsInVP = Broadcast::make(wild_i32, WrdsInVP);

    SatAndPack.emplace_back(u8c(wild_i16x2W >> wild_i16xHWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrhubsat));
    SatAndPack.emplace_back(i8c(wild_i16x2W >> wild_i16xHWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrhbrndsat));
    SatAndPack.emplace_back(i16c(wild_i32x2W >> wild_i32xWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrwhsat));
    SatAndPack.emplace_back(u16c(wild_i32x2W >> wild_i32xWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrwuhsat));

    for (const Pattern &P : SatAndPack) {
      if (expr_match(P.pattern, op, matches)) {
        Value *DoubleVector = codegen(matches[0]);
        Value *ShiftOperand = codegen(matches[1]);
        std::vector<Value *> Ops = getHighAndLowVectors(DoubleVector);
        Ops.push_back(ShiftOperand);
        Value *SatAndPackInst = callLLVMIntrinsic(P.ID, Ops);
        value = convertValueType(SatAndPackInst, llvm_type_of(op->type));
        return;
      }
    }
    // when saturating a signed value to an unsigned value, we see "max"
    // unlike saturating an unsinged value to a smaller unsinged value when
    // the max(Expr, 0) part is redundant.
    SatAndPack.emplace_back(u8c(wild_i16x2W / wild_i16xHWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrhubsat));
    SatAndPack.emplace_back(i8c(wild_i16x2W / wild_i16xHWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrhbrndsat));
    SatAndPack.emplace_back(i16c(wild_i32x2W / wild_i32xWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrwhsat));
    SatAndPack.emplace_back(u16c(wild_i32x2W / wild_i32xWrdsInVP), IPICK(Intrinsic::hexagon_V6_vasrwuhsat));

    for (const Pattern &P : SatAndPack) {
      if (expr_match(P.pattern, op, matches)) {
          int rt_shift_by = 0;
          if (is_const_power_of_two_integer(matches[1], &rt_shift_by)) {
            Value *DoubleVector = codegen(matches[0]);
            Value *ShiftBy = codegen(rt_shift_by);
            std::vector<Value *> Ops = getHighAndLowVectors(DoubleVector);
            Ops.push_back(ShiftBy);
            Value *Result = callLLVMIntrinsic(P.ID, Ops);
            value = convertValueType(Result, llvm_type_of(op->type));
            return;
          }
      }
    }
    SatAndPack.clear();
    matches.clear();
    SatAndPack.emplace_back(u8_(min(wild_u16x2W, 255)), IPICK(Intrinsic::hexagon_V6_vsathub));
    SatAndPack.emplace_back(u8c(wild_i16x2W), IPICK(Intrinsic::hexagon_V6_vsathub));
    SatAndPack.emplace_back(i8c(wild_i16x2W), Intrinsic::not_intrinsic);
    // -128 when implicitly coerced to uint16 is 65408.
    SatAndPack.emplace_back(i8_(max(min(wild_u16x2W, 127), 65408 /*uint16(-128)*/)),
                            Intrinsic::not_intrinsic);
    for (const Pattern &P : SatAndPack) {
      if (expr_match(P.pattern, op, matches)) {
        if (P.ID == Intrinsic::not_intrinsic) {
          user_error << "Saturate and packing not supported when downcasting"
            " shorts (signed and unsigned) to signed chars\n";
        } else {
          Value *DoubleVector = codegen(matches[0]);
          Value *SatAndPackInst = callLLVMIntrinsic(P.ID, getHighAndLowVectors(DoubleVector));
          value = convertValueType(SatAndPackInst, llvm_type_of(op->type));
          return;
        }
      }
    }
    {
      std::vector<Pattern> Shuffles;
      // Vd.b = shuffe(Vu.b, Vv.b)
      Shuffles.emplace_back(u8_(wild_u16x2W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      Shuffles.emplace_back(i8_(wild_u16x2W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      Shuffles.emplace_back(u8_(wild_i16x2W), IPICK(Intrinsic::hexagon_V6_vshuffeb));
      Shuffles.emplace_back(i8_(wild_i16x2W), IPICK(Intrinsic::hexagon_V6_vshuffeb));

      // Vd.h = shuffe(Vu.h, Vv.h)
      Shuffles.emplace_back(u16_(wild_u32x2W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      Shuffles.emplace_back(i16_(wild_u32x2W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      Shuffles.emplace_back(u16_(wild_i32x2W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      Shuffles.emplace_back(i16_(wild_i32x2W), IPICK(Intrinsic::hexagon_V6_vshufeh));
      matches.clear();
      for (const Pattern &P : Shuffles) {
        if (expr_match(P.pattern, op, matches)) {
          Value *DoubleVector = codegen(matches[0]);
          Value *ShuffleInst = callLLVMIntrinsic(P.ID, getHighAndLowVectors(DoubleVector));
          value = convertValueType(ShuffleInst, llvm_type_of(op->type));
          return;
        }
      }
    }
      // ******** End Part 2: Down casts (Narrowing) ************
  } else if (isSameSizeVectorCast(op)) {
      // ******** Part 3: Same size casts (typecast) ************
      matches.clear();
      for (const Pattern &P : typecasts) {
        if (expr_match(P.pattern, op, matches)) {
          Intrinsic::ID ID = P.ID;
          llvm::Function *F = Intrinsic::getDeclaration(module.get(), ID);
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
      // ******** End Part 3: Same size casts (typecast) ********
  }

  ostringstream msgbuf;
  msgbuf << "<- " << op->value.type();
  string msg = msgbuf.str() + " in visit(Cast *)\n";
  checkVectorOp(op, msg);
  CodeGen_Posix::visit(op);
  return;
}
void CodeGen_Hexagon::visit(const Call *op) {
  vector<Expr> matches;
  debug(2) << "HexCG: " << op->type << ", " << "visit(Call)\n";
  for (const Pattern &P : combiners) {
    if (expr_match(P.pattern, op, matches)) {
      Intrinsic::ID ID = P.ID;
      bool InvertOperands = P.InvertOperands;
      llvm::Function *F = Intrinsic::getDeclaration(module.get(), ID);
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
      if (InvertOperands) {
        std::vector<Value *> Ops;
        Ops.push_back(Op1);
        Ops.push_back(Op0);
        Call = builder->CreateCall(F, Ops);
      } else {
        std::vector<Value *> Ops;
        Ops.push_back(Op0);
        Ops.push_back(Op1);
        Call = builder->CreateCall(F, Ops);
      }
      value = builder->CreateBitCast(Call, DestLLVMType);
      return;
    }
  }

  bool B128 = target.has_feature(Halide::Target::HVX_128);
  int VecSize = HEXAGON_SINGLE_MODE_VECTOR_SIZE;
  if (B128) VecSize *= 2;

  value = emitBinaryOp(op, vbitwise);
  if (!value) {
    if (op->is_intrinsic(Call::bitwise_not)) {
      if (op->type.is_vector() &&
          ((op->type.bytes() * op->type.lanes()) == VecSize)) {
        Value *Call = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_vnot), {codegen(op->args[0])});
        value = convertValueType(Call, llvm_type_of(op->type));
        return;
      }
    } else if (op->is_intrinsic(Call::absd)) {
      if (isLargerThanVector(op->type, native_vector_bits())) {
        value = handleLargeVectors_absd(op);
        if (value)
          return;
      }
      // vector sized absdiff should have been covered by the look up table
      // "combiners".
    } else if (op->is_intrinsic(Call::get_high_register)) {
      internal_assert(op->type.is_vector());
      internal_assert((op->type.bytes() * op->type.lanes()) == VecSize);
      Value *Call = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_hi), {codegen(op->args[0])});
      value = convertValueType(Call, llvm_type_of(op->type));
      return;
    } else if (op->is_intrinsic(Call::get_low_register)) {
      internal_assert(op->type.is_vector());
      internal_assert((op->type.bytes() * op->type.lanes()) == VecSize);
      Value *Call = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_lo), {codegen(op->args[0])});
      value = convertValueType(Call, llvm_type_of(op->type));
      return;
    } else if (op->is_intrinsic(Call::interleave_vectors)) {
      if (isDblVector(op->type, native_vector_bits()) &&
                      op->args.size() == 2) {
        Expr arg0 = op->args[0];
        Expr arg1 = op->args[1];
        if ((arg0.type() == arg1.type()) &&
            op->type.element_of() == arg0.type().element_of()) {
          internal_assert(op->type.lanes() == 2*arg0.type().lanes());
          int scalar = -1 * op->type.bytes();
          std::vector<Value *> Ops = {codegen(arg1), codegen(arg0), codegen(scalar)};
          Value * Call = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_vshuffvdd), Ops);
          value = convertValueType(Call, llvm_type_of(op->type));
          return;
        }
      }
    }

    int chk_call = show_chk;
    // Only check these calls at higher levels
    if ((chk_call < 2) &&
        ((op->is_intrinsic(Call::interleave_vectors)) ||
         (op->is_intrinsic(Call::shuffle_vector)))) {
      chk_call = 0;
    }
    if (chk_call > 0) {
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern>Patterns;
  std::vector<Expr> matches;
  Expr Vec, BC, bc_value;
  Intrinsic::ID IntrinsID = Intrinsic::not_intrinsic;
  //   Deal with these first.
  //   Vdd.h=vmpy(Vu.ub,Rt.b)
  //   Vdd.w=vmpy(Vu.h,Rt.h)
  Patterns.emplace_back(wild_i16x2W * wild_i16x2W, IPICK(Intrinsic::hexagon_V6_vmpybus));
  Patterns.emplace_back(wild_i32x2W * wild_i32x2W, IPICK(Intrinsic::hexagon_V6_vmpyh));
  for (const Pattern & P : Patterns) {
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
    //   Vdd.uh=vmpy(Vu.ub,Rt.ub)
    //   Vdd.uw=vmpy(Vu.uh,Rt.uh)
    Patterns.emplace_back(wild_u16x2W * wild_u16x2W, IPICK(Intrinsic::hexagon_V6_vmpyub));
    Patterns.emplace_back(wild_u32x2W * wild_u32x2W, IPICK(Intrinsic::hexagon_V6_vmpyuh));
    for (const Pattern & P : Patterns) {
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
  Value *Vmpy = callLLVMIntrinsic(IntrinsID, {Vector, Scalar});
  value = convertValueType(Vmpy, llvm_type_of(op->type));
  return true;
}
void CodeGen_Hexagon::visit(const Mul *op) {
  if (!op->a.type().is_vector() && !op->b.type().is_vector()) {
    // scalar x scalar multiply
    CodeGen_Posix::visit(op);
    return;
  }
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  int VecSize = HEXAGON_SINGLE_MODE_VECTOR_SIZE;
  if (B128) VecSize *= 2;
  debug(2) << "HexCG: " << op->type << ", " << "visit(Mul)\n";
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
      for (const Expr &P : Patterns) {
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
              debug(4) << "HexCG: Generating vmpyhss\n";

              std::vector<Value *> Ops = getHighAndLowVectors(VectorOp);
              Value *HiCall = Ops[0];  //Odd elements.
              Value *LoCall = Ops[1];  //Even elements
              Value *CallEven = callLLVMIntrinsic(IntrinsID, {HiCall, Scalar});
              Value *CallOdd = callLLVMIntrinsic(IntrinsID, {LoCall, Scalar});
              IntrinsID = IPICK(Intrinsic::hexagon_V6_vcombine);
              Value *CombineCall = callLLVMIntrinsic(IntrinsID, {CallEven, CallOdd});
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
    bool B128 = target.has_feature(Halide::Target::HVX_128);

    int Width = op->lanes;
    bool match32 = false;
    bool match16 = false;
    bool match8  = false;
    vector<Expr> Matches;

    int width_32 = CPICK(32,16);
    int width_16 = CPICK(64,32);
    int width_8  = CPICK(128,64);

    debug(4) << "HexCG: Entering vector broadcast\n";

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
#include <v62feat4.inc>
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
      llvm::Function *F = Intrinsic::getDeclaration(module.get(), ID);
      std::vector<Value *> Ops;
      if (splatval) {
         Ops.push_back(splatval);
      }
      Value *ResOne = callLLVMIntrinsic(F, Ops);
      ResVec.push_back(ResOne);
    }

    value = concat_vectors(ResVec);
    value = convertValueType(value, llvm_type_of(op->type));
}
void CodeGen_Hexagon::visit(const Load *op) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  if (op->type.is_vector() && isValidHexagonVector(op->type,
                                                   native_vector_bits())) {

    bool possibly_misaligned = (might_be_misaligned.find(op->name) != might_be_misaligned.end());
    const Ramp *ramp = op->index.as<Ramp>();
    const IntImm *stride = ramp ? ramp->stride.as<IntImm>() : NULL;
    if (ramp && stride && stride->value == 1) {
      int lanes = ramp->lanes;
      // int alignment = op->type.bytes(); // The size of a single element
      int native_vector_bytes = native_vector_bits() / 8;

      // We are loading a partial vector. if we are, default to vanilla codegen.
      if (lanes * op->type.bytes() != native_vector_bytes) {
        CodeGen_Posix::visit(op);
        return;
      }
      // At this point we are satisfied that we are loading a native vector.
      ModulusRemainder mod_rem = get_alignment_info(ramp->base);
      if (mod_rem.modulus == 1 &&
          mod_rem.remainder == 0) {
        // We know nothing about alignment. Just fall back upon vanilla codegen.
        CodeGen_Posix::visit(op);
        return;
      }
      // ModulusRemainder tells us if something can be written in the
      // form
      // (ModulusRemainder.modulus * c1) + ModulusRemainder.remainder.
      // So for us to be able to generate an aligned load, ramp->base
      // should be
      // (lanes * c1) + c2.
      if (!possibly_misaligned && !(mod_rem.modulus % lanes)) {
        if (mod_rem.remainder == 0) {
          // This is a perfectly aligned address. Vanilla codegen can deal with
          // this.
          CodeGen_Posix::visit(op);
          return;
        } else {
          Expr base = simplify(ramp->base);
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
          int b_val = b->value;
          // offset_elements is an expr that tells us how many elements away we
          // are from an aligned vector;
          int offset_elements = mod_imp(b_val, lanes);
          if (offset_elements == 0) {
            CodeGen_Posix::visit(op);
            return;
          }
          // If the index is A + b, then we know that A is already aligned. We need
          // to know if b, which is an IntImm also contains an aligned vector inside.
          // For e.g. if b is 65 and lanes is 64, then we have 1 aligned vector in it.
          // and base_low should be (A + 64)
          int offset_vector = div_imp(b_val, lanes) * lanes;
          // offset_elements tells us that we are off by those many elements
          // from the vector width. We will load two vectors
          // v_low = load(add->a + offset_vector)
          // v_high = load(add->a + offset_vector + lanes)
          // Now,
          // valign (v_high, v_low, x) = vlalign(v_high, v_low, vec_length - x);
          // Since offset_elements is always between 0 and (lanes-1), we need to
          // look at the sign of b_val to create the right offset for vlalign.
          int bytes_off = b_val > 0 ? offset_elements * op->type.bytes() :
              (lanes - offset_elements)  * op->type.bytes();
          Expr base_low =  simplify(add->a + offset_vector);
          Expr base_high =  simplify(base_low + lanes);
          Expr ramp_low = Ramp::make(base_low, 1, lanes);
          Expr ramp_high = Ramp::make(base_high, 1, lanes);
          Expr load_low = Load::make(op->type, op->name, ramp_low, op->image,
                                     op->param);
          Expr load_high = Load::make(op->type, op->name, ramp_high, op->image,
                                      op->param);
          Value *vec_low = codegen(load_low);
          Value *vec_high = codegen(load_high);

          Intrinsic::ID IntrinsID = (Intrinsic::ID) 0;
          if (b_val > 0) {
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
            Value *valign = callLLVMIntrinsic(IntrinsID, {vec_high, vec_low, Scalar});
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
            Value *valign = callLLVMIntrinsic(IntrinsID, {vec_high, vec_low, Scalar});
            value = convertValueType(valign, llvm_type_of(op->type));
            return;
          }
        }
      }
    } else if (ramp && stride && stride->value == 2) {
        // Load two vectors worth and then shuffle
        Expr base_a = ramp->base, base_b = ramp->base + ramp->lanes;

        // False indicates we should take the even-numbered lanes
        // from the load, true indicates we should take the
        // odd-numbered-lanes.
        bool shifted_a = false, shifted_b = false;
        // If the base ends in an odd constant, then subtract one
        // and do a different shuffle. This helps expressions like
        // (f(2*x) + f(2*x+1) share loads
        const Add *add = ramp->base.as<Add>();
        const IntImm *offset = add ? add->b.as<IntImm>() : NULL;
        if (offset && offset->value & 1) {
          base_a -= 1;
          shifted_a = true;
          base_b -= 1;
          shifted_b = true;
        }

        // Do each load.
        Expr ramp_a = Ramp::make(base_a, 1, ramp->lanes);
        Expr ramp_b = Ramp::make(base_b, 1, ramp->lanes);
        Expr load_a = Load::make(op->type, op->name, ramp_a, op->image,
                                 op->param);
        Expr load_b = Load::make(op->type, op->name, ramp_b, op->image,
                                 op->param);
        Value *vec_a = codegen(load_a);
        Value *vec_b = codegen(load_b);

        // Shuffle together the results.
        vector<Constant *> indices(ramp->lanes);
        for (int i = 0; i < (ramp->lanes + 1)/2; i++) {
          indices[i] = ConstantInt::get(i32, i*2 + (shifted_a ? 1 : 0));
        }
        for (int i = (ramp->lanes + 1)/2; i < ramp->lanes; i++) {
          indices[i] = ConstantInt::get(i32, i*2 + (shifted_b ? 1 : 0));
        }

        debug(2) << "Loading two vectors and shuffle: \n";
        value = builder->CreateShuffleVector(vec_a, vec_b,
                                             ConstantVector::get(indices));
        if (debug::debug_level >= 2) value -> dump();
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  Expr char_cmp_vector = Variable::make(Bool(CPICK(128, 64)), "*");
  Expr short_cmp_vector = Variable::make(Bool(CPICK(64, 32)), "*");
  Expr word_cmp_vector = Variable::make(Bool(CPICK(32, 16)), "*");
  Expr char_dbl_cmp_vector = Variable::make(Bool(CPICK(128*2, 64*2)), "*");
  Expr short_dbl_cmp_vector = Variable::make(Bool(CPICK(64*2, 32*2)), "*");
  Expr word_dbl_cmp_vector = Variable::make(Bool(CPICK(32*2, 16*2)), "*");

  if (op->condition.type().is_vector()) {
      if (isValidHexagonVectorPair(op->type, native_vector_bits())) {
        if (possiblyCodeGenNarrowerType(op))
          return;
        std::vector<Expr> PairSelects;
        std::vector<Expr> matches;
        int PredVectorSize = CPICK(1024, 512);
        PairSelects.push_back(select(char_dbl_cmp_vector, wild_i8x2W, wild_i8x2W));
        PairSelects.push_back(select(char_dbl_cmp_vector, wild_u8x2W, wild_u8x2W));
        PairSelects.push_back(select(short_dbl_cmp_vector, wild_i16x2W, wild_i16x2W));
        PairSelects.push_back(select(short_dbl_cmp_vector, wild_u16x2W, wild_u16x2W));
        PairSelects.push_back(select(word_dbl_cmp_vector, wild_i32x2W, wild_i32x2W));
        PairSelects.push_back(select(word_dbl_cmp_vector, wild_u32x2W, wild_u32x2W));
        for (Expr P : PairSelects) {
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

            // 4. Slice them into halves or vector registers.
            std::vector<Value *> OpsA = getHighAndLowVectors(DblVecA);
            std::vector<Value *> OpsB = getHighAndLowVectors(DblVecB);
            Intrinsic::ID ID = IPICK(Intrinsic::hexagon_V6_vmux);
            Value *HighMux = callLLVMIntrinsic(ID, {HighCond, OpsA[0], OpsB[0]});
            Value *LowMux = callLLVMIntrinsic(ID, {LowCond, OpsA[1], OpsB[1]});
            value = convertValueType(concat_vectors({LowMux, HighMux}),
                                     llvm_type_of(op->type));
            return;
          }
        }
      }
      else if (isValidHexagonVector(op->type, native_vector_bits())) {
        std::vector<Expr> Selects;
        std::vector<Expr> matches;
        Selects.push_back(select(char_cmp_vector, wild_i8xW, wild_i8xW));
        Selects.push_back(select(char_cmp_vector, wild_u8xW, wild_u8xW));
        Selects.push_back(select(short_cmp_vector, wild_i16xW, wild_i16xW));
        Selects.push_back(select(short_cmp_vector, wild_u16xW, wild_u16xW));
        Selects.push_back(select(word_cmp_vector, wild_i32xW, wild_i32xW));
        Selects.push_back(select(word_cmp_vector, wild_u32xW, wild_u32xW));
        for (Expr P : Selects) {
          if (expr_match(P, op, matches)) {
            // 1. Codegen the condition. Since we are dealing with vector pairs,
            // the condition will be of type 2048i1(128B mode) or
            // 1024i1(64B mode).
            Value *Cond = codegen(op->condition);
            // 2. Codegen the vectors.
            Value *VecA = codegen(matches[1]);
            Value *VecB = codegen(matches[2]);
            // 3. Generate the vmux intrinsic.
            Value *Mux = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_vmux), {Cond, VecA, VecB});
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
  Value *Cmp = callLLVMIntrinsic(F, {a, b});
  return Cmp;
}
llvm::Value *
CodeGen_Hexagon::negate(llvm::Value *a) {
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  Value *Cmp = callLLVMIntrinsic(IPICK(Intrinsic::hexagon_V6_pred_not), {a});
  return Cmp;
}
llvm::Value*
CodeGen_Hexagon::generate_vector_comparison(const BaseExprNode *op,
                                            std::vector<Pattern> &VecPairCompares,
                                            std::vector<Pattern> &VecCompares,
                                            bool invert_ops, bool negate_after) {
  std::vector<Expr> matches;
  for (const Pattern &P : VecPairCompares) {
    if (expr_match(P.pattern, op, matches)) {
      Value *DblVecA = codegen(matches[0]);
      Value *DblVecB = codegen(matches[1]);
      Intrinsic::ID IntrinsID = P.ID;

      std::vector<Value *> OpsA = getHighAndLowVectors(DblVecA);
      std::vector<Value *> OpsB = getHighAndLowVectors(DblVecB);
      // OpsA[0] == HighVector;
      // OpsA[1] = LowVector;
      // a <= b is !(a > b)
      llvm::Function *CmpF = Intrinsic::getDeclaration(module.get(), IntrinsID);
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
      // Do not change type back to llvm_type_of(op->type);
      return concat_vectors({LowCmp, HighCmp});
     }
  }
  for (const Pattern &P : VecCompares) {
    if (expr_match(P.pattern, op, matches)) {
      Value *VecA = codegen(matches[0]);
      Value *VecB = codegen(matches[1]);
      Intrinsic::ID IntrinsID = P.ID;
      // a <= b is !(a > b)
      llvm::Function *CmpF = Intrinsic::getDeclaration(module.get(), IntrinsID);
      Value *Cmp;
      if (invert_ops)
        Cmp = compare(VecB, VecA, CmpF);
      else
        Cmp = compare(VecA, VecB, CmpF);
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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.emplace_back(wild_i8x2W <= wild_i8x2W, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecPairCompares.emplace_back(wild_u8x2W <= wild_u8x2W, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecPairCompares.emplace_back(wild_i16x2W <= wild_i16x2W, IPICK(Intrinsic::hexagon_V6_vgth));
  VecPairCompares.emplace_back(wild_u16x2W <= wild_u16x2W, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecPairCompares.emplace_back(wild_i32x2W <= wild_i32x2W, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecPairCompares.emplace_back(wild_u32x2W <= wild_u32x2W, IPICK(Intrinsic::hexagon_V6_vgtuw));
  std::vector<Pattern> VecCompares;
  VecCompares.emplace_back(wild_i8xW <= wild_i8xW, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecCompares.emplace_back(wild_u8xW <= wild_u8xW, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecCompares.emplace_back(wild_i16xW <= wild_i16xW, IPICK(Intrinsic::hexagon_V6_vgth));
  VecCompares.emplace_back(wild_u16xW <= wild_u16xW, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecCompares.emplace_back(wild_i32xW <= wild_i32xW, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecCompares.emplace_back(wild_u32xW <= wild_u32xW, IPICK(Intrinsic::hexagon_V6_vgtuw));

  value = generate_vector_comparison(op, VecPairCompares, VecCompares, false, true);

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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.emplace_back(wild_i8x2W < wild_i8x2W, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecPairCompares.emplace_back(wild_u8x2W < wild_u8x2W, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecPairCompares.emplace_back(wild_i16x2W < wild_i16x2W, IPICK(Intrinsic::hexagon_V6_vgth));
  VecPairCompares.emplace_back(wild_u16x2W < wild_u16x2W, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecPairCompares.emplace_back(wild_i32x2W < wild_i32x2W, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecPairCompares.emplace_back(wild_u32x2W < wild_u32x2W, IPICK(Intrinsic::hexagon_V6_vgtuw));
  std::vector<Pattern> VecCompares;
  VecCompares.emplace_back(wild_i8xW < wild_i8xW, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecCompares.emplace_back(wild_u8xW < wild_u8xW, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecCompares.emplace_back(wild_i16xW < wild_i16xW, IPICK(Intrinsic::hexagon_V6_vgth));
  VecCompares.emplace_back(wild_u16xW < wild_u16xW, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecCompares.emplace_back(wild_i32xW < wild_i32xW, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecCompares.emplace_back(wild_u32xW < wild_u32xW, IPICK(Intrinsic::hexagon_V6_vgtuw));

  value = generate_vector_comparison(op, VecPairCompares, VecCompares, true, false);

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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.emplace_back(wild_i8x2W != wild_i8x2W, IPICK(Intrinsic::hexagon_V6_veqb));
  VecPairCompares.emplace_back(wild_u8x2W != wild_u8x2W, IPICK(Intrinsic::hexagon_V6_veqb));
  VecPairCompares.emplace_back(wild_i16x2W != wild_i16x2W, IPICK(Intrinsic::hexagon_V6_veqh));
  VecPairCompares.emplace_back(wild_u16x2W != wild_u16x2W, IPICK(Intrinsic::hexagon_V6_veqh));
  VecPairCompares.emplace_back(wild_i32x2W != wild_i32x2W, IPICK(Intrinsic::hexagon_V6_veqw));
  VecPairCompares.emplace_back(wild_u32x2W != wild_u32x2W, IPICK(Intrinsic::hexagon_V6_veqw));
  std::vector<Pattern> VecCompares;
  VecCompares.emplace_back(wild_i8xW != wild_i8xW, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecCompares.emplace_back(wild_u8xW != wild_u8xW, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecCompares.emplace_back(wild_i16xW != wild_i16xW, IPICK(Intrinsic::hexagon_V6_vgth));
  VecCompares.emplace_back(wild_u16xW != wild_u16xW, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecCompares.emplace_back(wild_i32xW != wild_i32xW, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecCompares.emplace_back(wild_u32xW != wild_u32xW, IPICK(Intrinsic::hexagon_V6_vgtuw));

  value = generate_vector_comparison(op, VecPairCompares, VecCompares, false, true);

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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.emplace_back(wild_i8x2W > wild_i8x2W, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecPairCompares.emplace_back(wild_u8x2W > wild_u8x2W, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecPairCompares.emplace_back(wild_i16x2W > wild_i16x2W, IPICK(Intrinsic::hexagon_V6_vgth));
  VecPairCompares.emplace_back(wild_u16x2W > wild_u16x2W, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecPairCompares.emplace_back(wild_i32x2W > wild_i32x2W, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecPairCompares.emplace_back(wild_u32x2W > wild_u32x2W, IPICK(Intrinsic::hexagon_V6_vgtuw));
  std::vector<Pattern> VecCompares;
  VecCompares.emplace_back(wild_i8xW > wild_i8xW, IPICK(Intrinsic::hexagon_V6_vgtb));
  VecCompares.emplace_back(wild_u8xW > wild_u8xW, IPICK(Intrinsic::hexagon_V6_vgtub));
  VecCompares.emplace_back(wild_i16xW > wild_i16xW, IPICK(Intrinsic::hexagon_V6_vgth));
  VecCompares.emplace_back(wild_u16xW > wild_u16xW, IPICK(Intrinsic::hexagon_V6_vgtuh));
  VecCompares.emplace_back(wild_i32xW > wild_i32xW, IPICK(Intrinsic::hexagon_V6_vgtw));
  VecCompares.emplace_back(wild_u32xW > wild_u32xW, IPICK(Intrinsic::hexagon_V6_vgtuw));

  value = generate_vector_comparison(op, VecPairCompares, VecCompares, false, false);

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
  bool B128 = target.has_feature(Halide::Target::HVX_128);
  std::vector<Pattern> VecPairCompares;
  VecPairCompares.emplace_back(wild_i8x2W == wild_i8x2W, IPICK(Intrinsic::hexagon_V6_veqb));
  VecPairCompares.emplace_back(wild_u8x2W == wild_u8x2W, IPICK(Intrinsic::hexagon_V6_veqb));
  VecPairCompares.emplace_back(wild_i16x2W == wild_i16x2W, IPICK(Intrinsic::hexagon_V6_veqh));
  VecPairCompares.emplace_back(wild_u16x2W == wild_u16x2W, IPICK(Intrinsic::hexagon_V6_veqh));
  VecPairCompares.emplace_back(wild_i32x2W == wild_i32x2W, IPICK(Intrinsic::hexagon_V6_veqw));
  VecPairCompares.emplace_back(wild_u32x2W == wild_u32x2W, IPICK(Intrinsic::hexagon_V6_veqw));
  std::vector<Pattern> VecCompares;
  VecCompares.emplace_back(wild_i8xW == wild_i8xW, IPICK(Intrinsic::hexagon_V6_veqb));
  VecCompares.emplace_back(wild_u8xW == wild_u8xW, IPICK(Intrinsic::hexagon_V6_veqb));
  VecCompares.emplace_back(wild_i16xW == wild_i16xW, IPICK(Intrinsic::hexagon_V6_veqh));
  VecCompares.emplace_back(wild_u16xW == wild_u16xW, IPICK(Intrinsic::hexagon_V6_veqh));
  VecCompares.emplace_back(wild_i32xW == wild_i32xW, IPICK(Intrinsic::hexagon_V6_veqw));
  VecCompares.emplace_back(wild_u32xW == wild_u32xW, IPICK(Intrinsic::hexagon_V6_veqw));

  value = generate_vector_comparison(op, VecPairCompares, VecCompares, false, false);

  if (!value) {
    checkVectorOp(op, "in visit(EQ *)\n");
    CodeGen_Posix::visit(op);
  }
}
}}
