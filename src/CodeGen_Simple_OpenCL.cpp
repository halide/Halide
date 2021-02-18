/** \file
 *
 * Defines an IRPrinter that emits OpenCL & C++ code equivalent to a halide stmt
 */
#include "CodeGen_Posix.h"

#include "CodeGen_GPU_Dev.h"
#include "CodeGen_GPU_Host.h"
#include "CodeGen_OpenCL_Dev.h"
#include "CodeGen_Simple_OpenCL.h"
#include "Debug.h"
#include "DeviceArgument.h"
#include "Error.h"
#include "IROperator.h"

#include <functional>
#include <numeric>

namespace Halide {

namespace Internal {

using std::string;
using std::vector;

extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenCL_h[];

CodeGen_Simple_OpenCL::CodeGen_Simple_OpenCL(std::ostream &dest,
                                             const Target &target, OutputKind output_kind)
    : CodeGen_C(dest, target, output_kind) {
    user_assert(target.has_feature(Target::OpenCL))
        << "Compiling code for OpenCL which feature does not appear in target: "
        << target.to_string() << "\n";

    cgdev = new_CodeGen_OpenCL_Dev(target);

    stream << halide_internal_runtime_header_HalideRuntimeOpenCL_h << "\n";
};

void CodeGen_Simple_OpenCL::visit(const For *loop) {
    if (CodeGen_GPU_Dev::is_gpu_var(loop->name)) {
        // We're in the loop over outermost block dimension
        debug(2) << "Kernel launch: " << loop->name << "\n";

        internal_assert(loop->device_api != DeviceAPI::Default_GPU)
            << "A concrete device API should have been selected before codegen.";

        ExtractBounds bounds;
        loop->accept(&bounds);

        debug(2) << "Kernel bounds: ("
                 << bounds.num_threads[0] << ", "
                 << bounds.num_threads[1] << ", "
                 << bounds.num_threads[2] << ", "
                 << bounds.num_threads[3] << ") threads, ("
                 << bounds.num_blocks[0] << ", "
                 << bounds.num_blocks[1] << ", "
                 << bounds.num_blocks[2] << ", "
                 << bounds.num_blocks[3] << ") blocks\n";

        // compile the kernel
        string kernel_name = unique_name("kernel_" + loop->name);
        for (size_t i = 0; i < kernel_name.size(); i++) {
            if (!isalnum(kernel_name[i])) {
                kernel_name[i] = '_';
            }
        }

        // compute a closure over the state passed into the kernel
        HostClosure c(loop->body, loop->name);

        // Determine the arguments that must be passed into the halide function
        vector<DeviceArgument> closure_args = c.arguments();

        // Sort the args by the size of the underlying type. This is
        // helpful for avoiding struct-packing ambiguities in metal,
        // which passes the scalar args as a struct.
        std::sort(closure_args.begin(), closure_args.end(),
                  [](const DeviceArgument &a, const DeviceArgument &b) {
                      if (a.is_buffer == b.is_buffer) {
                          return a.type.bits() > b.type.bits();
                      } else {
                          // Ensure that buffer arguments come first:
                          // for many OpenGL/Compute systems, the
                          // legal indices for buffer args are much
                          // more restrictive than for scalar args,
                          // and scalar args can be 'grown' by
                          // LICM. Putting buffers first makes it much
                          // more likely we won't fail on some
                          // hardware.
                          return a.is_buffer > b.is_buffer;
                      }
                  });

        for (size_t i = 0; i < closure_args.size(); i++) {
            if (closure_args[i].is_buffer && allocations.contains(closure_args[i].name)) {
                //Think of something todo with the sizes we now don't have
                // closure_args[i].size = allocations.get(closure_args[i].name).constant_bytes;
            }
        }

        CodeGen_GPU_Dev *gpu_codegen = cgdev.get();
        user_assert(gpu_codegen != nullptr)
            << "The device API: " << loop->device_api
            << " is not yet initialize\n";
        gpu_codegen->add_kernel(loop, kernel_name, closure_args);

        // // get the actual name of the generated kernel for this loop
        kernel_name = gpu_codegen->get_current_kernel_name();
        debug(2) << "Compiled launch to kernel \"" << kernel_name << "\"\n";

        // // build the kernel arguments array
        int num_args = (int)closure_args.size();
        vector<string> gpu_arg_sizes_arr;
        vector<string> gpu_arg_is_buffer_arr;
        vector<string> gpu_args_arr;

        string api_unique_name = gpu_codegen->api_unique_name();

        for (int i = 0; i < num_args; i++) {
            // get the closure argument
            string name = closure_args[i].name;
            if (!closure_args[i].is_buffer) {
                gpu_args_arr.emplace_back("&" + print_name(name));

            } else {
                gpu_args_arr.emplace_back(print_name(name + "_buffer"));
            }

            gpu_arg_sizes_arr.emplace_back(std::to_string(closure_args[i].is_buffer ? 8 : closure_args[i].type.bytes()));
            gpu_arg_is_buffer_arr.emplace_back(closure_args[i].is_buffer ? "1" : "0");
        }
        // nullptr-terminate the lists
        gpu_arg_sizes_arr.emplace_back("0");
        gpu_args_arr.emplace_back("nullptr");
        gpu_arg_is_buffer_arr.emplace_back("0");

        // // TODO: only three dimensions can be passed to
        // // cuLaunchKernel. How should we handle blkid[3]?
        internal_assert(is_const_one(bounds.num_threads[3]) && is_const_one(bounds.num_blocks[3]))
            << bounds.num_threads[3] << ", " << bounds.num_blocks[3] << "\n";
        // debug(4) << "CodeGen_GPU_Host get_user_context returned " << get_user_context() << "\n";
        debug(3) << "bounds.num_blocks[0] = " << bounds.num_blocks[0] << "\n";
        debug(3) << "bounds.num_blocks[1] = " << bounds.num_blocks[1] << "\n";
        debug(3) << "bounds.num_blocks[2] = " << bounds.num_blocks[2] << "\n";
        debug(3) << "bounds.num_threads[0] = " << bounds.num_threads[0] << "\n";
        debug(3) << "bounds.num_threads[1] = " << bounds.num_threads[1] << "\n";
        debug(3) << "bounds.num_threads[2] = " << bounds.num_threads[2] << "\n";

        string run_fn_name = "halide_" + api_unique_name + "_run";

        auto make_list = [](vector<string> xs) {
            string result = "{";
            if (!xs.empty()) {
                auto folder = [](string a, const string &b) {
                    return std::move(a) + ", " + b;
                };
                result = std::move(result) + xs[0];
                result = std::accumulate(std::next(xs.begin()), xs.end(), result, folder);
            }
            result = std::move(result) + "}";
            return result;
        };
        Type target_size_t_type = target.bits == 32 ? UInt(32) : UInt(64);

        string arg_sizes = make_list(gpu_arg_sizes_arr);
        string arg_sizes_var = print_array_assignment(target_size_t_type, arg_sizes);

        string args = make_list(gpu_args_arr);
        string args_var = print_array_assignment(Handle(), args);

        string is_buffer = make_list(gpu_arg_is_buffer_arr);
        string is_buffer_var = print_array_assignment(Int(8), is_buffer);
        string module_state = current__api_module_state[api_unique_name];

        stream << get_indent() << "// Kernel call \n"
               << get_indent() << "int " << kernel_name << "_result = " << run_fn_name << "(";
        indent++;
        stream << "_ucon, "                                                                                                          //user_context
               << module_state + ", "                                                                                                //The string with the module
               << "\"" << kernel_name + "\", \n"                                                                                     //The kernel/entry name
               << get_indent() << bounds.num_blocks[0] << ", " << bounds.num_blocks[1] << ", " << bounds.num_blocks[2] << ", \n"     //number of blocks
               << get_indent() << bounds.num_threads[0] << ", " << bounds.num_threads[1] << ", " << bounds.num_threads[2] << ", \n"  //number of threads
               << get_indent() << bounds.shared_mem_size << ", \n"                                                                   //shared memory size
               << get_indent() << arg_sizes_var << ", " << args_var << ", " << is_buffer_var << ", \n"
               << get_indent() << "0, nullptr, 0, 0);\n";  //Vertex buffer and coords are not present or used currently in OpenCL implementation
        indent--;

        if (target.has_feature(Target::NoAsserts)) {
            stream << get_indent() << "halide_unused(" << kernel_name << "_result"
                   << ");\n";
            return;
        }

        stream << get_indent() << "if (" << kernel_name << "_result"
               << ")\n";
        open_scope();
        stream << get_indent() << "return halide_error_code_device_run_failed;\n";
        close_scope("");
    } else {
        CodeGen_C::visit(loop);
    }
}

void CodeGen_Simple_OpenCL::compile(const Module &input) {
    CodeGen_C::compile(input);
}

void CodeGen_Simple_OpenCL::compile(const LoweredFunc &f) {
    // Don't put non-external function declarations in headers.
    if (is_header_or_extern_decl() && f.linkage == LinkageType::Internal) {
        return;
    }

    const vector<LoweredArgument> &args = f.args;

    have_user_context = false;
    for (size_t i = 0; i < args.size(); i++) {
        // TODO: check that its type is void *?
        have_user_context |= (args[i].name == "__user_context");
    }

    NameMangling name_mangling = f.name_mangling;
    if (name_mangling == NameMangling::Default) {
        name_mangling = (target.has_feature(Target::CPlusPlusMangling) ? NameMangling::CPlusPlus : NameMangling::C);
    }

    set_name_mangling_mode(name_mangling);

    vector<string> namespaces;
    string simple_name = extract_namespaces(f.name, namespaces);
    if (!is_c_plus_plus_interface()) {
        user_assert(namespaces.empty()) << "Namespace qualifiers not allowed on function name if not compiling with Target::CPlusPlusNameMangling.\n";
    }

    if (!namespaces.empty()) {
        for (const auto &ns : namespaces) {
            stream << "namespace " << ns << " {\n";
        }
        stream << "\n";
    }

    //Initialize the OpenCL Kernel (seperately for each function)
    cgdev->init_module();
    //Declare the functions, which allow us to later on get the kernel source.
    string api_unique_name = cgdev->api_unique_name();
    std::string kernel_name = print_name("halide_" + simple_name + "_" + api_unique_name + "_kernel");
    std::string kernel_get_length = kernel_name + "_get_length";
    std::string kernel_get_src = kernel_name + "_get_src";

    stream << "int32_t " << kernel_get_length << "();\n"
           << "void " << kernel_get_src << "(char* out);\n \n";

    // Emit the function prototype
    if (f.linkage == LinkageType::Internal) {
        // If the function isn't public, mark it static.
        stream << "static ";
    }
    stream << "HALIDE_FUNCTION_ATTRS\n";
    stream << "int " << simple_name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            stream << "struct halide_buffer_t *"
                   << print_name(args[i].name)
                   << "_buffer";
        } else {
            stream << print_type(args[i].type, AppendSpace)
                   << print_name(args[i].name);
        }

