#include <iostream>
#include <fstream>
#include <memory>

#include "ExtractKernels.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Closure.h"
#include "Param.h"
#include "Image.h"
#include "LLVM_Output.h"
#include "RemoveTrivialForLoops.h"
#include "InjectHostDevBufferCopies.h"
#include "LLVM_Headers.h"
#include "DeviceArgument.h"
#include "CodeGen_GPU_Dev.h"

namespace Halide {
namespace Internal {

using std::map;
using std::string;
using std::vector;

namespace {

// Replace the parameter objects of loads/stores with a new parameter
// object.
class ReplaceParams : public IRMutator {
    const map<string, Parameter> &replacements;

    using IRMutator::visit;

    void visit(const Load *op) {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            expr = Load::make(op->type, op->name, mutate(op->index), op->image, i->second);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        auto i = replacements.find(op->name);
        if (i != replacements.end()) {
            stmt = Store::make(op->name, mutate(op->value), mutate(op->index), i->second);
        } else {
            IRMutator::visit(op);
        }
    }

public:
    ReplaceParams(const map<string, Parameter> &replacements)
        : replacements(replacements) {}
};

Stmt replace_params(Stmt s, const map<string, Parameter> &replacements) {
    return ReplaceParams(replacements).mutate(s);
}

DeviceAPI device_api_for_target_feature(const Target &t) {
    if (t.has_feature(Target::CUDA)) {
        return DeviceAPI::CUDA;
    } else if (t.has_feature(Target::OpenCL)) {
        return DeviceAPI::OpenCL;
    } else if (t.has_feature(Target::OpenGL)) {
        return DeviceAPI::GLSL;
    } else if (t.has_feature(Target::Renderscript)) {
        return DeviceAPI::Renderscript;
    } else if (t.has_feature(Target::OpenGLCompute)) {
        return DeviceAPI::OpenGLCompute;
    } else if (t.has_feature(Target::Metal)) {
        return DeviceAPI::Metal;
    } else if (t.has_feature(Target::HVX_64)) {
        return DeviceAPI::Hexagon;
    } else if (t.has_feature(Target::HVX_128)) {
        return DeviceAPI::Hexagon;
    } else if (t.has_feature(Target::HVX_v62)) {
        return DeviceAPI::Hexagon;
    } else {
        return DeviceAPI::None;
    }
}

// Sniff the contents of a kernel to extracts the bounds of all the
// thread indices (so we know how many threads to launch), and the
// amount of shared memory to allocate.
class ExtractBounds : public IRVisitor {
public:

    Expr num_threads[4];
    Expr num_blocks[4];
    Expr shared_mem_size;

    ExtractBounds() : shared_mem_size(0), found_shared(false) {
        for (int i = 0; i < 4; i++) {
            num_threads[i] = num_blocks[i] = 1;
        }
    }

private:

    bool found_shared;

    using IRVisitor::visit;

    void visit(const For *op) {
        if (CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            internal_assert(is_zero(op->min));
        }

        if (ends_with(op->name, ".__thread_id_x")) {
            num_threads[0] = op->extent;
        } else if (ends_with(op->name, ".__thread_id_y")) {
            num_threads[1] = op->extent;
        } else if (ends_with(op->name, ".__thread_id_z")) {
            num_threads[2] = op->extent;
        } else if (ends_with(op->name, ".__thread_id_w")) {
            num_threads[3] = op->extent;
        } else if (ends_with(op->name, ".__block_id_x")) {
            num_blocks[0] = op->extent;
        } else if (ends_with(op->name, ".__block_id_y")) {
            num_blocks[1] = op->extent;
        } else if (ends_with(op->name, ".__block_id_z")) {
            num_blocks[2] = op->extent;
        } else if (ends_with(op->name, ".__block_id_w")) {
            num_blocks[3] = op->extent;
        }

        op->body.accept(this);
    }

    void visit(const LetStmt *op) {
        if (expr_uses_var(shared_mem_size, op->name)) {
            shared_mem_size = Let::make(op->name, op->value, shared_mem_size);
        }
        op->body.accept(this);
    }

