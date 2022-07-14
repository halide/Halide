#ifndef HALIDE_SPIRV_IR_H
#define HALIDE_SPIRV_IR_H

#include <stack>
#include <vector>
#include <unordered_map>

#include "Type.h"
#include "IntrusivePtr.h"

#include "spirv/1.0/spirv.h"

namespace Halide {
namespace Internal {

class SpvModule;
class SpvFunction;
class SpvBlock;
class SpvInstruction;
class SpvBuilder;

struct SpvModuleContents;
struct SpvFunctionContents;
struct SpvBlockContents;
struct SpvInstructionContents;

using SpvModuleContentsPtr = IntrusivePtr<SpvModuleContents>;
using SpvFunctionContentsPtr = IntrusivePtr<SpvFunctionContents>;
using SpvBlockContentsPtr = IntrusivePtr<SpvBlockContents>;
using SpvInstructionContentsPtr = IntrusivePtr<SpvInstructionContents>;

using SpvId = uint32_t;
using SpvBinary = std::vector<uint32_t>;

enum SpvPrecision {
    SpvFullPrecision,
    SpvRelaxedPrecision,
};

enum SpvPredefinedConstant {
    SpvNullConstant,
    SpvTrueConstant,
    SpvFalseConstant,
};

enum SpvKind {
    SpvInvalidItem,
    SpvTypeId,
    SpvVoidTypeId,
    SpvBoolTypeId,
    SpvIntTypeId,
    SpvFloatTypeId,
    SpvVectorTypeId,
    SpvArrayTypeId,
    SpvRuntimeArrayTypeId,
    SpvStringTypeId,
    SpvPointerTypeId,
    SpvStructTypeId,
    SpvFunctionTypeId,
    SpvConstantId,
    SpvBoolConstantId,
    SpvIntConstantId,
    SpvFloatConstantId,
    SpvStringConstantId,
    SpvCompositeConstantId,
    SpvResultId,
    SpvVariableId,
    SpvInstructionId,
    SpvFunctionId,
    SpvBlockId,
    SpvLabelId,
    SpvParameterId,
    SpvModuleId,
    SpvUnknownItem,
};

static constexpr SpvId SpvInvalidId = SpvId(-1);
static constexpr SpvId SpvNoResult = 0;
static constexpr SpvId SpvNoType = 0;

// --

class SpvInstruction {
public:

    SpvInstruction() = default;
    ~SpvInstruction() = default;

    SpvInstruction(const SpvInstruction &) = default;
    SpvInstruction &operator=(const SpvInstruction &) = default;
    SpvInstruction(SpvInstruction &&) = default;
    SpvInstruction &operator=(SpvInstruction &&) = default;

    void set_block(SpvBlock block);
    void set_result_id(SpvId id);
    void set_type_id(SpvId id);
    void set_op_code(SpvOp opcode);
    void add_operand(SpvId id);
    void add_immediate(SpvId id);
    void add_data(uint32_t bytes, const void* data);
    void add_string(const std::string& str);    
    
    SpvId result_id() const;
    SpvId type_id() const;
    SpvOp op_code() const;
    SpvId operand(uint32_t index);

    bool has_type(void) const;
    bool has_result(void) const;
    bool is_defined(void) const;
    bool is_immediate(uint32_t index) const;
    uint32_t length() const;
    SpvBlock block() const;
    
    void encode(SpvBinary& binary) const;
    
    static SpvInstruction make(SpvOp op_code);

protected:
    SpvInstructionContentsPtr contents;
};

// -- Factory Methods for Specific Instructions

struct SpvLabelInst {
    static SpvInstruction make( SpvId result_id ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpLabel);
        inst.set_result_id(result_id);
        return inst;
    }
};

struct SpvDecorateInst {
    using Literals = std::vector<uint32_t>;
    static SpvInstruction make( SpvId target_id, SpvDecoration decoration_type, const Literals& literals = {}) {
        SpvInstruction inst = SpvInstruction::make(SpvOpDecorate);
        inst.add_operand(target_id);
        inst.add_immediate(decoration_type);
        for( uint32_t l : literals) {
            inst.add_immediate(l);
        }
        return inst;
    }
};

struct SpvMemberDecorateInst {
    using Literals = std::vector<uint32_t>;
    static SpvInstruction make( SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals& literals = {}) {
        SpvInstruction inst = SpvInstruction::make(SpvOpMemberDecorate);
        inst.add_operand(struct_type_id);
        inst.add_immediate(decoration);
        for( uint32_t l : literals) {
            inst.add_immediate(l);
        }
        return inst;
    }
};

struct SpvUnaryOpInstruction {
    static SpvInstruction make(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id ) {
        SpvInstruction inst = SpvInstruction::make(op_code);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(src_id);
        return inst;
    }
};

struct SpvBinaryOpInstruction {
    static SpvInstruction make(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id  ) {
        SpvInstruction inst = SpvInstruction::make(op_code);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(src_a_id);
        inst.add_operand(src_b_id);
        return inst;
    }
};

struct SpvVectorInsertDynamicInst {
    static SpvInstruction make(SpvId result_id, SpvId vector_id, SpvId value_id, uint32_t index) {
        SpvInstruction inst = SpvInstruction::make(SpvOpVectorInsertDynamic);
        inst.set_type_id(SpvOpTypeVector);
        inst.set_result_id(result_id);
        inst.add_operand(vector_id);
        inst.add_operand(value_id);
        inst.add_immediate(index);
        return inst;
    }
};

struct SpvConstantInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, size_t bytes, const void* data) {
        SpvInstruction inst = SpvInstruction::make(SpvOpConstant);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_data(bytes, data);
        return inst;
    }
};

struct SpvConstantNullInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id) {
        SpvInstruction inst = SpvInstruction::make(SpvOpConstantNull);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        return inst;
    }
};

struct SpvConstantBoolInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, bool value) {
        SpvOp op_code = value ? SpvOpConstantTrue : SpvOpConstantFalse;
        SpvInstruction inst = SpvInstruction::make(op_code);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        return inst;
    }
};

struct SpvConstantCompositeInst {
    using Components = std::vector<SpvId>;
    static SpvInstruction make(SpvId type_id, SpvId result_id, const Components& components) {
        SpvInstruction inst = SpvInstruction::make(SpvOpConstantComposite);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        for(SpvId scalar_id : components) {
            inst.add_operand(scalar_id);
        }
        return inst;
    }
};

struct SpvTypeVoidInst {
    static SpvInstruction make(SpvId void_type_id) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeVoid);
        inst.set_result_id(void_type_id);
        return inst;
    }
};

struct SpvTypeBoolInst {
    static SpvInstruction make(SpvId bool_type_id) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeBool);
        inst.set_result_id(bool_type_id);
        return inst;
    }
};

struct SpvTypeIntInst {
    static SpvInstruction make(SpvId int_type_id, uint32_t bits, uint32_t signedness) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeInt);
        inst.set_result_id(int_type_id);
        inst.add_immediate(bits);
        inst.add_immediate(signedness);
        return inst;
    }
};

struct SpvTypeFloatInst {
    static SpvInstruction make(SpvId float_type_id, uint32_t bits) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeFloat);
        inst.set_result_id(float_type_id);
        inst.add_immediate(bits);
        return inst;
    }
};

struct SpvTypeVectorInst {
    static SpvInstruction make(SpvId vector_type_id, SpvId element_type_id, uint32_t vector_size) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeVector);
        inst.set_result_id(vector_type_id);
        inst.add_operand(element_type_id);
        inst.add_immediate(vector_size);
        return inst;
    }
};

struct SpvTypeArrayInst {
    static SpvInstruction make(SpvId array_type_id, SpvId element_type_id, uint32_t array_size) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeArray);
        inst.set_result_id(array_type_id);
        inst.add_operand(element_type_id);
        inst.add_immediate(array_size);        
        return inst;
    }
};

struct SpvTypeStructInst {
    using MemberTypeIds = std::vector<SpvId>;
    static SpvInstruction make(SpvId result_id, const MemberTypeIds& member_type_ids) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeStruct);
        inst.set_result_id(result_id);
        for(const SpvId member_type : member_type_ids) {
            inst.add_operand(member_type);
        }
        return inst;
    }
};

