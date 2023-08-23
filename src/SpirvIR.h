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

#include <spirv/1.6/GLSL.std.450.h>  // GLSL extended instructions for common intrinsics
#include <spirv/1.6/spirv.h>         // Use v1.6 headers but only use the minimal viable format version (for maximum compatiblity)

namespace Halide {
namespace Internal {

/** Precision requirment for return values */
enum SpvPrecision {
    SpvFullPrecision,
    SpvRelaxedPrecision,
};

/** Scope qualifiers for Execution & Memory operations */
enum SpvScope {
    SpvCrossDeviceScope = 0,
    SpvDeviceScope = 1,
    SpvWorkgroupScope = 2,
    SpvSubgroupScope = 3,
    SpvInvocationScope = 4
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
    SpvUIntTypeId,
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
    SpvImportId,
    SpvModuleId,
    SpvUnknownItem,
};

/** Specific types of SPIR-V operand types */
enum SpvValueType {
    SpvInvalidValueType,
    SpvOperandId,
    SpvBitMaskLiteral,
    SpvIntegerLiteral,
    SpvIntegerData,
    SpvFloatData,
    SpvStringData,
    SpvUnknownValueType
};

/** SPIR-V requires all IDs to be 32-bit unsigned integers */
using SpvId = uint32_t;
using SpvBinary = std::vector<uint32_t>;

static constexpr SpvStorageClass SpvInvalidStorageClass = SpvStorageClassMax;  // sentinel for invalid storage class
static constexpr SpvId SpvInvalidId = SpvId(-1);
static constexpr SpvId SpvNoResult = 0;
static constexpr SpvId SpvNoType = 0;

/** Pre-declarations for SPIR-V IR classes */
class SpvModule;
class SpvFunction;
class SpvBlock;
class SpvInstruction;
class SpvBuilder;
class SpvContext;
struct SpvFactory;

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
    using LiteralValue = std::pair<uint32_t, SpvValueType>;
    using Immediates = std::vector<LiteralValue>;
    using Operands = std::vector<SpvId>;
    using ValueTypes = std::vector<SpvValueType>;

    SpvInstruction() = default;
    ~SpvInstruction();

    SpvInstruction(const SpvInstruction &) = default;
    SpvInstruction &operator=(const SpvInstruction &) = default;
    SpvInstruction(SpvInstruction &&) = default;
    SpvInstruction &operator=(SpvInstruction &&) = default;

    void set_result_id(SpvId id);
    void set_type_id(SpvId id);
    void set_op_code(SpvOp opcode);
    void add_operand(SpvId id);
    void add_operands(const Operands &operands);
    void add_immediate(SpvId id, SpvValueType type);
    void add_immediates(const Immediates &Immediates);
    void add_data(uint32_t bytes, const void *data, SpvValueType type);
    void add_string(const std::string &str);

    template<typename T>
    void append(const T &operands_or_immediates_or_strings);

    SpvId result_id() const;
    SpvId type_id() const;
    SpvOp op_code() const;
    SpvId operand(uint32_t index) const;
    const void *data(uint32_t index = 0) const;
    SpvValueType value_type(uint32_t index) const;
    const Operands &operands() const;

    bool has_type() const;
    bool has_result() const;
    bool is_defined() const;
    bool is_immediate(uint32_t index) const;
    uint32_t length() const;
    void check_defined() const;
    void clear();

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
    ~SpvBlock();

    SpvBlock(const SpvBlock &) = default;
    SpvBlock &operator=(const SpvBlock &) = default;
    SpvBlock(SpvBlock &&) = default;
    SpvBlock &operator=(SpvBlock &&) = default;

    void add_instruction(SpvInstruction inst);
    void add_variable(SpvInstruction var);
    const Instructions &instructions() const;
    const Variables &variables() const;
    bool is_reachable() const;
    bool is_terminated() const;
    bool is_defined() const;
    SpvId id() const;
    void check_defined() const;
    void clear();

    void encode(SpvBinary &binary) const;

    static SpvBlock make(SpvId block_id);

protected:
    SpvBlockContentsPtr contents;
};

/** General interface for representing a SPIR-V Function */
class SpvFunction {
public:
    using Blocks = std::vector<SpvBlock>;
    using Parameters = std::vector<SpvInstruction>;

