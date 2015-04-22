//===-- Bitcode/Writer/ValueEnumerator.h - Number values --------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class gives values and types Unique ID's.
//
//===----------------------------------------------------------------------===//

#ifndef VALUE_ENUMERATOR_H
#define VALUE_ENUMERATOR_H

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/UseListOrder.h>
#include <vector>

namespace llvm {

class Type;
class Value;
class Instruction;
class BasicBlock;
class Function;
class Module;
class Metadata;
class LocalAsMetadata;
class MDNode;
class NamedMDNode;
class AttributeSet;
class ValueSymbolTable;
class MDSymbolTable;
class raw_ostream;

}  // end llvm namespace

namespace llvm_3_2 {

class ValueEnumerator {
public:
  typedef std::vector<llvm::Type*> TypeList;

  // For each value, we remember its Value* and occurrence frequency.
  typedef std::vector<std::pair<const llvm::Value*, unsigned> > ValueList;

  llvm::UseListOrderStack UseListOrders;
private:
  typedef llvm::DenseMap<llvm::Type*, unsigned> TypeMapType;
  TypeMapType TypeMap;
  TypeList Types;

  typedef llvm::DenseMap<const llvm::Value*, unsigned> ValueMapType;
  ValueMapType ValueMap;
  ValueList Values;


  std::vector<const llvm::Metadata *> MDs;
  llvm::SmallVector<const llvm::LocalAsMetadata *, 8> FunctionLocalMDs;
  typedef llvm::DenseMap<const llvm::Metadata *, unsigned> MetadataMapType;
  MetadataMapType MDValueMap;
  bool HasMDString;
  bool HasMDLocation;
  bool ShouldPreserveUseListOrder;

  typedef llvm::DenseMap<llvm::AttributeSet, unsigned> AttributeGroupMapType;
  AttributeGroupMapType AttributeGroupMap;
  std::vector<llvm::AttributeSet> AttributeGroups;

  typedef llvm::DenseMap<llvm::AttributeSet, unsigned> AttributeMapType;
  AttributeMapType AttributeMap;
  std::vector<llvm::AttributeSet> Attribute;

  /// GlobalBasicBlockIDs - This map memoizes the basic block ID's referenced by
  /// the "getGlobalBasicBlockID" method.
  mutable llvm::DenseMap<const llvm::BasicBlock*, unsigned> GlobalBasicBlockIDs;

  typedef llvm::DenseMap<const llvm::Instruction*, unsigned> InstructionMapType;
  InstructionMapType InstructionMap;
  unsigned InstructionCount;

  /// BasicBlocks - This contains all the basic blocks for the currently
  /// incorporated function.  Their reverse mapping is stored in ValueMap.
  std::vector<const llvm::BasicBlock*> BasicBlocks;

  /// When a function is incorporated, this is the size of the Values list
  /// before incorporation.
  unsigned NumModuleValues;

  /// When a function is incorporated, this is the size of the MDValues list
  /// before incorporation.
  unsigned NumModuleMDs;

  unsigned FirstFuncConstantID;
  unsigned FirstInstID;

  ValueEnumerator(const ValueEnumerator &) = delete;
  void operator=(const ValueEnumerator &) = delete;
public:
  ValueEnumerator(const llvm::Module &M, bool ShouldPreserveUseListOrder);

  void dump() const;
  void print(llvm::raw_ostream &OS, const ValueMapType &Map, const char *Name) const;
  void print(llvm::raw_ostream &OS, const MetadataMapType &Map,
             const char *Name) const;

  unsigned getValueID(const llvm::Value *V) const;
  unsigned getMetadataID(const llvm::Metadata *MD) const {
    auto ID = getMetadataOrNullID(MD);
    assert(ID != 0 && "Metadata not in slotcalculator!");
    return ID - 1;
  }
  unsigned getMetadataOrNullID(const llvm::Metadata *MD) const {
    return MDValueMap.lookup(MD);
  }

  bool hasMDString() const { return HasMDString; }
  bool hasMDLocation() const { return HasMDLocation; }

  bool shouldPreserveUseListOrder() const { return ShouldPreserveUseListOrder; }

  unsigned getTypeID(llvm::Type *T) const {
    TypeMapType::const_iterator I = TypeMap.find(T);
    assert(I != TypeMap.end() && "Type not in ValueEnumerator!");
    return I->second-1;
  }

  unsigned getInstructionID(const llvm::Instruction *I) const;
  void setInstructionID(const llvm::Instruction *I);

  unsigned getAttributeID(llvm::AttributeSet PAL) const {
    if (PAL.isEmpty()) return 0;  // Null maps to zero.
    AttributeMapType::const_iterator I = AttributeMap.find(PAL);
    assert(I != AttributeMap.end() && "Attribute not in ValueEnumerator!");
    return I->second;
  }

  unsigned getAttributeGroupID(llvm::AttributeSet PAL) const {
    if (PAL.isEmpty()) return 0;  // Null maps to zero.
    AttributeGroupMapType::const_iterator I = AttributeGroupMap.find(PAL);
    assert(I != AttributeGroupMap.end() && "Attribute not in ValueEnumerator!");
    return I->second;
  }

  /// getFunctionConstantRange - Return the range of values that corresponds to
  /// function-local constants.
  void getFunctionConstantRange(unsigned &Start, unsigned &End) const {
    Start = FirstFuncConstantID;
    End = FirstInstID;
  }

  const ValueList &getValues() const { return Values; }
  const std::vector<const llvm::Metadata *> &getMDs() const { return MDs; }
  const llvm::SmallVectorImpl<const llvm::LocalAsMetadata *> &getFunctionLocalMDs() const {
    return FunctionLocalMDs;
  }
  const TypeList &getTypes() const { return Types; }
  const std::vector<const llvm::BasicBlock*> &getBasicBlocks() const {
    return BasicBlocks;
  }
  const std::vector<llvm::AttributeSet> &getAttributes() const {
    return Attribute;
  }
  const std::vector<llvm::AttributeSet> &getAttributeGroups() const {
    return AttributeGroups;
  }

  /// getGlobalBasicBlockID - This returns the function-specific ID for the
  /// specified basic block.  This is relatively expensive information, so it
  /// should only be used by rare constructs such as address-of-label.
  unsigned getGlobalBasicBlockID(const llvm::BasicBlock *BB) const;

  /// incorporateFunction/purgeFunction - If you'd like to deal with a function,
  /// use these two methods to get its data into the ValueEnumerator!
  ///
  void incorporateFunction(const llvm::Function &F);
  void purgeFunction();

private:
  void OptimizeConstants(unsigned CstStart, unsigned CstEnd);

  void EnumerateMDNodeOperands(const llvm::MDNode *N);
  void EnumerateMetadata(const llvm::Metadata *MD);
  void EnumerateFunctionLocalMetadata(const llvm::LocalAsMetadata *Local);
  void EnumerateNamedMDNode(const llvm::NamedMDNode *NMD);
  void EnumerateValue(const llvm::Value *V);
  void EnumerateType(llvm::Type *T);
  void EnumerateOperandType(const llvm::Value *V);
  void EnumerateAttributes(llvm::AttributeSet PAL);

  void EnumerateValueSymbolTable(const llvm::ValueSymbolTable &ST);
  void EnumerateNamedMetadata(const llvm::Module &M);
};

}  // End llvm_3_2 namespace

#endif
