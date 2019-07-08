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
  // TODO: remove this duplicate
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
    ostringstream oss;

    if (type.is_float()) {
        if (type.bits() == 32) {
          if(isCuda) {
            oss << "THCudaTensor";
          } else {
            oss << "THFloatTensor";
          }
        } else if (type.bits() == 64) {
          if(isCuda) {
            oss << "THCudaDoubleTensor";
          } else {
            oss << "THDoubleTensor";
          }
        } else {
            user_error << "Can't represent a float with this many bits in C: " << type << "\n";
        }
    } else if (type.is_int()) {
        if (type.bits() == 32) {
          if(isCuda) {
            oss << "THCudaIntTensor";
          } else {
            oss << "THIntTensor";
          }
        } else if (type.bits() == 64) {
          if(isCuda) {
            oss << "THCudaLongTensor";
          } else {
            oss << "THLongTensor";
          }
        } else {
            user_error << "Can't represent a float with this many bits in C: " << type << "\n";
        }
    } else {
      user_error << "Type " << type << " not handled by pytorch wrapper" << type << "\n";
    }

    return oss.str();
}


} // ns anon


CodeGen_PyTorch::CodeGen_PyTorch(ostream &s, Target t, OutputKind output_kind,
    std::string cpp_header) :
    IRPrinter(s), target(t), output_kind(output_kind), cpp_header(cpp_header)
{
  if(!is_header()) {
    stream << "#include <TH/TH.h>\n";
    if(target.has_feature(Target::CUDA)) {
      stream << "#include <THC/THC.h>\n";
    }
    stream << "#include <stdio.h>\n"
              "#include <HalideBuffer.h>\n"
              "#include <HalidePytorchHelpers.h>\n"
              "\n";
    stream << "#include \"" << cpp_header << "\"\n";

    stream << "using Halide::Runtime::Buffer;\n\n";

    if(target.has_feature(Target::CUDA)) {
      stream << "extern THCState *state;\n\n";
    }

    stream << "extern \"C\" {\n";
  }

}