struct SpvTypeRuntimeArrayInst {
    static SpvInstruction make(SpvId result_type_id, SpvId base_type_id ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeRuntimeArray);
        inst.set_result_id(result_type_id);
        inst.add_operand(base_type_id);
        return inst;
    }
};

struct SpvTypePointerInst {
    static SpvInstruction make(SpvId pointer_type_id, SpvStorageClass storage_class, SpvId base_type_id ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypePointer);
        inst.set_result_id(pointer_type_id);
        inst.add_immediate(storage_class);
        inst.add_operand(base_type_id);
        return inst;
    }
};

struct SpvTypeFunctionInst {
    using ParamTypes = std::vector<SpvId>;
    static SpvInstruction make(SpvId function_type_id, SpvId return_type_id, const ParamTypes& param_type_ids) {
        SpvInstruction inst = SpvInstruction::make(SpvOpTypeFunction);
        inst.set_type_id(return_type_id);
        inst.set_result_id(function_type_id);
        for(SpvId type_id : param_type_ids) {
            inst.add_operand(type_id);
        }
        return inst;
    }
};

struct SpvVariableInst {
    static SpvInstruction make(SpvId result_type_id, SpvId result_id, uint32 storage_class, SpvId initializer_id = SpvInvalidId) {
        SpvInstruction inst = SpvInstruction::make(SpvOpVariable);
        inst.set_type_id(result_type_id);
        inst.set_result_id(result_id);
        inst.add_immediate(storage_class);
        if(initializer_id != SpvInvalidId) {
            inst.add_operand(initializer_id);
        }
        return inst;
    }
};

struct SpvFunctionInst {
    static SpvInstruction make(SpvId return_type_id, SpvId func_id, uint32_t control_mask, SpvId func_type_id) {
        SpvInstruction inst = SpvInstruction::make(SpvOpFunction);
        inst.set_type_id(return_type_id);
        inst.set_result_id(func_id);
        inst.add_immediate(control_mask);
        inst.add_operand(func_type_id);
        return inst;
    }
};

struct SpvFunctionParameterInst {
    static SpvInstruction make(SpvId param_type_id, SpvId param_id) {
        SpvInstruction inst = SpvInstruction::make(SpvOpFunctionParameter);
        inst.set_type_id(param_type_id);
        inst.set_result_id(param_id);
        return inst;
    }
};

struct SpvReturnInst {
    static SpvInstruction make(SpvId return_value_id = SpvInvalidId) {
        SpvOp opcode = (return_value_id == SpvInvalidId) ? SpvOpReturn : SpvOpReturnValue;
        SpvInstruction inst = SpvInstruction::make( opcode );
        if(return_value_id != SpvInvalidId)
            inst.add_operand(return_value_id);
        return inst;
    }
};

struct SpvEntryPointInst {
    using Variables = std::vector<SpvId>;
    static SpvInstruction make(SpvId exec_model, SpvId func_id, const std::string& name, const Variables& variables) {
        SpvInstruction inst = SpvInstruction::make(SpvOpEntryPoint);
        inst.add_immediate(exec_model);
        inst.add_operand(func_id);
        inst.add_string(name);
        for(SpvId var : variables) {
            inst.add_operand(var);
        }
        return inst;
    }
};

struct SpvMemoryModelInst {
    static SpvInstruction make(SpvAddressingModel addressing_model, SpvMemoryModel memory_model) {
        SpvInstruction inst = SpvInstruction::make(SpvOpMemoryModel);
        inst.add_immediate(addressing_model);
        inst.add_immediate(memory_model);    
        return inst;
    }
};

struct SpvExecutionModeLocalSizeInst {
    static SpvInstruction make(SpvId function_id, uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z) {
        SpvInstruction inst = SpvInstruction::make(SpvOpExecutionMode);
        inst.add_operand(function_id);
        inst.add_immediate(SpvExecutionModeLocalSize);
        inst.add_immediate(wg_size_x);
        inst.add_immediate(wg_size_y);
        inst.add_immediate(wg_size_z);
        return inst;
    }
};

struct SpvControlBarrierInst {
    static SpvInstruction make(SpvId execution_scope_id, SpvId memory_scope_id, uint32_t semantics_mask ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpControlBarrier);
        inst.add_operand(execution_scope_id);
        inst.add_operand(memory_scope_id);
        inst.add_immediate(semantics_mask);
        return inst;
    }
};

struct SpvNotInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId src_id ) {
        return SpvUnaryOpInstruction::make(SpvOpNot, type_id, result_id, src_id);
    }
};

struct SpvMulExtendedInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed ) {
        return SpvBinaryOpInstruction::make(is_signed ? SpvOpSMulExtended : SpvOpUMulExtended, type_id, result_id, src_a_id, src_b_id);
    }
};

struct SpvSelectInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId condition_id, SpvId true_id, SpvId false_id ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpSelect);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(condition_id);
        inst.add_operand(true_id);
        inst.add_operand(false_id);
        return inst;
    }
};

struct SpvInBoundsAccessChainInst {
    using Indices = std::vector<SpvId>;
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId base_id, SpvId element_id, const Indices& indices ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpInBoundsAccessChain);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(base_id);
        inst.add_operand(element_id);
        for(SpvId i : indices) {
            inst.add_operand(i);
        }
        return inst;
    }
};

struct SpvLoadInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId ptr_id, uint32_t access_mask = 0x0) {
        SpvInstruction inst = SpvInstruction::make(SpvOpLoad);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(ptr_id);
        inst.add_immediate(access_mask);
        return inst;
    }
};

struct SpvStoreInst {
    static SpvInstruction make(SpvId ptr_id, SpvId obj_id, uint32_t access_mask = 0x0) {
        SpvInstruction inst = SpvInstruction::make(SpvOpStore);
        inst.add_operand(ptr_id);
        inst.add_operand(obj_id);
        inst.add_immediate(access_mask);
        return inst;
    }
};

struct SpvCompositeExtractInst {
    using Indices = std::vector<SpvId>;
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId composite_id, const Indices& indices ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpCompositeExtract);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(composite_id);
        for(SpvId i : indices) {
            inst.add_immediate(i);
        }
        return inst;
    }
};

struct SpvBitcastInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId src_id ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpBitcast);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        inst.add_operand(src_id);
        return inst;
    }
};

struct SpvIAddInst {
    static SpvInstruction make(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id  ) {
        return SpvBinaryOpInstruction::make(SpvOpIAdd, type_id, result_id, src_a_id, src_b_id);
    }
};

struct SpvBranchInst {
    static SpvInstruction make( SpvId target_label_id ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpBranch);
        inst.add_operand(target_label_id);
        return inst;
    }
};

struct SpvBranchConditionalInst {
    using BranchWeights = std::vector<uint32_t>;
    static SpvInstruction make( SpvId condition_label_id, SpvId true_label_id, SpvId false_label_id, const BranchWeights& weights = {} ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpBranch);
        inst.add_operand(condition_label_id);
        inst.add_operand(true_label_id);
        inst.add_operand(false_label_id);
        for( uint32_t w : weights) {
            inst.add_immediate(w);
        }
        return inst;
    }
};

struct SpvLoopMergeInst {
    static SpvInstruction make( SpvId merge_label_id, SpvId continue_label_id, uint32_t loop_control_mask = SpvLoopControlMaskNone) {
        SpvInstruction inst = SpvInstruction::make(SpvOpLoopMerge);
        inst.add_operand(merge_label_id);
        inst.add_operand(continue_label_id);
        inst.add_immediate(loop_control_mask);
        return inst;
    }
};

struct SpvSelectionMergeInst {
    static SpvInstruction make( SpvId merge_label_id, uint32_t selection_control_mask = SpvSelectionControlMaskNone) {
        SpvInstruction inst = SpvInstruction::make(SpvOpSelectionMerge);
        inst.add_operand(merge_label_id);
        inst.add_immediate(selection_control_mask);
        return inst;
    }
};

struct SpvOpPhiInst {
    using VariableBlockIdPair = std::pair<SpvId, SpvId>; // (Variable Id, Block Id)
    using BlockVariables = std::vector<VariableBlockIdPair>;
    static SpvInstruction make(SpvId type_id, SpvId result_id, const BlockVariables& block_vars ) {
        SpvInstruction inst = SpvInstruction::make(SpvOpPhi);
        inst.set_type_id(type_id);
        inst.set_result_id(result_id);
        for( const VariableBlockIdPair& vb : block_vars) {
            inst.add_operand(vb.first);     // variable id
            inst.add_operand(vb.second);    // block id
        }
        return inst;
    }
};

