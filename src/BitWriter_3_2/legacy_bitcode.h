//
//  Copied from https://android.googlesource.com/platform/frameworks/compile/slang/+/master/BitWriter_3_2/
//  DO NOT EDIT
//

/*
 * Copyright 2013, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <llvm/IR/Attributes.h>

// Shared functionality for legacy function attribute encoding.
static inline uint64_t encodeLLVMAttributesForBitcode(llvm::AttributeSet A,
                                                      unsigned i) {
  uint64_t EncodedAttrs = A.Raw(i) & 0xffff;
  if (A.hasAttribute(i, llvm::Attribute::Alignment)) {
    // The alignment is stored as an actual power of 2 value (instead of the
    // compressed log2 form). It occupies bits 31-16 inclusive.
    EncodedAttrs |= (A.getParamAlignment(i) << 16);
  }
  // There are only 12 remaining attributes (bits 21-32), hence our 0xfff mask.
  EncodedAttrs |= (A.Raw(i) & (0xfffull << 21))  << 11;
  return EncodedAttrs;
}

