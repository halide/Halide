#ifndef HALIDE_SPIRV_IR_H
#define HALIDE_SPIRV_IR_H

/** \file
 * Defines methods for constructing and encoding instructions into the Khronos
 * format specification known as the Standard Portable Intermediate Representation
 * for Vulkan (SPIR-V). These class interfaces adopt Halide's conventions for its
 * own IR, but is implemented as a stand-alone optional component that can be
 * enabled as required for certain runtimes (eg Vulkan).
 *
 * NOTE: This file is only used internally for CodeGen! *DO NOT* add this file
 * to the list of exported Halide headers in the src/CMakeFiles.txt or the
 * top level Makefile.
 */
#ifdef WITH_SPIRV

#include <map>
#include <set>
#include <stack>
#include <unordered_map>
#include <vector>

#include "IntrusivePtr.h"
#include "Type.h"

#include <spirv/1.0/spirv.h>  // Use v1.0 spec as the minimal viable version (for maximum compatiblity)

namespace Halide {
namespace Internal {

/** Precision requirment for return values */
enum SpvPrecision {
    SpvFullPrecision,
    SpvRelaxedPrecision,
};

/** Specific types of predefined constants */
enum SpvPredefinedConstant {
    SpvNullConstant,
    SpvTrueConstant,
    SpvFalseConstant,
};

/** Specific types of SPIR-V object ids */
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
    SpvAccessChainId,
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

/** SPIR-V requires all IDs to be 32-bit unsigned integers */
using SpvId = uint32_t;
using SpvBinary = std::vector<uint32_t>;

static constexpr SpvId SpvInvalidId = SpvId(-1);
static constexpr SpvId SpvNoResult = 0;
static constexpr SpvId SpvNoType = 0;

/** Pre-declarations for SPIR-V IR classes */
class SpvModule;
class SpvFunction;
class SpvBlock;
class SpvInstruction;
class SpvBuilder;

/** Pre-declarations for SPIR-V IR data structures */
struct SpvModuleContents;
struct SpvFunctionContents;
struct SpvBlockContents;
struct SpvInstructionContents;

/** Intrusive pointer types for SPIR-V IR data */
using SpvModuleContentsPtr = IntrusivePtr<SpvModuleContents>;
using SpvFunctionContentsPtr = IntrusivePtr<SpvFunctionContents>;
using SpvBlockContentsPtr = IntrusivePtr<SpvBlockContents>;
using SpvInstructionContentsPtr = IntrusivePtr<SpvInstructionContents>;

/** General interface for representing a SPIR-V Instruction */
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
    void add_data(uint32_t bytes, const void *data);
    void add_string(const std::string &str);

    SpvId result_id() const;
    SpvId type_id() const;
    SpvOp op_code() const;
    SpvId operand(uint32_t index);

    bool has_type() const;
    bool has_result() const;
    bool is_defined() const;
    bool is_immediate(uint32_t index) const;
    uint32_t length() const;
    SpvBlock block() const;
    void check_defined() const;

    void encode(SpvBinary &binary) const;

    static SpvInstruction make(SpvOp op_code);

protected:
    SpvInstructionContentsPtr contents;
};

/** General interface for representing a SPIR-V Block */
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
    const Instructions &instructions() const;
    const Variables &variables() const;
    SpvFunction function() const;
    bool is_reachable() const;
    bool is_terminated() const;
    bool is_defined() const;
    SpvId id() const;
    void check_defined() const;

    void encode(SpvBinary &binary) const;

    static SpvBlock make(SpvFunction func, SpvId id);

protected:
    SpvBlockContentsPtr contents;
};

/** General interface for representing a SPIR-V Function */
class SpvFunction {
public:
    SpvFunction() = default;
    ~SpvFunction() = default;

    SpvFunction(const SpvFunction &) = default;
    SpvFunction &operator=(const SpvFunction &) = default;
    SpvFunction(SpvFunction &&) = default;
    SpvFunction &operator=(SpvFunction &&) = default;