// --

class SpvBlock {
public:
    using Instructions = std::vector<SpvInstruction>;
    using Variables = std::vector<SpvInstruction>;
    using Blocks = std::vector<SpvBlock>;

    SpvBlock() = default;
    ~SpvBlock() = default;

    SpvBlock(const SpvBlock &) = default;
    SpvBlock &operator=(const SpvBlock &) = default;
    SpvBlock(SpvBlock &&) = default;
    SpvBlock &operator=(SpvBlock &&) = default;

    void add_instruction(SpvInstruction inst);
    void add_variable(SpvInstruction var);
    void set_function(SpvFunction func);
    const Instructions& instructions() const;
    const Variables& variables() const;
    SpvFunction function() const;
    bool is_reachable() const;
    bool is_terminated() const;
    bool is_defined() const;
    SpvId id() const;

    void encode(SpvBinary& binary) const;

    static SpvBlock make(SpvFunction func, SpvId id);

protected:
   
    SpvBlockContentsPtr contents;
};

// --

class SpvFunction {
public:
    SpvFunction() = default;
    ~SpvFunction() = default;

    SpvFunction(const SpvFunction &) = default;
    SpvFunction &operator=(const SpvFunction &) = default;
    SpvFunction(SpvFunction &&) = default;
    SpvFunction &operator=(SpvFunction &&) = default;

    void add_block(SpvBlock block);
    void add_parameter(SpvInstruction param);
    void set_module(SpvModule module);
    void set_return_precision(SpvPrecision precision);
    void set_parameter_precision(uint32_t index, SpvPrecision precision);
    bool is_defined() const;

    SpvBlock entry_block() const;
    SpvPrecision return_precision() const;
    SpvPrecision parameter_precision(uint32_t index) const;
    uint32_t parameter_count() const;
    uint32_t control_mask() const;
    SpvInstruction declaration() const;
    SpvModule module() const;
    SpvId return_type_id() const;
    SpvId type_id() const;
    SpvId id() const;

    void encode(SpvBinary& binary) const;

    static SpvFunction make(SpvId func_id, SpvId func_type_id, SpvId return_type_id, uint32_t control_mask = SpvFunctionControlMaskNone);

protected:
    SpvFunctionContentsPtr contents;
};

// --

class SpvModule {
public:
    using EntryPointNames = std::vector<std::string>;
    using Instructions = std::vector<SpvInstruction>;

    SpvModule() = default;
    ~SpvModule() = default;

    SpvModule(const SpvModule &) = default;
    SpvModule &operator=(const SpvModule &) = default;
    SpvModule(SpvModule &&) = default;
    SpvModule &operator=(SpvModule &&) = default;

    void add_debug(SpvInstruction val);    
    void add_annotation(SpvInstruction val);    
    void add_type(SpvInstruction val);
    void add_constant(SpvInstruction val);
    void add_global(SpvInstruction val);
    void add_execution_mode(SpvInstruction val);
    void add_function(SpvFunction val);
    void add_instruction(SpvInstruction val);
    void add_entry_point(const std::string& name, SpvInstruction entry_point);

    void require_capability(SpvCapability val);
    void require_extension(const std::string& val);    

    void set_source_language(SpvSourceLanguage val);
    void set_addressing_model(SpvAddressingModel val);
    void set_memory_model(SpvMemoryModel val);
    SpvSourceLanguage source_language() const;
    SpvAddressingModel addressing_model() const;
    SpvMemoryModel memory_model() const;
    SpvInstruction entry_point(const std::string& name) const;    
    EntryPointNames entry_point_names() const;
    const Instructions& execution_modes() const;
    SpvModule module() const;
    
    bool is_capability_required(SpvCapability val) const;
    bool is_extension_required(const std::string& val) const;
    bool is_defined() const;
    SpvId id() const;

    void encode(SpvBinary& binary) const;

    static SpvModule make(SpvId module_id, 
        SpvSourceLanguage source_language = SpvSourceLanguageUnknown,
        SpvAddressingModel addressing_model = SpvAddressingModelLogical,
        SpvMemoryModel memory_model = SpvMemoryModelSimple);

protected:
    SpvModuleContentsPtr contents;
};

// --

class SpvBuilder {
public:
    using ParamTypes = std::vector<SpvId>;
    using StructMemberTypes = std::vector<SpvId>;
    using Variables = std::vector<SpvId>;
    using Indices = std::vector<uint32_t>;
    using Literals = std::vector<uint32_t>;

    SpvBuilder();
    ~SpvBuilder() = default;

    SpvBuilder(const SpvBuilder &) = delete;
    SpvBuilder &operator=(const SpvBuilder &) = delete;

    SpvId reserve_id(SpvKind = SpvResultId);
    SpvKind kind_of(SpvId id);

    SpvId map_type(const Type& type, uint32_t array_size=1);
    SpvId map_pointer_type(const Type& type, SpvStorageClass storage_class);
    SpvId map_pointer_type(SpvId type_id, SpvStorageClass storage_class);
    SpvId map_constant(const Type& type, const void* data);
    SpvId map_null_constant(const Type& type);
    SpvId map_bool_constant(bool value);
    SpvId map_function_type(SpvId return_type, const ParamTypes& param_types = {});

    SpvId declare_type(const Type& type, uint32_t array_size=1);
    SpvId declare_struct(const StructMemberTypes& member_types);
    SpvId declare_runtime_array(SpvId base_type_id);
    SpvId declare_pointer_type(const Type& type, SpvStorageClass storage_class);
    SpvId declare_pointer_type(SpvId base_type_id, SpvStorageClass storage_class);
    SpvId declare_constant(const Type& type, const void* data);
    SpvId declare_null_constant(const Type& type);
    SpvId declare_bool_constant(bool value);
    SpvId declare_string_constant(const std::string& str);
    SpvId declare_scalar_constant(const Type& type, const void* data);
    SpvId declare_vector_constant(const Type& type, const void* data);
    SpvId declare_access_chain(SpvId ptr_type_id, SpvId base_id, SpvId element_id, const Indices& indices );
    SpvId declare_function_type(SpvId return_type_id, const ParamTypes& param_type_ids);

    SpvFunction add_function(SpvId return_type, const ParamTypes& param_types = {});
    SpvId add_instruction(SpvInstruction val);
    SpvId add_annotation( SpvId target_id, SpvDecoration decoration_type, const Literals& literals = {});
    SpvId add_struct_annotation( SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals& literals = {});
    
    SpvId add_variable(SpvId type_id, uint32 storage_class, SpvId initializer_id = SpvInvalidId);
    SpvId add_global_variable(SpvId type_id, uint32 storage_class, SpvId initializer_id = SpvInvalidId);

    SpvId map_struct(const StructMemberTypes& member_types);

    void add_entry_point(const std::string& name, 
                         SpvId func_id, SpvExecutionModel exec_model,
                         const Variables& variables = {});

    void add_execution_mode_local_size(SpvId entry_point_id, uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z);

    void set_source_language(SpvSourceLanguage val);
    void set_addressing_model(SpvAddressingModel val);
    void set_memory_model(SpvMemoryModel val);

    SpvSourceLanguage source_language() const;
    SpvAddressingModel addressing_model() const;
    SpvMemoryModel memory_model() const;
    
    void enter_block(SpvBlock block);
    SpvBlock current_block() const;
    SpvBlock leave_block();

    void enter_function(SpvFunction func_id);
    SpvFunction current_function() const;
    SpvFunction leave_function();
    
    void set_current_id(SpvId id);
    SpvId current_id() const;

    SpvModule current_module() const;

    void require_extension(const std::string& extension);
    void require_capability(SpvCapability);

    bool is_extension_required(const std::string& extension) const;
    bool is_capability_required(SpvCapability) const;

    void append(SpvInstruction inst);
    void encode(SpvBinary& binary) const;

protected:

