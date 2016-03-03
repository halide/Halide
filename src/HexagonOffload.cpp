#include "HexagonOffload.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "Closure.h"
#include "Param.h"
#include "Image.h"
#include "Output.h"
#include "LLVM_Headers.h"
#include <llvm/Object/ELFObjectFile.h>

#include <iostream>
#include <fstream>

#include "runtime/hexagon_remote/elf.h"

namespace Halide {
namespace Internal {

using std::string;
using std::vector;

namespace {

Target hexagon_remote_target(Target::NoOS, Target::Hexagon, 32);

}

/////////////////////////////////////////////////////////////////////////////
class InjectHexagonRpc : public IRMutator {
    using IRMutator::visit;

    std::map<std::string, Expr> state_vars;

    Module device_code;

    Expr state_var(const std::string& name, Type type) {
        Expr& var = state_vars[name];
        if (!var.defined()) {
            Buffer storage(type, {}, nullptr, name + "_buf");
            *(void **)storage.host_ptr() = nullptr;
            var = Load::make(type_of<void*>(), name, 0, storage, Parameter());
        }
        return var;
    }

    Expr state_var_ptr(const std::string& name, Type type) {
        Expr var = state_var(name, type);
        return Call::make(Handle(), Call::address_of, {var}, Call::Intrinsic);
    }

    Expr module_state_ptr() {
        return state_var_ptr("module_state", type_of<void*>());
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
    InjectHexagonRpc() : device_code("hexagon", hexagon_remote_target) {}

    void visit(const For *loop) {
        if (loop->device_api == DeviceAPI::Hexagon) {
            std::string hex_name = "hex_" + loop->name;

            // Build a closure for the device code.
            // TODO: Should this move the body of the loop to Hexagon,
            // or the loop itself? Currently, this moves the loop itself.
            Closure c(loop);

            // Make an argument list, and generate a function in the
            // device_code module.
            std::vector<Argument> args;
            for (const auto& i : c.buffers) {
                // Output buffers are last (below).
                if (i.second.write) {
                    continue;
                }
                Argument::Kind kind = Argument::InputBuffer;
                args.push_back(Argument(i.first, kind, i.second.type, i.second.dimensions));
            }
            for (const auto& i : c.vars) {
                args.push_back(Argument(i.first, Argument::InputScalar, i.second, 0));
            }
            // Output buffers come last.
            for (const auto& i : c.buffers) {
                if (i.second.write) {
                    Argument::Kind kind = Argument::OutputBuffer;
                    args.push_back(Argument(i.first, kind, i.second.type, i.second.dimensions));
                }
            }
            device_code.append(LoweredFunc(hex_name, args, loop, LoweredFunc::External));

            // Generate a call to hexagon_device_run.
            std::vector<Expr> input_arg_sizes;
            std::vector<Expr> input_arg_ptrs;
            std::vector<Expr> input_arg_flags;
            std::vector<Expr> output_arg_sizes;
            std::vector<Expr> output_arg_ptrs;
            std::vector<Expr> output_arg_flags;

            for (const auto& i : c.buffers) {
                if (i.second.write) continue;

                input_arg_sizes.push_back(Expr((size_t)sizeof(buffer_t*)));
                input_arg_ptrs.push_back(Variable::make(type_of<buffer_t *>(), i.first + ".buffer"));
                input_arg_flags.push_back(1);
            }
            for (const auto& i : c.vars) {
                Expr arg = Variable::make(i.second, i.first);
                Expr arg_ptr = Call::make(type_of<void *>(), Call::make_struct, {arg}, Call::Intrinsic);

                input_arg_sizes.push_back(Expr((size_t)i.second.bytes()));
                input_arg_ptrs.push_back(arg_ptr);
                input_arg_flags.push_back(0);
            }
            for (const auto& i : c.buffers) {
                if (!i.second.write) continue;

                output_arg_sizes.push_back(Expr((size_t)sizeof(buffer_t*)));
                output_arg_ptrs.push_back(Variable::make(type_of<buffer_t *>(), i.first + ".buffer"));
                output_arg_flags.push_back(i.second.read ? 3 : 2);
            }

            // The argument list is terminated with an argument of size 0.
            input_arg_sizes.push_back(Expr((size_t)0));
            output_arg_sizes.push_back(Expr((size_t)0));

            std::string pipeline_name = hex_name + "_argv";
            std::vector<Expr> params;
            params.push_back(module_state_ptr());
            params.push_back(pipeline_name);
            params.push_back(state_var_ptr(hex_name, type_of<int>()));
            params.push_back(Call::make(type_of<size_t*>(), Call::make_struct, input_arg_sizes, Call::Intrinsic));
            params.push_back(Call::make(type_of<void**>(), Call::make_struct, input_arg_ptrs, Call::Intrinsic));
            params.push_back(Call::make(type_of<int*>(), Call::make_struct, input_arg_flags, Call::Intrinsic));
            params.push_back(Call::make(type_of<size_t*>(), Call::make_struct, output_arg_sizes, Call::Intrinsic));
            params.push_back(Call::make(type_of<void**>(), Call::make_struct, output_arg_ptrs, Call::Intrinsic));
            params.push_back(Call::make(type_of<int*>(), Call::make_struct, output_arg_flags, Call::Intrinsic));

            stmt = call_extern_and_assert("halide_hexagon_run", params);

        } else {
            IRMutator::visit(loop);
        }
    }

    template <typename ObjFileType>
    static int get_symbol_offset(const ObjFileType& obj_file, const std::string& symbol) {
        for (const llvm::object::SymbolRef& i : obj_file.symbols()) {
            llvm::ErrorOr<llvm::StringRef> name_ref = i.getName();
            if (!name_ref || name_ref.get() != symbol) continue;

            int section_offset = 0;
            llvm::ErrorOr<llvm::object::section_iterator> sec_i = i.getSection();
            if (sec_i) {
                const llvm::object::SectionRef& sec = *sec_i.get();
                section_offset = static_cast<int>(obj_file.getSection(sec.getRawDataRefImpl())->sh_offset);
            }

            return section_offset + static_cast<int>(i.getValue());
        }
        return -1;
    }

    Stmt inject(Stmt s) {
        s = mutate(s);

        // Skip if there are no device kernels.
        if (device_code.functions.empty()) {
            return s;
        }

        // Compile the device code.
        std::vector<uint8_t> object;
        compile_module_to_object(device_code, object);

        compile_module_to_object(device_code, "/tmp/hex.o");

        Elf::Object<uint32_t> test;
        std::cout << test.init(&object[0]) << std::endl;
        std::cout << test.do_relocations() << std::endl;

        std::ofstream temp("/tmp/hex2.o");
        temp.write((char*)&object[0], object.size());
        temp.close();

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

Stmt inject_hexagon_rpc(Stmt s) {
    InjectHexagonRpc injector;
    s = injector.inject(s);
    return s;
}

}  // namespace Internal
}  // namespace Halide
