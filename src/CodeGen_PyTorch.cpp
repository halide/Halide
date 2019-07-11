#include <iostream>
#include <limits>

#include "CodeGen_Internal.h"
#include "CodeGen_PyTorch.h"
#include "Deinterleave.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Param.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Type.h"
#include "Util.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::endl;
using std::map;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

namespace {

/** Type to PyTorch Tensor type */
string type_to_pytorch_tensor(Type type, bool is_cuda) {
    return "at::Tensor";
}

} // namespace anon


CodeGen_PyTorch::CodeGen_PyTorch(ostream &s, Target t, std::string cpp_header) :
    IRPrinter(s), target(t), cpp_header(cpp_header)
{
  stream << "#include \"torch/extension.h\"\n";
  stream << "#include \"HalideBuffer.h\"\n\n";
  stream << "#include \"HalidePyTorchHelpers.h\"\n";

  if (target.has_feature(Target::CUDA)) {
    if (!target.has_feature(Target::UserContext)) {
      user_error << "Compile a PyTorch wrapper for a CUDA op requires the "
        "UserContext feature to properly manage the GPU memory. "
        "Please add \"-user_context\" to the generator's target options.\n";
    }
    stream << "#include \"ATen/cuda/CUDAContext.h\"\n";  
    stream << "#include \"HalidePyTorchCudaHelpers.h\"\n";
  }

  std::vector<std::string> header_path = split_string(cpp_header, "/");
  std::string header = header_path.back();

  stream << "\n#include \"" << header << "\"\n\n";
  stream << "using Halide::Runtime::Buffer;\n\n";
}

void CodeGen_PyTorch::compile(const Module &input) {
    for (const auto &f : input.functions()) {
      if (f.name.find("old_buffer_t") != std::string::npos) {
        debug(1) << "ignoring " << f.name;
        continue;
      }
      if (target.has_feature(Target::CUDA)) {
        compile(f, true);
      } else {
        compile(f, false);
      }
    }
}

void CodeGen_PyTorch::compile(const LoweredFunc &f, bool is_cuda) {
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
      stream 
        << type_to_pytorch_tensor(args[i].type, is_cuda)
        << " &"
        << c_print_name(args[i].name);
    } else {
      stream
        << type_to_c_type(args[i].type, true)
        << c_print_name(args[i].name);
    }

    if (i < args.size()-1) stream << ", ";
  }

  stream << ") {\n";
  indent += 2;

  do_indent();
  if (is_cuda) {
    stream << "// Setup CUDA\n";
    do_indent();
    stream << "int device_id = at::cuda::current_device();\n";
    do_indent();
    stream << "CUcontext ctx = 0;\n";
    do_indent();
    stream << "CUresult res = cuCtxGetCurrent(&ctx);\n";
    do_indent();
    stream << "AT_ASSERTM(res == 0, \"Could not acquire CUDA context\");\n";
    do_indent();
    stream << "cudaStream_t stream = at::cuda::getCurrentCUDAStream(device_id);\n";
    do_indent();
    stream << "Halide::PyTorch::UserContext user_ctx(device_id, &ctx, &stream);\n";
    do_indent();
    stream << "void* __user_context = (void*) &user_ctx;\n\n";
  }

  do_indent();
  stream << "// Check tensors have contiguous memory and are on the correct device\n";
  for (size_t i = 0; i < buffer_args.size(); i++) {
    do_indent();
    stream
      << "HLPT_CHECK_CONTIGUOUS("
      << c_print_name(buffer_args[i].name) 
      << ");\n";
    if (is_cuda) {
      do_indent();
      stream
        << "HLPT_CHECK_DEVICE("
        << c_print_name(buffer_args[i].name) 
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
      << c_print_name(buffer_args[i].name) 
      << "_buffer = Halide::PyTorch::wrap<" << tp << ">("
      << c_print_name(buffer_args[i].name) 
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
        << c_print_name(args[i].name)
        << "_buffer";
    } else {
      stream << c_print_name(args[i].name);
    }
    if (i < args.size()-1) stream << ", ";
  }
  stream << ");\n";
  do_indent();
  stream << "AT_ASSERTM(err == 0, \"Halide call failed\");\n";

  if (is_cuda) {
    do_indent();
    stream << "// Make sure data is on device\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
      if (buffer_args[i].is_buffer()) {
        do_indent();
        stream 
          << "AT_ASSERTM(!"
          << c_print_name(buffer_args[i].name) << "_buffer.host_dirty(),"
          << "\"device not synchronized for buffer "
          << c_print_name(buffer_args[i].name)
          << ", make sure all update stages are excplicitly computed on GPU."
          <<"\");\n";
        do_indent();
        stream 
          << c_print_name(buffer_args[i].name) << "_buffer"
          << ".device_detach_native();\n";
      }
    }
    stream << "\n";
  }

  // TODO(mgharbi): this is not very well documented
  if (get_env_variable("FLUSH_MEMOIZE_CACHE") == "1") {
      do_indent();
      stream << "// Flush cache\n";
      if (is_cuda) {
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

  if (!namespaces.empty()) {
    stream << "\n";
    for (size_t i = namespaces.size(); i > 0; i--) {
      stream << "}  // namespace " << namespaces[i-1] << "\n";
    }
    stream << "\n";
  }
}

void CodeGen_PyTorch::test() {
    std::cout << "CodeGen_PyTorch test passed\n";
}

}  // namespace Internal
}  // namespace Halide