    using TypeKey = std::string;
    using TypeMap = std::unordered_map<TypeKey, SpvId>;
    using KindMap = std::unordered_map<SpvId, SpvKind>;
    using PointerTypeKey = std::pair<SpvId, SpvStorageClass>;
    using PointerTypeMap = std::map<PointerTypeKey, SpvId>;
    using ConstantKey = std::string;
    using ConstantMap = std::unordered_map<ConstantKey, SpvId>;
    using StringMap = std::unordered_map<ConstantKey, SpvId>;
    using InstructionMap = std::unordered_map<SpvId, SpvInstruction>;
    using FunctionTypeKey = std::string;
    using FunctionTypeMap = std::unordered_map<FunctionTypeKey, SpvId>;
    using FunctionMap = std::unordered_map<SpvId, SpvFunction>;
    using FunctionStack = std::stack<SpvFunction>;
    using BlockStack = std::stack<SpvBlock>;

    SpvId declare_id(SpvKind kind);

    TypeKey hash_type(const Type& type, uint32_t array_size=1) const;
    SpvId lookup_type(const Type& type, uint32_t array_size=1) const;

    TypeKey hash_struct(const StructMemberTypes& member_types) const;
    SpvId lookup_struct(const StructMemberTypes& member_types) const;

    PointerTypeKey hash_pointer_type(const Type& type, SpvStorageClass storage_class) const;
    SpvId lookup_pointer_type(const Type& type, SpvStorageClass storage_class) const;

    PointerTypeKey hash_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const;
    SpvId lookup_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const;

    ConstantKey hash_bool_constant(bool value) const;

    ConstantKey hash_constant(const Type& type, const void* data) const;
    SpvId lookup_constant(const Type& type, const void* data) const;

    ConstantKey hash_null_constant(const Type& type) const;
    SpvId lookup_null_constant(const Type& type) const;

    SpvId map_instruction(SpvInstruction inst);
    SpvInstruction lookup_instruction(SpvId result_id) const;
    bool has_instruction(SpvId inst) const;

    FunctionTypeKey hash_function_type(SpvId return_type_id, const ParamTypes& param_type_ids) const;
    SpvId lookup_function_type(SpvId return_type_id, const ParamTypes& param_type_ids) const;

    SpvId scope_id = SpvInvalidId;
    SpvModule module;
    KindMap kind_map;
    TypeMap type_map;
    TypeMap struct_map;
    StringMap string_map;
    ConstantMap constant_map;
    FunctionMap function_map;
    InstructionMap instruction_map;
    PointerTypeMap pointer_type_map;
    FunctionTypeMap function_type_map;
    FunctionStack function_stack;
    BlockStack block_stack;
};

// --

struct SpvInstructionContents {
    using Operands = std::vector<SpvId>;
    using Immediates = std::vector<bool>;
    mutable RefCount ref_count;
    SpvOp op_code = SpvOpNop;
    SpvId result_id = SpvNoResult;
    SpvId type_id = SpvNoType;
    Operands operands;
    Immediates immediates;
    SpvBlock block;
};

struct SpvBlockContents {
    using Instructions = std::vector<SpvInstruction>;
    using Variables = std::vector<SpvInstruction>;
    using Blocks = std::vector<SpvBlock>;
    mutable RefCount ref_count;
    SpvId block_id = SpvInvalidId;
    SpvFunction parent;
    Instructions instructions;
    Variables variables;
    Blocks before;
    Blocks after;
    bool reachable = true;    
};

struct SpvFunctionContents {
    using PrecisionMap = std::unordered_map<SpvId, SpvPrecision>;
    using Parameters = std::vector<SpvInstruction>;
    using Blocks = std::vector<SpvBlock>;
    mutable RefCount ref_count;
    SpvModule parent;
    SpvId function_id;
    SpvId function_type_id;
    SpvId return_type_id;
    uint32_t control_mask;
    SpvInstruction declaration;
    Parameters parameters;
    PrecisionMap precision;
    Blocks blocks; 
};

struct SpvModuleContents {
    using Capabilities = std::set<SpvCapability>;
    using Extensions = std::set<std::string>;
    using Imports = std::set<std::string>;
    using Functions = std::vector<SpvFunction>;
    using Instructions = std::vector<SpvInstruction>;
    using EntryPoints = std::unordered_map<std::string, SpvInstruction>;

    mutable RefCount ref_count;
    SpvId module_id = SpvInvalidId;
    SpvSourceLanguage source_language = SpvSourceLanguageUnknown;
    SpvAddressingModel addressing_model = SpvAddressingModelLogical;
    SpvMemoryModel memory_model = SpvMemoryModelSimple;
    Capabilities capabilities;
    Extensions extensions;
    Imports imports;
    EntryPoints entry_points;    
    Instructions execution_modes;
    Instructions debug;
    Instructions annotations;
    Instructions types;
    Instructions constants;
    Instructions globals;
    Functions functions;
    Instructions instructions;
};

SpvInstruction SpvInstruction::make(SpvOp op_code) {
    SpvInstruction instance;
    instance.contents = SpvInstructionContentsPtr(new SpvInstructionContents);
    instance.contents->op_code = op_code;
    instance.contents->result_id = SpvNoResult;
    instance.contents->type_id = SpvNoType;
    return instance;
}  

void SpvInstruction::set_block(SpvBlock block) { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    contents->block = block;
}

void SpvInstruction::set_result_id(SpvId result_id) { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    contents->result_id = result_id; 
}

void SpvInstruction::set_type_id(SpvId type_id) { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    contents->type_id = type_id; 
}

void SpvInstruction::set_op_code(SpvOp op_code) { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    contents->op_code = op_code; 
}

void SpvInstruction::add_operand(SpvId id) {
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    contents->operands.push_back(id);
    contents->immediates.push_back(false);
}

void SpvInstruction::add_immediate(SpvId id) {
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    contents->operands.push_back(id);
    contents->immediates.push_back(true);
}

SpvId SpvInstruction::result_id() const { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before accessing its properties\n";
    return contents->result_id; 
}

SpvId SpvInstruction::type_id() const { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before accessing its properties\n";
    return contents->type_id; 
}

SpvOp SpvInstruction::op_code() const { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before accessing its properties\n";
    return contents->op_code; 
}

SpvId SpvInstruction::operand(uint32_t index) { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before accessing its properties\n";
    return contents->operands[index]; 
}

bool SpvInstruction::has_type(void) const { 
    if(!is_defined()) { return false; }
    return contents->type_id != SpvNoType; 
}

bool SpvInstruction::has_result(void) const { 
    if(!is_defined()) { return false; }
    return contents->result_id != SpvNoResult; 
}

bool SpvInstruction::is_defined() const {
    return contents.defined();
}

bool SpvInstruction::is_immediate(uint32_t index) const { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    return contents->immediates[index]; 
}

uint32_t SpvInstruction::length() const { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    return (uint32_t)contents->operands.size(); 
}

SpvBlock SpvInstruction::block() const { 
    user_assert(is_defined()) << "An SpvInstruction must be defined before modifying its properties\n";
    return contents->block; 
}

void SpvInstruction::add_data(uint32_t bytes, const void* data) {
    uint32_t extra_words = (bytes + 3) / 4;
    const uint8_t *ptr = (const uint8_t *)data;
    size_t bytes_copied = 0;
    for (uint32_t i = 0; i < extra_words; i++) {
        size_t copy_size = std::min(bytes - bytes_copied, (size_t)4);
        SpvId entry = 0;
        memcpy(&entry, ptr, copy_size);
        bytes_copied += copy_size;
        add_immediate(entry);
        ptr++;
    }
}

void SpvInstruction::add_string(const std::string& str) {
    add_data(str.length() + 1, (const void*)str.c_str());
}

void SpvInstruction::encode(SpvBinary& binary) const {
    user_assert(is_defined()) << "An SpvInstruction must be defined before accessing its properties\n";

    // Count the number of 32-bit words to represent the instruction
    uint32_t word_count = 1; 
    word_count += has_type() ? 1 : 0;
    word_count += has_result() ? 1 : 0;
    word_count += length();

    // Preface the instruction with the format
    // - high 16-bits indicate instruction length (number of 32-bit words)
    // - low 16-bits indicate op code
    binary.push_back(((word_count) << SpvWordCountShift) | contents->op_code);
    if(has_type()) { binary.push_back(contents->type_id); }
    if(has_result()) { binary.push_back(contents->result_id); }
    for(SpvId id : contents->operands) { binary.push_back(id); }
}