    void add_block(const SpvBlock &block);
    void add_parameter(const SpvInstruction &param);
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
    void check_defined() const;

    void encode(SpvBinary &binary) const;

    static SpvFunction make(SpvId func_id, SpvId func_type_id, SpvId return_type_id, uint32_t control_mask = SpvFunctionControlMaskNone);

protected:
    SpvFunctionContentsPtr contents;
};

/** General interface for representing a SPIR-V code module */
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

    void add_debug(const SpvInstruction &val);
    void add_annotation(const SpvInstruction &val);
    void add_type(const SpvInstruction &val);
    void add_constant(const SpvInstruction &val);
    void add_global(const SpvInstruction &val);
    void add_execution_mode(const SpvInstruction &val);
    void add_function(SpvFunction val);
    void add_instruction(const SpvInstruction &val);
    void add_entry_point(const std::string &name, SpvInstruction entry_point);

    void require_capability(SpvCapability val);
    void require_extension(const std::string &val);

    void set_source_language(SpvSourceLanguage val);
    void set_addressing_model(SpvAddressingModel val);
    void set_memory_model(SpvMemoryModel val);
    SpvSourceLanguage source_language() const;
    SpvAddressingModel addressing_model() const;
    SpvMemoryModel memory_model() const;
    SpvInstruction entry_point(const std::string &name) const;
    EntryPointNames entry_point_names() const;
    const Instructions &execution_modes() const;
    SpvModule module() const;

    bool is_capability_required(SpvCapability val) const;
    bool is_extension_required(const std::string &val) const;
    bool is_defined() const;
    SpvId id() const;
    void check_defined() const;

    void encode(SpvBinary &binary) const;

    static SpvModule make(SpvId module_id,
                          SpvSourceLanguage source_language = SpvSourceLanguageUnknown,
                          SpvAddressingModel addressing_model = SpvAddressingModelLogical,
                          SpvMemoryModel memory_model = SpvMemoryModelSimple);

protected:
    SpvModuleContentsPtr contents;
};