    SpvFunction() = default;
    ~SpvFunction();

    SpvFunction(const SpvFunction &) = default;
    SpvFunction &operator=(const SpvFunction &) = default;
    SpvFunction(SpvFunction &&) = default;
    SpvFunction &operator=(SpvFunction &&) = default;

    SpvBlock create_block(SpvId block_id);
    void add_block(SpvBlock block);
    void add_parameter(SpvInstruction param);
    void set_return_precision(SpvPrecision precision);
    void set_parameter_precision(uint32_t index, SpvPrecision precision);
    bool is_defined() const;
    void clear();

    const Blocks &blocks() const;
    SpvBlock entry_block() const;
    SpvBlock tail_block() const;
    SpvPrecision return_precision() const;
    const Parameters &parameters() const;
    SpvPrecision parameter_precision(uint32_t index) const;
    uint32_t parameter_count() const;
    uint32_t control_mask() const;
    SpvInstruction declaration() const;
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
    using ImportDefinition = std::pair<SpvId, std::string>;
    using ImportNames = std::vector<std::string>;
    using EntryPointNames = std::vector<std::string>;
    using Instructions = std::vector<SpvInstruction>;
    using Functions = std::vector<SpvFunction>;
    using Capabilities = std::vector<SpvCapability>;
    using Extensions = std::vector<std::string>;
    using Imports = std::vector<ImportDefinition>;

    SpvModule() = default;
    ~SpvModule();

    SpvModule(const SpvModule &) = default;
    SpvModule &operator=(const SpvModule &) = default;
    SpvModule(SpvModule &&) = default;
    SpvModule &operator=(SpvModule &&) = default;

    void add_debug_string(SpvId result_id, const std::string &string);
    void add_debug_symbol(SpvId id, const std::string &symbol);
    void add_annotation(SpvInstruction val);
    void add_type(SpvInstruction val);
    void add_constant(SpvInstruction val);
    void add_global(SpvInstruction val);
    void add_execution_mode(SpvInstruction val);
    void add_function(SpvFunction val);
    void add_instruction(SpvInstruction val);
    void add_entry_point(const std::string &name, SpvInstruction entry_point);

    void import_instruction_set(SpvId id, const std::string &instruction_set);
    void require_capability(SpvCapability val);
    void require_extension(const std::string &val);

    void set_version_format(uint32_t version);
    void set_source_language(SpvSourceLanguage val);
    void set_addressing_model(SpvAddressingModel val);
    void set_memory_model(SpvMemoryModel val);
    void set_binding_count(SpvId count);

    uint32_t version_format() const;
    SpvSourceLanguage source_language() const;
    SpvAddressingModel addressing_model() const;
    SpvMemoryModel memory_model() const;
    SpvInstruction entry_point(const std::string &name) const;
    EntryPointNames entry_point_names() const;
    ImportNames import_names() const;
    SpvId lookup_import(const std::string &Instruction_set) const;
    uint32_t entry_point_count() const;

    Imports imports() const;
    Extensions extensions() const;
    Capabilities capabilities() const;
    Instructions entry_points() const;
    const Instructions &execution_modes() const;
    const Instructions &debug_source() const;
    const Instructions &debug_symbols() const;
    const Instructions &annotations() const;
    const Instructions &type_definitions() const;
    const Instructions &global_constants() const;
    const Instructions &global_variables() const;
    const Functions &function_definitions() const;

    uint32_t binding_count() const;
    SpvModule module() const;

    bool is_imported(const std::string &instruction_set) const;
    bool is_capability_required(SpvCapability val) const;
    bool is_extension_required(const std::string &val) const;
    bool is_defined() const;
    SpvId id() const;
    void check_defined() const;
    void clear();

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
    using Components = std::vector<SpvId>;
    using StructMemberTypes = std::vector<SpvId>;
    using Variables = std::vector<SpvId>;
    using Indices = std::vector<uint32_t>;
    using Literals = std::vector<uint32_t>;

    SpvBuilder();
    ~SpvBuilder() = default;

    SpvBuilder(const SpvBuilder &) = delete;
    SpvBuilder &operator=(const SpvBuilder &) = delete;

    // Reserve a unique ID to use for identifying a specifc kind of SPIR-V result **/
    SpvId reserve_id(SpvKind = SpvResultId);