// --

template<>
RefCount &ref_count<SpvInstructionContents>(const SpvInstructionContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvInstructionContents>(const SpvInstructionContents *c) {
    delete c;
}

// --

SpvBlock SpvBlock::make(SpvFunction func, SpvId block_id) {
    SpvBlock instance;
    instance.contents = SpvBlockContentsPtr( new SpvBlockContents() );
    instance.contents->parent = func;
    instance.contents->block_id = block_id;
    return instance;
}

void SpvBlock::add_instruction(SpvInstruction inst) {
    user_assert(is_defined()) << "An SpvBlock must be defined before modifying its properties\n";
    inst.set_block(*this);
    contents->instructions.push_back(inst);        
}

void SpvBlock::add_variable(SpvInstruction var) {
    user_assert(is_defined()) << "An SpvBlock must be defined before modifying its properties\n";
    var.set_block(*this);
    contents->instructions.push_back(var);        
}

void SpvBlock::set_function(SpvFunction func) { 
    user_assert(is_defined()) << "An SpvBlock must be defined before modifying its properties\n";
    contents->parent = func; 
}

SpvFunction SpvBlock::function() const { 
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    return contents->parent; 
}

const SpvBlock::Instructions& SpvBlock::instructions () const {
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    return contents->instructions; 
}

const SpvBlock::Variables& SpvBlock::variables () const {
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    return contents->variables; 
}

bool SpvBlock::is_reachable() const { 
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    return contents->reachable; 
}

bool SpvBlock::is_defined() const { 
    return contents.defined();
}

bool SpvBlock::is_terminated() const {
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    switch(contents->instructions.back().op_code()) {
        case SpvOpBranch:
        case SpvOpBranchConditional:
        case SpvOpSwitch:
        case SpvOpKill:
        case SpvOpReturn:
        case SpvOpReturnValue:
        case SpvOpUnreachable:
            return true;
        default:
            return false;
    };
}

SpvId SpvBlock::id() const { 
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    return contents->block_id; 
}

void SpvBlock::encode(SpvBinary& binary) const {
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
    
    // add a label for this block
    SpvInstruction label = SpvLabelInst::make(contents->block_id);
    label.encode(binary);

    // encode all variables
    for(const SpvInstruction& variable : contents->variables) {
        variable.encode(binary);
    }
    // encode all instructions
    for(const SpvInstruction& instruction : contents->instructions) {
        instruction.encode(binary);
    }
}

// --

template<>
RefCount &ref_count<SpvBlockContents>(const SpvBlockContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvBlockContents>(const SpvBlockContents *c) {
    delete c;
}

// --

SpvFunction SpvFunction::make(SpvId func_type_id, SpvId func_id, SpvId return_type_id, uint32_t control_mask = SpvFunctionControlMaskNone) {
    SpvFunction instance;
    instance.contents = SpvFunctionContentsPtr( new SpvFunctionContents() );
    instance.contents->function_id = func_id;
    instance.contents->function_type_id = func_type_id;
    instance.contents->return_type_id = return_type_id;
    instance.contents->control_mask = control_mask;
    instance.contents->declaration = SpvFunctionInst::make(return_type_id, func_id, control_mask, func_type_id);
    return instance;
}

bool SpvFunction::is_defined() const { 
    return contents.defined();
}

void SpvFunction::add_block(SpvBlock block) {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    contents->blocks.push_back(block);        
}

void SpvFunction::add_parameter(SpvInstruction param) {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    contents->parameters.push_back(param);        
}

uint32_t SpvFunction::parameter_count() const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return (uint32_t)contents->parameters.size();
}

SpvBlock SpvFunction::entry_block() const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return contents->blocks.front();        
}

SpvPrecision SpvFunction::return_precision() const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    SpvId return_id = contents->declaration.result_id();
    SpvFunctionContents::PrecisionMap::const_iterator it = contents->precision.find(return_id);
    if(it == contents->precision.end()) {
        return SpvPrecision::SpvFullPrecision;
    } else {
        return contents->precision[return_id];
    }
}

void SpvFunction::set_return_precision(SpvPrecision precision) { 
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    SpvId return_id = contents->declaration.result_id();
    SpvFunctionContents::PrecisionMap::const_iterator it = contents->precision.find(return_id);
    if(it == contents->precision.end()) {
        contents->precision.insert( {return_id, precision });
    } else {
        contents->precision[return_id] = precision;
    }
}

SpvPrecision SpvFunction::parameter_precision(uint32_t index) const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    user_assert(contents->parameters.size() > index) << "Invalid parameter index specified!\n";
    SpvId param_id = contents->parameters[index].result_id();
    SpvFunctionContents::PrecisionMap::const_iterator it = contents->precision.find(param_id);
    if(it == contents->precision.end()) {
        return SpvPrecision::SpvFullPrecision;
    } else {
        return contents->precision[param_id];
    }
}

void SpvFunction::set_module(SpvModule module) { 
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    contents->parent = module; 
}

SpvInstruction SpvFunction::declaration() const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return contents->declaration;
}

SpvModule SpvFunction::module() const { 
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return contents->parent; 
}

SpvId SpvFunction::return_type_id() const { 
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return contents->return_type_id; 
}

SpvId SpvFunction::type_id() const { 
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return contents->function_type_id; 
}

SpvId SpvFunction::id() const { 
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    return contents->function_id; 
}

void SpvFunction::encode(SpvBinary& binary) const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
    contents->declaration.encode(binary);
    for(const SpvInstruction& param : contents->parameters) {
        param.encode(binary);
    }
    for(const SpvBlock& block : contents->blocks) {
        block.encode(binary);
    }        

    SpvInstruction inst = SpvInstruction::make(SpvOpFunctionEnd);
    inst.encode(binary);
}

// --

template<>
RefCount &ref_count<SpvFunctionContents>(const SpvFunctionContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvFunctionContents>(const SpvFunctionContents *c) {
    delete c;
}

// --

SpvModule SpvModule::make(SpvId module_id, 
    SpvSourceLanguage source_language,
    SpvAddressingModel addressing_model,
    SpvMemoryModel memory_model) {
    SpvModule instance;
    instance.contents = SpvModuleContentsPtr( new SpvModuleContents() );
    instance.contents->module_id = module_id;
    instance.contents->source_language = source_language;
    instance.contents->addressing_model = addressing_model;
    instance.contents->memory_model = memory_model;
    return instance;
}

bool SpvModule::is_defined() const { 
    return contents.defined();
}

void SpvModule::add_debug(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->debug.push_back(val);
}   

void SpvModule::add_annotation(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->annotations.push_back(val);
}   

void SpvModule::add_type(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->types.push_back(val);
}

void SpvModule::add_constant(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->constants.push_back(val);
}

void SpvModule::add_global(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->globals.push_back(val);
}

void SpvModule::add_execution_mode(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->execution_modes.push_back(val);
}

void SpvModule::add_instruction(SpvInstruction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->instructions.push_back(val);
}

void SpvModule::add_function(SpvFunction val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    val.set_module(*this);
    contents->functions.push_back(val);
}

void SpvModule::add_entry_point(const std::string& name, SpvInstruction inst) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->entry_points[name] = inst;
}

void SpvModule::set_source_language(SpvSourceLanguage val){
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->source_language = val;
}

void SpvModule::set_addressing_model(SpvAddressingModel val){
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->addressing_model = val;
}

void SpvModule::set_memory_model(SpvMemoryModel val) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    contents->memory_model = val;
}

SpvSourceLanguage SpvModule::source_language() const{
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    return contents->source_language;
}

SpvAddressingModel SpvModule::addressing_model() const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    return contents->addressing_model;
}

const SpvModule::Instructions& SpvModule::execution_modes() const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    return contents->execution_modes;
}

SpvMemoryModel SpvModule::memory_model() const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    return contents->memory_model;
}

SpvInstruction SpvModule::entry_point(const std::string& name) const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    if(contents->entry_points.find(name) != contents->entry_points.end()) {
        return contents->entry_points[name]; 
    }
    else
    {
        SpvInstruction noop = SpvInstruction::make(SpvOpNop);
        return noop;
    }
}   

