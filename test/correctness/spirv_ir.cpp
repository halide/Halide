#include "Halide.h"

// SpirvIR.h is an internal-only header (not part of the public Halide.h
// umbrella) used to construct SPIR-V modules for the Vulkan backend.
#include "SpirvIR.h"

using namespace Halide;
using namespace Halide::Internal;

int main(int argc, char **argv) {
    SpvBinary binary;
    SpvInstruction label_inst = SpvFactory::label(777);
    internal_assert(label_inst.result_id() == 777);
    internal_assert(label_inst.op_code() == SpvOpLabel);
    label_inst.encode(binary);
    internal_assert(binary.size() == 2);  // encodes to 2x 32-bit words [Length|OpCode, ResultId]

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

    printf("Success!\n");
    return 0;
}