CodeGen_PyTorch::~CodeGen_PyTorch() {
  if(!is_header()) {
    stream << "}  // extern \"C\"\n";
  }
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

  if (is_header() && f.linkage == LinkageType::Internal) {
    stream << "// internal func "<< simple_name << "\n";
    return;
  }

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
      stream 
        << type_to_pytorch_tensor(args[i].type, isCuda)
        << " *"
        << print_name(args[i].name);
    } else {
      stream
        << type_to_c_type(args[i].type, true)
        << print_name(args[i].name);
    }

    if (i < args.size()-1) stream << ", ";
  }

  if (is_header()) {
    stream << ");\n";
  } else {
    stream << ") {\n";
    indent += 2;

    do_indent();
    stream << "// Grab references to contiguous memory\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
      // Get the device id of one of the buffers
      if (isCuda) {
        if (i == 0) {
          do_indent();
          stream << "int device_id = " << type_to_pytorch_tensor(buffer_args[i].type, isCuda) << "_getDevice(state, "
          // stream << "int device_id = THCudaTensor_getDevice(state, "
                 << print_name(buffer_args[i].name) << ");\n";
          do_indent();
          stream << "CUcontext ctx = 0;\n";
          do_indent();
          stream << "CUresult res = cuCtxGetCurrent(&ctx);\n";
          do_indent();
          stream << "if(res != 0) throw Halide::Pytorch::CudaContextException();\n";
          do_indent();
          stream << "cudaStream_t stream = THCState_getCurrentStreamOnDevice(state, device_id);\n";
          do_indent();
          stream << "Halide::Pytorch::UserContext user_ctx(device_id, &ctx, &stream);\n";
          do_indent();
          stream << "void* __user_context = (void*) &user_ctx;\n\n";
        } 
        else {
          do_indent();
          stream << "if(device_id != " << type_to_pytorch_tensor(buffer_args[i].type, isCuda) << "_getDevice(state, "
                 << print_name(buffer_args[i].name) << ")) throw Halide::Pytorch::InvalidDeviceException();\n";
        }
      }

      do_indent();
      stream
        << print_name(buffer_args[i].name) 
        << " = "
        << type_to_pytorch_tensor(buffer_args[i].type, isCuda)
        << "_newContiguous(";
      if(isCuda) {
        stream << "state, ";
      }

      stream
        << print_name(buffer_args[i].name) 
        << ");\n"
        ;
    }
    stream << "\n";


    do_indent();
    stream << "// Wrap tensors in Halide buffers\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
      if (!buffer_args[i].is_buffer())
        continue;
      do_indent();
      // stream
      //   << "Buffer<"
      //   << type_to_c_type(buffer_args[i].type, false)
      //   << "> "
      //   << print_name(buffer_args[i].name) 
      //   << "_buffer;\n";
      // do_indent();
      // stream
      //   << "Halide::Pytorch::wrap("
      //   << print_name(buffer_args[i].name) 
      //   << ", "
      //   << print_name(buffer_args[i].name) 
      //   << "_buffer);\n"
      //   ;
      
      string tp = type_to_c_type(buffer_args[i].type, false);
      stream
        << "Buffer<"
        << tp
        << "> "
        << print_name(buffer_args[i].name) 
        << "_buffer = Halide::Pytorch::wrap<" << tp << ">("
        << print_name(buffer_args[i].name) 
        << ");\n"
        ;
    }

    do_indent();
    stream << "// Run code\n";
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
    stream << "if (err != 0) throw Halide::Pytorch::CudaRunException();\n";
    stream << "\n";

    if(isCuda) {
      do_indent();
      stream << "// Make sure data is on device\n";
      do_indent();
      stream << "const halide_device_interface_t* cuda_interface = halide_cuda_device_interface();\n";
      for (size_t i = 0; i < buffer_args.size(); i++) {
        if (buffer_args[i].is_buffer()) {
          do_indent();
          stream 
            << "if ("
            << print_name(buffer_args[i].name) << "_buffer.host_dirty() )"
            << " throw Halide::Pytorch::DeviceNotSynchronizedException(\""
            << print_name(buffer_args[i].name)
            << "\");\n";

          // << ".copy_to_device(cuda_interface, __user_context);\n";
          // do_indent();
          // stream 
          //   << print_name(buffer_args[i].name) << "_buffer"
          //   << ".copy_to_device(cuda_interface, __user_context);\n";
          // do_indent();
          // stream 
          //   << print_name(buffer_args[i].name) << "_buffer"
          //   << ".device_sync();\n";
          // do_indent();
          // stream 
          //   << print_name(buffer_args[i].name) << "_buffer"
          //   << ".device_detach_native();\n";
        }
      }
      stream << "\n";
    }

    do_indent();
    stream << "// Free references\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
      if (buffer_args[i].is_buffer()) {
        do_indent();
        stream
          << type_to_pytorch_tensor(buffer_args[i].type, isCuda)
          << "_free(";
        if(isCuda) {
          stream << "state, ";
        }
        stream
          << print_name(buffer_args[i].name) 
          << ");\n"
          ;
      }
    }
    stream << "\n";

    if (get_env_variable("FLUSH_MEMOIZE_CACHE") == "1") {
        do_indent();
        // flush cache
        if (isCuda) {
            stream << "halide_memoization_cache_cleanup(__user_context);\n";
        } else {
            stream << "halide_memoization_cache_cleanup(NULL);\n";
        }
    }

    // if(isCuda) {
      // do_indent();
      // stream << "// Synchronize device\n";
      // do_indent();
      // stream << "halide_device_sync(NULL, cuda_interface);\n";
      // stream << "\n";

      // do_indent();
      // stream << "// Release device\n";
      // do_indent();
      // stream << "halide_device_release(NULL, cuda_interface);\n";
      // stream << "\n";
    // }

    do_indent();
    stream << "return 0;\n";

    indent -= 2;
    stream << "}\n";
  }

  if (!namespaces.empty()) {
    stream << "\n";
    for (size_t i = namespaces.size(); i > 0; i--) {
      stream << "}  // namespace " << namespaces[i-1] << "\n";
    }
    stream << "\n";
  }
}

  // TODO: remove this duplicate
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