void SpvModule::require_extension(const std::string& extension) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    if(contents->extensions.find(extension) == contents->extensions.end()) {
        contents->extensions.insert(extension);
    }
}

bool SpvModule::is_extension_required(const std::string& extension) const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    if(contents->extensions.find(extension) != contents->extensions.end()) {
        return true;
    }
    return false;
}

void SpvModule::require_capability(SpvCapability capability) {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    if(contents->capabilities.find(capability) == contents->capabilities.end()) {
        contents->capabilities.insert(capability);
    }
}

bool SpvModule::is_capability_required(SpvCapability capability) const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    if(contents->capabilities.find(capability) != contents->capabilities.end()) {
        return true;
    }
    return false;
}

SpvModule::EntryPointNames SpvModule::entry_point_names() const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    SpvModule::EntryPointNames entry_point_names(contents->entry_points.size());
    for(const SpvModuleContents::EntryPoints::value_type& v : contents->entry_points) {
        entry_point_names.push_back(v.first);
    }
    return entry_point_names;
}

SpvId SpvModule::id() const { 
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
    return contents->module_id; 
}

void SpvModule::encode(SpvBinary& binary) const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";

    // 0. Encode the header
    binary.push_back(SpvMagicNumber);
    binary.push_back(SpvVersion);
    binary.push_back(contents->source_language);
    binary.push_back(0);  // Bound placeholder (aka last id used)
    binary.push_back(0);  // Reserved for schema.

    // 1. Capabilities
    for(const SpvCapability& capability : contents->capabilities) {
        SpvInstruction inst = SpvInstruction::make(SpvOpCapability);
        inst.add_immediate(capability);
        inst.encode(binary);
    }

    // 2. Extensions
    for(const std::string& extension : contents->extensions) {
        SpvInstruction inst = SpvInstruction::make(SpvOpExtension);
        inst.add_string(extension);
        inst.encode(binary);
    }

    // 3. Extended Instruction Set Imports
    for(const std::string& import : contents->imports) {
        SpvInstruction inst = SpvInstruction::make(SpvOpExtInstImport);
        inst.add_string(import);
        inst.encode(binary);
    }

    // 4. Memory Model
    SpvInstruction memory_model_inst = SpvMemoryModelInst::make(contents->addressing_model, contents->memory_model);
    memory_model_inst.encode(binary);

    // 5. Entry Points
    for(const SpvModuleContents::EntryPoints::value_type& value : contents->entry_points) {
        SpvInstruction entry_point_inst = value.second;
        entry_point_inst.encode(binary);        
    }

    // 6. Execution Modes
    for(const SpvInstruction& inst : contents->execution_modes) {
        inst.encode(binary);        
    }

    // 7. Debug 
    for(const SpvInstruction& inst : contents->debug) {
        inst.encode(binary);        
    }

    // 8. Annotations 
    for(const SpvInstruction& inst : contents->annotations) {
        inst.encode(binary);        
    }

    // 9a. Type Declarations 
    for(const SpvInstruction& inst : contents->types) {
        inst.encode(binary);        
    }

    // 9b. Constants 
    for(const SpvInstruction& inst : contents->constants) {
        inst.encode(binary);        
    }

    // 9c. Globals 
    for(const SpvInstruction& inst : contents->globals) {
        inst.encode(binary);        
    }

    // 10-11. Function Declarations & Definitions 
    for(const SpvFunction& func : contents->functions) {
        func.encode(binary);
    }
}

// --

template<>
RefCount &ref_count<SpvModuleContents>(const SpvModuleContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvModuleContents>(const SpvModuleContents *c) {
    delete c;
}

// --

SpvBuilder::SpvBuilder() {
    SpvId module_id = declare_id(SpvModuleId);
    module = SpvModule::make(module_id);
}
    
SpvId SpvBuilder::reserve_id(SpvKind kind) {
    return declare_id(kind);
}

SpvId SpvBuilder::declare_id(SpvKind kind) {
    // use type-agnostic non-overlapping increasing ids
    SpvId item_id = kind_map.size() + 1;
    kind_map[item_id] = kind;
    return item_id;
}

SpvKind SpvBuilder::kind_of(SpvId item_id) {
    KindMap::const_iterator it = kind_map.find(item_id);
    if (it != kind_map.end()) {
        return SpvInvalidItem;
    }
    return it->second;
}

void SpvBuilder::encode(SpvBinary& binary) const {
    // Encode the module
    module.encode(binary);
}

SpvId SpvBuilder::map_type(const Type& type, uint32_t array_size) {
    SpvId type_id = lookup_type(type, array_size);
    if(type_id == SpvInvalidId) {
        type_id = declare_type(type, array_size);
    }
    return type_id;
}

SpvId SpvBuilder::map_pointer_type(const Type& type, SpvStorageClass storage_class) {
    SpvId type_id = lookup_pointer_type(type, storage_class);
    if(type_id == SpvInvalidId) {
        type_id = declare_pointer_type(type, storage_class);
    }
    return type_id;
}


SpvId SpvBuilder::map_function_type(SpvId return_type, const ParamTypes& param_types) {
    SpvId type_id = lookup_function_type(return_type, param_types);
    if(type_id == SpvInvalidId) {
        type_id = declare_function_type(return_type, param_types);
    }
    return type_id;
}

SpvId SpvBuilder::map_constant(const Type& type, const void* data) {
    SpvId result_id = lookup_constant(type, data);
    if(result_id == SpvInvalidId) {
        result_id = declare_constant(type, data);
    }
    return result_id;
}

void SpvBuilder::add_entry_point(const std::string& name, 
    SpvId func_id, SpvExecutionModel exec_model,
    const Variables& variables) {
    
    SpvInstruction inst = SpvEntryPointInst::make(exec_model, func_id, name, variables);
    module.add_entry_point(name, inst);
}

SpvFunction SpvBuilder::add_function(SpvId return_type_id, const ParamTypes& param_types) {
    SpvId func_id = declare_id(SpvFunctionId);
    SpvId func_type_id = map_function_type(return_type_id, param_types);
    SpvFunction func = SpvFunction::make(func_type_id, func_id, return_type_id);
    for(SpvId param_type_id : param_types) {
        SpvId param_id = declare_id(SpvParameterId);
        SpvInstruction param_inst = SpvFunctionParameterInst::make(param_type_id, param_id);
        func.add_parameter(param_inst);
        map_instruction(param_inst);
    }
    SpvId block_id = declare_id(SpvBlockId);
    SpvBlock entry_block = SpvBlock::make(func, block_id);
    func.add_block(entry_block);
    module.add_function(func);  
    function_map[func_id] = func;
    map_instruction(func.declaration());
    return func;
}

SpvId SpvBuilder::add_global_variable(SpvId type_id, uint32 storage_class, SpvId init_id) {
    SpvId var_id = reserve_id(SpvVariableId);
    module.add_global( SpvVariableInst::make(type_id, var_id, storage_class, init_id) );
    return var_id;
}

SpvId SpvBuilder::add_variable(SpvId type_id, uint32 storage_class, SpvId init_id) {
    SpvId var_id = reserve_id(SpvVariableId);
    current_block().add_variable( SpvVariableInst::make(type_id, var_id, storage_class, init_id) );
    return var_id;
}

SpvId SpvBuilder::add_annotationadd_annotation( SpvId target_id, SpvDecoration decoration_type, const Literals& literals) {
    SpvId annotation_id = reserve_id(SpvResultId);
    current_block().add_annotation( SpvVariableInst::make(type_id, var_id, storage_class, init_id) );
    return var_id;
}

SpvId SpvBuilder::add_annotation( SpvId target_id, SpvDecoration decoration_type, const Literals& literals);
    SpvInstruction inst = SpvDecorateInst::make( target_id, decoration_type, literals );
    builder.current_module().add_annotation( inst );
}

SpvId SpvBuilder::add_struct_annotation( SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals& literals) {
    SpvInstruction inst = SpvMemberDecorateInst::make( struct_type_id, member_index, decoration_type, literals );
    builder.current_module().add_annotation( inst );
}

