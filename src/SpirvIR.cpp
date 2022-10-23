#include "SpirvIR.h"
#include <iostream>
#include <signal.h>

#ifdef WITH_SPIRV

namespace Halide {
namespace Internal {

namespace {

/** Returns the major version of the SPIR-V header version indicator **/
inline uint32_t spirv_major_version(uint32_t version) {
    return ((version >> 16) & 0xff);
}

/** Returns the minor version of the SPIR-V header version indicator **/
inline uint32_t spirv_minor_version(uint32_t version) {
    return ((version >> 8) & 0xff);
}

template<typename T, typename S>
typename std::enable_if<std::is_unsigned<T>::value, T>::type constexpr rotl(const T n, const S i) {
    const T m = (std::numeric_limits<T>::digits - 1);
    const T c = i & m;
    return (n << c) | (n >> ((T(0) - c) & m));
}

inline uint64_t hash_splitmix64(uint64_t x) {
    // http://xorshift.di.unimi.it/splitmix64.c
    x += uint64_t(0x9e3779b97f4a7c15);
    x = (x ^ (x >> 30)) * uint64_t(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * uint64_t(0x94d049bb133111eb);
    return x ^ (x >> 31);
}

inline uint64_t hash_combine(uint64_t &seed, const uint64_t &value) {
    // mix using a cheap asymmetric binary rotation
    const uint64_t r = std::numeric_limits<uint64_t>::digits / 3;
    return rotl(seed, r) ^ hash_splitmix64(value);
}

}  // namespace

/** SpvInstruction implementation **/
SpvInstruction SpvInstruction::make(SpvOp op_code) {
    SpvInstruction instance;
    instance.contents = SpvInstructionContentsPtr(new SpvInstructionContents);
    instance.contents->op_code = op_code;
    instance.contents->result_id = SpvNoResult;
    instance.contents->type_id = SpvNoType;
    return instance;
}

void SpvInstruction::set_block(SpvBlock block) {
    check_defined();
    contents->block = std::move(block);
}

void SpvInstruction::set_result_id(SpvId result_id) {
    check_defined();
    contents->result_id = result_id;
}

void SpvInstruction::set_type_id(SpvId type_id) {
    check_defined();
    contents->type_id = type_id;
}

void SpvInstruction::set_op_code(SpvOp op_code) {
    check_defined();
    contents->op_code = op_code;
}

void SpvInstruction::add_operand(SpvId id) {
    check_defined();
    contents->operands.push_back(id);
    contents->value_types.push_back(SpvOperandId);
}

void SpvInstruction::add_immediate(SpvId id, SpvValueType value_type) {
    check_defined();
    contents->operands.push_back(id);
    contents->value_types.push_back(value_type);
}

SpvId SpvInstruction::result_id() const {
    check_defined();
    return contents->result_id;
}

SpvId SpvInstruction::type_id() const {
    check_defined();
    return contents->type_id;
}

SpvOp SpvInstruction::op_code() const {
    check_defined();
    return contents->op_code;
}

const void *SpvInstruction::data(uint32_t index) const {
    check_defined();
    return &(contents->operands[index]);
}

SpvId SpvInstruction::operand(uint32_t index) const {
    check_defined();
    return contents->operands[index];
}

SpvValueType SpvInstruction::value_type(uint32_t index) const {
    check_defined();
    return contents->value_types[index];
}

const SpvInstruction::Operands &SpvInstruction::operands() const {
    check_defined();
    return contents->operands;
}

bool SpvInstruction::has_type() const {
    if (!is_defined()) {
        return false;
    }
    return contents->type_id != SpvNoType;
}

bool SpvInstruction::has_result() const {
    if (!is_defined()) {
        return false;
    }
    return contents->result_id != SpvNoResult;
}

bool SpvInstruction::is_defined() const {
    return contents.defined();
}

bool SpvInstruction::is_immediate(uint32_t index) const {
    check_defined();
    return (contents->value_types[index] != SpvOperandId);
}

uint32_t SpvInstruction::length() const {
    check_defined();
    return (uint32_t)contents->operands.size();
}

SpvBlock SpvInstruction::block() const {
    check_defined();
    return contents->block;
}

void SpvInstruction::add_data(uint32_t bytes, const void *data, SpvValueType value_type) {
    check_defined();
    uint32_t extra_words = (bytes + 3) / 4;
    const uint8_t *ptr = (const uint8_t *)data;
    size_t bytes_copied = 0;
    for (uint32_t i = 0; i < extra_words; i++) {
        size_t copy_size = std::min(bytes - bytes_copied, (size_t)4);
        SpvId entry = 0;
        memcpy(&entry, ptr, copy_size);
        bytes_copied += copy_size;
        add_immediate(entry, value_type);
        ptr += 4;
    }
}

void SpvInstruction::add_string(const std::string &str) {
    check_defined();
    add_data(str.length() + 1, (const void *)str.c_str(), SpvStringData);
}

void SpvInstruction::check_defined() const {
    user_assert(is_defined()) << "An SpvInstruction must be defined before accessing its properties\n";
}

void SpvInstruction::encode(SpvBinary &binary) const {
    check_defined();

    // Count the number of 32-bit words to represent the instruction
    uint32_t word_count = 1;
    word_count += has_type() ? 1 : 0;
    word_count += has_result() ? 1 : 0;
    word_count += length();

    // Preface the instruction with the format
    // - high 16-bits indicate instruction length (number of 32-bit words)
    // - low 16-bits indicate op code
    binary.push_back(((word_count) << SpvWordCountShift) | contents->op_code);
    if (has_type()) {
        binary.push_back(contents->type_id);
    }
    if (has_result()) {
        binary.push_back(contents->result_id);
    }
    for (SpvId id : contents->operands) {
        binary.push_back(id);
    }
}

// --

SpvBlock SpvBlock::make(SpvFunction func, SpvId block_id) {
    SpvBlock instance;
    instance.contents = SpvBlockContentsPtr(new SpvBlockContents());
    instance.contents->parent = std::move(func);
    instance.contents->block_id = block_id;
    return instance;
}

void SpvBlock::add_instruction(SpvInstruction inst) {
    check_defined();
    inst.set_block(*this);
    contents->instructions.push_back(inst);
}

void SpvBlock::add_variable(SpvInstruction var) {
    check_defined();
    var.set_block(*this);
    contents->variables.push_back(var);
}

void SpvBlock::set_function(SpvFunction func) {
    check_defined();
    contents->parent = std::move(func);
}

SpvFunction SpvBlock::function() const {
    check_defined();
    return contents->parent;
}

const SpvBlock::Instructions &SpvBlock::instructions() const {
    check_defined();
    return contents->instructions;
}

const SpvBlock::Variables &SpvBlock::variables() const {
    check_defined();
    return contents->variables;
}

bool SpvBlock::is_reachable() const {
    check_defined();
    return contents->reachable;
}

bool SpvBlock::is_defined() const {
    return contents.defined();
}

bool SpvBlock::is_terminated() const {
    check_defined();
    if(contents->instructions.empty()) {
        return false;
    }
    switch (contents->instructions.back().op_code()) {
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
    check_defined();
    return contents->block_id;
}

void SpvBlock::check_defined() const {
    user_assert(is_defined()) << "An SpvBlock must be defined before accessing its properties\n";
}

void SpvBlock::encode(SpvBinary &binary) const {
    check_defined();

    // add a label for this block
    SpvInstruction label = SpvFactory::label(contents->block_id);
    label.encode(binary);

    // encode all variables
    for (const SpvInstruction &variable : contents->variables) {
        variable.encode(binary);
    }
    // encode all instructions
    for (const SpvInstruction &instruction : contents->instructions) {
        instruction.encode(binary);
    }
}

// --

SpvFunction SpvFunction::make(SpvId func_type_id, SpvId func_id, SpvId return_type_id, uint32_t control_mask) {
    SpvFunction instance;
    instance.contents = SpvFunctionContentsPtr(new SpvFunctionContents());
    instance.contents->function_id = func_id;
    instance.contents->function_type_id = func_type_id;
    instance.contents->return_type_id = return_type_id;
    instance.contents->control_mask = control_mask;
    instance.contents->declaration = SpvFactory::function(return_type_id, func_id, control_mask, func_type_id);
    return instance;
}

bool SpvFunction::is_defined() const {
    return contents.defined();
}

SpvBlock SpvFunction::create_block(SpvId block_id) {
    check_defined();
    if (!contents->blocks.empty()) {
        SpvBlock last_block = tail_block();
        if (last_block.is_defined() && !last_block.is_terminated()) {
            last_block.add_instruction(SpvFactory::branch(block_id));
        }
    }
    SpvBlock block = SpvBlock::make(*this, block_id);
    contents->blocks.push_back(block);
    return block;
}

void SpvFunction::add_block(const SpvBlock &block) {
    check_defined();
    if (!contents->blocks.empty()) {
        SpvBlock last_block = tail_block();
        if (!last_block.is_terminated()) {
            last_block.add_instruction(SpvFactory::branch(block.id()));
        }
    }
    contents->blocks.push_back(block);
}

void SpvFunction::add_parameter(const SpvInstruction &param) {
    check_defined();
    contents->parameters.push_back(param);
}

uint32_t SpvFunction::parameter_count() const {
    check_defined();
    return (uint32_t)contents->parameters.size();
}

SpvBlock SpvFunction::entry_block() const {
    check_defined();
    return contents->blocks.front();
}

SpvBlock SpvFunction::tail_block() const {
    check_defined();
    return contents->blocks.back();
}

SpvPrecision SpvFunction::return_precision() const {
    check_defined();
    SpvId return_id = contents->declaration.result_id();
    SpvFunctionContents::PrecisionMap::const_iterator it = contents->precision.find(return_id);
    if (it == contents->precision.end()) {
        return SpvPrecision::SpvFullPrecision;
    } else {
        return contents->precision[return_id];
    }
}

void SpvFunction::set_return_precision(SpvPrecision precision) {
    check_defined();
    SpvId return_id = contents->declaration.result_id();
    SpvFunctionContents::PrecisionMap::const_iterator it = contents->precision.find(return_id);
    if (it == contents->precision.end()) {
        contents->precision.insert({return_id, precision});
    } else {
        contents->precision[return_id] = precision;
    }
}

SpvPrecision SpvFunction::parameter_precision(uint32_t index) const {
    check_defined();
    user_assert(contents->parameters.size() > index) << "Invalid parameter index specified!\n";
    SpvId param_id = contents->parameters[index].result_id();
    SpvFunctionContents::PrecisionMap::const_iterator it = contents->precision.find(param_id);
    if (it == contents->precision.end()) {
        return SpvPrecision::SpvFullPrecision;
    } else {
        return contents->precision[param_id];
    }
}

void SpvFunction::set_module(SpvModule module) {
    check_defined();
    contents->parent = std::move(module);
}

SpvInstruction SpvFunction::declaration() const {
    check_defined();
    return contents->declaration;
}

const SpvFunction::Blocks &SpvFunction::blocks() const {
    check_defined();
    return contents->blocks;
}

const SpvFunction::Parameters &SpvFunction::parameters() const {
    check_defined();
    return contents->parameters;
}

SpvModule SpvFunction::module() const {
    check_defined();
    return contents->parent;
}

SpvId SpvFunction::return_type_id() const {
    check_defined();
    return contents->return_type_id;
}

SpvId SpvFunction::type_id() const {
    check_defined();
    return contents->function_type_id;
}

SpvId SpvFunction::id() const {
    check_defined();
    return contents->function_id;
}

void SpvFunction::check_defined() const {
    user_assert(is_defined()) << "An SpvFunction must be defined before accessing its properties\n";
}

void SpvFunction::encode(SpvBinary &binary) const {
    check_defined();
    contents->declaration.encode(binary);
    for (const SpvInstruction &param : contents->parameters) {
        param.encode(binary);
    }
    for (const SpvBlock &block : contents->blocks) {
        block.encode(binary);
    }

    SpvInstruction inst = SpvFactory::function_end();
    inst.encode(binary);
}

// --

SpvModule SpvModule::make(SpvId module_id,
                          SpvSourceLanguage source_language,
                          SpvAddressingModel addressing_model,
                          SpvMemoryModel memory_model) {
    SpvModule instance;
    instance.contents = SpvModuleContentsPtr(new SpvModuleContents());
    instance.contents->module_id = module_id;
    instance.contents->source_language = source_language;
    instance.contents->addressing_model = addressing_model;
    instance.contents->memory_model = memory_model;
    return instance;
}

bool SpvModule::is_defined() const {
    return contents.defined();
}

void SpvModule::add_debug_string(SpvId result_id, const std::string &string) {
    check_defined();
    contents->debug_source.push_back(SpvFactory::debug_string(result_id, string));
}

void SpvModule::add_debug_symbol(SpvId id, const std::string &symbol) {
    check_defined();
    contents->debug_symbols.push_back(SpvFactory::debug_symbol(id, symbol));
}

void SpvModule::add_annotation(const SpvInstruction &val) {
    check_defined();
    contents->annotations.push_back(val);
}

void SpvModule::add_type(const SpvInstruction &val) {
    check_defined();
    contents->types.push_back(val);
}

void SpvModule::add_constant(const SpvInstruction &val) {
    check_defined();
    contents->constants.push_back(val);
}

void SpvModule::add_global(const SpvInstruction &val) {
    check_defined();
    contents->globals.push_back(val);
}

void SpvModule::add_execution_mode(const SpvInstruction &val) {
    check_defined();
    contents->execution_modes.push_back(val);
}

void SpvModule::add_instruction(const SpvInstruction &val) {
    check_defined();
    contents->instructions.push_back(val);
}

void SpvModule::add_function(SpvFunction val) {
    check_defined();
    val.set_module(*this);
    contents->functions.emplace_back(val);
}

void SpvModule::add_entry_point(const std::string &name, SpvInstruction inst) {
    check_defined();
    contents->entry_points[name] = std::move(inst);
}

void SpvModule::set_binding_count(SpvId val) {
    check_defined();
    contents->binding_count = val;
}

void SpvModule::set_source_language(SpvSourceLanguage val) {
    check_defined();
    contents->source_language = val;
}

void SpvModule::set_addressing_model(SpvAddressingModel val) {
    check_defined();
    contents->addressing_model = val;
}

void SpvModule::set_memory_model(SpvMemoryModel val) {
    check_defined();
    contents->memory_model = val;
}

uint32_t SpvModule::entry_point_count() const {
    check_defined();
    return (uint32_t)contents->entry_points.size();
}

uint32_t SpvModule::binding_count() const {
    check_defined();
    return contents->binding_count;
}

uint32_t SpvModule::version_format() const {
    check_defined();
    return contents->version_format;
}

SpvSourceLanguage SpvModule::source_language() const {
    check_defined();
    return contents->source_language;
}

SpvAddressingModel SpvModule::addressing_model() const {
    check_defined();
    return contents->addressing_model;
}

SpvModule::Imports SpvModule::imports() const {
    check_defined();
    SpvModule::Imports results;
    results.reserve(contents->imports.size());
    for (const SpvModuleContents::Imports::value_type &v : contents->imports) {
        SpvModule::ImportDefinition definition = {v.second, v.first};
        results.push_back(definition);
    }
    return results;
}

SpvModule::Extensions SpvModule::extensions() const {
    check_defined();
    SpvModule::Extensions results;
    results.reserve(contents->extensions.size());
    for (const SpvModuleContents::Extensions::value_type &v : contents->extensions) {
        results.push_back(v);
    }
    return results;
}

SpvModule::Capabilities SpvModule::capabilities() const {
    check_defined();
    SpvModule::Capabilities results;
    results.reserve(contents->capabilities.size());
    for (const SpvModuleContents::Capabilities::value_type &v : contents->capabilities) {
        results.push_back(v);
    }
    return results;
}

const SpvModule::Instructions &SpvModule::execution_modes() const {
    check_defined();
    return contents->execution_modes;
}

const SpvModule::Instructions &SpvModule::debug_source() const {
    check_defined();
    return contents->debug_source;
}

const SpvModule::Instructions &SpvModule::debug_symbols() const {
    check_defined();
    return contents->debug_symbols;
}

const SpvModule::Instructions &SpvModule::annotations() const {
    check_defined();
    return contents->annotations;
}

const SpvModule::Instructions &SpvModule::type_definitions() const {
    check_defined();
    return contents->types;
}

const SpvModule::Instructions &SpvModule::global_constants() const {
    check_defined();
    return contents->constants;
}

const SpvModule::Instructions &SpvModule::global_variables() const {
    check_defined();
    return contents->globals;
}

const SpvModule::Functions &SpvModule::function_definitions() const {
    check_defined();
    return contents->functions;
}

SpvMemoryModel SpvModule::memory_model() const {
    check_defined();
    return contents->memory_model;
}

SpvInstruction SpvModule::entry_point(const std::string &name) const {
    check_defined();
    if (contents->entry_points.find(name) != contents->entry_points.end()) {
        return contents->entry_points[name];
    } else {
        SpvInstruction noop = SpvInstruction::make(SpvOpNop);
        return noop;
    }
}

void SpvModule::import_instruction_set(SpvId id, const std::string &instruction_set) {
    check_defined();
    if (contents->imports.find(instruction_set) == contents->imports.end()) {
        contents->imports.insert({instruction_set, id});
    }
}

void SpvModule::require_extension(const std::string &extension) {
    check_defined();
    if (contents->extensions.find(extension) == contents->extensions.end()) {
        contents->extensions.insert(extension);
    }
}

bool SpvModule::is_imported(const std::string &instruction_set) const {
    check_defined();
    if (contents->imports.find(instruction_set) != contents->imports.end()) {
        return true;
    }
    return false;
}

bool SpvModule::is_extension_required(const std::string &extension) const {
    check_defined();
    if (contents->extensions.find(extension) != contents->extensions.end()) {
        return true;
    }
    return false;
}

void SpvModule::require_capability(SpvCapability capability) {
    check_defined();
    if (contents->capabilities.find(capability) == contents->capabilities.end()) {
        contents->capabilities.insert(capability);
    }
}

bool SpvModule::is_capability_required(SpvCapability capability) const {
    check_defined();
    if (contents->capabilities.find(capability) != contents->capabilities.end()) {
        return true;
    }
    return false;
}

SpvModule::EntryPointNames SpvModule::entry_point_names() const {
    check_defined();
    SpvModule::EntryPointNames entry_point_names;
    entry_point_names.reserve(contents->entry_points.size());
    for (const SpvModuleContents::EntryPoints::value_type &v : contents->entry_points) {
        entry_point_names.push_back(v.first);
    }
    return entry_point_names;
}

SpvModule::Instructions SpvModule::entry_points() const {
    check_defined();
    SpvModule::Instructions entry_points;
    entry_points.reserve(contents->entry_points.size());
    for (const SpvModuleContents::EntryPoints::value_type &v : contents->entry_points) {
        entry_points.push_back(v.second);
    }
    return entry_points;
}

SpvModule::ImportNames SpvModule::import_names() const {
    check_defined();
    SpvModule::ImportNames results;
    results.reserve(contents->imports.size());
    for (const SpvModuleContents::Imports::value_type &v : contents->imports) {
        results.push_back(v.first);
    }
    return results;
}

SpvId SpvModule::lookup_import(const std::string &instruction_set) const {
    SpvId result_id = SpvInvalidId;
    SpvModuleContents::Imports::const_iterator it = contents->imports.find(instruction_set);
    if (it != contents->imports.end()) {
        result_id = it->second;
    }
    return result_id;
}

SpvId SpvModule::id() const {
    check_defined();
    return contents->module_id;
}

void SpvModule::check_defined() const {
    user_assert(is_defined()) << "An SpvModule must be defined before accessing its properties\n";
}

void SpvModule::encode(SpvBinary &binary) const {
    check_defined();

    // 0. Encode the header
    binary.push_back(SpvMagicNumber);
    binary.push_back(contents->version_format);
    binary.push_back(contents->source_language);
    binary.push_back(contents->binding_count);  // last id bound to this module (aka last id used)
    binary.push_back(0);                        // Reserved for schema.

    // 1. Capabilities
    for (const SpvCapability &capability : contents->capabilities) {
        SpvInstruction inst = SpvFactory::capability(capability);
        inst.encode(binary);
    }

    // 2. Extensions
    for (const std::string &extension : contents->extensions) {
        SpvInstruction inst = SpvFactory::extension(extension);
        inst.encode(binary);
    }

    // 3. Extended Instruction Set Imports
    for (const SpvModuleContents::Imports::value_type &import : contents->imports) {
        const std::string &import_name = import.first;
        SpvId import_id = import.second;
        SpvInstruction inst = SpvFactory::import(import_id, import_name);
        inst.encode(binary);
    }

    // 4. Memory Model
    SpvInstruction memory_model_inst = SpvFactory::memory_model(contents->addressing_model, contents->memory_model);
    memory_model_inst.encode(binary);

    // 5. Entry Points
    for (const SpvModuleContents::EntryPoints::value_type &value : contents->entry_points) {
        SpvInstruction entry_point_inst = value.second;
        entry_point_inst.encode(binary);
    }

    // 6. Execution Modes
    for (const SpvInstruction &inst : contents->execution_modes) {
        inst.encode(binary);
    }

    // 7. Debug Source & Names
    for (const SpvInstruction &inst : contents->debug_source) {
        inst.encode(binary);
    }
    for (const SpvInstruction &inst : contents->debug_symbols) {
        inst.encode(binary);
    }

    // 8. Annotations
    for (const SpvInstruction &inst : contents->annotations) {
        inst.encode(binary);
    }

    // 9a. Type Declarations
    for (const SpvInstruction &inst : contents->types) {
        inst.encode(binary);
    }

    // 9b. Constants
    for (const SpvInstruction &inst : contents->constants) {
        inst.encode(binary);
    }

    // 9c. Globals
    for (const SpvInstruction &inst : contents->globals) {
        inst.encode(binary);
    }

    // 10-11. Function Declarations & Definitions
    for (const SpvFunction &func : contents->functions) {
        func.encode(binary);
    }
}

// --

SpvBuilder::SpvBuilder() {
    reset();
}

void SpvBuilder::reset() {

    kind_map.clear();
    type_map.clear();
    struct_map.clear();
    scope_map.clear();
    string_map.clear();
    constant_map.clear();
    function_map.clear();
    id_symbol_map.clear();
    symbol_id_map.clear();
    base_type_map.clear();
    storage_class_map.clear();
    pointer_type_map.clear();
    variable_type_map.clear();
    function_type_map.clear();

    active_id = SpvInvalidId;
    active_block = SpvBlock();
    active_function = SpvFunction();

    SpvId module_id = make_id(SpvModuleId);
    module = SpvModule::make(module_id);
}

SpvId SpvBuilder::reserve_id(SpvKind kind) {
    return make_id(kind);
}

SpvId SpvBuilder::make_id(SpvKind kind) {
    // use type-agnostic non-overlapping increasing ids
    SpvId item_id = kind_map.size() + 1;
    debug(3) << "    make_id: %" << item_id << " kind=" << kind_name(kind) << "\n";
    kind_map[item_id] = kind;
    return item_id;
}

std::string SpvBuilder::kind_name(SpvKind kind) const {
    switch (kind) {
    case SpvInvalidItem: {
        return "InvalidItem";
    }
    case SpvTypeId: {
        return "TypeId";
    }
    case SpvVoidTypeId: {
        return "VoidTypeId";
    }
    case SpvBoolTypeId: {
        return "BoolTypeId";
    }
    case SpvIntTypeId: {
        return "IntTypeId";
    }
    case SpvFloatTypeId: {
        return "FloatTypeId";
    }
    case SpvVectorTypeId: {
        return "VectorTypeId";
    }
    case SpvArrayTypeId: {
        return "ArrayTypeId";
    }
    case SpvRuntimeArrayTypeId: {
        return "RuntimeArrayTypeId";
    }
    case SpvStringTypeId: {
        return "StringTypeId";
    }
    case SpvPointerTypeId: {
        return "PointerTypeId";
    }
    case SpvStructTypeId: {
        return "StructTypeId";
    }
    case SpvFunctionTypeId: {
        return "FunctionTypeId";
    }
    case SpvAccessChainId: {
        return "AccessChainId";
    }
    case SpvConstantId: {
        return "ConstantId";
    }
    case SpvBoolConstantId: {
        return "BoolConstantId";
    }
    case SpvIntConstantId: {
        return "IntConstantId";
    }
    case SpvFloatConstantId: {
        return "FloatConstantId";
    }
    case SpvStringConstantId: {
        return "StringConstantId";
    }
    case SpvCompositeConstantId: {
        return "CompositeConstantId";
    }
    case SpvResultId: {
        return "ResultId";
    }
    case SpvVariableId: {
        return "VariableId";
    }
    case SpvInstructionId: {
        return "InstructionId";
    }
    case SpvFunctionId: {
        return "FunctionId";
    }
    case SpvBlockId: {
        return "BlockId";
    }
    case SpvLabelId: {
        return "LabelId";
    }
    case SpvParameterId: {
        return "ParameterId";
    }
    case SpvModuleId: {
        return "ModuleId";
    }
    case SpvUnknownItem: {
        return "UnknownItem";
    }
    default: {
        return "InvalidItem";
    }
    };
    return "InvalidItem";
}

SpvKind SpvBuilder::kind_of(SpvId item_id) const {
    KindMap::const_iterator it = kind_map.find(item_id);
    if (it != kind_map.end()) {
        return it->second;
    }
    return SpvInvalidItem;
}

SpvId SpvBuilder::type_of(SpvId variable_id) const {
    VariableTypeMap::const_iterator it = variable_type_map.find(variable_id);
    if (it != variable_type_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

void SpvBuilder::finalize() {
    SpvId last_id = (SpvId)(kind_map.size() + 1);
    module.set_binding_count(last_id);

    if (module.is_capability_required(SpvCapabilityInt8)) {
        module.require_extension("SPV_KHR_8bit_storage");
    }

    if (module.is_capability_required(SpvCapabilityInt16)) {
        module.require_extension("SPV_KHR_16bit_storage");
    }
}

void SpvBuilder::encode(SpvBinary &binary) const {
    // Encode the module
    module.encode(binary);
}

SpvId SpvBuilder::declare_type(const Type &type, uint32_t array_size) {
    SpvId type_id = lookup_type(type, array_size);
    if (type_id == SpvInvalidId) {
        type_id = add_type(type, array_size);
    }
    return type_id;
}

SpvId SpvBuilder::declare_pointer_type(const Type &type, SpvStorageClass storage_class) {
    SpvId ptr_type_id = lookup_pointer_type(type, storage_class);
    if (ptr_type_id == SpvInvalidId) {
        ptr_type_id = add_pointer_type(type, storage_class);
    }
    return ptr_type_id;
}

SpvId SpvBuilder::declare_pointer_type(SpvId type_id, SpvStorageClass storage_class) {
    SpvId ptr_type_id = lookup_pointer_type(type_id, storage_class);
    if (ptr_type_id == SpvInvalidId) {
        ptr_type_id = add_pointer_type(type_id, storage_class);
    }
    return ptr_type_id;
}

SpvId SpvBuilder::declare_function_type(SpvId return_type, const ParamTypes &param_types) {
    SpvId type_id = lookup_function_type(return_type, param_types);
    if (type_id == SpvInvalidId) {
        type_id = add_function_type(return_type, param_types);
    }
    return type_id;
}

SpvId SpvBuilder::declare_function(const std::string &name, SpvId function_type) {
    SpvId existing_id = lookup_id(name);
    if (existing_id != SpvInvalidId) {
        if (kind_of(existing_id) == SpvFunctionId) {
            SpvFunction existing_func = lookup_function(existing_id);
            if (existing_func.type_id() == function_type) {
                return existing_id;
            }
        }
    }
    return add_function(name, function_type);
}

SpvId SpvBuilder::declare_constant(const Type &type, const void *data) {
    SpvId result_id = lookup_constant(type, data);
    if (result_id == SpvInvalidId) {
        result_id = add_constant(type, data);
    }
    return result_id;
}

SpvId SpvBuilder::declare_symbol(const std::string &symbol, SpvId id, SpvId scope_id) {
    SpvId existing_id = lookup_id(symbol);
    if (existing_id != SpvInvalidId) {
        SpvId existing_scope = lookup_scope(existing_id);
        if (existing_scope == scope_id) {
            return existing_id;
        }
    }
    add_symbol(symbol, id, scope_id);
    return id;
}

SpvStorageClass SpvBuilder::lookup_storage_class(SpvId id) const {
    SpvStorageClass result = SpvInvalidStorageClass;
    StorageClassMap::const_iterator it = storage_class_map.find(id);
    if (it != storage_class_map.end()) {
        result = it->second;
    }
    return result;
}

SpvId SpvBuilder::lookup_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId scope_id) const {
    SpvId existing_id = lookup_id(name);
    if (existing_id != SpvInvalidId) {
        if ((kind_of(existing_id) == SpvVariableId) &&
            (type_of(existing_id) == type_id) &&
            (lookup_storage_class(existing_id) == storage_class) &&
            (lookup_scope(existing_id) == scope_id)) {
            return existing_id;
        }
    }
    return SpvInvalidId;
}

bool SpvBuilder::has_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId scope_id) const {
    return (lookup_variable(name, type_id, storage_class, scope_id) != SpvInvalidId);
}

SpvId SpvBuilder::declare_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId init_id) {
    SpvId block_id = current_function().entry_block().id();
    SpvId existing_id = lookup_variable(name, type_id, storage_class, block_id);
    if (existing_id != SpvInvalidId) {
        return existing_id;
    }

    SpvId var_id = reserve_id(SpvVariableId);
    debug(3) << "    declare_variable: %" << var_id << " name='" << name << "' type_id=" << type_id << " storage_class=" << (uint32_t)storage_class << " init_id=" << init_id << "\n";
    current_function().entry_block().add_variable(SpvFactory::variable(var_id, type_id, storage_class, init_id));
    declare_symbol(name, var_id, block_id);
    storage_class_map[var_id] = storage_class;
    variable_type_map[var_id] = type_id;
    return var_id;
}

SpvId SpvBuilder::declare_global_variable(const std::string &name, SpvId type_id, SpvStorageClass storage_class, SpvId init_id) {
    SpvId var_id = reserve_id(SpvVariableId);
    debug(3) << "    declare_global_variable: %" << var_id << " name='" << name << "' type_id=" << type_id << " storage_class=" << (uint32_t)storage_class << " init_id=" << init_id << "\n";
    module.add_global(SpvFactory::variable(var_id, type_id, storage_class, init_id));
    declare_symbol(name, var_id, module.id());
    storage_class_map[var_id] = storage_class;
    variable_type_map[var_id] = type_id;
    return var_id;
}

void SpvBuilder::add_entry_point(SpvId func_id, SpvExecutionModel exec_model,
                                 const Variables &variables) {

    const std::string &func_name = lookup_symbol(func_id);
    if (func_name.empty()) {
        internal_error << "SPIRV: Function missing name definition: " << func_id << "\n";
    } else {
        debug(3) << "    add_entry_point: %" << func_id << " func_name='" << func_name << "' exec_model=" << (uint32_t)exec_model << " variable_count=" << (uint32_t)variables.size() << "\n";
        SpvInstruction inst = SpvFactory::entry_point(exec_model, func_id, func_name, variables);
        module.add_entry_point(func_name, inst);
    }
}

SpvId SpvBuilder::add_function(const std::string &name, SpvId return_type_id, const ParamTypes &param_types) {
    SpvId func_id = make_id(SpvFunctionId);
    SpvId func_type_id = declare_function_type(return_type_id, param_types);

    debug(3) << "    add_function: %" << func_id << " func_type_id=" << func_type_id << " return_type_id=" << return_type_id << " parameter_count=" << (uint32_t)param_types.size() << "\n";
    SpvFunction func = SpvFunction::make(func_type_id, func_id, return_type_id);
    for (SpvId param_type_id : param_types) {
        SpvId param_id = make_id(SpvParameterId);
        SpvInstruction param_inst = SpvFactory::function_parameter(param_type_id, param_id);
        func.add_parameter(param_inst);
    }
    SpvId block_id = make_id(SpvBlockId);
    SpvBlock entry_block = SpvBlock::make(func, block_id);
    func.add_block(entry_block);
    module.add_function(func);
    function_map[func_id] = func;
    declare_symbol(name, func_id, module.id());
    return func_id;
}

void SpvBuilder::add_annotation(SpvId target_id, SpvDecoration decoration_type, const Literals &literals) {
    SpvInstruction inst = SpvFactory::decorate(target_id, decoration_type, literals);
    debug(3) << "    add_annotation: %" << target_id << " decoration_type=" << uint32_t(decoration_type) << " literals=[";
    for (uint32_t v : literals) {
        debug(3) << " " << v;
    }
    debug(3) << " ]\n";
    current_module().add_annotation(inst);
}

void SpvBuilder::add_struct_annotation(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals &literals) {
    SpvInstruction inst = SpvFactory::decorate_member(struct_type_id, member_index, decoration_type, literals);
    debug(3) << "    add_struct_annotation: %" << struct_type_id << " member_index=" << member_index << " decoration_type=" << uint32_t(decoration_type) << " literals=[";
    for (uint32_t v : literals) {
        debug(3) << " " << v;
    }
    debug(3) << " ]\n";
    current_module().add_annotation(inst);
}

void SpvBuilder::add_execution_mode_local_size(SpvId func_id,
                                               uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z) {

    wg_size_x = std::max(wg_size_x, (uint32_t)1);
    wg_size_y = std::max(wg_size_y, (uint32_t)1);
    wg_size_z = std::max(wg_size_z, (uint32_t)1);

    SpvInstruction exec_mode_inst = SpvFactory::exec_mode_local_size(func_id, wg_size_x, wg_size_y, wg_size_z);
    module.add_execution_mode(exec_mode_inst);
}

void SpvBuilder::enter_block(const SpvBlock &block) {
    active_block = block;
}

SpvBlock SpvBuilder::current_block() const {
    return active_block;
}

SpvBlock SpvBuilder::create_block(SpvId block_id) {
    return current_function().create_block(block_id);
}

SpvBlock SpvBuilder::leave_block() {
    SpvBlock prev_block = active_block;
    active_block = SpvBlock();
    return prev_block;
}

SpvFunction SpvBuilder::lookup_function(SpvId func_id) const {
    SpvFunction func;
    FunctionMap::const_iterator it = function_map.find(func_id);
    if (it != function_map.end()) {
        func = it->second;
    }
    return func;
}

std::string SpvBuilder::lookup_symbol(SpvId id) const {
    std::string name;
    IdSymbolMap::const_iterator it = id_symbol_map.find(id);
    if (it != id_symbol_map.end()) {
        name = it->second;
    }
    return name;
}

SpvId SpvBuilder::lookup_id(const std::string &symbol) const {
    SpvId result = SpvInvalidId;
    SymbolIdMap::const_iterator it = symbol_id_map.find(symbol);
    if (it != symbol_id_map.end()) {
        result = it->second;
    }
    return result;
}

void SpvBuilder::add_symbol(const std::string &symbol, SpvId id, SpvId scope_id) {
    symbol_id_map[symbol] = id;
    id_symbol_map[id] = symbol;
    scope_map[id] = scope_id;
    debug(3) << "    add_symbol: %" << id << " symbol='" << symbol << "' scope_id=" << scope_id << "\n";
    module.add_debug_symbol(id, symbol);
}

SpvId SpvBuilder::lookup_scope(SpvId id) const {
    SpvId result = SpvInvalidId;
    ScopeMap::const_iterator it = scope_map.find(id);
    if (it != scope_map.end()) {
        result = it->second;
    }
    return result;
}

SpvId SpvBuilder::lookup_import(const std::string &instruction_set) const {
    return module.lookup_import(instruction_set);
}

void SpvBuilder::enter_function(const SpvFunction &func) {
    active_function = func;
    enter_block(active_function.entry_block());
}

SpvFunction SpvBuilder::current_function() const {
    return active_function;
}

SpvFunction SpvBuilder::leave_function() {
    SpvFunction prev_func = active_function;
    active_function = SpvFunction();
    return prev_func;
}

SpvId SpvBuilder::current_id() const {
    return active_id;
}

void SpvBuilder::update_id(SpvId id) {
    active_id = id;
}

SpvModule SpvBuilder::current_module() const {
    return module;
}

void SpvBuilder::set_source_language(SpvSourceLanguage val) {
    module.set_source_language(val);
}

void SpvBuilder::set_addressing_model(SpvAddressingModel val) {
    module.set_addressing_model(val);
}

void SpvBuilder::set_memory_model(SpvMemoryModel val) {
    module.set_memory_model(val);
}

SpvSourceLanguage SpvBuilder::source_language() const {
    return module.source_language();
}

SpvAddressingModel SpvBuilder::addressing_model() const {
    return module.addressing_model();
}

SpvMemoryModel SpvBuilder::memory_model() const {
    return module.memory_model();
}

SpvId SpvBuilder::import_glsl_intrinsics() {
    return import_instruction_set("GLSL.std.450");
}

SpvId SpvBuilder::import_instruction_set(const std::string &instruction_set) {
    SpvId result_id = module.lookup_import(instruction_set);
    if (result_id == SpvInvalidId) {
        result_id = make_id(SpvImportId);
        module.import_instruction_set(result_id, instruction_set);
    }
    return result_id;
}

void SpvBuilder::require_capability(SpvCapability capability) {
    if (!module.is_capability_required(capability)) {
        module.require_capability(capability);
    }
}

bool SpvBuilder::is_imported(const std::string &instruction_set) const {
    return module.is_imported(instruction_set);
}

bool SpvBuilder::is_capability_required(SpvCapability capability) const {
    return module.is_capability_required(capability);
}

void SpvBuilder::require_extension(const std::string &extension) {
    if (!module.is_extension_required(extension)) {
        module.require_extension(extension);
    }
}

bool SpvBuilder::is_extension_required(const std::string &extension) const {
    return module.is_extension_required(extension);
}

SpvBuilder::TypeKey SpvBuilder::make_type_key(const Type &type, uint32_t array_size) const {
    TypeKey key = hash_splitmix64(type.code());
    key = hash_combine(key, type.bits());
    key = hash_combine(key, type.lanes());
    key = hash_combine(key, type.bytes());
    key = hash_combine(key, array_size);
    return key;
}

SpvId SpvBuilder::lookup_type(const Type &type, uint32_t array_size) const {
    SpvBuilder::TypeKey type_key = make_type_key(type, array_size);
    TypeMap::const_iterator it = type_map.find(type_key);
    if (it == type_map.end()) {
        return SpvInvalidId;
    }
    return it->second;
}

SpvId SpvBuilder::add_type(const Type &type, uint32_t array_size) {
    SpvBuilder::TypeKey type_key = make_type_key(type, array_size);
    TypeMap::const_iterator it = type_map.find(type_key);
    if (it != type_map.end()) {
        return it->second;
    }

    if (array_size > 1) {

        // first declare the array size as a uint32 constant value
        Type array_size_type = UInt(32);
        ConstantKey constant_key = make_constant_key(array_size_type, &array_size);
        SpvId array_size_id = make_id(SpvIntConstantId);
        SpvId array_size_type_id = add_type(array_size_type);
        SpvInstruction array_size_inst = SpvFactory::constant(array_size_id, array_size_type_id, array_size_type.bytes(), &array_size, SpvIntegerData);
        module.add_type(array_size_inst);  // needs to be defined in the type section (prior to its use in the array_type inst)
        constant_map[constant_key] = array_size_id;

        // declare the array type
        SpvId array_type_id = make_id(SpvArrayTypeId);
        SpvId element_type_id = add_type(type, 1);
        debug(3) << "    add_array_type: %" << array_type_id << " element_type_id='" << element_type_id << " array_size='" << array_size << "\n";
        SpvInstruction inst = SpvFactory::array_type(array_type_id, element_type_id, array_size_id);
        module.add_type(inst);
        type_map[type_key] = array_type_id;
        return array_type_id;
    }

    SpvId type_id = SpvInvalidId;
    if (type.is_vector()) {
        type_id = make_id(SpvVectorTypeId);
        SpvId element_type_id = add_type(type.with_lanes(1));
        debug(3) << "    add_vector_type: %" << type_id << " element_type_id='" << element_type_id << " lanes='" << type.lanes() << "\n";
        SpvInstruction inst = SpvFactory::vector_type(type_id, element_type_id, type.lanes());
        module.add_type(inst);
    } else {
        if (type.is_handle()) {
            type_id = make_id(SpvVoidTypeId);
            SpvInstruction inst = SpvFactory::void_type(type_id);
            debug(3) << "    add_void_type: %" << type_id << "\n";
            module.add_type(inst);
        } else if (type.is_bool()) {
            type_id = make_id(SpvBoolTypeId);
            debug(3) << "    add_bool_type: %" << type_id << "\n";
            SpvInstruction inst = SpvFactory::bool_type(type_id);
            module.add_type(inst);
        } else if (type.is_float()) {
            type_id = make_id(SpvFloatTypeId);
            debug(3) << "    add_float_type: %" << type_id << " bits=" << type.bits() << "\n";
            SpvInstruction inst = SpvFactory::float_type(type_id, type.bits());
            module.add_type(inst);
            if (type.bits() == 16) {
                module.require_capability(SpvCapabilityFloat16);
            } else if (type.bits() == 64) {
                module.require_capability(SpvCapabilityFloat64);
            }
        } else if (type.is_int_or_uint()) {
            SpvId signedness = 0;
            bool signedness_support = module.is_capability_required(SpvCapabilityKernel) ? false : true;  // kernel execution doesn't track signedness
            if (signedness_support) {
                signedness = type.is_uint() ? 0 : 1;
            }

            type_id = make_id(signedness ? SpvIntTypeId : SpvUIntTypeId);
            debug(3) << "    add_integer_type: %" << type_id << " bits=" << type.bits() << " signed=" << (signedness ? "true" : "false") << "\n";
            SpvInstruction inst = SpvFactory::integer_type(type_id, type.bits(), signedness);
            module.add_type(inst);
            if (type.bits() == 8) {
                module.require_capability(SpvCapabilityInt8);
            } else if (type.bits() == 16) {
                module.require_capability(SpvCapabilityInt16);
            } else if (type.bits() == 64) {
                module.require_capability(SpvCapabilityInt64);
            }
        } else {
            internal_error << "SPIRV: Unsupported type " << type << "\n";
        }
    }

    type_map[type_key] = type_id;
    return type_id;
}

SpvId SpvBuilder::declare_void_type() {
    return declare_type(Handle());
}

SpvBuilder::TypeKey SpvBuilder::make_struct_type_key(const StructMemberTypes &member_type_ids) const {
    TypeKey key = hash_splitmix64(member_type_ids.size());
    for (SpvId type_id : member_type_ids) {
        key = hash_combine(key, type_id);
    }
    return key;
}

SpvId SpvBuilder::lookup_struct(const std::string &struct_name, const StructMemberTypes &member_type_ids) const {
    TypeKey key = make_struct_type_key(member_type_ids);
    TypeMap::const_iterator it = struct_map.find(key);
    if (it != struct_map.end()) {
        if (struct_name == lookup_symbol(it->second)) {
            return it->second;
        }
    }
    return SpvInvalidId;
}

SpvId SpvBuilder::add_struct(const std::string &struct_name, const StructMemberTypes &member_type_ids) {
    TypeKey key = make_struct_type_key(member_type_ids);
    TypeMap::const_iterator it = struct_map.find(key);
    if (it != struct_map.end()) {
        if (struct_name == lookup_symbol(it->second)) {
            return it->second;
        }
    }

    SpvId struct_type_id = make_id(SpvStructTypeId);
    debug(3) << "    add_struct_type: %" << struct_type_id << " name=" << struct_name << " member_type_ids=[";
    for (SpvId m : member_type_ids) {
        debug(3) << " " << m;
    }
    debug(3) << " ]\n";
    SpvInstruction inst = SpvFactory::struct_type(struct_type_id, member_type_ids);
    module.add_type(inst);
    struct_map[key] = struct_type_id;
    add_symbol(struct_name, struct_type_id, module.id());
    return struct_type_id;
}

SpvId SpvBuilder::declare_struct(const std::string &struct_name, const StructMemberTypes &member_types) {
    SpvId struct_id = lookup_struct(struct_name, member_types);
    if (struct_id == SpvInvalidId) {
        struct_id = add_struct(struct_name, member_types);
    }
    return struct_id;
}

SpvBuilder::PointerTypeKey SpvBuilder::make_pointer_type_key(const Type &type, SpvStorageClass storage_class) const {
    SpvId base_type_id = lookup_type(type);
    if (base_type_id == SpvInvalidId) {
        internal_error << "SPIRV: Attempted to declare pointer type for undeclared base type! " << type << "\n";
    }
    return std::make_pair(base_type_id, storage_class);
}

SpvBuilder::PointerTypeKey SpvBuilder::make_pointer_type_key(SpvId base_type_id, SpvStorageClass storage_class) const {
    return std::make_pair(base_type_id, storage_class);
}

SpvId SpvBuilder::lookup_pointer_type(const Type &type, SpvStorageClass storage_class) const {
    SpvId base_type_id = lookup_type(type);
    if (base_type_id == SpvInvalidId) {
        internal_error << "SPIRV: Attempted to lookup pointer type for undeclared base type! " << type << "\n";
    }
    return lookup_pointer_type(base_type_id, storage_class);
}

SpvId SpvBuilder::lookup_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) const {
    PointerTypeKey key = make_pointer_type_key(base_type_id, storage_class);
    PointerTypeMap::const_iterator it = pointer_type_map.find(key);
    if (it != pointer_type_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

SpvId SpvBuilder::add_pointer_type(const Type &type, SpvStorageClass storage_class) {
    SpvId base_type_id = declare_type(type);
    debug(3) << "    add_pointer_type: base_type=" << type << " base_type_id=" << base_type_id << " storage_class=" << (uint32_t)(storage_class) << "\n";
    if (base_type_id == SpvInvalidId) {
        internal_error << "SPIRV: Attempted to create pointer type for undeclared base type! " << type << "\n";
    }
    return add_pointer_type(base_type_id, storage_class);
}

SpvId SpvBuilder::add_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) {
    if (base_type_id == SpvInvalidId) {
        internal_error << "SPIRV: Attempted to create pointer type for undeclared base type!\n";
    }

    PointerTypeKey key = make_pointer_type_key(base_type_id, storage_class);
    PointerTypeMap::const_iterator it = pointer_type_map.find(key);
    if (it != pointer_type_map.end()) {
        return it->second;
    }

    SpvId pointer_type_id = make_id(SpvPointerTypeId);
    debug(3) << "    add_pointer_type: %" << pointer_type_id << " base_type_id=" << base_type_id << " storage_class=" << (uint32_t)(storage_class) << "\n";
    SpvInstruction inst = SpvFactory::pointer_type(pointer_type_id, storage_class, base_type_id);
    module.add_type(inst);
    pointer_type_map[key] = pointer_type_id;
    storage_class_map[pointer_type_id] = storage_class;
    base_type_map[pointer_type_id] = base_type_id;
    return pointer_type_id;
}

SpvBuilder::ConstantKey SpvBuilder::make_constant_key(uint8_t code, uint8_t bits, int lanes, size_t bytes, const void *data) const {
    ConstantKey key = hash_splitmix64(code);
    key = hash_combine(key, bits);
    key = hash_combine(key, lanes);
    key = hash_combine(key, bytes);

    if(data != nullptr) {
        const int8_t *ptr = reinterpret_bits<const int8_t *>(data);
        for (size_t i = 0; i < bytes; ++i) {
            key = hash_combine(key, uint64_t(ptr[i]));
        }
    }
    return key;
}

SpvBuilder::ConstantKey SpvBuilder::make_constant_key(const Type &type, const void *data) const {
    return make_constant_key(type.code(), type.bits(), type.lanes(), type.bytes(), data);
}

SpvBuilder::ConstantKey SpvBuilder::make_bool_constant_key(bool value) const {
    Type type = Bool();
    bool data = value;
    return make_constant_key(type, &data);
}

SpvBuilder::ConstantKey SpvBuilder::make_string_constant_key(const std::string &value) const {
    return make_constant_key(halide_type_handle, 8, 1, value.length(), (const char *)(value.c_str()));
}

SpvBuilder::ConstantKey SpvBuilder::make_null_constant_key(const Type &type) const {
    return make_constant_key(type.code(), type.bits(), type.lanes(), type.bytes(), nullptr);
}

SpvId SpvBuilder::lookup_null_constant(const Type &type) const {
    ConstantKey key = make_null_constant_key(type);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_null_constant(const Type &type) {
    ConstantKey key = make_null_constant_key(type);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    SpvId result_id = make_id(SpvConstantId);
    SpvId type_id = add_type(type);

    debug(3) << "    declare_null_constant: %" << result_id << " " << type << "\n";
    SpvInstruction inst = SpvFactory::null_constant(result_id, type_id);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_bool_constant(bool value) {
    ConstantKey key = make_bool_constant_key(value);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    Type type = Bool();
    SpvId result_id = make_id(SpvBoolConstantId);
    SpvId type_id = add_type(type);

    debug(3) << "    declare_bool_constant: %" << result_id << " bool " << value << "\n";
    SpvInstruction inst = SpvFactory::bool_constant(result_id, type_id, value);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_string_constant(const std::string &value) {
    ConstantKey key = make_string_constant_key(value);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    SpvId result_id = make_id(SpvStringConstantId);
    debug(3) << "    declare_string_constant: %" << result_id << " string '" << value << "'\n";
    SpvInstruction inst = SpvFactory::string_constant(result_id, value);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_scalar_constant(const Type &scalar_type, const void *data) {
    if (scalar_type.lanes() != 1) {
        internal_error << "SPIRV: Invalid type provided for scalar constant!" << scalar_type << "\n";
        return SpvInvalidId;
    }

    ConstantKey constant_key = make_constant_key(scalar_type, data);
    ConstantMap::const_iterator it = constant_map.find(constant_key);
    if (it != constant_map.end()) {
        return it->second;
    }

    if (scalar_type.is_bool() && data) {
        bool value = *reinterpret_cast<const bool *>(data);
        return declare_bool_constant(value);
    }

    SpvId result_id = SpvInvalidId;
    SpvValueType value_type = SpvInvalidValueType;
    if (scalar_type.is_float()) {
        result_id = make_id(SpvFloatConstantId);
        value_type = SpvFloatData;
        debug(3) << "    declare_scalar_constant: %" << result_id << " type=" << scalar_type << " data=" << (*(const float *)(data)) << "\n";
    } else if (scalar_type.is_int_or_uint()) {
        result_id = make_id(SpvIntConstantId);
        value_type = SpvIntegerData;
        debug(3) << "    declare_scalar_constant: %" << result_id << " type=" << scalar_type << " data=" << (*(const int32_t *)(data)) << "\n";
    } else {
        internal_error << "SPIRV: Unsupported type:" << scalar_type << "\n";
        return SpvInvalidId;
    }

    SpvId type_id = add_type(scalar_type);
    SpvInstruction inst = SpvFactory::constant(result_id, type_id, scalar_type.bytes(), data, value_type);
    module.add_constant(inst);
    constant_map[constant_key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_vector_constant(const Type &type, const void *data) {
    if (type.lanes() == 1) {
        internal_error << "SPIRV: Invalid type provided for vector constant!" << type << "\n";
        return SpvInvalidId;
    }

    ConstantKey key = make_constant_key(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    Type scalar_type = type.with_lanes(1);
    std::vector<SpvId> components(type.lanes());
    if (scalar_type.is_float()) {
        if (type.bits() == 64) {
            const double *values = (const double *)data;
            for (int c = 0; c < type.lanes(); c++) {
                const double *entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void *)entry);
                components.push_back(scalar_id);
            }
        } else {
            const float *values = (const float *)data;
            for (int c = 0; c < type.lanes(); c++) {
                const float *entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void *)entry);
                components.push_back(scalar_id);
            }
        }
    } else if (scalar_type.is_bool()) {
        const bool *values = (const bool *)data;
        for (int c = 0; c < type.lanes(); c++) {
            const bool *entry = &(values[c]);
            SpvId scalar_id = declare_scalar_constant(scalar_type, (const void *)entry);
            components.push_back(scalar_id);
        }
    } else if (scalar_type.is_int_or_uint()) {
        if (type.bits() == 64) {
            const uint64_t *values = (const uint64_t *)data;
            for (int c = 0; c < type.lanes(); c++) {
                const uint64_t *entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void *)entry);
                components.push_back(scalar_id);
            }
        } else {
            const uint32_t *values = (const uint32_t *)data;
            for (int c = 0; c < type.lanes(); c++) {
                const uint32_t *entry = &(values[c]);
                SpvId scalar_id = declare_scalar_constant(scalar_type, (const void *)entry);
                components.push_back(scalar_id);
            }
        }
    } else {
        internal_error << "SPIRV: Unsupported type:" << type << "\n";
        return SpvInvalidId;
    }

    SpvId result_id = make_id(SpvCompositeConstantId);
    SpvId type_id = add_type(type);
    debug(3) << "    declare_vector_constant: %" << result_id << " key=" << key << " type=" << type << " data=" << data << "\n";
    SpvInstruction inst = SpvFactory::composite_constant(result_id, type_id, components);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::lookup_constant(const Type &type, const void *data) const {
    ConstantKey key = make_constant_key(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

SpvId SpvBuilder::add_constant(const Type &type, const void *data) {

    ConstantKey key = make_constant_key(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    if (type.lanes() == 1) {
        return declare_scalar_constant(type, data);
    } else {
        return declare_vector_constant(type, data);
    }
}

SpvId SpvBuilder::declare_access_chain(SpvId ptr_type_id, SpvId base_id, const Indices &indices) {
    SpvId access_chain_id = make_id(SpvAccessChainId);
    append(SpvFactory::in_bounds_access_chain(ptr_type_id, access_chain_id, base_id, indices));
    return access_chain_id;
}

SpvId SpvBuilder::declare_pointer_access_chain(SpvId ptr_type_id, SpvId base_id, SpvId element_id, const Indices &indices) {
    SpvId access_chain_id = make_id(SpvAccessChainId);
    append(SpvFactory::pointer_access_chain(ptr_type_id, access_chain_id, base_id, element_id, indices));
    return access_chain_id;
}

SpvBuilder::FunctionTypeKey SpvBuilder::make_function_type_key(SpvId return_type_id, const ParamTypes &param_type_ids) const {
    TypeKey key = hash_splitmix64(return_type_id);
    for (SpvId type_id : param_type_ids) {
        key = hash_combine(key, type_id);
    }
    return key;
}

SpvId SpvBuilder::lookup_function_type(SpvId return_type_id, const ParamTypes &param_type_ids) const {
    FunctionTypeKey key = make_function_type_key(return_type_id, param_type_ids);
    FunctionTypeMap::const_iterator it = function_type_map.find(key);
    if (it != function_type_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

SpvId SpvBuilder::add_function_type(SpvId return_type_id, const ParamTypes &param_type_ids) {
    FunctionTypeKey func_type_key = make_function_type_key(return_type_id, param_type_ids);
    FunctionTypeMap::const_iterator it = function_type_map.find(func_type_key);
    if (it != function_type_map.end()) {
        return it->second;
    }

    SpvId function_type_id = make_id(SpvFunctionTypeId);
    debug(3) << "    add_function_type: %" << function_type_id << " return_type_id=" << return_type_id << " param_type_ids=[";
    for (SpvId p : param_type_ids) {
        debug(3) << " " << p;
    }
    debug(3) << " ]\n";
    SpvInstruction inst = SpvFactory::function_type(function_type_id, return_type_id, param_type_ids);
    module.add_type(inst);
    function_type_map[func_type_key] = function_type_id;
    return function_type_id;
}

SpvId SpvBuilder::add_runtime_array(SpvId base_type_id) {
    SpvId runtime_array_id = make_id(SpvRuntimeArrayTypeId);
    SpvInstruction inst = SpvFactory::runtime_array_type(runtime_array_id, base_type_id);
    module.add_type(inst);
    return runtime_array_id;
}

bool SpvBuilder::is_pointer_type(SpvId id) const {
    BaseTypeMap::const_iterator it = base_type_map.find(id);
    if (it != base_type_map.end()) {
        return true;
    }
    return false;
}

bool SpvBuilder::is_struct_type(SpvId id) const {
    SpvKind kind = kind_of(id);
    if (kind == SpvStructTypeId) {
        return true;
    }
    return false;
}

bool SpvBuilder::is_vector_type(SpvId id) const {
    SpvKind kind = kind_of(id);
    if (kind == SpvVectorTypeId) {
        return true;
    }
    return false;
}

bool SpvBuilder::is_scalar_type(SpvId id) const {
    SpvKind kind = kind_of(id);
    if ((kind == SpvFloatTypeId) ||
        (kind == SpvIntTypeId) ||
        (kind == SpvBoolTypeId)) {
        return true;
    }
    return false;
}

bool SpvBuilder::is_array_type(SpvId id) const {
    SpvKind kind = kind_of(id);
    if (kind == SpvArrayTypeId) {
        return true;
    }
    return false;
}

bool SpvBuilder::is_constant(SpvId id) const {
    SpvKind kind = kind_of(id);
    if ((kind == SpvConstantId) ||
        (kind == SpvBoolConstantId) ||
        (kind == SpvIntConstantId) ||
        (kind == SpvFloatConstantId) ||
        (kind == SpvStringConstantId) ||
        (kind == SpvCompositeConstantId)) {
        return true;
    }
    return false;
}

SpvId SpvBuilder::lookup_base_type(SpvId pointer_type) const {
    BaseTypeMap::const_iterator it = base_type_map.find(pointer_type);
    if (it != base_type_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

void SpvBuilder::append(SpvInstruction inst) {
    if (active_block.is_defined()) {
        active_block.add_instruction(std::move(inst));
    } else {
        internal_error << "SPIRV: Current block undefined! Unable to append!\n";
    }
}

// --

// -- Factory Methods for Specific Instructions

SpvInstruction SpvFactory::no_op(SpvId result_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpNop);
    return inst;
}

SpvInstruction SpvFactory::label(SpvId result_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpLabel);
    inst.set_result_id(result_id);
    return inst;
}

SpvInstruction SpvFactory::debug_line(SpvId string_id, uint32_t line, uint32_t column) {
    SpvInstruction inst = SpvInstruction::make(SpvOpLine);
    inst.add_operand(string_id);
    inst.add_immediate(line, SpvIntegerLiteral);
    inst.add_immediate(column, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::debug_string(SpvId result_id, const std::string &string) {
    SpvInstruction inst = SpvInstruction::make(SpvOpString);
    inst.set_result_id(result_id);
    inst.add_string(string);
    return inst;
}

SpvInstruction SpvFactory::debug_symbol(SpvId target_id, const std::string &symbol) {
    SpvInstruction inst = SpvInstruction::make(SpvOpName);
    inst.set_result_id(target_id);
    inst.add_string(symbol);
    return inst;
}

SpvInstruction SpvFactory::decorate(SpvId target_id, SpvDecoration decoration_type, const SpvFactory::Literals &literals) {
    SpvInstruction inst = SpvInstruction::make(SpvOpDecorate);
    inst.add_operand(target_id);
    inst.add_immediate(decoration_type, SpvIntegerLiteral);
    for (uint32_t l : literals) {
        inst.add_immediate(l, SpvIntegerLiteral);
    }
    return inst;
}

SpvInstruction SpvFactory::decorate_member(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const SpvFactory::Literals &literals) {
    SpvInstruction inst = SpvInstruction::make(SpvOpMemberDecorate);
    inst.add_operand(struct_type_id);
    inst.add_immediate(member_index, SpvIntegerLiteral);
    inst.add_immediate(decoration_type, SpvIntegerLiteral);
    for (uint32_t l : literals) {
        inst.add_immediate(l, SpvIntegerLiteral);
    }
    return inst;
}

SpvInstruction SpvFactory::unary_op(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id) {
    SpvInstruction inst = SpvInstruction::make(op_code);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_id);
    return inst;
}

SpvInstruction SpvFactory::binary_op(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    SpvInstruction inst = SpvInstruction::make(op_code);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::convert(SpvOp op_code, SpvId type_id, SpvId result_id, SpvId src_id) {
    SpvInstruction inst = SpvInstruction::make(op_code);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_id);
    return inst;
}

SpvInstruction SpvFactory::void_type(SpvId void_type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeVoid);
    inst.set_result_id(void_type_id);
    return inst;
}

SpvInstruction SpvFactory::bool_type(SpvId bool_type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeBool);
    inst.set_result_id(bool_type_id);
    return inst;
}

SpvInstruction SpvFactory::integer_type(SpvId int_type_id, uint32_t bits, uint32_t signedness) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeInt);
    inst.set_result_id(int_type_id);
    inst.add_immediate(bits, SpvIntegerLiteral);
    inst.add_immediate(signedness, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::float_type(SpvId float_type_id, uint32_t bits) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeFloat);
    inst.set_result_id(float_type_id);
    inst.add_immediate(bits, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::vector_type(SpvId vector_type_id, SpvId element_type_id, uint32_t vector_size) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeVector);
    inst.set_result_id(vector_type_id);
    inst.add_operand(element_type_id);
    inst.add_immediate(vector_size, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::array_type(SpvId array_type_id, SpvId element_type_id, SpvId array_size_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeArray);
    inst.set_result_id(array_type_id);
    inst.add_operand(element_type_id);
    inst.add_operand(array_size_id);
    return inst;
}

SpvInstruction SpvFactory::struct_type(SpvId result_id, const SpvFactory::MemberTypeIds &member_type_ids) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeStruct);
    inst.set_result_id(result_id);
    for (const SpvId member_type : member_type_ids) {
        inst.add_operand(member_type);
    }
    return inst;
}

SpvInstruction SpvFactory::runtime_array_type(SpvId result_type_id, SpvId base_type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeRuntimeArray);
    inst.set_result_id(result_type_id);
    inst.add_operand(base_type_id);
    return inst;
}

SpvInstruction SpvFactory::pointer_type(SpvId pointer_type_id, SpvStorageClass storage_class, SpvId base_type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypePointer);
    inst.set_result_id(pointer_type_id);
    inst.add_immediate(storage_class, SpvIntegerLiteral);
    inst.add_operand(base_type_id);
    return inst;
}

SpvInstruction SpvFactory::function_type(SpvId function_type_id, SpvId return_type_id, const SpvFactory::ParamTypes &param_type_ids) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeFunction);
    inst.set_result_id(function_type_id);
    inst.add_operand(return_type_id);
    for (SpvId type_id : param_type_ids) {
        inst.add_operand(type_id);
    }
    return inst;
}

SpvInstruction SpvFactory::constant(SpvId result_id, SpvId type_id, size_t bytes, const void *data, SpvValueType value_type) {
    SpvInstruction inst = SpvInstruction::make(SpvOpConstant);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_data(bytes, data, value_type);
    return inst;
}

SpvInstruction SpvFactory::null_constant(SpvId result_id, SpvId type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpConstantNull);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    return inst;
}

SpvInstruction SpvFactory::bool_constant(SpvId result_id, SpvId type_id, bool value) {
    SpvOp op_code = value ? SpvOpConstantTrue : SpvOpConstantFalse;
    SpvInstruction inst = SpvInstruction::make(op_code);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    return inst;
}

SpvInstruction SpvFactory::string_constant(SpvId result_id, const std::string &value) {
    SpvInstruction inst = SpvInstruction::make(SpvOpString);
    inst.set_result_id(result_id);
    inst.add_string(value);
    return inst;
}

SpvInstruction SpvFactory::composite_constant(SpvId result_id, SpvId type_id, const SpvFactory::Components &components) {
    SpvInstruction inst = SpvInstruction::make(SpvOpConstantComposite);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    for (SpvId scalar_id : components) {
        inst.add_operand(scalar_id);
    }
    return inst;
}

SpvInstruction SpvFactory::variable(SpvId result_id, SpvId result_type_id, uint32_t storage_class, SpvId initializer_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpVariable);
    inst.set_type_id(result_type_id);
    inst.set_result_id(result_id);
    inst.add_immediate(storage_class, SpvIntegerLiteral);
    if (initializer_id != SpvInvalidId) {
        inst.add_operand(initializer_id);
    }
    return inst;
}

