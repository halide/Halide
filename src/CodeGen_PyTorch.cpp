#include <iostream>
#include <limits>

#include "CodeGen_PyTorch.h"
#include "CodeGen_Internal.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include "Lerp.h"
#include "Simplify.h"
#include "Deinterleave.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;
using std::map;

namespace {
// Halide type to a C++ type
string type_to_c_type(Type type, bool include_space, bool c_plus_plus = true) {
    bool needs_space = true;
    ostringstream oss;

    if (type.is_float()) {
        if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            oss << "double";
        } else {
            user_error << "Can't represent a float with this many bits in C: " << type << "\n";
        }
        if (type.is_vector()) {
            oss << type.lanes();
        }
    } else if (type.is_handle()) {
        needs_space = false;

        // If there is no type info or is generating C (not C++) and
        // the type is a class or in an inner scope, just use void *.
        if (type.handle_type == NULL ||
            (!c_plus_plus &&
             (!type.handle_type->namespaces.empty() ||
              !type.handle_type->enclosing_types.empty() ||
              type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Class))) {
            oss << "void *";
        } else {
            if (type.handle_type->inner_name.cpp_type_type ==
                halide_cplusplus_type_name::Struct) {
                oss << "struct ";
            }

            if (!type.handle_type->namespaces.empty() ||
                !type.handle_type->enclosing_types.empty()) {
                oss << "::";
                for (size_t i = 0; i < type.handle_type->namespaces.size(); i++) {
                    oss << type.handle_type->namespaces[i] << "::";
                }
                for (size_t i = 0; i < type.handle_type->enclosing_types.size(); i++) {
                    oss << type.handle_type->enclosing_types[i].name << "::";
                }
            }
            oss << type.handle_type->inner_name.name;
            if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                oss << " &";
            } else if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                oss << " &&";
            }
            for (auto modifier : type.handle_type->cpp_type_modifiers) {
                if (modifier & halide_handle_cplusplus_type::Const) {
                    oss << " const";
                }
                if (modifier & halide_handle_cplusplus_type::Volatile) {
                    oss << " volatile";
                }
                if (modifier & halide_handle_cplusplus_type::Restrict) {
                    oss << " restrict";
                }
                if (modifier & halide_handle_cplusplus_type::Pointer) {
                    oss << " *";
                }
            }
        }
    } else {
        // This ends up using different type names than OpenCL does
        // for the integer vector types. E.g. uint16x8_t rather than
        // OpenCL's short8. Should be fine as CodeGen_C introduces
        // typedefs for them and codegen always goes through this
        // routine or its override in CodeGen_OpenCL to make the
        // names. This may be the better bet as the typedefs are less
        // likely to collide with built-in types (e.g. the OpenCL
        // ones for a C compiler that decides to compile OpenCL).
        // This code also supports arbitrary vector sizes where the
        // OpenCL ones must be one of 2, 3, 4, 8, 16, which is too
        // restrictive for already existing architectures.
        switch (type.bits()) {
        case 1:
            // bool vectors are always emitted as uint8 in the C++ backend
            if (type.is_vector()) {
                oss << "uint8x" << type.lanes() << "_t";
            } else {
                oss << "bool";
            }
            break;
        case 8: case 16: case 32: case 64:
            if (type.is_uint()) {
                oss << 'u';
            }
            oss << "int" << type.bits();
            if (type.is_vector()) {
                oss << "x" << type.lanes();
            }
            oss << "_t";
            break;
        default:
            user_error << "Can't represent an integer with this many bits in C: " << type << "\n";
        }
    }
    if (include_space && needs_space)
        oss << " ";
    return oss.str();
}

string type_to_pytorch_tensor(Type type, bool isCuda) {
    return "at::Tensor";
}

} // ns anon