/** Builder interface for constructing a SPIR-V code module and
 * all associated types, declarations, blocks, functions &
 * instructions */
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

    SpvId map_type(const Type &type, uint32_t array_size = 1);
    SpvId map_pointer_type(const Type &type, SpvStorageClass storage_class);
    SpvId map_pointer_type(SpvId type_id, SpvStorageClass storage_class);
    SpvId map_constant(const Type &type, const void *data);
    SpvId map_null_constant(const Type &type);
    SpvId map_bool_constant(bool value);
    SpvId map_function_type(SpvId return_type, const ParamTypes &param_types = {});

    SpvId declare_type(const Type &type, uint32_t array_size = 1);
    SpvId declare_struct(const StructMemberTypes &member_types);
    SpvId declare_runtime_array(SpvId base_type_id);
    SpvId declare_pointer_type(const Type &type, SpvStorageClass storage_class);
    SpvId declare_pointer_type(SpvId base_type_id, SpvStorageClass storage_class);
    SpvId declare_constant(const Type &type, const void *data);
    SpvId declare_null_constant(const Type &type);
    SpvId declare_bool_constant(bool value);
    SpvId declare_string_constant(const std::string &str);
    SpvId declare_scalar_constant(const Type &type, const void *data);
    SpvId declare_vector_constant(const Type &type, const void *data);
    SpvId declare_access_chain(SpvId ptr_type_id, SpvId base_id, SpvId element_id, const Indices &indices);
    SpvId declare_function_type(SpvId return_type_id, const ParamTypes &param_type_ids);

    SpvFunction add_function(SpvId return_type, const ParamTypes &param_types = {});
    SpvId add_instruction(SpvInstruction val);
    void add_annotation(SpvId target_id, SpvDecoration decoration_type, const Literals &literals = {});
    void add_struct_annotation(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals &literals = {});

    SpvId add_variable(SpvId type_id, uint32_t storage_class, SpvId initializer_id = SpvInvalidId);
    SpvId add_global_variable(SpvId type_id, uint32_t storage_class, SpvId initializer_id = SpvInvalidId);

    SpvId map_struct(const StructMemberTypes &member_types);

    void add_entry_point(const std::string &name,
                         SpvId func_id, SpvExecutionModel exec_model,
                         const Variables &variables = {});

    void add_execution_mode_local_size(SpvId entry_point_id, uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z);

    void set_source_language(SpvSourceLanguage val);
    void set_addressing_model(SpvAddressingModel val);
    void set_memory_model(SpvMemoryModel val);

    SpvSourceLanguage source_language() const;
    SpvAddressingModel addressing_model() const;
    SpvMemoryModel memory_model() const;

    void enter_block(const SpvBlock &block);
    SpvBlock current_block() const;
    SpvBlock leave_block();

    void enter_function(const SpvFunction &func);
    SpvFunction lookup_function(SpvId func_id) const;
    SpvFunction current_function() const;
    SpvFunction leave_function();

    void set_current_id(SpvId id);
    SpvId current_id() const;

    SpvModule current_module() const;

    void require_extension(const std::string &extension);
    void require_capability(SpvCapability);

    bool is_extension_required(const std::string &extension) const;
    bool is_capability_required(SpvCapability) const;

    void append(SpvInstruction inst);
    void encode(SpvBinary &binary) const;

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

    TypeKey make_type_key(const Type &type, uint32_t array_size = 1) const;
    SpvId lookup_type(const Type &type, uint32_t array_size = 1) const;

    TypeKey make_struct_type_key(const StructMemberTypes &member_types) const;
    SpvId lookup_struct(const StructMemberTypes &member_types) const;

    PointerTypeKey make_pointer_type_key(const Type &type, SpvStorageClass storage_class) const;
    SpvId lookup_pointer_type(const Type &type, SpvStorageClass storage_class) const;

    PointerTypeKey make_pointer_type_key(SpvId base_type_id, SpvStorageClass storage_class) const;
    SpvId lookup_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const;

    ConstantKey make_bool_constant_key(bool value) const;

    ConstantKey make_constant_key(const Type &type, const void *data) const;
    SpvId lookup_constant(const Type &type, const void *data) const;

    ConstantKey make_null_constant_key(const Type &type) const;
    SpvId lookup_null_constant(const Type &type) const;

    SpvId map_instruction(const SpvInstruction &inst);
    SpvInstruction lookup_instruction(SpvId result_id) const;
    bool has_instruction(SpvId inst) const;

    FunctionTypeKey make_function_type_key(SpvId return_type_id, const ParamTypes &param_type_ids) const;
    SpvId lookup_function_type(SpvId return_type_id, const ParamTypes &param_type_ids) const;

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

/** Factory interface for constructing specific SPIR-V instructions */
struct SpvFactory {
    using Indices = std::vector<uint32_t>;
    using Literals = std::vector<uint32_t>;
    using BranchWeights = std::vector<uint32_t>;
    using Components = std::vector<SpvId>;
    using ParamTypes = std::vector<SpvId>;
    using MemberTypeIds = std::vector<SpvId>;
    using Variables = std::vector<SpvId>;
    using VariableBlockIdPair = std::pair<SpvId, SpvId>;  // (Variable Id, Block Id)
    using BlockVariables = std::vector<VariableBlockIdPair>;