SpvInstruction SpvFactory::function(SpvId return_type_id, SpvId func_id, uint32_t control_mask, SpvId func_type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpFunction);
    inst.set_type_id(return_type_id);
    inst.set_result_id(func_id);
    inst.add_immediate(control_mask, SpvBitMaskLiteral);
    inst.add_operand(func_type_id);
    return inst;
}

SpvInstruction SpvFactory::function_parameter(SpvId param_type_id, SpvId param_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpFunctionParameter);
    inst.set_type_id(param_type_id);
    inst.set_result_id(param_id);
    return inst;
}

SpvInstruction SpvFactory::function_end() {
    SpvInstruction inst = SpvInstruction::make(SpvOpFunctionEnd);
    return inst;
}

SpvInstruction SpvFactory::return_stmt(SpvId return_value_id) {
    SpvOp opcode = (return_value_id == SpvInvalidId) ? SpvOpReturn : SpvOpReturnValue;
    SpvInstruction inst = SpvInstruction::make(opcode);
    if (return_value_id != SpvInvalidId) {
        inst.add_operand(return_value_id);
    }
    return inst;
}

SpvInstruction SpvFactory::entry_point(SpvId exec_model, SpvId func_id, const std::string &name, const SpvFactory::Variables &variables) {
    SpvInstruction inst = SpvInstruction::make(SpvOpEntryPoint);
    inst.add_immediate(exec_model, SpvIntegerLiteral);
    inst.add_operand(func_id);
    inst.add_string(name);
    for (SpvId var : variables) {
        inst.add_operand(var);
    }
    return inst;
}

