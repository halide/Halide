#include "HexagonOffload.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Closure.h"
#include "Param.h"
#include "Image.h"
#include "Output.h"
#include "LLVM_Headers.h"
#include "RemoveTrivialForLoops.h"

#include <iostream>
#include <fstream>

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

Target hexagon_remote_target(Target::NoOS, Target::Hexagon, 32);

class InjectHexagonRpc : public IRMutator {
    using IRMutator::visit;

    std::map<std::string, Expr> state_vars;

    Module device_code;

    Expr state_var(const std::string& name, Type type) {
        Expr& var = state_vars[name];
        if (!var.defined()) {
            Buffer storage(type, {}, nullptr, name + "_buf");
            *(void **)storage.host_ptr() = nullptr;
            var = Load::make(type_of<void*>(), name + "_buf", 0, storage, Parameter());
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
        Buffer code(type_of<uint8_t>(), {(int)size}, nullptr, name);
        memcpy(code.host_ptr(), buffer, (int)size);

        Expr ptr_0 = Load::make(type_of<uint8_t>(), name, 0, code, Parameter());
        return Call::make(Handle(), Call::address_of, {ptr_0}, Call::Intrinsic);
    }

    Stmt call_extern_and_assert(const std::string& name, const std::vector<Expr>& args) {
        Expr call = Call::make(Int(32), name, args, Call::Extern);
        string call_result_name = unique_name(name + "_result");
        Expr call_result_var = Variable::make(Int(32), call_result_name);
        return LetStmt::make(call_result_name, call,
                             AssertStmt::make(EQ::make(call_result_var, 0), call_result_var));
    }

public:
    InjectHexagonRpc(const Target &target) : device_code("hexagon", target) {}

    void visit(const For *loop) {
        if (loop->device_api == DeviceAPI::Hexagon) {
            // Unrolling or loop partitioning might generate multiple
            // loops with the same name, so we need to unique them.
            std::string hex_name = "hex_" + loop->name;

            Stmt body = remove_trivial_for_loops(loop, true /*device_loops*/);

            // Build a closure for the device code.
            // TODO: Should this move the body of the loop to Hexagon,
            // or the loop itself? Currently, this moves the loop itself.
            Closure c(body);

            // Make an argument list, and generate a function in the
            // device_code module. The hexagon runtime code expects
            // the arguments to appear in the order of (input buffers,
            // output buffers, input scalars).  There's a huge hack
            // here, in that the scalars must be last for the scalar
            // arguments to shadow the symbols of the buffer.
            std::vector<Argument> args;
            for (const auto& i : c.buffers) {
                if (i.second.write) {
                    continue;
                }
                Argument::Kind kind = Argument::InputBuffer;
                args.push_back(Argument(i.first, kind, i.second.type, i.second.dimensions));
            }
            for (const auto& i : c.buffers) {
                if (i.second.write) {
                    Argument::Kind kind = Argument::OutputBuffer;
                    args.push_back(Argument(i.first, kind, i.second.type, i.second.dimensions));
                }
            }
            for (const auto& i : c.vars) {
                args.push_back(Argument(i.first, Argument::InputScalar, i.second, 0));
            }
            device_code.append(LoweredFunc(hex_name, args, body, LoweredFunc::External));

            // Generate a call to hexagon_device_run.
            std::vector<Expr> arg_sizes;
            std::vector<Expr> arg_ptrs;
            std::vector<Expr> arg_flags;

            for (const auto& i : c.buffers) {
                arg_sizes.push_back(Expr((size_t)sizeof(buffer_t*)));
                arg_ptrs.push_back(Variable::make(type_of<buffer_t *>(), i.first + ".buffer"));
                int flags = 0;
                if (i.second.read) flags |= 0x1;
                if (i.second.write) flags |= 0x2;
                arg_flags.push_back(flags);
            }
            for (const auto& i : c.vars) {
                Expr arg = Variable::make(i.second, i.first);
                Expr arg_ptr = Call::make(type_of<void *>(), Call::make_struct, {arg}, Call::Intrinsic);

                arg_sizes.push_back(Expr((size_t)i.second.bytes()));
                arg_ptrs.push_back(arg_ptr);
                arg_flags.push_back(0x0);
            }

            // The argument list is terminated with an argument of size 0.
            arg_sizes.push_back(Expr((size_t)0));

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

    Stmt inject(Stmt s) {
        s = mutate(s);

        // Skip if there are no device kernels.
        if (device_code.functions.empty()) {
            return s;
        }

        // Compile the device code.
        std::vector<uint8_t> object;
#if 0
        compile_module_to_shared_object(device_code, object);
        //compile_module_to_shared_object(device_code, "/tmp/hex.so");
#else
        debug(1) << "Hexagon device code module: " << device_code << "\n";
        compile_module_to_llvm_bitcode(device_code, "hex.bc");
        int result = system("${HEX_TOOLS}/bin/hexagon-clang hex.bc -fPIC -O3 -shared -o hex.so");
        internal_assert(result == 0) << "hexagon-clang failed\n";

        std::ifstream so("hex.so", std::ios::binary | std::ios::ate);
        object.resize(so.tellg());
        so.seekg(0, std::ios::beg);
        so.read(reinterpret_cast<char*>(&object[0]), object.size());
#endif

        // Put the compiled object into a buffer.
        size_t code_size = object.size();
        Expr code_ptr = buffer_ptr(&object[0], code_size, "hexagon_code");

        // Wrap the statement in calls to halide_initialize_kernels.
        Stmt init_kernels = call_extern_and_assert("halide_hexagon_initialize_kernels",
                                                   {module_state_ptr(), code_ptr, Expr(code_size)});
        s = Block::make(init_kernels, s);

        return s;
    }
};

}

Stmt inject_hexagon_rpc(Stmt s, const Target &host_target) {
    Target target = hexagon_remote_target;
    for (Target::Feature i : {Target::Debug, Target::HVX_64, Target::HVX_128}) {
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
