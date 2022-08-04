#include "SpirvIR.h"
#include <iostream>

#ifdef WITH_SPIRV

namespace Halide {
namespace Internal {

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
    contents->immediates.push_back(false);
}

void SpvInstruction::add_immediate(SpvId id) {
    check_defined();
    contents->operands.push_back(id);
    contents->immediates.push_back(true);
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

SpvId SpvInstruction::operand(uint32_t index) {
    check_defined();
    return contents->operands[index];
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
    return contents->immediates[index];
}

uint32_t SpvInstruction::length() const {
    check_defined();
    return (uint32_t)contents->operands.size();
}

SpvBlock SpvInstruction::block() const {
    check_defined();
    return contents->block;
}

void SpvInstruction::add_data(uint32_t bytes, const void *data) {
    check_defined();
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

void SpvInstruction::add_string(const std::string &str) {
    check_defined();
    add_data(str.length() + 1, (const void *)str.c_str());
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
    contents->instructions.push_back(var);
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

void SpvFunction::add_block(const SpvBlock &block) {
    check_defined();
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

void SpvModule::add_debug(const SpvInstruction &val) {
    check_defined();
    contents->debug.push_back(val);
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

SpvSourceLanguage SpvModule::source_language() const {
    check_defined();
    return contents->source_language;
}

SpvAddressingModel SpvModule::addressing_model() const {
    check_defined();
    return contents->addressing_model;
}

const SpvModule::Instructions &SpvModule::execution_modes() const {
    check_defined();
    return contents->execution_modes;
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

void SpvModule::require_extension(const std::string &extension) {
    check_defined();
    if (contents->extensions.find(extension) == contents->extensions.end()) {
        contents->extensions.insert(extension);
    }
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
    SpvModule::EntryPointNames entry_point_names(contents->entry_points.size());
    for (const SpvModuleContents::EntryPoints::value_type &v : contents->entry_points) {
        entry_point_names.push_back(v.first);
    }
    return entry_point_names;
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
    binary.push_back(SpvVersion);
    binary.push_back(contents->source_language);
    binary.push_back(0);  // Bound placeholder (aka last id used)
    binary.push_back(0);  // Reserved for schema.

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
    for (const std::string &import : contents->imports) {
        SpvInstruction inst = SpvFactory::import(import);
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

    // 7. Debug
    for (const SpvInstruction &inst : contents->debug) {
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

void SpvBuilder::encode(SpvBinary &binary) const {
    // Encode the module
    module.encode(binary);
}

SpvId SpvBuilder::map_type(const Type &type, uint32_t array_size) {
    SpvId type_id = lookup_type(type, array_size);
    if (type_id == SpvInvalidId) {
        type_id = declare_type(type, array_size);
    }
    return type_id;
}

SpvId SpvBuilder::map_pointer_type(const Type &type, SpvStorageClass storage_class) {
    SpvId ptr_type_id = lookup_pointer_type(type, storage_class);
    if (ptr_type_id == SpvInvalidId) {
        ptr_type_id = declare_pointer_type(ptr_type_id, storage_class);
    }
    return ptr_type_id;
}

SpvId SpvBuilder::map_pointer_type(SpvId type_id, SpvStorageClass storage_class) {
    SpvId ptr_type_id = lookup_pointer_type(type_id, storage_class);
    if (ptr_type_id == SpvInvalidId) {
        ptr_type_id = declare_pointer_type(type_id, storage_class);
    }
    return ptr_type_id;
}

SpvId SpvBuilder::map_function_type(SpvId return_type, const ParamTypes &param_types) {
    SpvId type_id = lookup_function_type(return_type, param_types);
    if (type_id == SpvInvalidId) {
        type_id = declare_function_type(return_type, param_types);
    }
    return type_id;
}

SpvId SpvBuilder::map_constant(const Type &type, const void *data) {
    SpvId result_id = lookup_constant(type, data);
    if (result_id == SpvInvalidId) {
        result_id = declare_constant(type, data);
    }
    return result_id;
}

void SpvBuilder::add_entry_point(const std::string &name,
                                 SpvId func_id, SpvExecutionModel exec_model,
                                 const Variables &variables) {

    SpvInstruction inst = SpvFactory::entry_point(exec_model, func_id, name, variables);
    module.add_entry_point(name, inst);
}

SpvFunction SpvBuilder::add_function(SpvId return_type_id, const ParamTypes &param_types) {
    SpvId func_id = declare_id(SpvFunctionId);
    SpvId func_type_id = map_function_type(return_type_id, param_types);
    SpvFunction func = SpvFunction::make(func_type_id, func_id, return_type_id);
    for (SpvId param_type_id : param_types) {
        SpvId param_id = declare_id(SpvParameterId);
        SpvInstruction param_inst = SpvFactory::function_parameter(param_type_id, param_id);
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

SpvId SpvBuilder::add_global_variable(SpvId type_id, uint32_t storage_class, SpvId init_id) {
    SpvId var_id = reserve_id(SpvVariableId);
    module.add_global(SpvFactory::variable(var_id, type_id, storage_class, init_id));
    return var_id;
}

SpvId SpvBuilder::add_variable(SpvId type_id, uint32_t storage_class, SpvId init_id) {
    SpvId var_id = reserve_id(SpvVariableId);
    current_block().add_variable(SpvFactory::variable(var_id, type_id, storage_class, init_id));
    return var_id;
}

void SpvBuilder::add_annotation(SpvId target_id, SpvDecoration decoration_type, const Literals &literals) {
    SpvInstruction inst = SpvFactory::decorate(target_id, decoration_type, literals);
    current_module().add_annotation(inst);
}

void SpvBuilder::add_struct_annotation(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const Literals &literals) {
    SpvInstruction inst = SpvFactory::decorate_member(struct_type_id, member_index, decoration_type, literals);
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
    block_stack.push(block);
}

SpvBlock SpvBuilder::current_block() const {
    SpvBlock block;
    if (!block_stack.empty()) {
        block = block_stack.top();
    }
    return block;
}

SpvBlock SpvBuilder::leave_block() {
    SpvBlock block;
    if (!block_stack.empty()) {
        block = block_stack.top();
        block_stack.pop();
    }
    return block;
}

SpvFunction SpvBuilder::lookup_function(SpvId func_id) const {
    SpvFunction func;
    FunctionMap::const_iterator it = function_map.find(func_id);
    if (it != function_map.end()) {
        func = it->second;
    }
    return func;
}

void SpvBuilder::enter_function(const SpvFunction &func) {
    function_stack.push(func);
    enter_block(func.entry_block());
}

SpvFunction SpvBuilder::current_function() const {
    SpvFunction func;
    if (!function_stack.empty()) {
        func = function_stack.top();
    }
    return func;
}

SpvFunction SpvBuilder::leave_function() {
    SpvFunction func;
    leave_block();
    if (!function_stack.empty()) {
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
    if (!module.is_capability_required(capability)) {
        module.require_capability(capability);
    }
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
    TypeKey key(4 + sizeof(uint32_t), ' ');
    key[0] = type.code();
    key[1] = type.bits();
    key[2] = type.lanes() & 0xff;
    key[3] = (type.lanes() >> 8) & 0xff;
    for (size_t i = 0; i < sizeof(uint32_t); i++) {
        key[i + 4] = (array_size & 0xff);
        array_size >>= 8;
    }
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

SpvId SpvBuilder::declare_type(const Type &type, uint32_t array_size) {
    SpvBuilder::TypeKey type_key = make_type_key(type, array_size);
    TypeMap::const_iterator it = type_map.find(type_key);
    if (it != type_map.end()) {
        return it->second;
    }

    if (array_size > 1) {
        SpvId array_type_id = declare_id(SpvArrayTypeId);
        SpvId element_type_id = declare_type(type, 1);
        SpvInstruction inst = SpvFactory::array_type(array_type_id, element_type_id, array_size);
        module.add_type(inst);
        type_map[type_key] = array_type_id;
        return array_type_id;
    }

    SpvId type_id = SpvInvalidId;
    if (type.is_vector()) {
        type_id = declare_id(SpvVectorTypeId);
        SpvId element_type_id = declare_type(type.with_lanes(1));
        SpvInstruction inst = SpvFactory::vector_type(type_id, element_type_id, type.lanes());
        module.add_type(inst);
    } else {
        if (type.is_handle()) {
            type_id = declare_id(SpvVoidTypeId);
            SpvInstruction inst = SpvFactory::void_type(type_id);
            module.add_type(inst);
        } else if (type.is_bool()) {
            type_id = declare_id(SpvBoolTypeId);
            SpvInstruction inst = SpvFactory::bool_type(type_id);
            module.add_type(inst);
        } else if (type.is_float()) {
            type_id = declare_id(SpvFloatTypeId);
            SpvInstruction inst = SpvFactory::float_type(type_id, type.bits());
            module.add_type(inst);
        } else if (type.is_int_or_uint()) {
            type_id = declare_id(SpvIntTypeId);
            SpvId signedness = type.is_uint() ? 0 : 1;
            SpvInstruction inst = SpvFactory::integer_type(type_id, type.bits(), signedness);
            module.add_type(inst);
        } else {
            internal_error << "SPIRV: Unsupported type " << type << "\n";
        }
    }

    type_map[type_key] = type_id;
    return type_id;
}

SpvBuilder::TypeKey SpvBuilder::make_struct_type_key(const StructMemberTypes &member_type_ids) const {
    TypeKey key(member_type_ids.size() * sizeof(SpvId), ' ');
    uint32_t index = 0;
    for (SpvId type_id : member_type_ids) {
        for (size_t i = 0; i < sizeof(uint32_t); i++, index++) {
            key[index] = (type_id & 0xff);
            type_id >>= 8;
        }
    }
    return key;
}

SpvId SpvBuilder::lookup_struct(const StructMemberTypes &member_type_ids) const {
    TypeKey key = make_struct_type_key(member_type_ids);
    TypeMap::const_iterator it = struct_map.find(key);
    if (it != struct_map.end()) {
        return it->second;
    }
    return SpvInvalidId;
}

SpvId SpvBuilder::declare_struct(const StructMemberTypes &member_type_ids) {
    TypeKey key = make_struct_type_key(member_type_ids);
    TypeMap::const_iterator it = struct_map.find(key);
    if (it != struct_map.end()) {
        return it->second;
    }

    SpvId struct_type_id = declare_id(SpvStructTypeId);
    SpvInstruction inst = SpvFactory::struct_type(struct_type_id, member_type_ids);
    module.add_type(inst);
    struct_map[key] = struct_type_id;
    return struct_type_id;
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

SpvId SpvBuilder::declare_pointer_type(const Type &type, SpvStorageClass storage_class) {
    SpvId base_type_id = map_type(type);
    return declare_pointer_type(base_type_id, storage_class);
}

SpvId SpvBuilder::declare_pointer_type(SpvId base_type_id, SpvStorageClass storage_class) {
    PointerTypeKey key = make_pointer_type_key(base_type_id, storage_class);
    PointerTypeMap::const_iterator it = pointer_type_map.find(key);
    if (it != pointer_type_map.end()) {
        return it->second;
    }

    SpvId pointer_type_id = declare_id(SpvPointerTypeId);
    SpvInstruction inst = SpvFactory::pointer_type(pointer_type_id, storage_class, base_type_id);
    module.add_type(inst);
    pointer_type_map[key] = pointer_type_id;
    return pointer_type_id;
}

SpvBuilder::ConstantKey SpvBuilder::make_constant_key(const Type &type, const void *data) const {
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

SpvBuilder::ConstantKey SpvBuilder::make_bool_constant_key(bool value) const {
    Type type = Bool();
    bool data = value;
    return make_constant_key(type, &data);
}

SpvBuilder::ConstantKey SpvBuilder::make_null_constant_key(const Type &type) const {
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

    SpvId result_id = declare_id(SpvConstantId);
    SpvId type_id = declare_type(type);
    SpvInstruction inst = SpvFactory::null_constant(result_id, type_id);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_bool_constant(bool value) {
    const std::string key = make_bool_constant_key(value);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    debug(3) << "declare_bool_constant for " << value << "\n";

    Type type = Bool();
    SpvId result_id = declare_id(SpvBoolConstantId);
    SpvId type_id = declare_type(type);
    SpvInstruction inst = SpvFactory::bool_constant(result_id, type_id, value);
    module.add_constant(inst);
    constant_map[key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_scalar_constant(const Type &scalar_type, const void *data) {
    if (scalar_type.lanes() != 1) {
        internal_error << "SPIRV: Invalid type provided for scalar constant!" << scalar_type << "\n";
        return SpvInvalidId;
    }

    const std::string constant_key = make_constant_key(scalar_type, data);
    ConstantMap::const_iterator it = constant_map.find(constant_key);
    if (it != constant_map.end()) {
        return it->second;
    }

    if (scalar_type.is_bool() && data) {
        bool value = *reinterpret_cast<const bool *>(data);
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
    SpvInstruction inst = SpvFactory::constant(result_id, type_id, scalar_type.bytes(), data);
    module.add_constant(inst);
    constant_map[constant_key] = result_id;
    return result_id;
}

SpvId SpvBuilder::declare_vector_constant(const Type &type, const void *data) {
    if (type.lanes() == 1) {
        internal_error << "SPIRV: Invalid type provided for vector constant!" << type << "\n";
        return SpvInvalidId;
    }

    const std::string key = make_constant_key(type, data);
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

    SpvId result_id = declare_id(SpvCompositeConstantId);
    SpvId type_id = declare_type(type);
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

SpvId SpvBuilder::declare_constant(const Type &type, const void *data) {

    const std::string key = make_constant_key(type, data);
    ConstantMap::const_iterator it = constant_map.find(key);
    if (it != constant_map.end()) {
        return it->second;
    }

    debug(3) << "declare_constant for type " << type << "\n";
    if (type.lanes() == 1) {
        return declare_scalar_constant(type, data);
    } else {
        return declare_vector_constant(type, data);
    }
}

SpvId SpvBuilder::declare_access_chain(SpvId ptr_type_id, SpvId base_id, SpvId element_id, const Indices &indices) {
    SpvId access_chain_id = declare_id(SpvAccessChainId);
    append(SpvFactory::in_bounds_access_chain(ptr_type_id, access_chain_id, base_id, element_id, indices));
    return access_chain_id;
}

SpvId SpvBuilder::map_instruction(const SpvInstruction &inst) {
    const SpvId key = inst.result_id();
    if (instruction_map.find(key) == instruction_map.end()) {
        instruction_map.insert({key, inst});
    } else {
        instruction_map[key] = inst;
    }
    return key;
}

SpvInstruction SpvBuilder::lookup_instruction(SpvId result_id) const {
    InstructionMap::const_iterator it = instruction_map.find(result_id);
    if (it == instruction_map.end()) {
        return SpvInstruction();
    }
    return it->second;
}

SpvBuilder::FunctionTypeKey SpvBuilder::make_function_type_key(SpvId return_type_id, const ParamTypes &param_type_ids) const {
    TypeKey key((1 + param_type_ids.size()) * sizeof(SpvId), ' ');

    uint32_t index = 0;
    for (size_t i = 0; i < sizeof(uint32_t); i++, index++) {
        key[index] = (return_type_id & 0xff);
        return_type_id >>= 8;
    }
    for (SpvId type_id : param_type_ids) {
        for (size_t i = 0; i < sizeof(uint32_t); i++, index++) {
            key[index] = (type_id & 0xff);
            type_id >>= 8;
        }
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

SpvId SpvBuilder::declare_function_type(SpvId return_type_id, const ParamTypes &param_type_ids) {
    FunctionTypeKey func_type_key = make_function_type_key(return_type_id, param_type_ids);
    FunctionTypeMap::const_iterator it = function_type_map.find(func_type_key);
    if (it != function_type_map.end()) {
        return it->second;
    }

    SpvId function_type_id = declare_id(SpvFunctionTypeId);
    SpvInstruction inst = SpvFactory::function_type(function_type_id, return_type_id, param_type_ids);
    module.add_type(inst);
    function_type_map[func_type_key] = function_type_id;
    return function_type_id;
}

SpvId SpvBuilder::declare_runtime_array(SpvId base_type_id) {
    SpvId runtime_array_id = declare_id(SpvRuntimeArrayTypeId);
    SpvInstruction inst = SpvFactory::runtime_array_type(runtime_array_id, base_type_id);
    module.add_type(inst);
    return runtime_array_id;
}

void SpvBuilder::append(SpvInstruction inst) {
    if (!block_stack.empty()) {
        current_block().add_instruction(std::move(inst));
    } else {
        internal_error << "SPIRV: Current block undefined! Unable to append!\n";
    }
}

// --

// -- Factory Methods for Specific Instructions

SpvInstruction SpvFactory::label(SpvId result_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpLabel);
    inst.set_result_id(result_id);
    return inst;
}

SpvInstruction SpvFactory::decorate(SpvId target_id, SpvDecoration decoration_type, const SpvFactory::Literals &literals) {
    SpvInstruction inst = SpvInstruction::make(SpvOpDecorate);
    inst.add_operand(target_id);
    inst.add_immediate(decoration_type);
    for (uint32_t l : literals) {
        inst.add_immediate(l);
    }
    return inst;
}

SpvInstruction SpvFactory::decorate_member(SpvId struct_type_id, uint32_t member_index, SpvDecoration decoration_type, const SpvFactory::Literals &literals) {
    SpvInstruction inst = SpvInstruction::make(SpvOpMemberDecorate);
    inst.add_operand(struct_type_id);
    inst.add_immediate(decoration_type);
    for (uint32_t l : literals) {
        inst.add_immediate(l);
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
    inst.add_immediate(bits);
    inst.add_immediate(signedness);
    return inst;
}

SpvInstruction SpvFactory::float_type(SpvId float_type_id, uint32_t bits) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeFloat);
    inst.set_result_id(float_type_id);
    inst.add_immediate(bits);
    return inst;
}

SpvInstruction SpvFactory::vector_type(SpvId vector_type_id, SpvId element_type_id, uint32_t vector_size) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeVector);
    inst.set_result_id(vector_type_id);
    inst.add_operand(element_type_id);
    inst.add_immediate(vector_size);
    return inst;
}

SpvInstruction SpvFactory::array_type(SpvId array_type_id, SpvId element_type_id, uint32_t array_size) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeArray);
    inst.set_result_id(array_type_id);
    inst.add_operand(element_type_id);
    inst.add_immediate(array_size);
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
    inst.add_immediate(storage_class);
    inst.add_operand(base_type_id);
    return inst;
}

SpvInstruction SpvFactory::function_type(SpvId function_type_id, SpvId return_type_id, const SpvFactory::ParamTypes &param_type_ids) {
    SpvInstruction inst = SpvInstruction::make(SpvOpTypeFunction);
    inst.set_type_id(return_type_id);
    inst.set_result_id(function_type_id);
    for (SpvId type_id : param_type_ids) {
        inst.add_operand(type_id);
    }
    return inst;
}

SpvInstruction SpvFactory::constant(SpvId result_id, SpvId type_id, size_t bytes, const void *data) {
    SpvInstruction inst = SpvInstruction::make(SpvOpConstant);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_data(bytes, data);
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
    inst.add_immediate(storage_class);
    if (initializer_id != SpvInvalidId) {
        inst.add_operand(initializer_id);
    }
    return inst;
}

SpvInstruction SpvFactory::function(SpvId return_type_id, SpvId func_id, uint32_t control_mask, SpvId func_type_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpFunction);
    inst.set_type_id(return_type_id);
    inst.set_result_id(func_id);
    inst.add_immediate(control_mask);
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
    inst.add_immediate(exec_model);
    inst.add_operand(func_id);
    inst.add_string(name);
    for (SpvId var : variables) {
        inst.add_operand(var);
    }
    return inst;
}

SpvInstruction SpvFactory::memory_model(SpvAddressingModel addressing_model, SpvMemoryModel memory_model) {
    SpvInstruction inst = SpvInstruction::make(SpvOpMemoryModel);
    inst.add_immediate(addressing_model);
    inst.add_immediate(memory_model);
    return inst;
}

SpvInstruction SpvFactory::exec_mode_local_size(SpvId function_id, uint32_t wg_size_x, uint32_t wg_size_y, uint32_t wg_size_z) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExecutionMode);
    inst.add_operand(function_id);
    inst.add_immediate(SpvExecutionModeLocalSize);
    inst.add_immediate(wg_size_x);
    inst.add_immediate(wg_size_y);
    inst.add_immediate(wg_size_z);
    return inst;
}

SpvInstruction SpvFactory::control_barrier(SpvId execution_scope_id, SpvId memory_scope_id, uint32_t semantics_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpControlBarrier);
    inst.add_operand(execution_scope_id);
    inst.add_operand(memory_scope_id);
    inst.add_immediate(semantics_mask);
    return inst;
}

SpvInstruction SpvFactory::logical_not(SpvId type_id, SpvId result_id, SpvId src_id) {
    return unary_op(SpvOpNot, type_id, result_id, src_id);
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

SpvInstruction SpvFactory::in_bounds_access_chain(SpvId type_id, SpvId result_id, SpvId base_id, SpvId element_id, const SpvFactory::Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpInBoundsAccessChain);
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
    inst.add_immediate(access_mask);
    return inst;
}

SpvInstruction SpvFactory::store(SpvId ptr_id, SpvId obj_id, uint32_t access_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpStore);
    inst.add_operand(ptr_id);
    inst.add_operand(obj_id);
    inst.add_immediate(access_mask);
    return inst;
}

SpvInstruction SpvFactory::composite_extract(SpvId type_id, SpvId result_id, SpvId composite_id, const SpvFactory::Indices &indices) {
    SpvInstruction inst = SpvInstruction::make(SpvOpCompositeExtract);
    inst.set_type_id(type_id);
    inst.set_result_id(result_id);
    inst.add_operand(composite_id);
    for (SpvId i : indices) {
        inst.add_immediate(i);
    }
    return inst;
}

SpvInstruction SpvFactory::vector_insert_dynamic(SpvId result_id, SpvId vector_id, SpvId value_id, uint32_t index) {
    SpvInstruction inst = SpvInstruction::make(SpvOpVectorInsertDynamic);
    inst.set_type_id(SpvOpTypeVector);
    inst.set_result_id(result_id);
    inst.add_operand(vector_id);
    inst.add_operand(value_id);
    inst.add_immediate(index);
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

SpvInstruction SpvFactory::branch(SpvId target_label_id) {
    SpvInstruction inst = SpvInstruction::make(SpvOpBranch);
    inst.add_operand(target_label_id);
    return inst;
}

SpvInstruction SpvFactory::conditional_branch(SpvId condition_label_id, SpvId true_label_id, SpvId false_label_id, const SpvFactory::BranchWeights &weights) {
    SpvInstruction inst = SpvInstruction::make(SpvOpBranch);
    inst.add_operand(condition_label_id);
    inst.add_operand(true_label_id);
    inst.add_operand(false_label_id);
    for (uint32_t w : weights) {
        inst.add_immediate(w);
    }
    return inst;
}

SpvInstruction SpvFactory::loop_merge(SpvId merge_label_id, SpvId continue_label_id, uint32_t loop_control_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpLoopMerge);
    inst.add_operand(merge_label_id);
    inst.add_operand(continue_label_id);
    inst.add_immediate(loop_control_mask);
    return inst;
}

SpvInstruction SpvFactory::selection_merge(SpvId merge_label_id, uint32_t selection_control_mask) {
    SpvInstruction inst = SpvInstruction::make(SpvOpSelectionMerge);
    inst.add_operand(merge_label_id);
    inst.add_immediate(selection_control_mask);
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
    inst.add_immediate(capability);
    return inst;
}

SpvInstruction SpvFactory::extension(const std::string &extension) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExtension);
    inst.add_string(extension);
    return inst;
}

SpvInstruction SpvFactory::import(const std::string &import) {
    SpvInstruction inst = SpvInstruction::make(SpvOpExtInstImport);
    inst.add_string(import);
    return inst;
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

    SpvId int_type_id = builder.map_type(Int(32));
    SpvId uint_type_id = builder.map_type(UInt(32));
    SpvId float_type_id = builder.map_type(Float(32));

    SpvBuilder::ParamTypes param_types = {int_type_id, uint_type_id, float_type_id};
    SpvFunction function = builder.add_function(void_type_id, param_types);

    builder.enter_function(function);
    SpvId intrinsic_type_id = builder.map_type(Type(Type::UInt, 32, 3));
    SpvId intrinsic_id = builder.add_global_variable(intrinsic_type_id, SpvStorageClassInput);

    SpvId output_type_id = builder.map_type(Type(Type::UInt, 32, 1));
    SpvId output_id = builder.add_global_variable(output_type_id, SpvStorageClassOutput);

    SpvBuilder::Variables entry_point_variables;
    entry_point_variables.push_back(intrinsic_id);
    entry_point_variables.push_back(output_id);
    builder.add_entry_point("entry_func", function.id(), SpvExecutionModelKernel, entry_point_variables);

    SpvBuilder::Literals annotation_literals = {SpvBuiltInWorkgroupId};
    builder.add_annotation(intrinsic_id, SpvDecorationBuiltIn, annotation_literals);

    SpvId intrinsic_loaded_id = builder.reserve_id();
    builder.append(SpvFactory::load(intrinsic_type_id, intrinsic_loaded_id, intrinsic_id));

    float float_value = 32.0f;
    SpvId float_src_id = builder.declare_constant(Float(32), &float_value);
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