SpvInstruction SpvFactory::memory_model(SpvAddressingModel addressing_model, SpvMemoryModel memory_model) {
    SpvInstruction inst = SpvInstruction::make(SpvOpMemoryModel);
    inst.add_immediate(addressing_model, SpvIntegerLiteral);
    inst.add_immediate(memory_model, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::exec_mode_local_size(SpvId function_id, uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExecutionMode);
    inst.add_operand(function_id);
    inst.add_immediate(SpvExecutionModeLocalSize, SpvIntegerLiteral);
    inst.add_immediate(wg_size_x, SpvIntegerLiteral);
    inst.add_immediate(wg_size_y, SpvIntegerLiteral);
    inst.add_immediate(wg_size_z, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::memory_barrier(SpvId memory_scope_id, SpvId semantics_mask_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpMemoryBarrier);
    inst.add_operand(memory_scope_id);
    inst.add_operand(semantics_mask_id);
    return inst;
}

SpvInstruction SpvFactory::control_barrier(SpvId execution_scope_id, SpvId memory_scope_id, SpvId semantics_mask_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpControlBarrier);
    inst.add_operand(execution_scope_id);
    inst.add_operand(memory_scope_id);
    inst.add_operand(semantics_mask_id);
    return inst;
}

SpvInstruction SpvFactory::bitwise_not(SpvId type_id, SpvId result_id, SpvId src_id) {
    return unary_op(SpvOpNot, type_id, result_id, src_id);
}

SpvInstruction SpvFactory::bitwise_and(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    return binary_op(SpvOpBitwiseAnd, type_id, result_id, src_a_id, src_b_id);
}

SpvInstruction SpvFactory::logical_not(SpvId type_id, SpvId result_id, SpvId src_id) {
    return unary_op(SpvOpLogicalNot, type_id, result_id, src_id);
}

SpvInstruction SpvFactory::logical_and(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    return binary_op(SpvOpLogicalAnd, type_id, result_id, src_a_id, src_b_id);
}

SpvInstruction SpvFactory::shift_right_logical(SpvId type_id, SpvId result_id, SpvId src_id, SpvId shift_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpShiftRightLogical);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_id);
    inst.add_operand(shift_id);
    return inst;
}