        if (i < args.size() - 1) {
            stream << ", ";
        }
    }

    if (is_header_or_extern_decl()) {
        stream << ");\n";
    } else {
        stream << ") {\n";
        indent += 1;

        // Emit a local user_context we can pass in all cases, either
        // aliasing __user_context or nullptr.
        stream << get_indent() << "void * const _ucon = "
               << (have_user_context ? "const_cast<void *>(__user_context)" : "nullptr")
               << ";\n";

        if (target.has_feature(Target::NoAsserts)) {
            stream << get_indent() << "halide_unused(_ucon);";
        }

        std::string module_state = print_name("module_state_" + simple_name + "_" + api_unique_name);
        current__api_module_state[api_unique_name] = module_state;
        std::string kernel_length = kernel_name + "_length";
        std::string kernel_src = kernel_name + "_src";
        std::string init_kernels_name = "halide_" + api_unique_name + "_initialize_kernels";

        //Declare module state
        stream << get_indent() << "void *" << module_state << ";\n"
               //Get the kernel length
               << get_indent() << "int32_t " << kernel_length << " = " << kernel_get_length << "();\n"
               //Declare the string for the kernel source
               << get_indent() << "char " << kernel_src << "[" << kernel_length << "];\n"
               //Memset everything to the 'end' character
               << get_indent() << "memset(" << kernel_src << ", '\\0', sizeof(" << kernel_src << "));\n"
               //Get the actual kernel source
               << get_indent() << kernel_get_src << "(" << kernel_src << ");\n"
               //Initialize the kernels
               << get_indent() << init_kernels_name << "(_ucon, &" << module_state << ", " << kernel_src << ", " << kernel_length << ");\n";

        // Emit the body
        print(f.body);

        // Return success.
        stream << get_indent() << "return 0;\n";

        indent -= 1;
        stream << "}\n\n";

        //Now define the functions that get the kernel source, and the kernel source length
        vector<char> kernel_raw_src = cgdev->compile_to_src();

        stream << "int32_t " << kernel_get_length << "() {\n";
        indent += 1;
        stream << get_indent() << "return " << kernel_raw_src.size() << " + 1;\n}\n\n";
        indent -= 1;

        stream << "void " << kernel_get_src << "(char* out) {\n";
        indent += 1;
        stream << get_indent() << "const char _kernel_code[] = \n";
        indent += 1;
        stream << get_indent() << "\"";

        for (auto c : kernel_raw_src) {
            if (c == '\n') {
                stream << "\\n\"\n"
                       << get_indent() << "\"";
            } else if (c == '\"') {
                stream << "\"";
                //Null character means end of string
            } else if (c == '\0') {
                break;
            } else {
                stream << c;
            }
        }
        stream << "\";\n";
        indent -= 1;
        stream << get_indent() << "strcpy(out, _kernel_code);\n";

        indent -= 1;
        stream << "}\n\n";
    }

    if (is_header_or_extern_decl() && f.linkage == LinkageType::ExternalPlusMetadata) {
        // Emit the argv version
        stream << "\nHALIDE_FUNCTION_ATTRS\nint " << simple_name << "_argv(void **args);\n";

        // And also the metadata.
        stream << "\nHALIDE_FUNCTION_ATTRS\nconst struct halide_filter_metadata_t *" << simple_name << "_metadata();\n";
    }

    if (!namespaces.empty()) {
        stream << "\n";
        for (size_t i = namespaces.size(); i > 0; i--) {
            stream << "}  // namespace " << namespaces[i - 1] << "\n";
        }
        stream << "\n";
    }
}

string CodeGen_Simple_OpenCL::print_array_assignment(Type t, const std::string &rhs) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        stream << get_indent() << print_type(t, AppendSpace) << (output_kind == CPlusPlusImplementation ? "const " : "") << id << "[] = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

}  // namespace Internal
}  // namespace Halide