    // Look up the specific kind of SPIR-V item from its unique ID
    SpvKind kind_of(SpvId id) const;

    // Get a human readable name for a specific kind of SPIR-V item
    std::string kind_name(SpvKind kind) const;

    // Look up the ID associated with the type for a given variable ID
    SpvId type_of(SpvId variable_id) const;

    // Top-Level declaration methods ... each of these is a convenvience
    // function that checks to see if the requested thing has already been
    // declared, in which case it returns its existing id, otherwise it
    // adds a new declaration, and returns the new id.  This avoids all
    // the logic checks in the calling code, and also ensures that
    // duplicates aren't created.

    SpvId declare_void_type();
    SpvId declare_type(const Type &type, uint32_t array_size = 1);
    SpvId declare_pointer_type(const Type &type, SpvStorageClass storage_class);
    SpvId declare_pointer_type(SpvId type_id, SpvStorageClass storage_class);
    SpvId declare_constant(const Type &type, const void *data, bool is_specialization = false);
    SpvId declare_null_constant(const Type &type);
    SpvId declare_bool_constant(bool value);
    SpvId declare_string_constant(const std::string &str);
    SpvId declare_integer_constant(const Type &type, int64_t value);
    SpvId declare_float_constant(const Type &type, double value);
    SpvId declare_scalar_constant(const Type &type, const void *data);
    SpvId declare_vector_constant(const Type &type, const void *data);
    SpvId declare_specialization_constant(const Type &type, const void *data);
    SpvId declare_access_chain(SpvId ptr_type_id, SpvId base_id, const Indices &indices);
    SpvId declare_pointer_access_chain(SpvId ptr_type_id, SpvId base_id, SpvId element_id, const Indices &indices);
    SpvId declare_function_type(SpvId return_type, const ParamTypes &param_types = {});
    SpvId declare_function(const std::string &name, SpvId function_type);
    SpvId declare_struct(const std::string &name, const StructMemberTypes &member_types);
    SpvId declare_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId initializer_id = SpvInvalidId);
    SpvId declare_global_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId initializer_id = SpvInvalidId);
    SpvId declare_symbol(const std::string &symbol, SpvId id, SpvId scope_id);

    // Top level creation methods for adding new items ... these have a limited
    // number of checks and the caller must ensure that duplicates aren't created
    SpvId add_type(const Type &type, uint32_t array_size = 1);
    SpvId add_struct(const std::string &name, const StructMemberTypes &member_types);
    SpvId add_array_with_default_size(SpvId base_type_id, SpvId array_size_id);
    SpvId add_runtime_array(SpvId base_type_id);
    SpvId add_pointer_type(const Type &type, SpvStorageClass storage_class);
    SpvId add_pointer_type(SpvId base_type_id, SpvStorageClass storage_class);
    SpvId add_constant(const Type &type, const void *data, bool is_specialization = false);
    SpvId add_function_type(SpvId return_type_id, const ParamTypes &param_type_ids);
    SpvId add_function(const std::string &name, SpvId return_type, const ParamTypes &param_types = {});
    SpvId add_instruction(SpvInstruction val);

    void add_annotation(SpvId target_id, SpvDecoration decoration_type, const Literals &literals = {});
    void add_struct_annotation(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals &literals = {});
    void add_symbol(const std::string &symbol, SpvId id, SpvId scope_id);

    void add_entry_point(SpvId func_id, SpvExecutionModel exec_model,
                         const Variables &variables = {});

    // Define the execution mode with a fixed local size for the workgroup (using literal values)
    void add_execution_mode_local_size(SpvId entry_point_id, uint32_t local_size_x, uint32_t local_size_y, uint32_t local_size_z);

    // Same as above but uses id's for the local size (to allow specialization constants to be used)
    void add_execution_mode_local_size_id(SpvId entry_point_id, SpvId local_size_x, SpvId local_size_y, SpvId local_size_z);

    // Assigns a specific SPIR-V version format for output (needed for compatibility)
    void set_version_format(uint32_t version);

    // Assigns a specific source language hint to the module
    void set_source_language(SpvSourceLanguage val);

    // Sets the addressing model to use for the module
    void set_addressing_model(SpvAddressingModel val);

    // Sets the memory model to use for the module
    void set_memory_model(SpvMemoryModel val);

    // Returns the source language hint for the module
    SpvSourceLanguage source_language() const;

    // Returns the addressing model used for the module
    SpvAddressingModel addressing_model() const;

    // Returns the memory model used for the module
    SpvMemoryModel memory_model() const;

    // Import the GLSL.std.450 external instruction set. Returns its corresponding ID.
    SpvId import_glsl_intrinsics();

    // Import an external instruction set bby name. Returns its corresponding ID.
    SpvId import_instruction_set(const std::string &instruction_set);

    // Add an extension string to the list of required extensions for the module
    void require_extension(const std::string &extension);

    // Add a specific capability to the list of requirements for the module
    void require_capability(SpvCapability);

    // Returns true if the given instruction set has been imported
    bool is_imported(const std::string &instruction_set) const;

    // Returns true if the given extension string is required by the module
    bool is_extension_required(const std::string &extension) const;

    // Returns true if the given capability is required by the module
    bool is_capability_required(SpvCapability) const;

    // Change the current build location to the given block. All local
    // declarations and instructions will be added here.
    void enter_block(const SpvBlock &block);

    // Create a new block with the given ID
    SpvBlock create_block(SpvId block_id);

    // Returns the current block (the active scope for building)
    SpvBlock current_block() const;

    // Resets the block build scope, and unassigns the current block
    SpvBlock leave_block();

    // Change the current build scope to be within the given function
    void enter_function(const SpvFunction &func);

    // Returns the function object for the given ID (or an invalid function if none is found)
    SpvFunction lookup_function(SpvId func_id) const;

    // Returns the current function being used as the active build scope
    SpvFunction current_function() const;

    // Resets the function build scope, and unassigns the current function
    SpvFunction leave_function();

    // Returns the current id being used for building (ie the last item created)
    SpvId current_id() const;

    // Updates the current id being used for building
    void update_id(SpvId id);

    // Returns true if the given id is of the corresponding type
    bool is_pointer_type(SpvId id) const;
    bool is_struct_type(SpvId id) const;
    bool is_vector_type(SpvId id) const;
    bool is_scalar_type(SpvId id) const;
    bool is_array_type(SpvId id) const;
    bool is_constant(SpvId id) const;

    // Looks up the given pointer type id and returns a corresponding base type id (or an invalid id if none is found)
    SpvId lookup_base_type(SpvId pointer_type) const;

    // Returns the storage class for the given variable id (or invalid if none is found)
    SpvStorageClass lookup_storage_class(SpvId id) const;

    // Returns the item id for the given symbol name (or an invalid id if none is found)
    SpvId lookup_id(const std::string &symbol) const;

    // Returns the build scope id for the item id (or an invalid id if none is found)
    SpvId lookup_scope(SpvId id) const;

    // Returns the id for the imported instruction set (or an invalid id if none is found)
    SpvId lookup_import(const std::string &instruction_set) const;

    // Returns the symbol string for the given id (or an empty string if none is found)
    std::string lookup_symbol(SpvId id) const;

    // Returns the current module being used for building
    SpvModule current_module() const;

    // Appends the given instruction to the current build location
    void append(SpvInstruction inst);

    // Finalizes the module and prepares it for encoding (must be called before module can be used)
    void finalize();

    // Encodes the current module to the given binary
    void encode(SpvBinary &binary) const;

    // Resets the builder and all internal state
    void reset();