SpvInstruction SpvFactory::shift_right_arithmetic(SpvId type_id, SpvId result_id, SpvId src_id, SpvId shift_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpShiftRightArithmetic);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_id);
    inst.add_operand(shift_id);
    return inst;
}

SpvInstruction SpvFactory::multiply_extended(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed) {
    return binary_op(is_signed ? SpvOpSMulExtended : SpvOpUMulExtended, type_id, result_id, src_a_id, src_b_id);
}

SpvInstruction SpvFactory::select(SpvId type_id, SpvId result_id, SpvId condition_id, SpvId true_id, SpvId false_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpSelect);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(condition_id);
    inst.add_operand(true_id);
    inst.add_operand(false_id);
    return inst;
}

SpvInstruction SpvFactory::in_bounds_access_chain(SpvId type_id, SpvId result_id, SpvId base_id, const SpvFactory::Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpInBoundsAccessChain);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(base_id);
    for (SpvId i : indices) {
        inst.add_operand(i);
    }
    return inst;
}

SpvInstruction SpvFactory::pointer_access_chain(SpvId type_id, SpvId result_id, SpvId base_id, SpvId element_id, const SpvFactory::Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpPtrAccessChain);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(base_id);
    inst.add_operand(element_id);
    for (SpvId i : indices) {
        inst.add_operand(i);
    }
    return inst;
}

