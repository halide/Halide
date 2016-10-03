#include <iostream>
#include <fstream>
#include <memory>

#include "HexagonOffload.h"
#include "Closure.h"
#include "InjectHostDevBufferCopies.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "LLVM_Output.h"
#include "LLVM_Headers.h"
#include "Param.h"
#include "RemoveTrivialForLoops.h"
#include "Substitute.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

// Replace the parameter objects of loads/stores with a new parameter
// object.
class ReplaceParams : public IRMutator {
    const std::map<std::string, Parameter> &replacements;

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
    ReplaceParams(const std::map<std::string, Parameter> &replacements)
        : replacements(replacements) {}
};

Stmt replace_params(Stmt s, const std::map<std::string, Parameter> &replacements) {
    return ReplaceParams(replacements).mutate(s);
}

// Wrap the stmt in a call to power_hvx_on, calling power_hvx_off
// as a destructor if successful.
Stmt power_hvx_on(Stmt stmt) {
    Expr power_on = Call::make(Int(32), "halide_hexagon_power_hvx_on", {}, Call::Extern);
    string power_on_result_name = unique_name("power_on_result");
    Expr power_on_result_var = Variable::make(Int(32), power_on_result_name);
    Stmt check_power_on = LetStmt::make(power_on_result_name, power_on,
                                        AssertStmt::make(EQ::make(power_on_result_var, 0), power_on_result_var));

    Expr dummy_obj = reinterpret(Handle(), cast<uint64_t>(1));
    Expr power_off = Call::make(Int(32), Call::register_destructor,
                                {Expr("halide_hexagon_power_hvx_off_as_destructor"), dummy_obj}, Call::Intrinsic);

    stmt = Block::make(Evaluate::make(power_off), stmt);
    stmt = Block::make(check_power_on, stmt);
    return stmt;
}

class InjectHexagonRpc : public IRMutator {
    std::map<std::string, Expr> state_vars;

    Module device_code;

    /** Alignment info for Int(32) variables in scope, so we don't
     * lose the information when creating Hexagon kernels. */
    Scope<ModulusRemainder> alignment_info;

    Expr state_var(const std::string& name, Type type) {
        Expr& var = state_vars[name];
        if (!var.defined()) {
            Image<void *> storage = Image<void *>::make_scalar();
            storage() = nullptr;
            BufferPtr buf(storage, name + "_buf");
            var = Load::make(type_of<void*>(), name + "_buf", 0, buf, Parameter());
        }
        return var;
    }

    Expr state_var_ptr(const std::string& name, Type type) {
        Expr var = state_var(name, type);
        return Call::make(Handle(), Call::address_of, {var}, Call::Intrinsic);
    }

    Expr module_state() {
        return state_var("hexagon_module_state", type_of<void*>());
    }

    Expr module_state_ptr() {
        return state_var_ptr("hexagon_module_state", type_of<void*>());
    }

    // Create a Buffer containing the given buffer/size, and return an
    // expression for a pointer to the first element.
    Expr buffer_ptr(const uint8_t* buffer, size_t size, const char* name) {
        Image<uint8_t> code((int)size);
        memcpy(code.data(), buffer, (int)size);
        BufferPtr buf(code, name);
        Expr ptr_0 = Load::make(type_of<uint8_t>(), name, 0, buf, Parameter());
        return Call::make(Handle(), Call::address_of, {ptr_0}, Call::Intrinsic);
    }

    using IRMutator::visit;

    void visit(const For *loop) {
        if (loop->device_api == DeviceAPI::Hexagon) {
            // Unrolling or loop partitioning might generate multiple
            // loops with the same name, so we need to make them unique.
            std::string hex_name = unique_name("hex_" + loop->name);

            // After moving this to Hexagon, it doesn't need to be
            // marked Hexagon anymore.
            Stmt body = For::make(loop->name, loop->min, loop->extent, loop->for_type,
                                  DeviceAPI::None, loop->body);
            body = remove_trivial_for_loops(body);

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
            std::vector<LoweredArgument> input_buffers, output_buffers;
            std::map<std::string, Parameter> replacement_params;
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

            std::vector<LoweredArgument> args;
            args.insert(args.end(), input_buffers.begin(), input_buffers.end());
            args.insert(args.end(), output_buffers.begin(), output_buffers.end());
            for (const auto& i : c.vars) {
                LoweredArgument arg(i.first, Argument::InputScalar, i.second, 0);
                if (alignment_info.contains(i.first)) {
                    arg.alignment = alignment_info.get(i.first);
                }
                args.push_back(arg);
            }
            device_code.append(LoweredFunc(hex_name, args, body, LoweredFunc::External));

            // Generate a call to hexagon_device_run.
            std::vector<Expr> arg_sizes;
            std::vector<Expr> arg_ptrs;
            std::vector<Expr> arg_flags;

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

            std::string pipeline_name = hex_name + "_argv";
            std::vector<Expr> params;
            params.push_back(module_state());
            params.push_back(pipeline_name);
            params.push_back(state_var_ptr(hex_name, type_of<int>()));
            params.push_back(Call::make(type_of<size_t*>(), Call::make_struct, arg_sizes, Call::Intrinsic));
            params.push_back(Call::make(type_of<void**>(), Call::make_struct, arg_ptrs, Call::Intrinsic));
            params.push_back(Call::make(type_of<int*>(), Call::make_struct, arg_flags, Call::Intrinsic));

            stmt = call_extern_and_assert("halide_hexagon_run", params);

        } else {
            IRMutator::visit(loop);
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
    InjectHexagonRpc(const Target &target) : device_code("hexagon", target) {}

    Stmt inject(Stmt s) {
        s = mutate(s);

        // Skip if there are no device kernels.
        if (device_code.functions().empty()) {
            return s;
        }

        // If we got here, it means the pipeline runs at least one
        // Hexagon kernel. To reduce overhead of individual
        // sub-pipelines running on Hexagon, we can power on HVX once
        // for the duration of this pipeline.
        s = power_hvx_on(s);

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
            for (size_t i = 0; i < fn.arg_size(); i++) {
                fn.removeAttribute(i, llvm::Attribute::WriteOnly);
            }
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
        std::vector<uint8_t> object(so.tellg());
        so.seekg(0, std::ios::beg);
        so.read(reinterpret_cast<char*>(&object[0]), object.size());

        size_t code_size = object.size();
        Expr code_ptr = buffer_ptr(&object[0], code_size, "hexagon_code");

        // Wrap the statement in calls to halide_initialize_kernels.
        Stmt init_kernels = call_extern_and_assert("halide_hexagon_initialize_kernels",
                                                   {module_state_ptr(), code_ptr, Expr((uint64_t) code_size)});
        s = Block::make(init_kernels, s);

        return s;
    }
};

}

Stmt inject_hexagon_rpc(Stmt s, const Target &host_target) {
    // Make a new target for the device module.
    Target target(Target::NoOS, Target::Hexagon, 32);

    // These feature flags are propagated from the host target to the
    // device module.
    //
    // TODO: We'd like Target::Debug to be in this list too, but trunk
    // llvm currently disagrees with hexagon clang as to what
    // constitutes valid debug info.
    static const Target::Feature shared_features[] = {
        Target::Profile,
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

    InjectHexagonRpc injector(target);
    s = injector.inject(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