    void visit(const Allocate *allocate) {
        user_assert(!allocate->new_expr.defined()) << "Allocate node inside GPU kernel has custom new expression.\n" <<
            "(Memoization is not supported inside GPU kernels at present.)\n";

        if (allocate->name == "__shared") {
            internal_assert(allocate->type == UInt(8) && allocate->extents.size() == 1);
            shared_mem_size = allocate->extents[0];
            found_shared = true;
        }
        allocate->body.accept(this);
    }
};

class InjectDeviceRPC : public IRMutator {
    string function_name;
    map<string, Expr> state_vars;

    Module device_code;

    /** Alignment info for Int(32) variables in scope, so we don't
     * lose the information when creating device kernels. */
    Scope<ModulusRemainder> alignment_info;

    using IRMutator::visit;

    Expr state_var(const string& name, Type type) {
        Expr& var = state_vars[name];
        if (!var.defined()) {
            Buffer storage(type, {}, nullptr, name + "_buf");
            *(void **)storage.host_ptr() = nullptr;
            var = Load::make(type_of<void*>(), name + "_buf", 0, storage, Parameter());
        }
        return var;
    }

    Expr state_var_ptr(const string& name, Type type) {
        Expr var = state_var(name, type);
        return Call::make(Handle(), Call::address_of, {var}, Call::Intrinsic);
    }

    Expr module_state(const string &api_unique_name) {
        return state_var("module_state_" + function_name + "_" + api_unique_name,
                         type_of<void*>());
    }

    Expr module_state_ptr(const string &api_unique_name) {
        return state_var_ptr("module_state_" + function_name + "_" + api_unique_name,
                             type_of<void*>());
    }

    // Create a Buffer containing the given buffer/size, and return an
    // expression for a pointer to the first element.
    Expr buffer_ptr(const uint8_t* buffer, size_t size, const char* name) {
        Buffer code(type_of<uint8_t>(), {(int)size}, nullptr, name);
        memcpy(code.host_ptr(), buffer, (int)size);

        Expr ptr_0 = Load::make(type_of<uint8_t>(), name, 0, code, Parameter());
        return Call::make(Handle(), Call::address_of, {ptr_0}, Call::Intrinsic);
    }