SpvInstruction SpvFactory::load(SpvId type_id, SpvId result_id, SpvId ptr_id, uint32_t access_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpLoad);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(ptr_id);
    inst.add_immediate(access_mask, SpvBitMaskLiteral);
    return inst;
}

SpvInstruction SpvFactory::store(SpvId ptr_id, SpvId obj_id, uint32_t access_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpStore);
    inst.add_operand(ptr_id);
    inst.add_operand(obj_id);
    inst.add_immediate(access_mask, SpvBitMaskLiteral);
    return inst;
}

SpvInstruction SpvFactory::composite_insert(SpvId type_id, SpvId result_id, SpvId object_id, SpvId composite_id, const SpvFactory::Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpCompositeInsert);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(object_id);
    inst.add_operand(composite_id);
    for (SpvId i : indices) {
        inst.add_immediate(i, SpvIntegerLiteral);
    }
    return inst;
}

SpvInstruction SpvFactory::composite_extract(SpvId type_id, SpvId result_id, SpvId composite_id, const SpvFactory::Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpCompositeExtract);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(composite_id);
    for (SpvId i : indices) {
        inst.add_immediate(i, SpvIntegerLiteral);
    }
    return inst;
}

SpvInstruction SpvFactory::composite_construct(SpvId type_id, SpvId result_id, const Components &constituents) {
    SpvInstruction inst = SpvInstruction::make(SpvOpCompositeConstruct);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    for (SpvId id : constituents) {
        inst.add_operand(id);
    }
    return inst;
}

SpvInstruction SpvFactory::vector_insert_dynamic(SpvId type_id, SpvId result_id, SpvId vector_id, SpvId value_id, SpvId index_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpVectorInsertDynamic);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(vector_id);
    inst.add_operand(value_id);
    inst.add_operand(index_id);
    return inst;
}

SpvInstruction SpvFactory::vector_extract_dynamic(SpvId type_id, SpvId result_id, SpvId vector_id, SpvId value_id, SpvId index_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpVectorExtractDynamic);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(vector_id);
    inst.add_operand(value_id);
    inst.add_operand(index_id);
    return inst;
}

SpvInstruction SpvFactory::vector_shuffle(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, const Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpVectorShuffle);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    for (SpvId i : indices) {
        inst.add_immediate(i, SpvIntegerLiteral);
    }
    return inst;
}

SpvInstruction SpvFactory::is_inf(SpvId type_id, SpvId result_id, SpvId src_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpIsInf);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_id);
    return inst;
}

SpvInstruction SpvFactory::is_nan(SpvId type_id, SpvId result_id, SpvId src_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpIsNan);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_id);
    return inst;
}

SpvInstruction SpvFactory::bitcast(SpvId type_id, SpvId result_id, SpvId src_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpBitcast);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(src_id);
    return inst;
}

SpvInstruction SpvFactory::integer_add(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    return binary_op(SpvOpIAdd, type_id, result_id, src_a_id, src_b_id);
}

SpvInstruction SpvFactory::float_add(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    return binary_op(SpvOpFAdd, type_id, result_id, src_a_id, src_b_id);
}

SpvInstruction SpvFactory::branch(SpvId target_label_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpBranch);
    inst.add_operand(target_label_id);
    return inst;
}

SpvInstruction SpvFactory::conditional_branch(SpvId condition_label_id, SpvId true_label_id, SpvId false_label_id, const SpvFactory::BranchWeights &weights) {
    SpvInstruction inst = SpvInstruction::make(SpvOpBranchConditional);
    inst.add_operand(condition_label_id);
    inst.add_operand(true_label_id);
    inst.add_operand(false_label_id);
    for (uint32_t w : weights) {
        inst.add_immediate(w, SpvIntegerLiteral);
    }
    return inst;
}

SpvInstruction SpvFactory::equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpIEqual);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::not_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpINotEqual);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::less_than(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed) {
    SpvInstruction inst = SpvInstruction::make(is_signed ? SpvOpSLessThan : SpvOpULessThan);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::less_than_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed) {
    SpvInstruction inst = SpvInstruction::make(is_signed ? SpvOpSLessThanEqual : SpvOpULessThanEqual);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::greater_than(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed) {
    SpvInstruction inst = SpvInstruction::make(is_signed ? SpvOpSGreaterThan : SpvOpUGreaterThan);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::greater_than_equal(SpvId type_id, SpvId result_id, SpvId src_a_id, SpvId src_b_id, bool is_signed) {
    SpvInstruction inst = SpvInstruction::make(is_signed ? SpvOpSGreaterThanEqual : SpvOpUGreaterThanEqual);
    inst.add_operand(type_id);
    inst.add_operand(result_id);
    inst.add_operand(src_a_id);
    inst.add_operand(src_b_id);
    return inst;
}

SpvInstruction SpvFactory::loop_merge(SpvId merge_label_id, SpvId continue_label_id, uint32_t loop_control_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpLoopMerge);
    inst.add_operand(merge_label_id);
    inst.add_operand(continue_label_id);
    inst.add_immediate(loop_control_mask, SpvBitMaskLiteral);
    return inst;
}

SpvInstruction SpvFactory::selection_merge(SpvId merge_label_id, uint32_t selection_control_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpSelectionMerge);
    inst.add_operand(merge_label_id);
    inst.add_immediate(selection_control_mask, SpvBitMaskLiteral);
    return inst;
}

SpvInstruction SpvFactory::phi(SpvId type_id, SpvId result_id, const SpvFactory::BlockVariables &block_vars) {
    SpvInstruction inst = SpvInstruction::make(SpvOpPhi);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    for (const SpvFactory::VariableBlockIdPair &vb : block_vars) {
        inst.add_operand(vb.first);   // variable id
        inst.add_operand(vb.second);  // block id
    }
    return inst;
}

SpvInstruction SpvFactory::capability(const SpvCapability &capability) {
    SpvInstruction inst = SpvInstruction::make(SpvOpCapability);
    inst.add_immediate(capability, SpvIntegerLiteral);
    return inst;
}

SpvInstruction SpvFactory::extension(const std::string &extension) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExtension);
    inst.add_string(extension);
    return inst;
}

SpvInstruction SpvFactory::import(SpvId instruction_set_id, const std::string &instruction_set_name) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExtInstImport);
    inst.set_result_id(instruction_set_id);
    inst.add_string(instruction_set_name);
    return inst;
}

SpvInstruction SpvFactory::extended(SpvId instruction_set_id, SpvId instruction_number, SpvId type_id, SpvId result_id, const SpvFactory::Operands &operands) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExtInst);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(instruction_set_id);
    inst.add_immediate(instruction_number, SpvIntegerLiteral);
    for (SpvId o : operands) {
        inst.add_operand(o);
    }
    return inst;
}