void SpvBuilder::add_execution_mode_local_size(SpvId func_id, 
    uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z){

    wg_size_x = std::max(wg_size_x, (uint32_t)1);
    wg_size_y = std::max(wg_size_y, (uint32_t)1);
    wg_size_z = std::max(wg_size_z, (uint32_t)1);

    SpvInstruction exec_mode_inst = SpvExecutionModeLocalSizeInst::make(func_id, wg_size_x, wg_size_y, wg_size_z);
    module.add_execution_mode(exec_mode_inst);
}

void SpvBuilder::enter_block(SpvBlock block) {
    block_stack.push(block);
}

SpvBlock SpvBuilder::current_block() const {
    SpvBlock block;
    if(block_stack.size()) {
        block = block_stack.top();
    }
    return block;
}

SpvBlock SpvBuilder::leave_block() {
    SpvBlock block;
    if(block_stack.size()) {
        block = block_stack.top();
        block_stack.pop();
    }
    return block;
}

SpvFunction SpvBuilder::lookup_function(SpvId func_id) {
    SpvFunction func;
    FunctionMap::const_iterator it = function_map.find(func_id);
    if(it != function_map.end()) {
        func = it->second;
    }
    return func;
}

void SpvBuilder::enter_function(SpvFunction func) {
    function_stack.push(func);
}

SpvFunction SpvBuilder::current_function() const {
    SpvFunction func;
    if(function_stack.size()) {
        func = function_stack.top();
    }
    return func;
}

SpvFunction SpvBuilder::leave_function() {
    SpvFunction func;
    if(function_stack.size()) {
        func = function_stack.top();
        function_stack.pop();
    }        
    return func;
}

void SpvBuilder::set_current_id(SpvId val) {
    scope_id = val;
}

SpvId SpvBuilder::current_id() const {
    return scope_id;
}

SpvModule SpvBuilder::current_module() const {
    return module;
}

void SpvBuilder::require_capability(SpvCapability capability) {
    if(!module.is_capability_required(capability)) {
        module.require_capability(capability);
    }
}

bool SpvBuilder::is_capability_required(SpvCapability capability) const {
    return module.is_capability_required(capability);
}

void SpvBuilder::require_extension(const std::string& extension) {
    if(!module.is_extension_required(extension)) {
        module.require_extension(extension);
    }
}

bool SpvBuilder::is_extension_required(const std::string& extension) const {
    return module.is_extension_required(extension);
}

SpvBuilder::TypeKey SpvBuilder::hash_type(const Type& type, uint32_t array_size) const {
    TypeKey key(4 + sizeof(uint32_t), ' ');
    key[0] = type.code();
    key[1] = type.bits();
    key[2] = type.lanes() & 0xff;
    key[3] = (type.lanes() >> 8) & 0xff;
    for (int i = 0; i < sizeof(uint32_t); i++) {
        key[i + 4] = (array_size & 0xff);
        array_size >>= 8;
    }
    return key;
}

SpvId SpvBuilder::lookup_type(const Type& type, uint32_t array_size) const {
    SpvBuilder::TypeKey type_key = hash_type(type, array_size);
    TypeMap::const_iterator it = type_map.find(type_key);
    if(it == type_map.end()) {
        return SpvInvalidId;
    }
    return it->second;
}

SpvId SpvBuilder::declare_type(const Type& type, uint32_t array_size) {
    SpvBuilder::TypeKey type_key = hash_type(type, array_size);
    TypeMap::const_iterator it = type_map.find(type_key);
    if (it != type_map.end()) {
        return it->second;
    }

    if(array_size > 1) {
        SpvId array_type_id = declare_id(SpvArrayTypeId);
        SpvId element_type_id = declare_type(type, 1);
        SpvInstruction inst = SpvTypeArrayInst::make(array_type_id, element_type_id, array_size);
        module.add_type(inst);
        type_map[type_key] = array_type_id;
        return array_type_id;
    }

    SpvId type_id = SpvInvalidId;
    if (type.is_vector()) {
        type_id = declare_id(SpvVectorTypeId);
        SpvId element_type_id = declare_type(type.with_lanes(1));
        SpvInstruction inst = SpvTypeVectorInst::make(type_id, element_type_id, type.lanes());
        module.add_type(inst);
    } else {
        if (type.is_handle()) {
            type_id = declare_id(SpvVoidTypeId);
            SpvInstruction inst = SpvTypeVoidInst::make(type_id);
            module.add_type(inst);
        } else if (type.is_bool()) {
            type_id = declare_id(SpvBoolTypeId);
            SpvInstruction inst = SpvTypeBoolInst::make(type_id);
            module.add_type(inst);
        } else if (type.is_float()) {
            type_id = declare_id(SpvFloatTypeId);
            SpvInstruction inst = SpvTypeFloatInst::make(type_id, type.bits());
            module.add_type(inst);
        } else if (type.is_int_or_uint()) {
            type_id = declare_id(SpvIntTypeId);
            SpvId signedness = type.is_uint() ? 0 : 1;
            SpvInstruction inst = SpvTypeIntInst::make(type_id, type.bits(), signedness);
            module.add_type(inst);
        } else {
            internal_error << "SPIRV: Unsupported type " << type << "\n";
        }
    }

    type_map[type_key] = type_id;
    return type_id;
}

SpvBuilder::TypeKey SpvBuilder::hash_struct(const StructMemberTypes& member_type_ids) const {
    TypeKey key(member_type_ids.size() * sizeof(SpvId), ' ');
    uint32_t index = 0;
    for (SpvId type_id : member_type_ids) {
        for (int i = 0; i < sizeof(uint32_t); i++, index++) {
            key[index] = (type_id & 0xff);
            type_id >>= 8;
        }
   }
   return key;
}