    Stmt launch_hexagon_kernel(const string &loop_name, DeviceAPI device_api, Stmt body) {
        const string api_unique_name = device_api_to_string(device_api);

        // Unrolling or loop partitioning might generate multiple
        // loops with the same name, so we need to make them unique.
        string api_func_name = unique_name(api_unique_name + "_" + loop_name);

        debug(1) << "Launching " << api_func_name << " device kernel\n";

        // Build a closure for the device code.
        // TODO: Should this move the body of the loop to Hexagon,
        // or the loop itself? Currently, this moves the loop itself.
        Closure c(body);

        // Make an argument list, and generate a function in the
        // device_code module. The hexagon runtime code expects
        // the arguments to appear in the order of (input buffers,
        // output buffers, input scalars).  Scalars must be last
        // for the scalar arguments to shadow the symbols of the
        // buffer that get generated by CodeGen_LLVM.
        vector<LoweredArgument> input_buffers, output_buffers;
        map<string, Parameter> replacement_params;
        for (const auto& i : c.buffers) {
            if (i.second.write) {
                Argument::Kind kind = Argument::OutputBuffer;
                output_buffers.push_back(LoweredArgument(i.first, kind, i.second.type, i.second.dimensions));
            } else {
                Argument::Kind kind = Argument::InputBuffer;
                input_buffers.push_back(LoweredArgument(i.first, kind, i.second.type, i.second.dimensions));
            }

            // Build a parameter to replace.
            Parameter p(i.second.type, true, i.second.dimensions);
            // Assert that buffers are aligned to one HVX vector.
            const int alignment = 128;
            p.set_host_alignment(alignment);
            // The other parameter constraints are already
            // accounted for by the closure grabbing those
            // arguments, so we only need to provide the host
            // alignment.
            replacement_params[i.first] = p;

            // Add an assert to the body that validates the
            // alignment of the buffer.
            if (!device_code.target().has_feature(Target::NoAsserts)) {
                Expr host_ptr = reinterpret<uint64_t>(Variable::make(Handle(), i.first + ".host"));
                Expr error = Call::make(Int(32), "halide_error_unaligned_host_ptr",
                                        {i.first, alignment}, Call::Extern);
                body = Block::make(AssertStmt::make(host_ptr % alignment == 0, error), body);
            }
        }
        body = replace_params(body, replacement_params);

        vector<LoweredArgument> args;
        args.insert(args.end(), input_buffers.begin(), input_buffers.end());
        args.insert(args.end(), output_buffers.begin(), output_buffers.end());
        for (const auto& i : c.vars) {
            LoweredArgument arg(i.first, Argument::InputScalar, i.second, 0);
            if (alignment_info.contains(i.first)) {
                arg.alignment = alignment_info.get(i.first);
            }
            args.push_back(arg);
        }
        device_code.append(LoweredFunc(api_func_name, args, body, LoweredFunc::External));

        // Generate a call to hexagon_device_run.
        vector<Expr> arg_sizes;
        vector<Expr> arg_ptrs;
        vector<Expr> arg_flags;

        for (const auto& i : c.buffers) {
            arg_sizes.push_back(Expr((uint64_t) sizeof(buffer_t*)));
            arg_ptrs.push_back(Variable::make(type_of<buffer_t *>(), i.first + ".buffer"));
            // In the flags parameter, bit 0 set indicates the
            // buffer is read, bit 1 set indicates the buffer is
            // written. If neither are set, the argument is a scalar.
            int flags = 0;
            if (i.second.read) flags |= 0x1;
            if (i.second.write) flags |= 0x2;
            arg_flags.push_back(flags);
        }
        for (const auto& i : c.vars) {
            Expr arg = Variable::make(i.second, i.first);
            Expr arg_ptr = Call::make(type_of<void *>(), Call::make_struct, {arg}, Call::Intrinsic);
            arg_sizes.push_back(Expr((uint64_t) i.second.bytes()));
            arg_ptrs.push_back(arg_ptr);
            arg_flags.push_back(0x0);
        }

        // The argument list is terminated with an argument of size 0.
        arg_sizes.push_back(Expr((uint64_t) 0));

        string pipeline_name = api_func_name + "_argv";
        vector<Expr> params;
        params.push_back(module_state(api_unique_name));
        params.push_back(pipeline_name);
        params.push_back(state_var_ptr(api_func_name, type_of<int>()));
        params.push_back(Call::make(type_of<size_t*>(), Call::make_struct, arg_sizes, Call::Intrinsic));
        params.push_back(Call::make(type_of<void**>(), Call::make_struct, arg_ptrs, Call::Intrinsic));
        params.push_back(Call::make(type_of<int*>(), Call::make_struct, arg_flags, Call::Intrinsic));

        return call_extern_and_assert("halide_hexagon_run", params);
    }