/** GLSL extended instruction utility methods */

bool is_glsl_unary_op(SpvId glsl_op_code) {
    return (glsl_operand_count(glsl_op_code) == 1);
}

bool is_glsl_binary_op(SpvId glsl_op_code) {
    return (glsl_operand_count(glsl_op_code) == 2);
}

uint32_t glsl_operand_count(SpvId glsl_op_code) {
    switch (glsl_op_code) {
    case GLSLstd450Round:
    case GLSLstd450RoundEven:
    case GLSLstd450Trunc:
    case GLSLstd450FAbs:
    case GLSLstd450SAbs:
    case GLSLstd450FSign:
    case GLSLstd450SSign:
    case GLSLstd450Floor:
    case GLSLstd450Ceil:
    case GLSLstd450Fract:
    case GLSLstd450Radians:
    case GLSLstd450Degrees:
    case GLSLstd450Sin:
    case GLSLstd450Cos:
    case GLSLstd450Tan:
    case GLSLstd450Asin:
    case GLSLstd450Acos:
    case GLSLstd450Atan:
    case GLSLstd450Asinh:
    case GLSLstd450Acosh:
    case GLSLstd450Atanh:
    case GLSLstd450Cosh:
    case GLSLstd450Sinh:
    case GLSLstd450Tanh:
    case GLSLstd450Exp:
    case GLSLstd450Log:
    case GLSLstd450Exp2:
    case GLSLstd450Log2:
    case GLSLstd450Sqrt:
    case GLSLstd450InverseSqrt:
    case GLSLstd450Determinant:
    case GLSLstd450MatrixInverse:
    case GLSLstd450ModfStruct:
    case GLSLstd450FrexpStruct:
    case GLSLstd450PackSnorm4x8:
    case GLSLstd450PackUnorm4x8:
    case GLSLstd450PackSnorm2x16:
    case GLSLstd450PackUnorm2x16:
    case GLSLstd450PackHalf2x16:
    case GLSLstd450PackDouble2x32:
    case GLSLstd450UnpackSnorm4x8:
    case GLSLstd450UnpackUnorm4x8:
    case GLSLstd450UnpackSnorm2x16:
    case GLSLstd450UnpackUnorm2x16:
    case GLSLstd450UnpackHalf2x16:
    case GLSLstd450UnpackDouble2x32:
    case GLSLstd450Length:
    case GLSLstd450Normalize:
    case GLSLstd450FindILsb:
    case GLSLstd450FindSMsb:
    case GLSLstd450FindUMsb:
    case GLSLstd450InterpolateAtCentroid: {
        return 1;  // unary op
    }
    case GLSLstd450Atan2:
    case GLSLstd450Pow:
    case GLSLstd450Modf:
    case GLSLstd450FMin:
    case GLSLstd450UMin:
    case GLSLstd450SMin:
    case GLSLstd450FMax:
    case GLSLstd450UMax:
    case GLSLstd450SMax:
    case GLSLstd450Step:
    case GLSLstd450Frexp:
    case GLSLstd450Ldexp:
    case GLSLstd450Distance:
    case GLSLstd450Cross:
    case GLSLstd450Reflect:
    case GLSLstd450InterpolateAtOffset:
    case GLSLstd450InterpolateAtSample:
    case GLSLstd450NMax:
    case GLSLstd450NMin: {
        return 2;  // binary op
    }
    case GLSLstd450FMix:
    case GLSLstd450IMix:
    case GLSLstd450SmoothStep:
    case GLSLstd450Fma:
    case GLSLstd450FClamp:
    case GLSLstd450UClamp:
    case GLSLstd450SClamp:
    case GLSLstd450NClamp: {
        return 3;  // trinary op
    }
    case GLSLstd450Bad:
    case GLSLstd450Count:
    default:
        break;
    };
    return SpvInvalidId;
}