protected:
    using TypeKey = uint64_t;
    using TypeMap = std::unordered_map<TypeKey, SpvId>;
    using KindMap = std::unordered_map<SpvId, SpvKind>;
    using PointerTypeKey = std::pair<SpvId, SpvStorageClass>;
    using PointerTypeMap = std::map<PointerTypeKey, SpvId>;
    using BaseTypeMap = std::unordered_map<SpvId, SpvId>;
    using VariableTypeMap = std::unordered_map<SpvId, SpvId>;
    using StorageClassMap = std::unordered_map<SpvId, SpvStorageClass>;
    using ConstantKey = uint64_t;
    using ConstantMap = std::unordered_map<ConstantKey, SpvId>;
    using StringMap = std::unordered_map<ConstantKey, SpvId>;
    using ScopeMap = std::unordered_map<SpvId, SpvId>;
    using IdSymbolMap = std::unordered_map<SpvId, std::string>;
    using SymbolIdMap = std::unordered_map<std::string, SpvId>;
    using FunctionTypeKey = uint64_t;
    using FunctionTypeMap = std::unordered_map<FunctionTypeKey, SpvId>;
    using FunctionMap = std::unordered_map<SpvId, SpvFunction>;

    // Internal methods for creating ids, keys, and look ups

    SpvId make_id(SpvKind kind);

    TypeKey make_type_key(const Type &type, uint32_t array_size = 1) const;
    SpvId lookup_type(const Type &type, uint32_t array_size = 1) const;

    TypeKey make_struct_type_key(const StructMemberTypes &member_types) const;
    SpvId lookup_struct(const std::string &name, const StructMemberTypes &member_types) const;

    PointerTypeKey make_pointer_type_key(const Type &type, SpvStorageClass storage_class) const;
    SpvId lookup_pointer_type(const Type &type, SpvStorageClass storage_class) const;

    PointerTypeKey make_pointer_type_key(SpvId base_type_id, SpvStorageClass storage_class) const;
    SpvId lookup_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const;

    template<typename T>
    SpvId declare_scalar_constant_of_type(const Type &scalar_type, const T *data);

    template<typename T>
    SpvId declare_specialization_constant_of_type(const Type &scalar_type, const T *data);

    template<typename T>
    SpvBuilder::Components declare_constants_for_each_lane(Type type, const void *data);

    ConstantKey make_bool_constant_key(bool value) const;
    ConstantKey make_string_constant_key(const std::string &value) const;
    ConstantKey make_constant_key(uint8_t code, uint8_t bits, int lanes, size_t bytes, const void *data, bool is_specialization = false) const;
    ConstantKey make_constant_key(const Type &type, const void *data, bool is_specialization = false) const;
    SpvId lookup_constant(const Type &type, const void *data, bool is_specialization = false) const;

    ConstantKey make_null_constant_key(const Type &type) const;
    SpvId lookup_null_constant(const Type &type) const;

    SpvId lookup_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId scope_id) const;
    bool has_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId scope_id) const;

    FunctionTypeKey make_function_type_key(SpvId return_type_id, const ParamTypes &param_type_ids) const;
    SpvId lookup_function_type(SpvId return_type_id, const ParamTypes &param_type_ids) const;

    SpvId active_id = SpvInvalidId;
    SpvFunction active_function;
    SpvBlock active_block;
    SpvModule module;
    KindMap kind_map;
    TypeMap type_map;
    TypeMap struct_map;
    ScopeMap scope_map;
    StringMap string_map;
    ConstantMap constant_map;
    FunctionMap function_map;
    IdSymbolMap id_symbol_map;
    SymbolIdMap symbol_id_map;
    BaseTypeMap base_type_map;
    StorageClassMap storage_class_map;
    PointerTypeMap pointer_type_map;
    VariableTypeMap variable_type_map;
    FunctionTypeMap function_type_map;
};