    Stmt launch_gpu_kernel(const string &loop_name, DeviceAPI device_api, Stmt body) {
        internal_assert(CodeGen_GPU_Dev::is_gpu_var(loop_name));
        internal_assert(device_api != DeviceAPI::Default_GPU)
            << "A concrete device API should have been selected\n";

        const string api_unique_name = device_api_to_string(device_api);

        // Unrolling or loop partitioning might generate multiple
        // loops with the same name, so we need to make them unique.
        string api_func_name = unique_name(api_unique_name + "_" + loop_name);

        debug(1) << "Launching " << api_func_name << " device kernel\n";

        ExtractBounds bounds;
        body.accept(&bounds);

        debug(2) << "GPU Kernel bounds: ("
                 << bounds.num_threads[0] << ", "
                 << bounds.num_threads[1] << ", "
                 << bounds.num_threads[2] << ", "
                 << bounds.num_threads[3] << ") threads, ("
                 << bounds.num_blocks[0] << ", "
                 << bounds.num_blocks[1] << ", "
                 << bounds.num_blocks[2] << ", "
                 << bounds.num_blocks[3] << ") blocks\n";

        // compile the kernel
        string kernel_name = unique_name("kernel_" + loop_name);
        for (size_t i = 0; i < kernel_name.size(); i++) {
            if (!isalnum(kernel_name[i])) {
                kernel_name[i] = '_';
            }
        }

        Value *null_float_ptr = ConstantPointerNull::get(CodeGen_LLVM::f32_t->getPointerTo());
        Value *zero_int32 = codegen(Expr(cast<int>(0)));

        Value *gpu_num_padded_attributes  = zero_int32;
        Value *gpu_vertex_buffer   = null_float_ptr;
        Value *gpu_num_coords_dim0 = zero_int32;
        Value *gpu_num_coords_dim1 = zero_int32;

        if (device_api == DeviceAPI::GLSL) {

            // GL draw calls that invoke the GLSL shader are issued for pairs of
            // for-loops over spatial x and y dimensions. For each for-loop we create
            // one scalar vertex attribute for the spatial dimension corresponding to
            // that loop, plus one scalar attribute for each expression previously
            // labeled as "glsl_varying"

            // Pass variables created during setup_gpu_vertex_buffer to the
            // dev run function call.
            gpu_num_padded_attributes = codegen(Variable::make(Int(32), "glsl.num_padded_attributes"));
            gpu_num_coords_dim0 = codegen(Variable::make(Int(32), "glsl.num_coords_dim0"));
            gpu_num_coords_dim1 = codegen(Variable::make(Int(32), "glsl.num_coords_dim1"));

            // Look up the allocation for the vertex buffer and cast it to the
            // right type
            gpu_vertex_buffer = codegen(Variable::make(Handle(), "glsl.vertex_buffer.host"));
            gpu_vertex_buffer = builder->CreatePointerCast(gpu_vertex_buffer,
                                                           CodeGen_LLVM::f32_t->getPointerTo());
        }

        // compute a closure over the state passed into the kernel
        HostClosure c(body, loop_name);

        // Determine the arguments that must be passed into the halide function
        vector<DeviceArgument> closure_args = c.arguments();

        if (device_api == DeviceAPI::Renderscript) {
            closure_args.insert(closure_args.begin(), DeviceArgument(".rs_slot_offset", false, Int(32), 0));
        }

        // Halide allows passing of scalar float and integer arguments. For
        // OpenGL, pack these into vec4 uniforms and varying attributes
        if (device_api == DeviceAPI::GLSL) {

            int num_uniform_floats = 0;

            // The spatial x and y coordinates are passed in the first two
            // scalar float varying slots
            int num_varying_floats = 2;
            int num_uniform_ints   = 0;

            // Pack scalar parameters into vec4
            for (size_t i = 0; i < closure_args.size(); i++) {
                if (closure_args[i].is_buffer) {
                    continue;
                } else if (ends_with(closure_args[i].name, ".varying")) {
                    closure_args[i].packed_index = num_varying_floats++;
                } else if (closure_args[i].type.is_float()) {
                    closure_args[i].packed_index = num_uniform_floats++;
                } else if (closure_args[i].type.is_int()) {
                    closure_args[i].packed_index = num_uniform_ints++;
                }
            }
        }

        for (size_t i = 0; i < closure_args.size(); i++) {
            if (closure_args[i].is_buffer && allocations.contains(closure_args[i].name)) {
                closure_args[i].size = allocations.get(closure_args[i].name).constant_bytes;
            }
        }

        CodeGen_GPU_Dev *gpu_codegen = cgdev[device_api];
        int slots_taken = 0;
        if (target.has_feature(Target::Renderscript)) {
            slots_taken = gpu_codegen->slots_taken();
            debug(4) << "Slots taken = " << slots_taken << "\n";
        }

        user_assert(gpu_codegen != nullptr)
            << "Loop is scheduled on device " << device_api
            << " which does not appear in target " << target.to_string() << "\n";
        gpu_codegen->add_kernel(loop, kernel_name, closure_args);

        // get the actual name of the generated kernel for this loop
        kernel_name = gpu_codegen->get_current_kernel_name();
        debug(2) << "Compiled launch to kernel \"" << kernel_name << "\"\n";
        Value *entry_name_str = builder->CreateGlobalStringPtr(kernel_name, "entry_name");

        llvm::Type *target_size_t_type = (target.bits == 32) ? i32_t : i64_t;

        // build the kernel arguments array
        llvm::PointerType *arg_t = i8_t->getPointerTo(); // void*
        int num_args = (int)closure_args.size();

        // nullptr-terminated list
        llvm::Type *gpu_args_arr_type = ArrayType::get(arg_t, num_args+1);
        Value *gpu_args_arr =
            create_alloca_at_entry(
                gpu_args_arr_type,
                num_args+1, false,
                kernel_name + "_args");

        // nullptr-terminated list of size_t's
        llvm::Type *gpu_arg_sizes_arr_type = ArrayType::get(target_size_t_type,
                                                            num_args+1);
        Value *gpu_arg_sizes_arr =
            create_alloca_at_entry(
                gpu_arg_sizes_arr_type,
                num_args+1, false,
                kernel_name + "_arg_sizes");

        llvm::Type *gpu_arg_is_buffer_arr_type = ArrayType::get(i8_t, num_args+1);
        Value *gpu_arg_is_buffer_arr =
            create_alloca_at_entry(
                gpu_arg_is_buffer_arr_type,
                num_args+1, false,
                kernel_name + "_arg_is_buffer");

        for (int i = 0; i < num_args; i++) {
            // get the closure argument
            string name = closure_args[i].name;
            Value *val;

            if (closure_args[i].is_buffer) {
                // If it's a buffer, dereference the dev handle
                val = buffer_dev(sym_get(name + ".buffer"));
            } else if (ends_with(name, ".varying")) {
                // Expressions for varying attributes are passed in the
                // expression mesh. Pass a non-nullptr value in the argument array
                // to keep it in sync with the argument names encoded in the
                // shader header
                val = ConstantInt::get(target_size_t_type, 1);
            } else if (name.compare(".rs_slot_offset") == 0) {
                user_assert(target.has_feature(Target::Renderscript)) <<
                    ".rs_slot_offset variable is used by Renderscript only.";
                // First argument for Renderscript _run method is slot offset.
                val = ConstantInt::get(target_size_t_type, slots_taken);
            } else {
                // Otherwise just look up the symbol
                val = sym_get(name);
            }

            // allocate stack space to mirror the closure element. It
            // might be in a register and we need a pointer to it for
            // the gpu args array.
            Value *ptr = create_alloca_at_entry(val->getType(), 1, false, name+".stack");
            // store the closure value into the stack space
            builder->CreateStore(val, ptr);

            // store a void * pointer to the argument into the gpu_args_arr
            Value *bits = builder->CreateBitCast(ptr, arg_t);
            builder->CreateStore(bits,
                                 builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                                    gpu_args_arr_type,
#endif
                                    gpu_args_arr,
                                    0,
                                    i));

            // store the size of the argument. Buffer arguments get
            // the dev field, which is 64-bits.
            int size_bits = (closure_args[i].is_buffer) ? 64 : closure_args[i].type.bits();
            builder->CreateStore(ConstantInt::get(target_size_t_type, size_bits/8),
                                 builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                                    gpu_arg_sizes_arr_type,
#endif
                                    gpu_arg_sizes_arr,
                                    0,
                                    i));

