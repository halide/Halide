// Optional type exclusions
#ifndef _EXCLu16x128
#define _EXCLu16x128  false
#endif

//
// Body of handleLargeVectors for 2 argument ops
// #define _OP(a, b) accordingly and then include
//
// llvm::Value *
// CodeGen_Hexagon::handleLargeVectors2arg(const Add *op) {
  std::vector<Expr> Patterns;
  std::vector<Expr> matches;
  bool B128 = target.has_feature(Halide::Target::HVX_128);

  // 4096-bit vector + vector
  Patterns.push_back(_OP(wild_u32x128, wild_u32x128));
  Patterns.push_back(_OP(wild_i32x128, wild_i32x128));
  Patterns.push_back(_OP(wild_u16x256, wild_u16x256));
  Patterns.push_back(_OP(wild_i16x256, wild_i16x256));
  Patterns.push_back(_OP(wild_u8x512,  wild_u8x512));
  Patterns.push_back(_OP(wild_i8x512,  wild_i8x512));

  // 2048-bit vector + vector
  Patterns.push_back(_OP(wild_u32x64,  wild_u32x64));
  Patterns.push_back(_OP(wild_i32x64,  wild_i32x64));
  if (!_EXCLu16x128)
  Patterns.push_back(_OP(wild_u16x128, wild_u16x128));
  Patterns.push_back(_OP(wild_i16x128, wild_i16x128));
  Patterns.push_back(_OP(wild_u8x256,  wild_u8x256));
  Patterns.push_back(_OP(wild_i8x256,  wild_i8x256));

  // 1024-bit vector + vector
  // the following are only wide in single mode
  if (!B128) {
    Patterns.push_back(_OP(wild_u32x32, wild_u32x32));
    Patterns.push_back(_OP(wild_i32x32, wild_i32x32));
    Patterns.push_back(_OP(wild_u16x64, wild_u16x64));
    Patterns.push_back(_OP(wild_i16x64, wild_i16x64));
    Patterns.push_back(_OP(wild_u8x128, wild_u8x128));
    Patterns.push_back(_OP(wild_i8x128, wild_i8x128));
  }

  for (size_t I = 0; I < Patterns.size(); ++I) {
    Expr pat = Patterns[I];
    if (expr_match(pat, op, matches)) {
      // 1. Slice the two operands into halves to get four operands
      std::vector<Expr> VectorRegisterPairsA;
      std::vector<Expr> VectorRegisterPairsB;
      if (isDblVector(op->type, native_vector_bits())) {
        VectorRegisterPairsA = getHighAndLowVectors(matches[0]);
        VectorRegisterPairsB = getHighAndLowVectors(matches[1]);
      } else {
        VectorRegisterPairsA = slice_into_halves(matches[0]);
        VectorRegisterPairsB = slice_into_halves(matches[1]);
      }

      // 2. Operate on the halves
      Expr A_high = VectorRegisterPairsA[0];
      Expr A_low  = VectorRegisterPairsA[1];
      Expr B_high = VectorRegisterPairsB[0];
      Expr B_low  = VectorRegisterPairsB[1];
      Value *LowRes = codegen(_OP(A_low, B_low));
      Value *HighRes = codegen(_OP(A_high, B_high));

      // 3. Combine the results
      Value *Result = NULL;
      if (isDblVector(op->type, native_vector_bits())) {
        Result = concatVectors(HighRes, LowRes);
      } else {
        std::vector<Value *>Ops;
        Ops.push_back(LowRes);
        Ops.push_back(HighRes);
        Result = concat_vectors(Ops);
      }
      return convertValueType(Result, llvm_type_of(op->type));
    }
  }

  return NULL;
// }

#undef _OP
#undef _EXCLu16x128