CodeGen_PyTorch::CodeGen_PyTorch(ostream &s, Target t, OutputKind output_kind,
    std::string cpp_header) :
    IRPrinter(s), target(t), output_kind(output_kind), cpp_header(cpp_header)
{
  // TODO(mgharbi): header guard and header / implementation split
  stream << "#include <torch/extension.h>\n";
  // TODO(mgharbi): find a shallower integration with torch cuda, handle no GPU case
  stream << "#include <HalideBuffer.h>\n\n";

  // Conditionally add CUDA features to the pytorch helper
  // if(target.has_feature(Target::CUDA)) {
  //   stream << "#define HL_PT_CUDA\n";
  // }
  stream << "#include <HalidePytorchHelpers.h>\n";
  if(target.has_feature(Target::CUDA)) {
    stream << "#include <ATen/cuda/CUDAContext.h>\n";  
    stream << "#include <HalidePytorchCudaHelpers.h>\n";
    // stream << "#undef HL_PT_CUDA\n";
  }

  std::vector<std::string> header_path = split_string(cpp_header, "/");
  std::string header = header_path.back();
  

  stream << "\n#include \"" << header << "\"\n\n";
  stream << "using Halide::Runtime::Buffer;\n\n";
}

CodeGen_PyTorch::~CodeGen_PyTorch() {
}

void CodeGen_PyTorch::compile(const Module &input) {
    for (const auto &f : input.functions()) {
      if (f.name.find("old_buffer_t") != std::string::npos) {
        debug(1) << "ignoring " << f.name;
        continue;
      }
      if(target.has_feature(Target::CUDA)) {
        compile(f, true);
      } else {
        compile(f, false);
      }
    }
}