            builder->CreateStore(ConstantInt::get(i8_t, closure_args[i].is_buffer),
                                 builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                                    gpu_arg_is_buffer_arr_type,
#endif
                                    gpu_arg_is_buffer_arr,
                                    0,
                                    i));
        }
        // nullptr-terminate the lists
        builder->CreateStore(ConstantPointerNull::get(arg_t),
                             builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                                gpu_args_arr_type,
#endif
                                gpu_args_arr,
                                0,
                                num_args));
        builder->CreateStore(ConstantInt::get(target_size_t_type, 0),
                             builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                                gpu_arg_sizes_arr_type,
#endif
                                gpu_arg_sizes_arr,
                                0,
                                num_args));
        builder->CreateStore(ConstantInt::get(i8_t, 0),
                             builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                                gpu_arg_is_buffer_arr_type,
#endif
                                gpu_arg_is_buffer_arr,
                                0,
                                num_args));

        std::string api_unique_name = gpu_codegen->api_unique_name();

        // TODO: only three dimensions can be passed to
        // cuLaunchKernel. How should we handle blkid[3]?
        internal_assert(is_one(bounds.num_threads[3]) && is_one(bounds.num_blocks[3]));
        debug(4) << "CodeGen_GPU_Host get_user_context returned " << get_user_context() << "\n";
        debug(3) << "bounds.num_blocks[0] = " << bounds.num_blocks[0] << "\n";
        debug(3) << "bounds.num_blocks[1] = " << bounds.num_blocks[1] << "\n";
        debug(3) << "bounds.num_blocks[2] = " << bounds.num_blocks[2] << "\n";
        debug(3) << "bounds.num_threads[0] = " << bounds.num_threads[0] << "\n";
        debug(3) << "bounds.num_threads[1] = " << bounds.num_threads[1] << "\n";
        debug(3) << "bounds.num_threads[2] = " << bounds.num_threads[2] << "\n";
        Value *launch_args[] = {
            get_user_context(),
            builder->CreateLoad(get_module_state(api_unique_name)),
            entry_name_str,
            codegen(bounds.num_blocks[0]), codegen(bounds.num_blocks[1]), codegen(bounds.num_blocks[2]),
            codegen(bounds.num_threads[0]), codegen(bounds.num_threads[1]), codegen(bounds.num_threads[2]),
            codegen(bounds.shared_mem_size),
            builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                gpu_arg_sizes_arr_type,
#endif
                gpu_arg_sizes_arr,
                0,
                0,
                "gpu_arg_sizes_ar_ref" + api_unique_name),
            builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                gpu_args_arr_type,
