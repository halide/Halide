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
    uint64_t EncodedAttrs = 0;
    // Encode it in the format expected by llvm 3.2
    if (A.hasAttribute(i, llvm::Attribute::ZExt))           EncodedAttrs |= 1 << 0;
    if (A.hasAttribute(i, llvm::Attribute::SExt))           EncodedAttrs |= 1 << 1;
    if (A.hasAttribute(i, llvm::Attribute::NoReturn))       EncodedAttrs |= 1 << 2;
    if (A.hasAttribute(i, llvm::Attribute::InReg))          EncodedAttrs |= 1 << 3;
    if (A.hasAttribute(i, llvm::Attribute::StructRet))      EncodedAttrs |= 1 << 4;
    if (A.hasAttribute(i, llvm::Attribute::NoUnwind))       EncodedAttrs |= 1 << 5;
    if (A.hasAttribute(i, llvm::Attribute::NoAlias))        EncodedAttrs |= 1 << 6;
    if (A.hasAttribute(i, llvm::Attribute::ByVal))          EncodedAttrs |= 1 << 7;
    if (A.hasAttribute(i, llvm::Attribute::Nest))           EncodedAttrs |= 1 << 8;
    if (A.hasAttribute(i, llvm::Attribute::ReadNone))       EncodedAttrs |= 1 << 9;
    if (A.hasAttribute(i, llvm::Attribute::ReadOnly))       EncodedAttrs |= 1 << 10;
    if (A.hasAttribute(i, llvm::Attribute::NoInline))       EncodedAttrs |= 1 << 11;
    if (A.hasAttribute(i, llvm::Attribute::AlwaysInline))   EncodedAttrs |= 1 << 12;
    if (A.hasAttribute(i, llvm::Attribute::OptimizeForSize))EncodedAttrs |= 1 << 13;
    if (A.hasAttribute(i, llvm::Attribute::StackProtect))   EncodedAttrs |= 1 << 14;
    if (A.hasAttribute(i, llvm::Attribute::StackProtectReq))EncodedAttrs |= 1 << 15;
    if (A.hasAttribute(i, llvm::Attribute::NoCapture))      EncodedAttrs |= 1 << 21;
    if (A.hasAttribute(i, llvm::Attribute::NoRedZone))      EncodedAttrs |= 1 << 22;
    if (A.hasAttribute(i, llvm::Attribute::NoImplicitFloat))EncodedAttrs |= 1 << 23;
    if (A.hasAttribute(i, llvm::Attribute::Naked))          EncodedAttrs |= 1 << 24;
    if (A.hasAttribute(i, llvm::Attribute::InlineHint))     EncodedAttrs |= 1 << 25;
    if (A.hasAttribute(i, llvm::Attribute::ReturnsTwice))   EncodedAttrs |= 1 << 29;
    if (A.hasAttribute(i, llvm::Attribute::UWTable))        EncodedAttrs |= 1 << 30;
    if (A.hasAttribute(i, llvm::Attribute::NonLazyBind))    EncodedAttrs |= 1U << 31;
    if (A.hasAttribute(i, llvm::Attribute::MinSize))        EncodedAttrs |= 1ULL << 33;

    if (A.hasAttribute(i, llvm::Attribute::StackAlignment)) {
        EncodedAttrs |= (A.getStackAlignment(i) << 26);
    }          
    
    if (A.hasAttribute(i, llvm::Attribute::Alignment)) {
        // The alignment is stored as an actual power of 2 value (instead of the
        // compressed log2 form). It occupies bits 31-16 inclusive.
        EncodedAttrs |= (A.getParamAlignment(i) << 16);
    }
    return EncodedAttrs;
}

