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
    } else if (t.has_feature(Target::GLSL)) {
        return DeviceAPI::OpenGL;
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

    Expr module_state(string &api_unique_name) {
        return state_var("module_state_" + function_name + "_" + api_unique_name,
                         type_of<void*>());
    }

    Expr module_state_ptr(string &api_unique_name) {
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

        return Stmt();
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
    InjectDeviceRPC(const string& name, const Target &target) : , device_code("hexagon", target) {}

    Stmt initialize_gpu_kernel(const Module &device_code) {
        // Skip if there are no device kernels.
        if (device_code.functions().empty()) {
            return s;
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
        Expr code_ptr = buffer_ptr(&object[0], code_size, api_unique_name + "_code");

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
            return s;
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

        s = Block::make(initialize_hexagon_kernel(device_code), s);

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