/** Factory interface for constructing specific SPIR-V instructions */
struct SpvFactory {
    using Indices = std::vector<uint32_t>;
    using Literals = std::vector<uint32_t>;
    using BranchWeights = std::vector<uint32_t>;
    using Components = std::vector<SpvId>;
    using ParamTypes = std::vector<SpvId>;
    using MemberTypeIds = std::vector<SpvId>;
    using Operands = std::vector<SpvId>;
    using Variables = std::vector<SpvId>;
    using VariableBlockIdPair = std::pair<SpvId, SpvId>;  // (Variable Id, Block Id)
    using BlockVariables = std::vector<VariableBlockIdPair>;

    static SpvInstruction no_op(SpvId result_id);
    static SpvInstruction capability(const SpvCapability &capability);
    static SpvInstruction extension(const std::string &extension);
    static SpvInstruction import(SpvId instruction_set_id, const std::string &instruction_set_name);
    static SpvInstruction label(SpvId result_id);
    static SpvInstruction debug_line(SpvId string_id, uint32_t line, uint32_t column);
    static SpvInstruction debug_string(SpvId result_id, const std::string &string);
    static SpvInstruction debug_symbol(SpvId target_id, const std::string &symbol);
    static SpvInstruction decorate(SpvId target_id, SpvDecoration decoration_type, const Literals &literals = {});
    static SpvInstruction decorate_member(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals &literals = {});
    static SpvInstruction void_type(SpvId void_type_id);
    static SpvInstruction bool_type(SpvId bool_type_id);
    static SpvInstruction integer_type(SpvId int_type_id, uint32_t bits, uint32_t signedness);
    static SpvInstruction float_type(SpvId float_type_id, uint32_t bits);
    static SpvInstruction vector_type(SpvId vector_type_id, SpvId element_type_id, uint32_t vector_size);
    static SpvInstruction array_type(SpvId array_type_id, SpvId element_type_id, SpvId array_size_id);
    static SpvInstruction struct_type(SpvId result_id, const MemberTypeIds &member_type_ids);
    static SpvInstruction runtime_array_type(SpvId result_type_id, SpvId base_type_id);
    static SpvInstruction pointer_type(SpvId pointer_type_id, SpvStorageClass storage_class, SpvId base_type_id);
    static SpvInstruction function_type(SpvId function_type_id, SpvId return_type_id, const ParamTypes &param_type_ids);
    static SpvInstruction constant(SpvId result_id, SpvId type_id, size_t bytes, const void *data, SpvValueType value_type);
    static SpvInstruction null_constant(SpvId result_id, SpvId type_id);
    static SpvInstruction bool_constant(SpvId result_id, SpvId type_id, bool value);
    static SpvInstruction string_constant(SpvId result_id, const std::string &value);
    static SpvInstruction composite_constant(SpvId result_id, SpvId type_id, const Components &components);
    static SpvInstruction specialization_constant(SpvId result_id, SpvId type_id, size_t bytes, const void *data, SpvValueType value_type);
    static SpvInstruction variable(SpvId result_id, SpvId result_type_id, uint32_t storage_class, SpvId initializer_id = SpvInvalidId);
    static SpvInstruction function(SpvId return_type_id, SpvId func_id, uint32_t control_mask, SpvId func_type_id);
    static SpvInstruction function_parameter(SpvId param_type_id, SpvId param_id);
    static SpvInstruction function_end();
    static SpvInstruction return_stmt(SpvId return_value_id = SpvInvalidId);
    static SpvInstruction entry_point(SpvId exec_model, SpvId func_id, const std::string &name, const Variables &variables);
    static SpvInstruction memory_model(SpvAddressingModel addressing_model, SpvMemoryModel memory_model);
    static SpvInstruction exec_mode_local_size(SpvId function_id, uint32_t local_size_size_x, uint32_t local_size_size_y, uint32_t local_size_size_z);
    static SpvInstruction exec_mode_local_size_id(SpvId function_id, SpvId local_size_x_id, SpvId local_size_y_id, SpvId local_size_z_id);  // only avail in 1.2
    static SpvInstruction memory_barrier(SpvId memory_scope_id, SpvId semantics_mask_id);
    static SpvInstruction control_barrier(SpvId execution_scope_id, SpvId memory_scope_id, SpvId semantics_mask_id);
    static SpvInstruction bitwise_not(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction bitwise_and(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction logical_not(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction logical_and(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction shift_right_logical(SpvId type_id, SpvId result_id, SpvId src_id, SpvId shift_id);
    static SpvInstruction shift_right_arithmetic(SpvId type_id, SpvId result_id, SpvId src_id, SpvId shift_id);
    static SpvInstruction multiply_extended(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed);
    static SpvInstruction select(SpvId type_id, SpvId result_id, SpvId condition_id, SpvId true_id, SpvId false_id);
    static SpvInstruction in_bounds_access_chain(SpvId type_id, SpvId result_id, SpvId base_id, const Indices &indices);
    static SpvInstruction pointer_access_chain(SpvId type_id, SpvId result_id, SpvId base_id, SpvId element_id, const Indices &indices);
    static SpvInstruction load(SpvId type_id, SpvId result_id, SpvId ptr_id, uint32_t access_mask = 0x0);
    static SpvInstruction store(SpvId ptr_id, SpvId obj_id, uint32_t access_mask = 0x0);
    static SpvInstruction vector_insert_dynamic(SpvId type_id, SpvId result_id, SpvId vector_id, SpvId value_id, SpvId index_id);
    static SpvInstruction vector_extract_dynamic(SpvId type_id, SpvId result_id, SpvId vector_id, SpvId value_id, SpvId index_id);
    static SpvInstruction vector_shuffle(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, const Indices &indices);
    static SpvInstruction composite_insert(SpvId type_id, SpvId result_id, SpvId object_id, SpvId composite_id, const SpvFactory::Indices &indices);
    static SpvInstruction composite_extract(SpvId type_id, SpvId result_id, SpvId composite_id, const Indices &indices);
    static SpvInstruction composite_construct(SpvId type_id, SpvId result_id, const Components &constituents);
    static SpvInstruction is_inf(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction is_nan(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction bitcast(SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction float_add(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction integer_add(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction integer_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction integer_not_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction integer_less_than(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed);
    static SpvInstruction integer_less_than_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed);
    static SpvInstruction integer_greater_than(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed);
    static SpvInstruction integer_greater_than_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed);
    static SpvInstruction branch(SpvId target_label_id);
    static SpvInstruction conditional_branch(SpvId condition_label_id, SpvId true_label_id, SpvId false_label_id, const BranchWeights &weights = {});
    static SpvInstruction loop_merge(SpvId merge_label_id, SpvId continue_label_id, uint32_t loop_control_mask = SpvLoopControlMaskNone);
    static SpvInstruction selection_merge(SpvId merge_label_id, uint32_t selection_control_mask = SpvSelectionControlMaskNone);
    static SpvInstruction phi(SpvId type_id, SpvId result_id, const BlockVariables &block_vars);
    static SpvInstruction unary_op(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction binary_op(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id);
    static SpvInstruction convert(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id);
    static SpvInstruction extended(SpvId instruction_set_id, SpvId instruction_number, SpvId type_id, SpvId result_id, const SpvFactory::Operands &operands);
};

/** Contents of a SPIR-V Instruction */
struct SpvInstructionContents {
    using Operands = std::vector<SpvId>;
    using ValueTypes = std::vector<SpvValueType>;
    mutable RefCount ref_count;
    SpvOp op_code = SpvOpNop;
    SpvId result_id = SpvNoResult;
    SpvId type_id = SpvNoType;
    Operands operands;
    ValueTypes value_types;
};

/** Contents of a SPIR-V code block */
struct SpvBlockContents {
    using Instructions = std::vector<SpvInstruction>;
    using Variables = std::vector<SpvInstruction>;
    using Blocks = std::vector<SpvBlock>;
    mutable RefCount ref_count;
    SpvId block_id = SpvInvalidId;
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
    using Imports = std::unordered_map<std::string, SpvId>;
    using Functions = std::vector<SpvFunction>;
    using Instructions = std::vector<SpvInstruction>;
    using EntryPoints = std::unordered_map<std::string, SpvInstruction>;

    mutable RefCount ref_count;
    SpvId module_id = SpvInvalidId;
    SpvId version_format = SpvVersion;
    SpvId binding_count = 0;
    SpvSourceLanguage source_language = SpvSourceLanguageUnknown;
    SpvAddressingModel addressing_model = SpvAddressingModelLogical;
    SpvMemoryModel memory_model = SpvMemoryModelSimple;
    Capabilities capabilities;
    Extensions extensions;
    Imports imports;
    EntryPoints entry_points;
    Instructions execution_modes;
    Instructions debug_source;
    Instructions debug_symbols;
    Instructions annotations;
    Instructions types;
    Instructions constants;
    Instructions globals;
    Functions functions;
    Instructions instructions;
};

/** Helper functions for determining calling convention of GLSL builtins **/
bool is_glsl_unary_op(SpvId glsl_op_code);
bool is_glsl_binary_op(SpvId glsl_op_code);
uint32_t glsl_operand_count(SpvId glsl_op_code);

/** Output the contents of a SPIR-V module in human-readable form **/
std::ostream &operator<<(std::ostream &stream, const SpvModule &);

/** Output the definition of a SPIR-V function in human-readable form **/
std::ostream &operator<<(std::ostream &stream, const SpvFunction &);

/** Output the contents of a SPIR-V block in human-readable form **/
std::ostream &operator<<(std::ostream &stream, const SpvBlock &);

/** Output a SPIR-V instruction in human-readable form **/
std::ostream &operator<<(std::ostream &stream, const SpvInstruction &);

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