    static SpvInstruction capability(const SpvCapability &capability);
    static SpvInstruction extension(const std::string &extension);
    static SpvInstruction import(const std::string &import);
    static SpvInstruction label(SpvId result_id);
    static SpvInstruction decorate(SpvId target_id, SpvDecoration decoration_type, const Literals &literals = {});
    static SpvInstruction decorate_member(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals &literals = {});
    static SpvInstruction void_type(SpvId void_type_id);
    static SpvInstruction bool_type(SpvId bool_type_id);
    static SpvInstruction integer_type(SpvId int_type_id, uint32_t bits, uint32_t signedness);
    static SpvInstruction float_type(SpvId float_type_id, uint32_t bits);
    static SpvInstruction vector_type(SpvId vector_type_id, SpvId element_type_id, uint32_t vector_size);
    static SpvInstruction array_type(SpvId array_type_id, SpvId element_type_id, uint32_t array_size);
    static SpvInstruction struct_type(SpvId result_id, const MemberTypeIds &member_type_ids);
    static SpvInstruction runtime_array_type(SpvId result_type_id, SpvId base_type_id);
    static SpvInstruction pointer_type(SpvId pointer_type_id, SpvStorageClass storage_class, SpvId base_type_id);
    static SpvInstruction function_type(SpvId function_type_id, SpvId return_type_id, const ParamTypes &param_type_ids);
    static SpvInstruction constant(SpvId result_id, SpvId type_id, size_t bytes, const void *data);
    static SpvInstruction null_constant(SpvId result_id, SpvId type_id);
    static SpvInstruction bool_constant(SpvId result_id, SpvId type_id, bool value);
    static SpvInstruction composite_constant(SpvId result_id, SpvId type_id, const Components &components);
    static SpvInstruction variable(SpvId result_id, SpvId result_type_id, uint32_t storage_class, SpvId initializer_id = SpvInvalidId);
    static SpvInstruction function(SpvId return_type_id, SpvId func_id, uint32_t control_mask, SpvId func_type_id);
    static SpvInstruction function_parameter(SpvId param_type_id, SpvId param_id);
    static SpvInstruction function_end();
    static SpvInstruction return_stmt(SpvId return_value_id = SpvInvalidId);
    static SpvInstruction entry_point(SpvId exec_model, SpvId func_id, const std::string &name, const Variables &variables);
    static SpvInstruction memory_model(SpvAddressingModel addressing_model, SpvMemoryModel memory_model);
    static SpvInstruction exec_mode_local_size(SpvId function_id, uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z);
    static SpvInstruction control_barrier(SpvId execution_scope_id, SpvId memory_scope_id, uint32_t semantics_mask);
    static SpvInstruction logical_not(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction multiply_extended(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed);
    static SpvInstruction select(SpvId type_id, SpvId result_id, SpvId condition_id, SpvId true_id, SpvId false_id);
    static SpvInstruction in_bounds_access_chain(SpvId type_id, SpvId result_id, SpvId base_id, SpvId element_id, const Indices &indices);
    static SpvInstruction load(SpvId type_id, SpvId result_id, SpvId ptr_id, uint32_t access_mask = 0x0);
    static SpvInstruction store(SpvId ptr_id, SpvId obj_id, uint32_t access_mask = 0x0);
    static SpvInstruction vector_insert_dynamic(SpvId result_id, SpvId vector_id, SpvId value_id, uint32_t index);
    static SpvInstruction composite_extract(SpvId type_id, SpvId result_id, SpvId composite_id, const Indices &indices);
    static SpvInstruction bitcast(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction integer_add(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction branch(SpvId target_label_id);
    static SpvInstruction conditional_branch(SpvId condition_label_id, SpvId true_label_id, SpvId false_label_id, const BranchWeights &weights = {});
    static SpvInstruction loop_merge(SpvId merge_label_id, SpvId continue_label_id, uint32_t loop_control_mask = SpvLoopControlMaskNone);
    static SpvInstruction selection_merge(SpvId merge_label_id, uint32_t selection_control_mask = SpvSelectionControlMaskNone);
    static SpvInstruction phi(SpvId type_id, SpvId result_id, const BlockVariables &block_vars);
    static SpvInstruction unary_op(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction binary_op(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction convert(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id);
};

/** Contents of a SPIR-V Instruction */
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

/** Contents of a SPIR-V code block */
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

/** Contents of a SPIR-V function */
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

/** Contents of a SPIR-V code module */
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

}  // namespace Internal
}  // namespace Halide

#endif  // WITH_SPIRV

namespace Halide {
namespace Internal {

/** Internal test for SPIR-V IR **/
void spirv_ir_test();

}  // namespace Internal
}  // namespace Halide

#endif  // HALIDE_SPIRV_IR_H