SpvId SpvBuilder::lookup_struct(const StructMemberTypes& member_type_ids) const {
    TypeKey key = hash_struct(member_type_ids);
    TypeMap::const_iterator it = struct_map.find(key);
    if (it != struct_map.end()) {
        return it->second;
    }    
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_struct(const StructMemberTypes& member_type_ids) {
    TypeKey key = hash_struct(member_type_ids);
    TypeMap::const_iterator it = struct_map.find(key);
    if (it != struct_map.end()) {
        return it->second;
    }    

    SpvId struct_type_id = declare_id(SpvStructTypeId);
    SpvInstruction inst = SpvTypeStructInst::make(struct_type_id, member_type_ids);
    module.add_type(inst);
    struct_map[key] = struct_type_id;
    return struct_type_id;
}

SpvBuilder::PointerTypeKey SpvBuilder::hash_pointer_type(const Type& type, SpvStorageClass storage_class) const {
    SpvId base_type_id = map_type(type);
    return std::make_pair(base_type_id, storage_class);
}

SpvBuilder::PointerTypeKey SpvBuilder::hash_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const {
    return std::make_pair(base_type_id, storage_class);
}

SpvId SpvBuilder::lookup_pointer_type(const Type& type, SpvStorageClass storage_class) const {
    SpvId base_type_id = map_type(type);
    return lookup_pointer_type(base_type_id, storage_class);
}

SpvId SpvBuilder::lookup_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const {
    PointerTypeKey key = hash_pointer_type(base_type_id, storage_class);
    PointerTypeMap::const_iterator it = pointer_type_map.find(key);
    if (it != pointer_type_map.end()) {
        return it->second;
    }    
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_pointer_type(const Type& type, SpvStorageClass storage_class) {
    SpvId base_type_id = map_type(type);
    return declare_pointer_type(base_type_id, storage_class);
}

SpvId SpvBuilder::declare_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) {
    PointerTypeKey key = hash_pointer_type(base_type_id, storage_class);
    PointerTypeMap::const_iterator it = pointer_type_map.find(key);
    if (it != pointer_type_map.end()) {
        return it->second;
    }

    SpvId pointer_type_id = declare_id(SpvPointerTypeId);
    SpvInstruction inst = SpvTypePointerInst::make(pointer_type_id, storage_class, base_type_id);
    module.add_type(inst);
    pointer_type_map[key] = pointer_type_id;
    return pointer_type_id;
}

SpvBuilder::ConstantKey SpvBuilder::hash_constant(const Type& type, const void* data) const {
    ConstantKey key(type.bytes() + 4, ' ');
    key[0] = type.code();
    key[1] = type.bits();
    key[2] = type.lanes() & 0xff;
    key[3] = (type.lanes() >> 8) & 0xff;
    const char *data_char = (const char *)data;
    for (int i = 0; i < type.bytes(); i++) {
        key[i + 4] = data_char[i];
    }
    return key;
}

SpvBuilder::ConstantKey SpvBuilder::hash_bool_constant(bool value) const {
    Type type = Bool();
    bool data = value;
    return hash_constant(type, &data);
}

SpvBuilder::ConstantKey SpvBuilder::hash_null_constant(const Type& type) const {
    ConstantKey key(type.bytes() + 4, ' ');
    key[0] = type.code();
    key[1] = type.bits();
    key[2] = type.lanes() & 0xff;
    key[3] = (type.lanes() >> 8) & 0xff;
    for (int i = 0; i < type.bytes(); i++) {
        key[i + 4] = 0;
    }
    return key;
}

SpvId SpvBuilder::lookup_null_constant(const Type& type) const {
    ConstantKey key = hash_null_constant(type);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }    
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_null_constant(const Type& type) {
    ConstantKey key = hash_null_constant(type);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    SpvId result_id = declare_id(SpvConstantId);
    SpvId type_id = declare_type(type);
    SpvInstruction inst = SpvConstantNullInst::make(result_id, type_id);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_bool_constant(bool value) {
    const std::string key = hash_bool_constant(value);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    debug(3) << "declare_bool_constant for " << value << "\n";

    Type type = Bool();
    SpvId result_id = declare_id(SpvBoolConstantId);
    SpvId type_id = declare_type(type);
    SpvInstruction inst = SpvConstantBoolInst::make(result_id, type_id, value);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_scalar_constant(const Type& scalar_type, const void* data) {
    if(scalar_type.lanes() != 1) {
        internal_error << "SPIRV: Invalid type provided for scalar constant!" << scalar_type << "\n";
        return SpvInvalidId;
    }

    const std::string constant_key = hash_constant(scalar_type, data);
    ConstantMap::const_iterator it = constant_map.find(constant_key);
    if (it != constant_map.end()) {
        return it->second;
    }

    if (scalar_type.is_bool() && data) {
        bool value = *reinterpret_cast<const bool*>(data);
        return declare_bool_constant(value);
    }

    debug(3) << "declare_scalar_constant for type " << scalar_type << "\n";

    SpvId result_id = SpvInvalidId;
    if (scalar_type.is_float()) {
        result_id = declare_id(SpvFloatConstantId);
    } else if (scalar_type.is_bool()) {
        result_id = declare_id(SpvBoolConstantId);
    } else if (scalar_type.is_int_or_uint()) {
        result_id = declare_id(SpvIntConstantId);
    } else {
        internal_error << "SPIRV: Unsupported type:" << scalar_type << "\n";
        return SpvInvalidId;
    }

    SpvId type_id = declare_type(scalar_type);
    SpvInstruction inst = SpvConstantInst::make(result_id, type_id, scalar_type.bytes(), data);
    module.add_constant(inst);
    constant_map[constant_key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_vector_constant(const Type& type, const void* data) {
    if(type.lanes() == 1) {
        internal_error << "SPIRV: Invalid type provided for vector constant!" << type << "\n";
        return SpvInvalidId;
    }

    const std::string key = hash_constant(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    Type scalar_type = type.with_lanes(1);
    std::vector<SpvId> components;
    if (scalar_type.is_float()) {
        if(type.bits() == 64) {
            const double* values = (const double*)data;
            for(int c = 0; c < type.lanes(); c++) {
                const double* entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void*)entry);
                components.push_back(scalar_id);
            }
        } else {
            const float* values = (const float*)data;
            for(int c = 0; c < type.lanes(); c++) {
                const float* entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void*)entry);
                components.push_back(scalar_id);
            }
        }
    } else if (scalar_type.is_bool()) {
        const bool* values = (const bool*)data;
        for(int c = 0; c < type.lanes(); c++) {
            const bool* entry = &(values[c]);
            SpvId scalar_id = declare_scalar_constant(scalar_type, (const void*)entry);
            components.push_back(scalar_id);
        }
    } else if (scalar_type.is_int_or_uint()) {
        if(type.bits() == 64) {
            const uint64_t* values = (const uint64_t*)data;
            for(int c = 0; c < type.lanes(); c++) {
                const uint64_t* entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void*)entry);
                components.push_back(scalar_id);
            }
        } else {
            const uint32_t* values = (const uint32_t*)data;
            for(int c = 0; c < type.lanes(); c++) {
                const uint32_t* entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void*)entry);
                components.push_back(scalar_id);
            }
        }
    } else {
        internal_error << "SPIRV: Unsupported type:" << type << "\n";
        return SpvInvalidId;
    }

    SpvId result_id = declare_id(SpvCompositeConstantId);
    SpvId type_id = declare_type(type);
    SpvInstruction inst = SpvConstantCompositeInst::make(result_id, type_id, components);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::lookup_constant(const Type& type, const void* data) const {
    ConstantKey key = hash_constant(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }    
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_constant(const Type& type, const void* data) {

    const std::string key = hash_constant(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    debug(3) << "declare_constant for type " << type << "\n";
    if(type.lanes() == 1) {
        return declare_scalar_constant(type, data);
    } else {
        return declare_vector_constant(type, data);
    }
}

SpvId SpvBuilder::declare_access_chain(SpvId ptr_type_id, SpvId base_id, SpvId element_id, const Indices& indices ) {
    SpvId access_chain_id = declare_id(SpvAccessChainId);
    append( SpvInBoundsAccessChainInst::make(ptr_type_id, access_chain_id, base_id, element_id, indices) );
    return access_chain_id;
}

SpvId SpvBuilder::map_instruction(SpvInstruction inst) {
    const SpvId key = inst.result_id();
    if(instruction_map.find(key) == instruction_map.end()) {
        instruction_map.insert({key, inst});
    } else {
        instruction_map[key] = inst;
    }
    return key;
}

SpvInstruction SpvBuilder::lookup_instruction(SpvId result_id) const {
    InstructionMap::const_iterator it = instruction_map.find(result_id);
    if(it == instruction_map.end()) {
        return SpvInstruction();
    }
    return it->second;
}

SpvBuilder::FunctionTypeKey SpvBuilder::hash_function_type(SpvId return_type_id, const ParamTypes& param_type_ids) const {
    TypeKey key((1 + param_type_ids.size()) * sizeof(SpvId), ' ');

    uint32_t index = 0;
    for (int i = 0; i < sizeof(uint32_t); i++, index++) {
        key[index] = (return_type_id & 0xff);
        return_type_id >>= 8;
    }
    for (SpvId type_id : param_type_ids) {
        for (int i = 0; i < sizeof(uint32_t); i++, index++) {
            key[index] = (type_id & 0xff);
            type_id >>= 8;
        }
   }
   return key;
}

SpvId SpvBuilder::lookup_function_type(SpvId return_type_id, const ParamTypes& param_type_ids) const {
    FunctionTypeKey key = hash_function_type(return_type_id, param_type_ids);
    FunctionTypeMap::const_iterator it = function_type_map.find(key);
    if (it != function_type_map.end()) {
        return it->second;
    }    
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_function_type(SpvId return_type_id, const ParamTypes& param_type_ids) {
    FunctionTypeKey func_type_key = hash_function_type(return_type_id, param_type_ids);
    FunctionTypeMap::const_iterator it = function_type_map.find(func_type_key);
    if (it != function_type_map.end()) {
        return it->second;
    }

    SpvId function_type_id = declare_id(SpvFunctionTypeId);
    SpvInstruction inst = SpvTypeFunctionInst::make(function_type_id, return_type_id, param_type_ids);
    module.add_type(inst);
    function_type_map[func_type_key] = function_type_id;
    return function_type_id;
}

SpvId SpvBuilder::declare_runtime_array(SpvId base_type_id) {
    SpvId runtime_array_id = declare_id(SpvRuntimeArrayId);
    SpvInstruction inst = SpvTypeRuntimeArrayInst::make();
    module.add_type(inst);
    return runtime_array_id;
}

void SpvBuilder::append(SpvInstruction inst) {
    if(block_stack.size()) {
        current_block().add_instruction(inst);
    } else {
        internal_error << "SPIRV: Current block undefined! Unable to append!\n";
    }
}

// --

}} // namespace: Halide::Internal

#endif // HALIDE_SPIRV_IR_H