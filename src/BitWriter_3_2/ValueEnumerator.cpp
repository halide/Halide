//
//  Copied from https://android.googlesource.com/platform/frameworks/compile/slang/+/master/BitWriter_3_2/
//  DO NOT EDIT
//

//===-- ValueEnumerator.cpp - Number values and types for bitcode writer --===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the ValueEnumerator class.
//
//===----------------------------------------------------------------------===//

#include "ValueEnumerator.h"
#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/IR/Constants.h>
#if LLVM_VERSION >= 37
#include <llvm/IR/DebugInfoMetadata.h>
#endif
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/ValueSymbolTable.h>
#include <llvm/Support/Debug.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
using namespace llvm;

namespace llvm_3_2 {

static bool isIntOrIntVectorValue(const std::pair<const Value*, unsigned> &V) {
  return V.first->getType()->isIntOrIntVectorTy();
}

/// ValueEnumerator - Enumerate module-level information.
ValueEnumerator::ValueEnumerator(const llvm::Module &M,
                                 bool ShouldPreserveUseListOrder)
    : HasMDString(false), HasMDLocation(false),
    ShouldPreserveUseListOrder(ShouldPreserveUseListOrder) {
  assert(!ShouldPreserveUseListOrder &&
         "not supported UseListOrders = predictUseListOrder(M)");

  // Enumerate the global variables.
  for (llvm::Module::const_global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I)
    EnumerateValue(I);

  // Enumerate the functions.
  for (llvm::Module::const_iterator I = M.begin(), E = M.end(); I != E; ++I) {
    EnumerateValue(I);
    EnumerateAttributes(cast<Function>(I)->getAttributes());
  }

  // Enumerate the aliases.
  for (llvm::Module::const_alias_iterator I = M.alias_begin(), E = M.alias_end();
       I != E; ++I)
    EnumerateValue(I);

  // Remember what is the cutoff between globalvalue's and other constants.
  unsigned FirstConstant = Values.size();

  // Enumerate the global variable initializers.
  for (llvm::Module::const_global_iterator I = M.global_begin(), E = M.global_end();
       I != E; ++I)
    if (I->hasInitializer())
      EnumerateValue(I->getInitializer());

  // Enumerate the aliasees.
  for (llvm::Module::const_alias_iterator I = M.alias_begin(), E = M.alias_end();
       I != E; ++I)
    EnumerateValue(I->getAliasee());

  // Enumerate the metadata type.
  //
  // TODO: Move this to ValueEnumerator::EnumerateOperandType() once bitcode
  // only encodes the metadata type when it's used as a value.
  EnumerateType(Type::getMetadataTy(M.getContext()));

  // Insert constants and metadata that are named at module level into the slot
  // pool so that the module symbol table can refer to them...
  EnumerateValueSymbolTable(M.getValueSymbolTable());
  EnumerateNamedMetadata(M);

  SmallVector<std::pair<unsigned, MDNode *>, 8> MDs;

  // Enumerate types used by function bodies and argument lists.
  for (const Function &F : M) {
    for (const Argument &A : F.args())
      EnumerateType(A.getType());

    for (const BasicBlock &BB : F)
      for (const Instruction &I : BB) {
        for (const Use &Op : I.operands()) {
          auto *MD = dyn_cast<MetadataAsValue>(&Op);
          if (!MD) {
            EnumerateOperandType(Op);
            continue;
          }

          // Local metadata is enumerated during function-incorporation.
          if (isa<LocalAsMetadata>(MD->getMetadata()))
            continue;

          EnumerateMetadata(MD->getMetadata());
        }
        EnumerateType(I.getType());
        if (const CallInst *CI = dyn_cast<CallInst>(&I))
          EnumerateAttributes(CI->getAttributes());
        else if (const InvokeInst *II = dyn_cast<InvokeInst>(&I))
          EnumerateAttributes(II->getAttributes());

        // Enumerate metadata attached with this instruction.
        MDs.clear();
        I.getAllMetadataOtherThanDebugLoc(MDs);
        for (unsigned i = 0, e = MDs.size(); i != e; ++i)
          EnumerateMetadata(MDs[i].second);

#if LLVM_VERSION >= 37
        if (I.getDebugLoc()) {
          MDNode* Scope = I.getDebugLoc().getScope();
          if (Scope) EnumerateMetadata(Scope);
          MDLocation *IA = I.getDebugLoc().getInlinedAt();
          if (IA) EnumerateMetadata(IA);
        }
#else
        if (!I.getDebugLoc().isUnknown()) {
          MDNode *Scope, *IA;
          I.getDebugLoc().getScopeAndInlinedAt(Scope, IA, I.getContext());
          if (Scope) EnumerateMetadata(Scope);
          if (IA) EnumerateMetadata(IA);
        }
#endif
      }
  }

  // Optimize constant ordering.
  OptimizeConstants(FirstConstant, Values.size());
}

unsigned ValueEnumerator::getInstructionID(const Instruction *Inst) const {
  InstructionMapType::const_iterator I = InstructionMap.find(Inst);
  assert(I != InstructionMap.end() && "Instruction is not mapped!");
  return I->second;
}

void ValueEnumerator::setInstructionID(const Instruction *I) {
  InstructionMap[I] = InstructionCount++;
}

unsigned ValueEnumerator::getValueID(const Value *V) const {
  if (auto *MD = dyn_cast<MetadataAsValue>(V))
    return getMetadataID(MD->getMetadata());

  ValueMapType::const_iterator I = ValueMap.find(V);
  assert(I != ValueMap.end() && "Value not in slotcalculator!");
  return I->second-1;
}

void ValueEnumerator::dump() const {
  print(dbgs(), ValueMap, "Default");
  dbgs() << '\n';
  print(dbgs(), MDValueMap, "MetaData");
  dbgs() << '\n';
}

void ValueEnumerator::print(raw_ostream &OS, const ValueMapType &Map,
                            const char *Name) const {

  OS << "Map Name: " << Name << "\n";
  OS << "Size: " << Map.size() << "\n";
  for (ValueMapType::const_iterator I = Map.begin(),
         E = Map.end(); I != E; ++I) {

    const Value *V = I->first;
    if (V->hasName())
      OS << "Value: " << V->getName();
    else
      OS << "Value: [null]\n";
    V->dump();

    OS << " Uses(" << std::distance(V->use_begin(),V->use_end()) << "):";
    for (const Use &U : V->uses()) {
      if (&U != &*V->use_begin())
        OS << ",";
      if(U->hasName())
        OS << " " << U->getName();
      else
        OS << " [null]";

    }
    OS <<  "\n\n";
  }
}

void ValueEnumerator::print(llvm::raw_ostream &OS, const MetadataMapType &Map,
                            const char *Name) const {

  OS << "Map Name: " << Name << "\n";
  OS << "Size: " << Map.size() << "\n";
  for (auto I = Map.begin(), E = Map.end(); I != E; ++I) {
    const llvm::Metadata *MD = I->first;
    OS << "Metadata: slot = " << I->second << "\n";
    MD->print(OS);
  }
}


// Optimize constant ordering.
namespace {
  struct CstSortPredicate {
    ValueEnumerator &VE;
    explicit CstSortPredicate(ValueEnumerator &ve) : VE(ve) {}
    bool operator()(const std::pair<const Value*, unsigned> &LHS,
                    const std::pair<const Value*, unsigned> &RHS) {
      // Sort by plane.
      if (LHS.first->getType() != RHS.first->getType())
        return VE.getTypeID(LHS.first->getType()) <
               VE.getTypeID(RHS.first->getType());
      // Then by frequency.
      return LHS.second > RHS.second;
    }
  };
}

/// OptimizeConstants - Reorder constant pool for denser encoding.
void ValueEnumerator::OptimizeConstants(unsigned CstStart, unsigned CstEnd) {
  if (CstStart == CstEnd || CstStart+1 == CstEnd) return;

  CstSortPredicate P(*this);
  std::stable_sort(Values.begin()+CstStart, Values.begin()+CstEnd, P);

  // Ensure that integer and vector of integer constants are at the start of the
  // constant pool.  This is important so that GEP structure indices come before
  // gep constant exprs.
  std::partition(Values.begin()+CstStart, Values.begin()+CstEnd,
                 isIntOrIntVectorValue);

  // Rebuild the modified portion of ValueMap.
  for (; CstStart != CstEnd; ++CstStart)
    ValueMap[Values[CstStart].first] = CstStart+1;
}


/// EnumerateValueSymbolTable - Insert all of the values in the specified symbol
/// table into the values table.
void ValueEnumerator::EnumerateValueSymbolTable(const ValueSymbolTable &VST) {
  for (ValueSymbolTable::const_iterator VI = VST.begin(), VE = VST.end();
       VI != VE; ++VI)
    EnumerateValue(VI->getValue());
}

/// EnumerateNamedMetadata - Insert all of the values referenced by
/// named metadata in the specified module.
void ValueEnumerator::EnumerateNamedMetadata(const llvm::Module &M) {
  for (llvm::Module::const_named_metadata_iterator I = M.named_metadata_begin(),
                                             E = M.named_metadata_end();
       I != E; ++I)
    EnumerateNamedMDNode(I);
}

void ValueEnumerator::EnumerateNamedMDNode(const NamedMDNode *MD) {
  for (unsigned i = 0, e = MD->getNumOperands(); i != e; ++i)
    EnumerateMetadata(MD->getOperand(i));
}

/// EnumerateMDNodeOperands - Enumerate all non-function-local values
/// and types referenced by the given MDNode.
void ValueEnumerator::EnumerateMDNodeOperands(const MDNode *N) {
  for (unsigned i = 0, e = N->getNumOperands(); i != e; ++i) {
    Metadata *MD = N->getOperand(i);
    if (!MD)
      continue;
    assert(!isa<LocalAsMetadata>(MD) && "MDNodes cannot be function-local");
    EnumerateMetadata(MD);
  }
}

void ValueEnumerator::EnumerateMetadata(const llvm::Metadata *MD) {
  assert(
      (isa<MDNode>(MD) || isa<MDString>(MD) || isa<ConstantAsMetadata>(MD)) &&
      "Invalid metadata kind");

  // Insert a dummy ID to block the co-recursive call to
  // EnumerateMDNodeOperands() from re-visiting MD in a cyclic graph.
  //
  // Return early if there's already an ID.
  if (!MDValueMap.insert(std::make_pair(MD, 0)).second)
    return;

  // Visit operands first to minimize RAUW.
  if (auto *N = dyn_cast<MDNode>(MD))
    EnumerateMDNodeOperands(N);
  else if (auto *C = dyn_cast<ConstantAsMetadata>(MD))
    EnumerateValue(C->getValue());

  HasMDString |= isa<MDString>(MD);
  HasMDLocation |= isa<MDLocation>(MD);

  // Replace the dummy ID inserted above with the correct one.  MDValueMap may
  // have changed by inserting operands, so we need a fresh lookup here.
  MDs.push_back(MD);
  MDValueMap[MD] = MDs.size();
}

/// EnumerateFunctionLocalMetadataa - Incorporate function-local metadata
/// information reachable from the metadata.
void ValueEnumerator::EnumerateFunctionLocalMetadata(
    const llvm::LocalAsMetadata *Local) {
  // Check to see if it's already in!
  unsigned &MDValueID = MDValueMap[Local];
  if (MDValueID)
    return;

  MDs.push_back(Local);
  MDValueID = MDs.size();

  EnumerateValue(Local->getValue());

  // Also, collect all function-local metadata for easy access.
  FunctionLocalMDs.push_back(Local);
}

void ValueEnumerator::EnumerateValue(const Value *V) {
  assert(!V->getType()->isVoidTy() && "Can't insert void values!");
  assert(!isa<MetadataAsValue>(V) && "EnumerateValue doesn't handle Metadata!");

  // Check to see if it's already in!
  unsigned &ValueID = ValueMap[V];
  if (ValueID) {
    // Increment use count.
    Values[ValueID-1].second++;
    return;
  }

  // Enumerate the type of this value.
  EnumerateType(V->getType());

  if (const Constant *C = dyn_cast<Constant>(V)) {
    if (isa<GlobalValue>(C)) {
      // Initializers for globals are handled explicitly elsewhere.
    } else if (C->getNumOperands()) {
      // If a constant has operands, enumerate them.  This makes sure that if a
      // constant has uses (for example an array of const ints), that they are
      // inserted also.

      // We prefer to enumerate them with values before we enumerate the user
      // itself.  This makes it more likely that we can avoid forward references
      // in the reader.  We know that there can be no cycles in the constants
      // graph that don't go through a global variable.
      for (User::const_op_iterator I = C->op_begin(), E = C->op_end();
           I != E; ++I)
        if (!isa<BasicBlock>(*I)) // Don't enumerate BB operand to BlockAddress.
          EnumerateValue(*I);

      // Finally, add the value.  Doing this could make the ValueID reference be
      // dangling, don't reuse it.
      Values.push_back(std::make_pair(V, 1U));
      ValueMap[V] = Values.size();
      return;
    } else if (const ConstantDataSequential *CDS =
               dyn_cast<ConstantDataSequential>(C)) {
      // For our legacy handling of the new ConstantDataSequential type, we
      // need to enumerate the individual elements, as well as mark the
      // outer constant as used.
      for (unsigned i = 0, e = CDS->getNumElements(); i != e; ++i)
        EnumerateValue(CDS->getElementAsConstant(i));
      Values.push_back(std::make_pair(V, 1U));
      ValueMap[V] = Values.size();
      return;
    }
  }

  // Add the value.
  Values.push_back(std::make_pair(V, 1U));
  ValueID = Values.size();
}


void ValueEnumerator::EnumerateType(Type *Ty) {
  unsigned *TypeID = &TypeMap[Ty];

  // We've already seen this type.
  if (*TypeID)
    return;

  // If it is a non-anonymous struct, mark the type as being visited so that we
  // don't recursively visit it.  This is safe because we allow forward
  // references of these in the bitcode reader.
  if (StructType *STy = dyn_cast<StructType>(Ty))
    if (!STy->isLiteral())
      *TypeID = ~0U;

  // Enumerate all of the subtypes before we enumerate this type.  This ensures
  // that the type will be enumerated in an order that can be directly built.
  for (Type *SubTy : Ty->subtypes())
    EnumerateType(SubTy);

  // Refresh the TypeID pointer in case the table rehashed.
  TypeID = &TypeMap[Ty];

  // Check to see if we got the pointer another way.  This can happen when
  // enumerating recursive types that hit the base case deeper than they start.
  //
  // If this is actually a struct that we are treating as forward ref'able,
  // then emit the definition now that all of its contents are available.
  if (*TypeID && *TypeID != ~0U)
    return;

  // Add this type now that its contents are all happily enumerated.
  Types.push_back(Ty);

  *TypeID = Types.size();
}

// Enumerate the types for the specified value.  If the value is a constant,
// walk through it, enumerating the types of the constant.
void ValueEnumerator::EnumerateOperandType(const Value *V) {
  EnumerateType(V->getType());

  if (auto *MD = dyn_cast<MetadataAsValue>(V)) {
    assert(!isa<LocalAsMetadata>(MD->getMetadata()) &&
           "Function-local metadata should be left for later");

    EnumerateMetadata(MD->getMetadata());
    return;
  }

  const Constant *C = dyn_cast<Constant>(V);
  if (!C)
    return;

  // If this constant is already enumerated, ignore it, we know its type must
  // be enumerated.
  if (ValueMap.count(C))
    return;

  // This constant may have operands, make sure to enumerate the types in
  // them.
  for (unsigned i = 0, e = C->getNumOperands(); i != e; ++i) {
    const Value *Op = C->getOperand(i);

    // Don't enumerate basic blocks here, this happens as operands to
    // blockaddress.
    if (isa<BasicBlock>(Op))
      continue;

    EnumerateOperandType(Op);
  }
}

void ValueEnumerator::EnumerateAttributes(AttributeSet PAL) {
  if (PAL.isEmpty()) return;  // null is always 0.

  // Do a lookup.
  unsigned &Entry = AttributeMap[PAL];
  if (Entry == 0) {
    // Never saw this before, add it.
    Attribute.push_back(PAL);
    Entry = Attribute.size();
  }

  // Do lookups for all attribute groups.
  for (unsigned i = 0, e = PAL.getNumSlots(); i != e; ++i) {
    AttributeSet AS = PAL.getSlotAttributes(i);
    unsigned &Entry = AttributeGroupMap[AS];
    if (Entry == 0) {
      AttributeGroups.push_back(AS);
      Entry = AttributeGroups.size();
    }
  }
}

void ValueEnumerator::incorporateFunction(const Function &F) {
  InstructionCount = 0;
  NumModuleValues = Values.size();
  NumModuleMDs = MDs.size();

  // Adding function arguments to the value table.
  for (Function::const_arg_iterator I = F.arg_begin(), E = F.arg_end();
       I != E; ++I)
    EnumerateValue(I);

  FirstFuncConstantID = Values.size();

  // Add all function-level constants to the value table.
  for (Function::const_iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    for (BasicBlock::const_iterator I = BB->begin(), E = BB->end(); I!=E; ++I)
      for (User::const_op_iterator OI = I->op_begin(), E = I->op_end();
           OI != E; ++OI) {
        if ((isa<Constant>(*OI) && !isa<GlobalValue>(*OI)) ||
            isa<InlineAsm>(*OI))
          EnumerateValue(*OI);
      }
    BasicBlocks.push_back(BB);
    ValueMap[BB] = BasicBlocks.size();
  }

  // Optimize the constant layout.
  OptimizeConstants(FirstFuncConstantID, Values.size());

  // Add the function's parameter attributes so they are available for use in
  // the function's instruction.
  EnumerateAttributes(F.getAttributes());

  FirstInstID = Values.size();

  SmallVector<llvm::LocalAsMetadata *, 8> FnLocalMDVector;
  // Add all of the instructions.
  for (Function::const_iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    for (BasicBlock::const_iterator I = BB->begin(), E = BB->end(); I!=E; ++I) {
      for (User::const_op_iterator OI = I->op_begin(), E = I->op_end();
           OI != E; ++OI) {
        if (auto *MD = dyn_cast<llvm::MetadataAsValue>(&*OI))
          if (auto *Local = dyn_cast<LocalAsMetadata>(MD->getMetadata()))
            // Enumerate metadata after the instructions they might refer to.
            FnLocalMDVector.push_back(Local);
      }

      if (!I->getType()->isVoidTy())
        EnumerateValue(I);
    }
  }

  // Add all of the function-local metadata.
  for (unsigned i = 0, e = FnLocalMDVector.size(); i != e; ++i)
    EnumerateFunctionLocalMetadata(FnLocalMDVector[i]);
}

void ValueEnumerator::purgeFunction() {
  /// Remove purged values from the ValueMap.
  for (unsigned i = NumModuleValues, e = Values.size(); i != e; ++i)
    ValueMap.erase(Values[i].first);
  for (unsigned i = NumModuleMDs, e = MDs.size(); i != e; ++i)
    MDValueMap.erase(MDs[i]);
  for (unsigned i = 0, e = BasicBlocks.size(); i != e; ++i)
    ValueMap.erase(BasicBlocks[i]);

  Values.resize(NumModuleValues);
  MDs.resize(NumModuleMDs);
  BasicBlocks.clear();
  FunctionLocalMDs.clear();
}

static void IncorporateFunctionInfoGlobalBBIDs(const Function *F,
                                 DenseMap<const BasicBlock*, unsigned> &IDMap) {
  unsigned Counter = 0;
  for (Function::const_iterator BB = F->begin(), E = F->end(); BB != E; ++BB)
    IDMap[BB] = ++Counter;
}

/// getGlobalBasicBlockID - This returns the function-specific ID for the
/// specified basic block.  This is relatively expensive information, so it
/// should only be used by rare constructs such as address-of-label.
unsigned ValueEnumerator::getGlobalBasicBlockID(const BasicBlock *BB) const {
  unsigned &Idx = GlobalBasicBlockIDs[BB];
  if (Idx != 0)
    return Idx-1;

  IncorporateFunctionInfoGlobalBBIDs(BB->getParent(), GlobalBasicBlockIDs);
  return getGlobalBasicBlockID(BB);
}

}  // end llvm_3_2 namespace