#endif
                gpu_args_arr,
                0,
                0,
                "gpu_args_arr_ref" + api_unique_name),
            builder->CreateConstGEP2_32(
#if LLVM_VERSION >= 37
                gpu_arg_is_buffer_arr_type,
#endif
                gpu_arg_is_buffer_arr,
                0,
                0,
                "gpu_arg_is_buffer_ref" + api_unique_name),
            gpu_num_padded_attributes,
            gpu_vertex_buffer,
            gpu_num_coords_dim0,
            gpu_num_coords_dim1,
        };
        std::string run_fn_name = "halide_" + api_unique_name + "_run";
        llvm::Function *dev_run_fn = module->getFunction(run_fn_name);
        internal_assert(dev_run_fn) << "Could not find " << run_fn_name << " in module\n";
        Value *result = builder->CreateCall(dev_run_fn, launch_args);
        Value *did_succeed = builder->CreateICmpEQ(result, ConstantInt::get(i32_t, 0));

        CodeGen_CPU::create_assertion(did_succeed,
                                      // Should have already called halide_error inside the gpu runtime
                                      halide_error_code_device_run_failed,
                                      result);

        return call_extern_and_assert("run_fn_name", params);
}


    void visit(const For *loop) {
        if ((loop->device_api == DeviceAPI::None) || (loop->device_api == DeviceAPI::Host)) {
            IRMutator::visit(loop);
            return;
        }

        // After moving this to device kernel's module, the loop's device api
        // doesn't need to be marked anymore.
        Stmt body = For::make(loop->name, loop->min, loop->extent, loop->for_type,
                              DeviceAPI::None, loop->body);

        body = remove_trivial_for_loops(body);

        if (loop->device_api == DeviceAPI::Hexagon) {
            stmt = launch_hexagon_kernel(loop->name, loop->device_api, body);
        } else {

            stmt = launch_gpu_kernel(loop->name, loop->device_api, body);
        }
    }

    void visit(const Let *op) {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        IRMutator::visit(op);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
    }

    void visit(const LetStmt *op) {
        if (op->value.type() == Int(32)) {
            alignment_info.push(op->name, modulus_remainder(op->value, alignment_info));
        }

        IRMutator::visit(op);

        if (op->value.type() == Int(32)) {
            alignment_info.pop(op->name);
        }
    }