void CodeGen_PyTorch::compile(const LoweredFunc &f, bool isCuda) {
  // Don't put non-external function declarations in headers.
  std::vector<std::string> namespaces;
  std::string simple_name = extract_namespaces(f.name, namespaces);

  if (!namespaces.empty()) {
    for (const auto &ns : namespaces) {
      stream << "namespace " << ns << " {\n";
    }
    stream << "\n";
  }
  const std::vector<LoweredArgument> &args = f.args;
  std::vector<LoweredArgument> buffer_args;

  stream << "int " << simple_name << "_th_(";
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i].name == "__user_context") {
      continue;
    } else if (args[i].is_buffer()) {
      buffer_args.push_back(args[i]);
      // if(args[i].is_input()) { // TODO
      //   stream << "const ";
      // }
      stream 
        << type_to_pytorch_tensor(args[i].type, isCuda)
        << " &"
        << print_name(args[i].name);
    } else {
      stream
        << type_to_c_type(args[i].type, true)
        << print_name(args[i].name);
    }

    if (i < args.size()-1) stream << ", ";
  }

  stream << ") {\n";
  indent += 2;

  do_indent();
  if (isCuda) {
    stream << "// Setup CUDA\n";
    do_indent();
    stream << "int device_id = at::cuda::current_device();\n";
    do_indent();
    stream << "CUcontext ctx = 0;\n";
    do_indent();
    stream << "CUresult res = cuCtxGetCurrent(&ctx);\n";
    do_indent();
    // stream << "if(res != 0) throw Halide::Pytorch::CudaContextException();\n";
    stream << "AT_ASSERTM(res == 0, \"Could not acquire CUDA context\");\n";
    do_indent();
    stream << "cudaStream_t stream = at::cuda::getCurrentCUDAStream(device_id);\n";
    do_indent();
    stream << "Halide::Pytorch::UserContext user_ctx(device_id, &ctx, &stream);\n";
    do_indent();
    stream << "void* __user_context = (void*) &user_ctx;\n\n";
  }

  do_indent();
  stream << "// Check tensors have contiguous memory and are on the correct device\n";
  for (size_t i = 0; i < buffer_args.size(); i++) {
    do_indent();
    stream
      << "HLPT_CHECK_CONTIGUOUS("
      << print_name(buffer_args[i].name) 
      << ");\n";
    if(isCuda) {
      do_indent();
      stream
        << "HLPT_CHECK_DEVICE("
        << print_name(buffer_args[i].name) 
        << ", device_id);\n";
    }
  }
  stream << "\n";

  do_indent();
  stream << "// Wrap tensors in Halide buffers\n";
  for (size_t i = 0; i < buffer_args.size(); i++) {
    if (!buffer_args[i].is_buffer())
      continue;

    do_indent();
    string tp = type_to_c_type(buffer_args[i].type, false);
    stream
      << "Buffer<" << tp << "> "
      << print_name(buffer_args[i].name) 
      << "_buffer = Halide::Pytorch::wrap<" << tp << ">("
      << print_name(buffer_args[i].name) 
      << ");\n"
      ;
  }
  stream << "\n";

  do_indent();
  stream << "// Run Halide pipeline\n";
  do_indent();
  stream << "int err = " << simple_name << "(";
  for (size_t i = 0; i < args.size(); i++) {
    if (args[i].is_buffer()) {
      stream 
        << print_name(args[i].name)
        << "_buffer";
    } else {
      stream << print_name(args[i].name);
    }
    if (i < args.size()-1) stream << ", ";
  }
  stream << ");\n";
  do_indent();
  stream << "AT_ASSERTM(err == 0, \"Halide call failed\");\n";

  if(isCuda) {
    do_indent();
    stream << "// Make sure data is on device\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
      if (buffer_args[i].is_buffer()) {
        do_indent();
        stream 
          << "AT_ASSERTM(!"
          << print_name(buffer_args[i].name) << "_buffer.host_dirty(),"
          << "\"device not synchronized for buffer "
          << print_name(buffer_args[i].name)
          << ", make sure all update stages are excplicitly computed on GPU."
          <<"\");\n";
        do_indent();
        stream 
          << print_name(buffer_args[i].name) << "_buffer"
          << ".device_detach_native();\n";
      }
    }
    stream << "\n";
  }

  // do_indent();
  // stream << "// Free references\n";
  // for (size_t i = 0; i < buffer_args.size(); i++) {
  //   if (buffer_args[i].is_buffer()) {
  //     do_indent();
  //     stream
  //       << type_to_pytorch_tensor(buffer_args[i].type, isCuda)
  //       << "_free(";
  //     if(isCuda) {
  //       stream << "state, ";
  //     }
  //     stream
  //       << print_name(buffer_args[i].name) 
  //       << ");\n"
  //       ;
  //   }
  // }
  // stream << "\n";

  // TODO(mgharbi): this is not very well documented
  if (get_env_variable("FLUSH_MEMOIZE_CACHE") == "1") {
      do_indent();
      stream << "// Flush cache\n";
      if (isCuda) {
          do_indent();
          stream << "halide_memoization_cache_cleanup(__user_context);\n";
      } else {
          do_indent();
          stream << "halide_memoization_cache_cleanup(NULL);\n";
      }
  }

  do_indent();
  stream << "return 0;\n";

  indent -= 2;
  stream << "}\n\n";

  // // Pybind interface
  // do_indent();
  // stream << "PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {\n";
  // indent += 2;
  // do_indent();
  // stream << "m.def(\"" << simple_name << "\", &" << simple_name 
  //        << "_th_, \"Pytorch wrapper of the Halide pipeline " << simple_name << "\");\n";
  // indent -= 2;
  // stream << "}\n";

  if (!namespaces.empty()) {
    stream << "\n";
    for (size_t i = namespaces.size(); i > 0; i--) {
      stream << "}  // namespace " << namespaces[i-1] << "\n";
    }
    stream << "\n";
  }
}

string CodeGen_PyTorch::print_name(const string &name) {
    ostringstream oss;

    // Prefix an underscore to avoid reserved words (e.g. a variable named "while")
    if (isalpha(name[0])) {
        oss << '_';
    }

    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') {
            oss << '_';
        } else if (name[i] == '$') {
            oss << "__";
        } else if (name[i] != '_' && !isalnum(name[i])) {
            oss << "___";
        }
        else oss << name[i];
    }
    return oss.str();
}

} // ns Internal
} // ns Halide