/** Specializations for reference counted classes */
template<>
RefCount &ref_count<SpvInstructionContents>(const SpvInstructionContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvInstructionContents>(const SpvInstructionContents *c) {
    delete c;
}

template<>
RefCount &ref_count<SpvBlockContents>(const SpvBlockContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvBlockContents>(const SpvBlockContents *c) {
    delete c;
}

template<>
RefCount &ref_count<SpvFunctionContents>(const SpvFunctionContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvFunctionContents>(const SpvFunctionContents *c) {
    delete c;
}

template<>
RefCount &ref_count<SpvModuleContents>(const SpvModuleContents *c) noexcept {
    return c->ref_count;
}

template<>
void destroy<SpvModuleContents>(const SpvModuleContents *c) {
    delete c;
}

// --

std::ostream &operator<<(std::ostream &stream, const SpvModule &module) {
    if (!module.is_defined()) {
        stream << "(undefined)";
        return stream;
    }

    stream << "; SPIR-V\n";
    stream << "; Version: "
           << std::to_string(spirv_major_version(module.version_format())) << "."
           << std::to_string(spirv_minor_version(module.version_format())) << "\n";
    stream << "; Generator: Khronos; 0\n";
    stream << "; Bound: " << std::to_string(module.binding_count()) << "\n";
    stream << "; Schema: 0\n";  // reserved for future use

    SpvModule::Capabilities capabilities = module.capabilities();
    if (!capabilities.empty()) {
        stream << "\n";
        stream << "; Capabilities\n";
        for (const SpvCapability &value : capabilities) {
            SpvInstruction inst = SpvFactory::capability(value);
            stream << inst;
        }
    }

    SpvModule::Extensions extensions = module.extensions();
    if (!extensions.empty()) {
        stream << "\n";
        stream << "; Extensions\n";
        for (const std::string &value : extensions) {
            SpvInstruction inst = SpvFactory::extension(value);
            stream << inst;
        }
    }

    SpvModule::Imports imports = module.imports();
    if (!imports.empty()) {
        stream << "\n";
        stream << "; Extended Instruction Set Imports\n";
        for (const SpvModule::Imports::value_type &v : imports) {
            SpvInstruction inst = SpvFactory::import(v.first, v.second);
            stream << inst;
        }
    }

    SpvInstruction memory_model = SpvFactory::memory_model(module.addressing_model(), module.memory_model());
    stream << "\n";
    stream << "; Memory Model\n";
    stream << memory_model;

    if (module.entry_point_count() > 0) {
        stream << "\n";
        stream << "; Entry Points\n";
        SpvModule::EntryPointNames entry_point_names = module.entry_point_names();
        for (const std::string &name : entry_point_names) {
            SpvInstruction inst = module.entry_point(name);
            stream << "; " << name << "\n";
            stream << inst;
        }
    }

    for (const SpvInstruction &inst : module.execution_modes()) {
        stream << inst;
    }

    if (!module.debug_source().empty() || !module.debug_symbols().empty()) {
        stream << "\n";
        stream << "; Debug Information\n";
    }
    for (const SpvInstruction &inst : module.debug_source()) {
        stream << inst;
    }
    for (const SpvInstruction &inst : module.debug_symbols()) {
        stream << inst;
    }

    if (!module.annotations().empty()) {
        stream << "\n";
        stream << "; Annotations\n";
        for (const SpvInstruction &inst : module.annotations()) {
            stream << inst;
        }
    }

    if (!module.type_definitions().empty()) {
        stream << "\n";
        stream << "; Type Definitions\n";
        for (const SpvInstruction &inst : module.type_definitions()) {
            stream << inst;
        }
    }

    if (!module.global_constants().empty()) {
        stream << "\n";
        stream << "; Global Constants\n";
        for (const SpvInstruction &inst : module.global_constants()) {
            stream << inst;
        }
    }

    if (!module.global_variables().empty()) {
        stream << "\n";
        stream << "; Global Variables\n";
        for (const SpvInstruction &inst : module.global_variables()) {
            stream << inst;
        }
    }

    if (!module.function_definitions().empty()) {
        stream << "\n";
        stream << "; Function Definitions\n";
        for (const SpvFunction &func : module.function_definitions()) {
            stream << func;
        }
    }

    return stream;
}

std::ostream &operator<<(std::ostream &stream, const SpvFunction &func) {
    if (!func.is_defined()) {
        stream << "(undefined)";
        return stream;
    }
    stream << func.declaration();
    for (const SpvInstruction &param : func.parameters()) {
        stream << param;
    }
    for (const SpvBlock &block : func.blocks()) {
        stream << block;
    }
    SpvInstruction inst = SpvFactory::function_end();
    stream << inst;
    return stream;
}

std::ostream &operator<<(std::ostream &stream, const SpvBlock &block) {
    if (!block.is_defined()) {
        stream << "(undefined)";
        return stream;
    }

    SpvInstruction label = SpvFactory::label(block.id());
    stream << label;
    for (const SpvInstruction &variable : block.variables()) {
        stream << variable;
    }
    for (const SpvInstruction &instruction : block.instructions()) {
        stream << instruction;
    }

    return stream;
}

std::ostream &operator<<(std::ostream &stream, const SpvInstruction &inst) {
    if (!inst.is_defined()) {
        stream << "(undefined)";
        return stream;
    }

    if (inst.has_result()) {
        stream << std::string("%") << std::to_string(inst.result_id());
        stream << " = ";
    }

    switch (inst.op_code()) {
    case SpvOpNop: {
        stream << "OpNop";
        break;
    }
    case SpvOpUndef: {
        stream << "OpUndef";
        break;
    }
    case SpvOpSourceContinued: {
        stream << "OpSourceContinued";
        break;
    }
    case SpvOpSource: {
        stream << "OpSource";
        break;
    }
    case SpvOpSourceExtension: {
        stream << "OpSourceExtension";
        break;
    }
    case SpvOpName: {
        stream << "OpName";
        break;
    }
    case SpvOpMemberName: {
        stream << "OpMemberName";
        break;
    }
    case SpvOpString: {
        stream << "OpString";
        break;
    }
    case SpvOpLine: {
        stream << "OpLine";
        break;
    }
    case SpvOpExtension: {
        stream << "OpExtension";
        break;
    }
    case SpvOpExtInstImport: {
        stream << "OpExtInstImport";
        break;
    }
    case SpvOpExtInst: {
        stream << "OpExtInst";
        break;
    }
    case SpvOpMemoryModel: {
        stream << "OpMemoryModel";
        break;
    }
    case SpvOpEntryPoint: {
        stream << "OpEntryPoint";
        break;
    }
    case SpvOpExecutionMode: {
        stream << "OpExecutionMode";
        break;
    }
    case SpvOpCapability: {
        stream << "OpCapability";
        break;
    }
    case SpvOpTypeVoid: {
        stream << "OpTypeVoid";
        break;
    }
    case SpvOpTypeBool: {
        stream << "OpTypeBool";
        break;
    }
    case SpvOpTypeInt: {
        stream << "OpTypeInt";
        break;
    }
    case SpvOpTypeFloat: {
        stream << "OpTypeFloat";
        break;
    }
    case SpvOpTypeVector: {
        stream << "OpTypeVector";
        break;
    }
    case SpvOpTypeMatrix: {
        stream << "OpTypeMatrix";
        break;
    }
    case SpvOpTypeImage: {
        stream << "OpTypeImage";
        break;
    }
    case SpvOpTypeSampler: {
        stream << "OpTypeSampler";
        break;
    }
    case SpvOpTypeSampledImage: {
        stream << "OpTypeSampledImage";
        break;
    }
    case SpvOpTypeArray: {
        stream << "OpTypeArray";
        break;
    }
    case SpvOpTypeRuntimeArray: {
        stream << "OpTypeRuntimeArray";
        break;
    }
    case SpvOpTypeStruct: {
        stream << "OpTypeStruct";
        break;
    }
    case SpvOpTypeOpaque: {
        stream << "OpTypeOpaque";
        break;
    }
    case SpvOpTypePointer: {
        stream << "OpTypePointer";
        break;
    }
    case SpvOpTypeFunction: {
        stream << "OpTypeFunction";
        break;
    }
    case SpvOpTypeEvent: {
        stream << "OpTypeEvent";
        break;
    }
    case SpvOpTypeDeviceEvent: {
        stream << "OpTypeDeviceEvent";
        break;
    }
    case SpvOpTypeReserveId: {
        stream << "OpTypeReserveId";
        break;
    }
    case SpvOpTypeQueue: {
        stream << "OpTypeQueue";
        break;
    }
    case SpvOpTypePipe: {
        stream << "OpTypePipe";
        break;
    }
    case SpvOpTypeForwardPointer: {
        stream << "OpTypeForwardPointer";
        break;
    }
    case SpvOpConstantTrue: {
        stream << "OpConstantTrue";
        break;
    }
    case SpvOpConstantFalse: {
        stream << "OpConstantFalse";
        break;
    }
    case SpvOpConstant: {
        stream << "OpConstant";
        break;
    }
    case SpvOpConstantComposite: {
        stream << "OpConstantComposite";
        break;
    }
    case SpvOpConstantSampler: {
        stream << "OpConstantSampler";
        break;
    }
    case SpvOpConstantNull: {
        stream << "OpConstantNull";
        break;
    }
    case SpvOpSpecConstantTrue: {
        stream << "OpSpecConstantTrue";
        break;
    }
    case SpvOpSpecConstantFalse: {
        stream << "OpSpecConstantFalse";
        break;
    }
    case SpvOpSpecConstant: {
        stream << "OpSpecConstant";
        break;
    }
    case SpvOpSpecConstantComposite: {
        stream << "OpSpecConstantComposite";
        break;
    }
    case SpvOpSpecConstantOp: {
        stream << "OpSpecConstantOp";
        break;
    }
    case SpvOpFunction: {
        stream << "OpFunction";
        break;
    }
    case SpvOpFunctionParameter: {
        stream << "OpFunctionParameter";
        break;
    }
    case SpvOpFunctionEnd: {
        stream << "OpFunctionEnd";
        break;
    }
    case SpvOpFunctionCall: {
        stream << "OpFunctionCall";
        break;
    }
    case SpvOpVariable: {
        stream << "OpVariable";
        break;
    }
    case SpvOpImageTexelPointer: {
        stream << "OpImageTexelPointer";
        break;
    }
    case SpvOpLoad: {
        stream << "OpLoad";
        break;
    }
    case SpvOpStore: {
        stream << "OpStore";
        break;
    }
    case SpvOpCopyMemory: {
        stream << "OpCopyMemory";
        break;
    }
    case SpvOpCopyMemorySized: {
        stream << "OpCopyMemorySized";
        break;
    }
    case SpvOpAccessChain: {
        stream << "OpAccessChain";
        break;
    }
    case SpvOpInBoundsAccessChain: {
        stream << "OpInBoundsAccessChain";
        break;
    }
    case SpvOpPtrAccessChain: {
        stream << "OpPtrAccessChain";
        break;
    }
    case SpvOpArrayLength: {
        stream << "OpArrayLength";
        break;
    }
    case SpvOpGenericPtrMemSemantics: {
        stream << "OpGenericPtrMemSemantics";
        break;
    }
    case SpvOpInBoundsPtrAccessChain: {
        stream << "OpInBoundsPtrAccessChain";
        break;
    }
    case SpvOpDecorate: {
        stream << "OpDecorate";
        break;
    }
    case SpvOpMemberDecorate: {
        stream << "OpMemberDecorate";
        break;
    }
    case SpvOpDecorationGroup: {
        stream << "OpDecorationGroup";
        break;
    }
    case SpvOpGroupDecorate: {
        stream << "OpGroupDecorate";
        break;
    }
    case SpvOpGroupMemberDecorate: {
        stream << "OpGroupMemberDecorate";
        break;
    }
    case SpvOpVectorExtractDynamic: {
        stream << "OpVectorExtractDynamic";
        break;
    }
    case SpvOpVectorInsertDynamic: {
        stream << "OpVectorInsertDynamic";
        break;
    }
    case SpvOpVectorShuffle: {
        stream << "OpVectorShuffle";
        break;
    }
    case SpvOpCompositeConstruct: {
        stream << "OpCompositeConstruct";
        break;
    }
    case SpvOpCompositeExtract: {
        stream << "OpCompositeExtract";
        break;
    }
    case SpvOpCompositeInsert: {
        stream << "OpCompositeInsert";
        break;
    }
    case SpvOpCopyObject: {
        stream << "OpCopyObject";
        break;
    }
    case SpvOpTranspose: {
        stream << "OpTranspose";
        break;
    }
    case SpvOpSampledImage: {
        stream << "OpSampledImage";
        break;
    }
    case SpvOpImageSampleImplicitLod: {
        stream << "OpImageSampleImplicitLod";
        break;
    }
    case SpvOpImageSampleExplicitLod: {
        stream << "OpImageSampleExplicitLod";
        break;
    }
    case SpvOpImageSampleDrefImplicitLod: {
        stream << "OpImageSampleDrefImplicitLod";
        break;
    }
    case SpvOpImageSampleDrefExplicitLod: {
        stream << "OpImageSampleDrefExplicitLod";
        break;
    }
    case SpvOpImageSampleProjImplicitLod: {
        stream << "OpImageSampleProjImplicitLod";
        break;
    }
    case SpvOpImageSampleProjExplicitLod: {
        stream << "OpImageSampleProjExplicitLod";
        break;
    }
    case SpvOpImageSampleProjDrefImplicitLod: {
        stream << "OpImageSampleProjDrefImplicitLod";
        break;
    }
    case SpvOpImageSampleProjDrefExplicitLod: {
        stream << "OpImageSampleProjDrefExplicitLod";
        break;
    }
    case SpvOpImageFetch: {
        stream << "OpImageFetch";
        break;
    }
    case SpvOpImageGather: {
        stream << "OpImageGather";
        break;
    }
    case SpvOpImageDrefGather: {
        stream << "OpImageDrefGather";
        break;
    }
    case SpvOpImageRead: {
        stream << "OpImageRead";
        break;
    }
    case SpvOpImageWrite: {
        stream << "OpImageWrite";
        break;
    }
    case SpvOpImage: {
        stream << "OpImage";
        break;
    }
    case SpvOpImageQueryFormat: {
        stream << "OpImageQueryFormat";
        break;
    }
    case SpvOpImageQueryOrder: {
        stream << "OpImageQueryOrder";
        break;
    }
    case SpvOpImageQuerySizeLod: {
        stream << "OpImageQuerySizeLod";
        break;
    }
    case SpvOpImageQuerySize: {
        stream << "OpImageQuerySize";
        break;
    }
    case SpvOpImageQueryLod: {
        stream << "OpImageQueryLod";
        break;
    }
    case SpvOpImageQueryLevels: {
        stream << "OpImageQueryLevels";
        break;
    }
    case SpvOpImageQuerySamples: {
        stream << "OpImageQuerySamples";
        break;
    }
    case SpvOpConvertFToU: {
        stream << "OpConvertFToU";
        break;
    }
    case SpvOpConvertFToS: {
        stream << "OpConvertFToS";
        break;
    }
    case SpvOpConvertSToF: {
        stream << "OpConvertSToF";
        break;
    }
    case SpvOpConvertUToF: {
        stream << "OpConvertUToF";
        break;
    }
    case SpvOpUConvert: {
        stream << "OpUConvert";
        break;
    }
    case SpvOpSConvert: {
        stream << "OpSConvert";
        break;
    }
    case SpvOpFConvert: {
        stream << "OpFConvert";
        break;
    }
    case SpvOpConvertPtrToU: {
        stream << "OpConvertPtrToU";
        break;
    }
    case SpvOpSatConvertSToU: {
        stream << "OpSatConvertSToU";
        break;
    }
    case SpvOpSatConvertUToS: {
        stream << "OpSatConvertUToS";
        break;
    }
    case SpvOpConvertUToPtr: {
        stream << "OpConvertUToPtr";
        break;
    }
    case SpvOpPtrCastToGeneric: {
        stream << "OpPtrCastToGeneric";
        break;
    }
    case SpvOpGenericCastToPtr: {
        stream << "OpGenericCastToPtr";
        break;
    }
    case SpvOpGenericCastToPtrExplicit: {
        stream << "OpGenericCastToPtrExplicit";
        break;
    }
    case SpvOpBitcast: {
        stream << "OpBitcast";
        break;
    }
    case SpvOpSNegate: {
        stream << "OpSNegate";
        break;
    }
    case SpvOpFNegate: {
        stream << "OpFNegate";
        break;
    }
    case SpvOpIAdd: {
        stream << "OpIAdd";
        break;
    }
    case SpvOpFAdd: {
        stream << "OpFAdd";
        break;
    }
    case SpvOpISub: {
        stream << "OpISub";
        break;
    }
    case SpvOpFSub: {
        stream << "OpFSub";
        break;
    }
    case SpvOpIMul: {
        stream << "OpIMul";
        break;
    }
    case SpvOpFMul: {
        stream << "OpFMul";
        break;
    }
    case SpvOpUDiv: {
        stream << "OpUDiv";
        break;
    }
    case SpvOpSDiv: {
        stream << "OpSDiv";
        break;
    }
    case SpvOpFDiv: {
        stream << "OpFDiv";
        break;
    }
    case SpvOpUMod: {
        stream << "OpUMod";
        break;
    }
    case SpvOpSRem: {
        stream << "OpSRem";
        break;
    }
    case SpvOpSMod: {
        stream << "OpSMod";
        break;
    }
    case SpvOpFRem: {
        stream << "OpFRem";
        break;
    }
    case SpvOpFMod: {
        stream << "OpFMod";
        break;
    }
    case SpvOpVectorTimesScalar: {
        stream << "OpVectorTimesScalar";
        break;
    }
    case SpvOpMatrixTimesScalar: {
        stream << "OpMatrixTimesScalar";
        break;
    }
    case SpvOpVectorTimesMatrix: {
        stream << "OpVectorTimesMatrix";
        break;
    }
    case SpvOpMatrixTimesVector: {
        stream << "OpMatrixTimesVector";
        break;
    }
    case SpvOpMatrixTimesMatrix: {
        stream << "OpMatrixTimesMatrix";
        break;
    }
    case SpvOpOuterProduct: {
        stream << "OpOuterProduct";
        break;
    }
    case SpvOpDot: {
        stream << "OpDot";
        break;
    }
    case SpvOpIAddCarry: {
        stream << "OpIAddCarry";
        break;
    }
    case SpvOpISubBorrow: {
        stream << "OpISubBorrow";
        break;
    }
    case SpvOpUMulExtended: {
        stream << "OpUMulExtended";
        break;
    }
    case SpvOpSMulExtended: {
        stream << "OpSMulExtended";
        break;
    }
    case SpvOpAny: {
        stream << "OpAny";
        break;
    }
    case SpvOpAll: {
        stream << "OpAll";
        break;
    }
    case SpvOpIsNan: {
        stream << "OpIsNan";
        break;
    }
    case SpvOpIsInf: {
        stream << "OpIsInf";
        break;
    }
    case SpvOpIsFinite: {
        stream << "OpIsFinite";
        break;
    }
    case SpvOpIsNormal: {
        stream << "OpIsNormal";
        break;
    }
    case SpvOpSignBitSet: {
        stream << "OpSignBitSet";
        break;
    }
    case SpvOpLessOrGreater: {
        stream << "OpLessOrGreater";
        break;
    }
    case SpvOpOrdered: {
        stream << "OpOrdered";
        break;
    }
    case SpvOpUnordered: {
        stream << "OpUnordered";
        break;
    }
    case SpvOpLogicalEqual: {
        stream << "OpLogicalEqual";
        break;
    }
    case SpvOpLogicalNotEqual: {
        stream << "OpLogicalNotEqual";
        break;
    }
    case SpvOpLogicalOr: {
        stream << "OpLogicalOr";
        break;
    }
    case SpvOpLogicalAnd: {
        stream << "OpLogicalAnd";
        break;
    }
    case SpvOpLogicalNot: {
        stream << "OpLogicalNot";
        break;
    }
    case SpvOpSelect: {
        stream << "OpSelect";
        break;
    }
    case SpvOpIEqual: {
        stream << "OpIEqual";
        break;
    }
    case SpvOpINotEqual: {
        stream << "OpINotEqual";
        break;
    }
    case SpvOpUGreaterThan: {
        stream << "OpUGreaterThan";
        break;
    }
    case SpvOpSGreaterThan: {
        stream << "OpSGreaterThan";
        break;
    }
    case SpvOpUGreaterThanEqual: {
        stream << "OpUGreaterThanEqual";
        break;
    }
    case SpvOpSGreaterThanEqual: {
        stream << "OpSGreaterThanEqual";
        break;
    }
    case SpvOpULessThan: {
        stream << "OpULessThan";
        break;
    }
    case SpvOpSLessThan: {
        stream << "OpSLessThan";
        break;
    }
    case SpvOpULessThanEqual: {
        stream << "OpULessThanEqual";
        break;
    }
    case SpvOpSLessThanEqual: {
        stream << "OpSLessThanEqual";
        break;
    }
    case SpvOpFOrdEqual: {
        stream << "OpFOrdEqual";
        break;
    }
    case SpvOpFUnordEqual: {
        stream << "OpFUnordEqual";
        break;
    }
    case SpvOpFOrdNotEqual: {
        stream << "OpFOrdNotEqual";
        break;
    }
    case SpvOpFUnordNotEqual: {
        stream << "OpFUnordNotEqual";
        break;
    }
    case SpvOpFOrdLessThan: {
        stream << "OpFOrdLessThan";
        break;
    }
    case SpvOpFUnordLessThan: {
        stream << "OpFUnordLessThan";
        break;
    }
    case SpvOpFOrdGreaterThan: {
        stream << "OpFOrdGreaterThan";
        break;
    }
    case SpvOpFUnordGreaterThan: {
        stream << "OpFUnordGreaterThan";
        break;
    }
    case SpvOpFOrdLessThanEqual: {
        stream << "OpFOrdLessThanEqual";
        break;
    }
    case SpvOpFUnordLessThanEqual: {
        stream << "OpFUnordLessThanEqual";
        break;
    }
    case SpvOpFOrdGreaterThanEqual: {
        stream << "OpFOrdGreaterThanEqual";
        break;
    }
    case SpvOpFUnordGreaterThanEqual: {
        stream << "OpFUnordGreaterThanEqual";
        break;
    }
    case SpvOpShiftRightLogical: {
        stream << "OpShiftRightLogical";
        break;
    }
    case SpvOpShiftRightArithmetic: {
        stream << "OpShiftRightArithmetic";
        break;
    }
    case SpvOpShiftLeftLogical: {
        stream << "OpShiftLeftLogical";
        break;
    }
    case SpvOpBitwiseOr: {
        stream << "OpBitwiseOr";
        break;
    }
    case SpvOpBitwiseXor: {
        stream << "OpBitwiseXor";
        break;
    }
    case SpvOpBitwiseAnd: {
        stream << "OpBitwiseAnd";
        break;
    }
    case SpvOpNot: {
        stream << "OpNot";
        break;
    }
    case SpvOpBitFieldInsert: {
        stream << "OpBitFieldInsert";
        break;
    }
    case SpvOpBitFieldSExtract: {
        stream << "OpBitFieldSExtract";
        break;
    }
    case SpvOpBitFieldUExtract: {
        stream << "OpBitFieldUExtract";
        break;
    }
    case SpvOpBitReverse: {
        stream << "OpBitReverse";
        break;
    }
    case SpvOpBitCount: {
        stream << "OpBitCount";
        break;
    }
    case SpvOpDPdx: {
        stream << "OpDPdx";
        break;
    }
    case SpvOpDPdy: {
        stream << "OpDPdy";
        break;
    }
    case SpvOpFwidth: {
        stream << "OpFwidth";
        break;
    }
    case SpvOpDPdxFine: {
        stream << "OpDPdxFine";
        break;
    }
    case SpvOpDPdyFine: {
        stream << "OpDPdyFine";
        break;
    }
    case SpvOpFwidthFine: {
        stream << "OpFwidthFine";
        break;
    }
    case SpvOpDPdxCoarse: {
        stream << "OpDPdxCoarse";
        break;
    }
    case SpvOpDPdyCoarse: {
        stream << "OpDPdyCoarse";
        break;
    }
    case SpvOpFwidthCoarse: {
        stream << "OpFwidthCoarse";
        break;
    }
    case SpvOpEmitVertex: {
        stream << "OpEmitVertex";
        break;
    }
    case SpvOpEndPrimitive: {
        stream << "OpEndPrimitive";
        break;
    }
    case SpvOpEmitStreamVertex: {
        stream << "OpEmitStreamVertex";
        break;
    }
    case SpvOpEndStreamPrimitive: {
        stream << "OpEndStreamPrimitive";
        break;
    }
    case SpvOpControlBarrier: {
        stream << "OpControlBarrier";
        break;
    }
    case SpvOpMemoryBarrier: {
        stream << "OpMemoryBarrier";
        break;
    }
    case SpvOpAtomicLoad: {
        stream << "OpAtomicLoad";
        break;
    }
    case SpvOpAtomicStore: {
        stream << "OpAtomicStore";
        break;
    }
    case SpvOpAtomicExchange: {
        stream << "OpAtomicExchange";
        break;
    }
    case SpvOpAtomicCompareExchange: {
        stream << "OpAtomicCompareExchange";
        break;
    }
    case SpvOpAtomicCompareExchangeWeak: {
        stream << "OpAtomicCompareExchangeWeak";
        break;
    }
    case SpvOpAtomicIIncrement: {
        stream << "OpAtomicIIncrement";
        break;
    }
    case SpvOpAtomicIDecrement: {
        stream << "OpAtomicIDecrement";
        break;
    }
    case SpvOpAtomicIAdd: {
        stream << "OpAtomicIAdd";
        break;
    }
    case SpvOpAtomicISub: {
        stream << "OpAtomicISub";
        break;
    }
    case SpvOpAtomicSMin: {
        stream << "OpAtomicSMin";
        break;
    }
    case SpvOpAtomicUMin: {
        stream << "OpAtomicUMin";
        break;
    }
    case SpvOpAtomicSMax: {
        stream << "OpAtomicSMax";
        break;
    }
    case SpvOpAtomicUMax: {
        stream << "OpAtomicUMax";
        break;
    }
    case SpvOpAtomicAnd: {
        stream << "OpAtomicAnd";
        break;
    }
    case SpvOpAtomicOr: {
        stream << "OpAtomicOr";
        break;
    }
    case SpvOpAtomicXor: {
        stream << "OpAtomicXor";
        break;
    }
    case SpvOpPhi: {
        stream << "OpPhi";
        break;
    }
    case SpvOpLoopMerge: {
        stream << "OpLoopMerge";
        break;
    }
    case SpvOpSelectionMerge: {
        stream << "OpSelectionMerge";
        break;
    }
    case SpvOpLabel: {
        stream << "OpLabel";
        break;
    }
    case SpvOpBranch: {
        stream << "OpBranch";
        break;
    }
    case SpvOpBranchConditional: {
        stream << "OpBranchConditional";
        break;
    }
    case SpvOpSwitch: {
        stream << "OpSwitch";
        break;
    }
    case SpvOpKill: {
        stream << "OpKill";
        break;
    }
    case SpvOpReturn: {
        stream << "OpReturn";
        break;
    }
    case SpvOpReturnValue: {
        stream << "OpReturnValue";
        break;
    }
    case SpvOpUnreachable: {
        stream << "OpUnreachable";
        break;
    }
    case SpvOpLifetimeStart: {
        stream << "OpLifetimeStart";
        break;
    }
    case SpvOpLifetimeStop: {
        stream << "OpLifetimeStop";
        break;
    }
    case SpvOpGroupAsyncCopy: {
        stream << "OpGroupAsyncCopy";
        break;
    }
    case SpvOpGroupWaitEvents: {
        stream << "OpGroupWaitEvents";
        break;
    }
    case SpvOpGroupAll: {
        stream << "OpGroupAll";
        break;
    }
    case SpvOpGroupAny: {
        stream << "OpGroupAny";
        break;
    }
    case SpvOpGroupBroadcast: {
        stream << "OpGroupBroadcast";
        break;
    }
    case SpvOpGroupIAdd: {
        stream << "OpGroupIAdd";
        break;
    }
    case SpvOpGroupFAdd: {
        stream << "OpGroupFAdd";
        break;
    }
    case SpvOpGroupFMin: {
        stream << "OpGroupFMin";
        break;
    }
    case SpvOpGroupUMin: {
        stream << "OpGroupUMin";
        break;
    }
    case SpvOpGroupSMin: {
        stream << "OpGroupSMin";
        break;
    }
    case SpvOpGroupFMax: {
        stream << "OpGroupFMax";
        break;
    }
    case SpvOpGroupUMax: {
        stream << "OpGroupUMax";
        break;
    }
    case SpvOpGroupSMax: {
        stream << "OpGroupSMax";
        break;
    }
    case SpvOpReadPipe: {
        stream << "OpReadPipe";
        break;
    }
    case SpvOpWritePipe: {
        stream << "OpWritePipe";
        break;
    }
    case SpvOpReservedReadPipe: {
        stream << "OpReservedReadPipe";
        break;
    }
    case SpvOpReservedWritePipe: {
        stream << "OpReservedWritePipe";
        break;
    }
    case SpvOpReserveReadPipePackets: {
        stream << "OpReserveReadPipePackets";
        break;
    }
    case SpvOpReserveWritePipePackets: {
        stream << "OpReserveWritePipePackets";
        break;
    }
    case SpvOpCommitReadPipe: {
        stream << "OpCommitReadPipe";
        break;
    }
    case SpvOpCommitWritePipe: {
        stream << "OpCommitWritePipe";
        break;
    }
    case SpvOpIsValidReserveId: {
        stream << "OpIsValidReserveId";
        break;
    }
    case SpvOpGetNumPipePackets: {
        stream << "OpGetNumPipePackets";
        break;
    }
    case SpvOpGetMaxPipePackets: {
        stream << "OpGetMaxPipePackets";
        break;
    }
    case SpvOpGroupReserveReadPipePackets: {
        stream << "OpGroupReserveReadPipePackets";
        break;
    }
    case SpvOpGroupReserveWritePipePackets: {
        stream << "OpGroupReserveWritePipePackets";
        break;
    }
    case SpvOpGroupCommitReadPipe: {
        stream << "OpGroupCommitReadPipe";
        break;
    }
    case SpvOpGroupCommitWritePipe: {
        stream << "OpGroupCommitWritePipe";
        break;
    }
    case SpvOpEnqueueMarker: {
        stream << "OpEnqueueMarker";
        break;
    }
    case SpvOpEnqueueKernel: {
        stream << "OpEnqueueKernel";
        break;
    }
    case SpvOpGetKernelNDrangeSubGroupCount: {
        stream << "OpGetKernelNDrangeSubGroupCount";
        break;
    }
    case SpvOpGetKernelNDrangeMaxSubGroupSize: {
        stream << "OpGetKernelNDrangeMaxSubGroupSize";
        break;
    }
    case SpvOpGetKernelWorkGroupSize: {
        stream << "OpGetKernelWorkGroupSize";
        break;
    }
    case SpvOpGetKernelPreferredWorkGroupSizeMultiple: {
        stream << "OpGetKernelPreferredWorkGroupSizeMultiple";
        break;
    }
    case SpvOpRetainEvent: {
        stream << "OpRetainEvent";
        break;
    }
    case SpvOpReleaseEvent: {
        stream << "OpReleaseEvent";
        break;
    }
    case SpvOpCreateUserEvent: {
        stream << "OpCreateUserEvent";
        break;
    }
    case SpvOpIsValidEvent: {
        stream << "OpIsValidEvent";
        break;
    }
    case SpvOpSetUserEventStatus: {
        stream << "OpSetUserEventStatus";
        break;
    }
    case SpvOpCaptureEventProfilingInfo: {
        stream << "OpCaptureEventProfilingInfo";
        break;
    }
    case SpvOpGetDefaultQueue: {
        stream << "OpGetDefaultQueue";
        break;
    }
    case SpvOpBuildNDRange: {
        stream << "OpBuildNDRange";
        break;
    }
    case SpvOpImageSparseSampleImplicitLod: {
        stream << "OpImageSparseSampleImplicitLod";
        break;
    }
    case SpvOpImageSparseSampleExplicitLod: {
        stream << "OpImageSparseSampleExplicitLod";
        break;
    }
    case SpvOpImageSparseSampleDrefImplicitLod: {
        stream << "OpImageSparseSampleDrefImplicitLod";
        break;
    }
    case SpvOpImageSparseSampleDrefExplicitLod: {
        stream << "OpImageSparseSampleDrefExplicitLod";
        break;
    }
    case SpvOpImageSparseSampleProjImplicitLod: {
        stream << "OpImageSparseSampleProjImplicitLod";
        break;
    }
    case SpvOpImageSparseSampleProjExplicitLod: {
        stream << "OpImageSparseSampleProjExplicitLod";
        break;
    }
    case SpvOpImageSparseSampleProjDrefImplicitLod: {
        stream << "OpImageSparseSampleProjDrefImplicitLod";
        break;
    }
    case SpvOpImageSparseSampleProjDrefExplicitLod: {
        stream << "OpImageSparseSampleProjDrefExplicitLod";
        break;
    }
    case SpvOpImageSparseFetch: {
        stream << "OpImageSparseFetch";
        break;
    }
    case SpvOpImageSparseGather: {
        stream << "OpImageSparseGather";
        break;
    }
    case SpvOpImageSparseDrefGather: {
        stream << "OpImageSparseDrefGather";
        break;
    }
    case SpvOpImageSparseTexelsResident: {
        stream << "OpImageSparseTexelsResident";
        break;
    }
    case SpvOpNoLine: {
        stream << "OpNoLine";
        break;
    }
    case SpvOpAtomicFlagTestAndSet: {
        stream << "OpAtomicFlagTestAndSet";
        break;
    }
    case SpvOpAtomicFlagClear: {
        stream << "OpAtomicFlagClear";
        break;
    }
    case SpvOpImageSparseRead: {
        stream << "OpImageSparseRead";
        break;
    }
    case SpvOpDecorateId: {
        stream << "OpDecorateId";
        break;
    }
    case SpvOpSubgroupBallotKHR: {
        stream << "OpSubgroupBallotKHR";
        break;
    }
    case SpvOpSubgroupFirstInvocationKHR: {
        stream << "OpSubgroupFirstInvocationKHR";
        break;
    }
    case SpvOpSubgroupAllKHR: {
        stream << "OpSubgroupAllKHR";
        break;
    }
    case SpvOpSubgroupAnyKHR: {
        stream << "OpSubgroupAnyKHR";
        break;
    }
    case SpvOpSubgroupAllEqualKHR: {
        stream << "OpSubgroupAllEqualKHR";
        break;
    }
    case SpvOpSubgroupReadInvocationKHR: {
        stream << "OpSubgroupReadInvocationKHR";
        break;
    }
    case SpvOpGroupIAddNonUniformAMD: {
        stream << "OpGroupIAddNonUniformAMD";
        break;
    }
    case SpvOpGroupFAddNonUniformAMD: {
        stream << "OpGroupFAddNonUniformAMD";
        break;
    }
    case SpvOpGroupFMinNonUniformAMD: {
        stream << "OpGroupFMinNonUniformAMD";
        break;
    }
    case SpvOpGroupUMinNonUniformAMD: {
        stream << "OpGroupUMinNonUniformAMD";
        break;
    }
    case SpvOpGroupSMinNonUniformAMD: {
        stream << "OpGroupSMinNonUniformAMD";
        break;
    }
    case SpvOpGroupFMaxNonUniformAMD: {
        stream << "OpGroupFMaxNonUniformAMD";
        break;
    }
    case SpvOpGroupUMaxNonUniformAMD: {
        stream << "OpGroupUMaxNonUniformAMD";
        break;
    }
    case SpvOpGroupSMaxNonUniformAMD: {
        stream << "OpGroupSMaxNonUniformAMD";
        break;
    }
    case SpvOpFragmentMaskFetchAMD: {
        stream << "OpFragmentMaskFetchAMD";
        break;
    }
    case SpvOpFragmentFetchAMD: {
        stream << "OpFragmentFetchAMD";
        break;
    }
    case SpvOpSubgroupShuffleINTEL: {
        stream << "OpSubgroupShuffleINTEL";
        break;
    }
    case SpvOpSubgroupShuffleDownINTEL: {
        stream << "OpSubgroupShuffleDownINTEL";
        break;
    }
    case SpvOpSubgroupShuffleUpINTEL: {
        stream << "OpSubgroupShuffleUpINTEL";
        break;
    }
    case SpvOpSubgroupShuffleXorINTEL: {
        stream << "OpSubgroupShuffleXorINTEL";
        break;
    }
    case SpvOpSubgroupBlockReadINTEL: {
        stream << "OpSubgroupBlockReadINTEL";
        break;
    }
    case SpvOpSubgroupBlockWriteINTEL: {
        stream << "OpSubgroupBlockWriteINTEL";
        break;
    }
    case SpvOpSubgroupImageBlockReadINTEL: {
        stream << "OpSubgroupImageBlockReadINTEL";
        break;
    }
    case SpvOpSubgroupImageBlockWriteINTEL: {
        stream << "OpSubgroupImageBlockWriteINTEL";
        break;
    }
    case SpvOpDecorateStringGOOGLE: {
        stream << "OpDecorateStringGOOGLE";
        break;
    }
    case SpvOpMemberDecorateStringGOOGLE: {
        stream << "OpMemberDecorateStringGOOGLE";
        break;
    }
    case SpvOpMax:
    default: {
        stream << "*INVALID*";
        break;
    }
    };

    if (inst.has_type()) {
        stream << std::string(" %") << std::to_string(inst.type_id());
    }

    for (uint32_t i = 0; i < inst.length(); i++) {
        if (inst.is_immediate(i)) {
            if (inst.value_type(i) == SpvStringData) {
                const char *str = (const char *)inst.data(i);
                stream << std::string(" \"") << str << "\"";
                break;
            } else if (inst.value_type(i) == SpvIntegerData) {
                const int *data = (const int *)inst.data(i);
                stream << std::string(" ") << std::to_string(*data);
                break;
            } else if (inst.value_type(i) == SpvFloatData) {
                const float *data = (const float *)inst.data(i);
                stream << std::string(" ") << std::to_string(*data);
                break;
            } else if (inst.value_type(i) == SpvBitMaskLiteral) {
                stream << std::string(" ") << std::hex << std::showbase << std::uppercase << inst.operand(i) << std::dec;
            } else {
                stream << std::string(" ") << std::to_string(inst.operand(i));
            }
        } else {
            stream << std::string(" %") << std::to_string(inst.operand(i));
        }
    }

    stream << "\n";
    return stream;
}

// --

}  // namespace Internal
}  // namespace Halide

#endif  // WITH_SPIRV

namespace Halide {
namespace Internal {

void spirv_ir_test() {

#ifdef WITH_SPIRV
    SpvBinary binary;
    SpvInstruction label_inst = SpvFactory::label(777);
    assert(label_inst.result_id() == 777);
    assert(label_inst.op_code() == SpvOpLabel);
    label_inst.encode(binary);
    assert(binary.size() == 2);  // encodes to 2x 32-bit words [Length|OpCode, ResultId]

    SpvBuilder builder;
    SpvId void_type_id = builder.reserve_id(SpvVoidTypeId);
    SpvInstruction void_inst = SpvFactory::void_type(void_type_id);
    builder.current_module().add_type(void_inst);

    SpvId int_type_id = builder.declare_type(Int(32));
    SpvId uint_type_id = builder.declare_type(UInt(32));
    SpvId float_type_id = builder.declare_type(Float(32));

    SpvBuilder::ParamTypes param_types = {int_type_id, uint_type_id, float_type_id};
    SpvId kernel_func_id = builder.add_function("kernel_func", void_type_id, param_types);
    SpvFunction kernel_func = builder.lookup_function(kernel_func_id);

    builder.enter_function(kernel_func);
    SpvId intrinsic_type_id = builder.declare_type(Type(Type::UInt, 32, 3));
    SpvId intrinsic_id = builder.declare_global_variable("InputVar", intrinsic_type_id, SpvStorageClassInput);

    SpvId output_type_id = builder.declare_type(Type(Type::UInt, 32, 1));
    SpvId output_id = builder.declare_global_variable("OutputVar", output_type_id, SpvStorageClassOutput);

    SpvBuilder::Variables entry_point_variables = {intrinsic_id, output_id};
    builder.add_entry_point(kernel_func_id, SpvExecutionModelKernel, entry_point_variables);

    SpvBuilder::Literals annotation_literals = {SpvBuiltInWorkgroupId};
    builder.add_annotation(intrinsic_id, SpvDecorationBuiltIn, annotation_literals);

    SpvId intrinsic_loaded_id = builder.reserve_id();
    builder.append(SpvFactory::load(intrinsic_type_id, intrinsic_loaded_id, intrinsic_id));

    float float_value = 32.0f;
    SpvId float_src_id = builder.add_constant(Float(32), &float_value);
    SpvId converted_value_id = builder.reserve_id(SpvResultId);
    builder.append(SpvFactory::convert(SpvOpConvertFToU, uint_type_id, converted_value_id, float_src_id));
    builder.append(SpvFactory::store(output_id, converted_value_id));
    builder.leave_function();

    binary.clear();
    builder.encode(binary);

    std::cout << "SpirV IR test passed" << std::endl;
#else
    std::cout << "SpirV IR test *disabled*" << std::endl;
#endif
}

}  // namespace Internal
}  // namespace Halide