public:
    InjectDeviceRPC(const string& name, const Target &target) :
        function_name(name), device_code("hexagon", target) {}

    Stmt initialize_gpu_kernel(const Module &device_code) {
        // Skip if there are no device kernels.
        if (device_code.functions().empty()) {
            return Stmt();
        }

        Target t = device_code.target();
        internal_assert(t.has_gpu_feature());
        DeviceAPI device_api = device_api_for_target_feature(t);
        const string api_unique_name = device_api_to_string(device_api);

        debug(1) << api_unique_name << " device code module: " << device_code << "\n";

        // First compile the module to an llvm module
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module =
            compile_module_to_llvm_module(device_code, context);

        // Dump the llvm module to a temp file as .ll
        TemporaryFile tmp_bitcode(api_unique_name, ".ll");
        TemporaryFile tmp_shared_object(api_unique_name, ".o");
        std::unique_ptr<llvm::raw_fd_ostream> ostream =
            make_raw_fd_ostream(tmp_bitcode.pathname());
        compile_llvm_module_to_llvm_assembly(*llvm_module, *ostream);
        ostream->flush();

        // Shell out to hexagon clang to compile it.
        string command;

        const char *path = getenv("CLANG");
        if (path && path[0]) {
            command = path;
        } else {
            user_error << "Unable to find clang: CLANG is not set properly.";
        }

        command += " -c ";
        command += tmp_bitcode.pathname();
        if (0) { // This path should also work, if we want to use PIC code
            command += " -fpic -O3 -Wno-override-module ";
        } else {
            command += " -fno-pic -G 0 -mlong-calls -O3 -Wno-override-module ";
        }
        command += " -o " + tmp_shared_object.pathname();
        int result = system(command.c_str());
        internal_assert(result == 0) << "gpu clang failed\n";

        // Read the compiled object back in and put it in a buffer in the module
        std::ifstream so(tmp_shared_object.pathname(), std::ios::binary | std::ios::ate);
        internal_assert(so.good()) << "failed to open temporary shared object.";
        vector<uint8_t> object(so.tellg());
        so.seekg(0, std::ios::beg);
        so.read(reinterpret_cast<char*>(&object[0]), object.size());

        size_t code_size = object.size();
        string code_name = api_unique_name + "_code";
        Expr code_ptr = buffer_ptr(&object[0], code_size, code_name.c_str());

        // Wrap the statement in calls to halide_initialize_kernels.
        string init_kernels_name = "halide_" + api_unique_name + "_initialize_kernels";
        Stmt init_kernels = call_extern_and_assert(init_kernels_name,
                                                   {module_state_ptr(api_unique_name),
                                                    code_ptr, Expr((uint64_t) code_size)});
        return init_kernels;
    }

    Stmt initialize_hexagon_kernel(const Module &device_code) {
        // Skip if there are no device kernels.
        if (device_code.functions().empty()) {
            return Stmt();
        }

        // Compile the device code.
        // TODO: Currently, this requires shelling out to
        // hexagon-clang from the Qualcomm Hexagon SDK, because the
        // Hexagon LLVM target is not fully open source yet. When the
        // LLVM Hexagon target is fully open sourced, we can instead
        // just compile the module to an object, and find a way to
        // link it to a shared object.
        debug(1) << "Hexagon device code module: " << device_code << "\n";

        // First compile the module to an llvm module
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module =
            compile_module_to_llvm_module(device_code, context);

        #if LLVM_VERSION >= 39
        // Then mess with it, to fix up version differences between
        // our LLVM and hexagon-clang. Yuck.
        for (auto &gv : llvm_module->globals()) {
            // hexagon-clang doesn't understand the local_unnamed_addr
            // attribute, so we must strip it.
            gv.setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);
        }
        for (auto &fn : llvm_module->functions()) {
            fn.setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);
        }
        #endif

        // Dump the llvm module to a temp file as .ll
        TemporaryFile tmp_bitcode("hex", ".ll");
        TemporaryFile tmp_shared_object("hex", ".o");
        std::unique_ptr<llvm::raw_fd_ostream> ostream =
            make_raw_fd_ostream(tmp_bitcode.pathname());
        compile_llvm_module_to_llvm_assembly(*llvm_module, *ostream);
        ostream->flush();

        // Shell out to hexagon clang to compile it.
        string hex_command;

        const char *path = getenv("HL_HEXAGON_CLANG");
        if (path && path[0]) {
            hex_command = path;
        } else {
            path = getenv("HL_HEXAGON_TOOLS");
            if (path && path[0]) {
                hex_command = string(path) + "/bin/hexagon-clang";
            } else {
                user_error << "Unable to find hexagon-clang: neither HL_HEXAGON_CLANG nor HL_HEXAGON_TOOLS are set properly.";
            }
        }

        hex_command += " -c ";
        hex_command += tmp_bitcode.pathname();
        if (0) { // This path should also work, if we want to use PIC code
            hex_command += " -fpic -O3 -Wno-override-module ";
        } else {
            hex_command += " -fno-pic -G 0 -mlong-calls -O3 -Wno-override-module ";
        }
        if (device_code.target().has_feature(Target::HVX_v62)) {
            hex_command += " -mv62";
        }
        if (device_code.target().has_feature(Target::HVX_128)) {
            hex_command += " -mhvx-double";
        } else {
            hex_command += " -mhvx";
        }
        hex_command += " -o " + tmp_shared_object.pathname();
        int result = system(hex_command.c_str());
        internal_assert(result == 0) << "hexagon-clang failed\n";

        // Read the compiled object back in and put it in a buffer in the module
        std::ifstream so(tmp_shared_object.pathname(), std::ios::binary | std::ios::ate);
        internal_assert(so.good()) << "failed to open temporary shared object.";
        vector<uint8_t> object(so.tellg());
        so.seekg(0, std::ios::beg);
        so.read(reinterpret_cast<char*>(&object[0]), object.size());

        size_t code_size = object.size();
        Expr code_ptr = buffer_ptr(&object[0], code_size, "hexagon_code");

        // Wrap the statement in calls to halide_initialize_kernels.
        Stmt init_kernels = call_extern_and_assert("halide_hexagon_initialize_kernels",
                                                   {module_state_ptr(device_api_to_string(DeviceAPI::Hexagon)),
                                                    code_ptr, Expr((uint64_t) code_size)});
        return init_kernels;
    }


    Stmt inject(Stmt s) {
        s = mutate(s);

        Stmt hexagon = initialize_hexagon_kernel(device_code);
        if (hexagon.defined()) {
            s = Block::make(hexagon, s);
        }
        return s;
    }
};

}

Stmt extract_device_kernels(Stmt s, const string &function_name, const Target &host_target) {
    // Make a new target for the device module.
    Target target(Target::NoOS, Target::Hexagon, 32);

    // These feature flags are propagated from the host target to the
    // device module.
    //
    // TODO: We'd like Target::Debug to be in this list too, but trunk
    // llvm currently disagrees with hexagon clang as to what
    // constitutes valid debug info.
    static const Target::Feature shared_features[] = {
        Target::NoAsserts,
        Target::HVX_64,
        Target::HVX_128,
        Target::HVX_v62
    };
    for (Target::Feature i : shared_features) {
        if (host_target.has_feature(i)) {
            target = target.with_feature(i);
        }
    }

    InjectDeviceRPC injector(function_name, target);
    s = injector.inject(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
