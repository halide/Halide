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
            auto storage = Buffer<void *>::make_scalar();
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
        Buffer<uint8_t> code((int)size);
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
                // The Hexagon runtime expects buffer args to be
                // passed as just the device and host field.
                Expr device = Variable::make(UInt(64), i.first + ".device");
                Expr host = Variable::make(Handle(), i.first + ".host");
                Expr pseudo_buffer = Call::make(Handle(), Call::make_struct, {device, host}, Call::Intrinsic);
                arg_ptrs.push_back(pseudo_buffer);
                arg_sizes.push_back(Expr((uint64_t)(pseudo_buffer.type().bytes())));

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

        // Compile the device code
        debug(1) << "Hexagon device code module: " << device_code << "\n";

        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(device_code, context));

        llvm::SmallVector<char, 4096> object;
        llvm::raw_svector_ostream object_stream(object);
        compile_llvm_module_to_object(*llvm_module, object_stream);

        if (debug::debug_level >= 2) {
            debug(2) << "Hexagon device code assembly: " << "\n";
            llvm::SmallString<4096> assembly;
            llvm::raw_svector_ostream assembly_stream(assembly);
            compile_llvm_module_to_assembly(*llvm_module, assembly_stream);
            debug(2) << assembly.c_str() << "\n";
        }

        // Wrap the statement in calls to halide_initialize_kernels.
        size_t code_size = object.size();
        Expr code_ptr = buffer_ptr(reinterpret_cast<uint8_t*>(&object[0]), code_size, "hexagon_code");
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
