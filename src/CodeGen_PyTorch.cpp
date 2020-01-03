#include <iostream>

#include "CodeGen_C.h"
#include "CodeGen_PyTorch.h"
#include "IROperator.h"
#include "Param.h"
#include "Util.h"
#include "Var.h"

namespace Halide {
namespace Internal {

CodeGen_PyTorch::CodeGen_PyTorch(std::ostream &s)
    : IRPrinter(s) {
}

void CodeGen_PyTorch::compile(const Module &module) {
    const Target target = module.target();

    if (target.has_feature(Target::CUDA)) {
        if (!target.has_feature(Target::UserContext)) {
            user_error << "Compile a PyTorch wrapper for a CUDA op requires the "
                          "UserContext feature to properly manage the GPU memory. "
                          "Please add \"-user_context\" to the generator's target options.\n";
        }
        stream << "#include \"ATen/cuda/CUDAContext.h\"\n";
        stream << "#include \"HalideBuffer.h\"\n";
        stream << "#include \"HalidePyTorchCudaHelpers.h\"\n";
        stream << "#include \"HalidePyTorchHelpers.h\"\n";
        stream << "#include \"torch/extension.h\"\n";
    } else {
        stream << "#include \"HalideBuffer.h\"\n";
        stream << "#include \"HalidePyTorchHelpers.h\"\n";
        stream << "#include \"torch/extension.h\"\n";
    }

    stream << "\n";

    // Emit extern decls of the Halide-generated functions we use directly
    // into this file, so that we don't have to #include the relevant .h
    // file directly; this simplifies certain compile/build setups (since
    // we don't have to build files in tandem and/or get include paths right),
    // and should be totally safe, since we are using the same codegen logic
    // that would be in the .h file anyway.
    {
        CodeGen_C extern_decl_gen(stream, module.target(), CodeGen_C::CPlusPlusExternDecl);
        extern_decl_gen.compile(module);
    }

    for (const auto &f : module.functions()) {
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

    stream << "HALIDE_FUNCTION_ATTRS\n";
    stream << "inline int " << simple_name << "_th_(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].name == "__user_context") {
            continue;
        } else if (args[i].is_buffer()) {
            buffer_args.push_back(args[i]);
            stream
                << "at::Tensor &"
                << c_print_name(args[i].name);
        } else {
            stream
                << type_to_c_type(args[i].type, true)
                << c_print_name(args[i].name);
        }

        if (i < args.size() - 1)
            stream << ", ";
    }

    stream << ") {\n";
    indent += 4;

    if (is_cuda) {
        stream << get_indent() << "// Setup CUDA\n";
        stream << get_indent() << "int device_id = at::cuda::current_device();\n";
        stream << get_indent() << "CUcontext ctx = 0;\n";
        stream << get_indent() << "CUresult res = cuCtxGetCurrent(&ctx);\n";
        stream << get_indent() << "AT_ASSERTM(res == 0, \"Could not acquire CUDA context\");\n";
        stream << get_indent() << "cudaStream_t stream = at::cuda::getCurrentCUDAStream(device_id);\n";
        stream << get_indent() << "Halide::PyTorch::UserContext user_ctx(device_id, &ctx, &stream);\n";
        stream << get_indent() << "void* __user_context = (void*) &user_ctx;\n\n";
    }

    stream << get_indent() << "// Check tensors have contiguous memory and are on the correct device\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
        stream << get_indent();
        stream
            << "HLPT_CHECK_CONTIGUOUS("
            << c_print_name(buffer_args[i].name)
            << ");\n";

        if (is_cuda) {
            stream << get_indent();
            stream
                << "HLPT_CHECK_DEVICE("
                << c_print_name(buffer_args[i].name)
                << ", device_id);\n";
        }
    }
    stream << "\n";

    stream << get_indent() << "// Wrap tensors in Halide buffers\n";
    for (size_t i = 0; i < buffer_args.size(); i++) {
        if (!buffer_args[i].is_buffer())
            continue;

        stream << get_indent();
        std::string tp = type_to_c_type(buffer_args[i].type, false);
        stream
            << "Halide::Runtime::Buffer<" << tp << "> "
            << c_print_name(buffer_args[i].name)
            << "_buffer = Halide::PyTorch::wrap<" << tp << ">("
            << c_print_name(buffer_args[i].name)
            << ");\n";
    }
    stream << "\n";

    stream << get_indent() << "// Run Halide pipeline\n";

    stream << get_indent() << "int err = " << simple_name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            stream
                << c_print_name(args[i].name)
                << "_buffer";
        } else {
            stream << c_print_name(args[i].name);
        }
        if (i < args.size() - 1)
            stream << ", ";
    }
    stream << ");\n";

    stream << "\n";

    stream << get_indent() << "AT_ASSERTM(err == 0, \"Halide call failed\");\n";

    if (is_cuda) {
        stream << get_indent() << "// Make sure data is on device\n";
        for (size_t i = 0; i < buffer_args.size(); i++) {
            if (buffer_args[i].is_buffer()) {
                stream << get_indent();
                stream
                    << "AT_ASSERTM(!"
                    << c_print_name(buffer_args[i].name) << "_buffer.host_dirty(),"
                    << "\"device not synchronized for buffer "
                    << c_print_name(buffer_args[i].name)
                    << ", make sure all update stages are excplicitly computed on GPU."
                    << "\");\n";
                stream << get_indent();
                stream
                    << c_print_name(buffer_args[i].name) << "_buffer"
                    << ".device_detach_native();\n";
            }
        }
        stream << "\n";
    }

    // TODO(mgharbi): this is not very well documented
    if (get_env_variable("FLUSH_MEMOIZE_CACHE") == "1") {
        stream << get_indent() << "// Flush cache\n";
        if (is_cuda) {
            stream << get_indent() << "halide_memoization_cache_cleanup(__user_context);\n";
        } else {
            stream << get_indent() << "halide_memoization_cache_cleanup(NULL);\n";
        }
    }

    stream << get_indent() << "return 0;\n";

    indent -= 4;
    stream << "}\n";

    if (!namespaces.empty()) {
        stream << "\n";
        for (size_t i = namespaces.size(); i > 0; i--) {
            stream << "}  // namespace " << namespaces[i - 1] << "\n";
        }
        stream << "\n";
    }
}

void CodeGen_PyTorch::test() {
    // Dummy Halide pipeline
    LoweredArgument buffer_arg("buf", Argument::OutputBuffer, Int(32), 3, ArgumentEstimates{});
    LoweredArgument float_arg("alpha", Argument::InputScalar, Float(32), 0, ArgumentEstimates{});
    LoweredArgument int_arg("beta", Argument::InputScalar, Int(32), 0, ArgumentEstimates{});
    std::vector<LoweredArgument> args = {buffer_arg, float_arg, int_arg};
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Add::make(alpha, Cast::make(Float(32), beta));
    Stmt s = Store::make("buf", e, x, Parameter(), const_true(), ModulusRemainder());
    Expr buf = Variable::make(Handle(), "buf.buffer");
    s = LetStmt::make("buf", Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern), s);

    std::ostringstream source;
    std::ostringstream source_cuda;
    {
        // TODO(mgharbi): test that Target("host-cuda") raises an exception since
        // we require the "user_context" feature when using CUDA

        Module m("", Target("host"));
        m.append(LoweredFunc("test1", args, s, LinkageType::External));

        CodeGen_PyTorch(source).compile(m);
    }
    {
        Module m("", Target("host-cuda-user_context"));
        m.append(LoweredFunc("test1", args, s, LinkageType::External));

        CodeGen_PyTorch(source_cuda).compile(m);
    }
    std::string src = source.str() + "\n" + source_cuda.str();

    // The correct source concatenates CPU and GPU headers
    std::string correct_src =
        R"GOLDEN_CODE(#include "HalideBuffer.h"
#include "HalidePyTorchHelpers.h"
#include "torch/extension.h"

struct halide_buffer_t;
struct halide_filter_metadata_t;

#ifndef HALIDE_MUST_USE_RESULT
#ifdef __has_attribute
#if __has_attribute(nodiscard)
#define HALIDE_MUST_USE_RESULT [[nodiscard]]
#elif __has_attribute(warn_unused_result)
#define HALIDE_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define HALIDE_MUST_USE_RESULT
#endif
#else
#define HALIDE_MUST_USE_RESULT
#endif
#endif

#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif



#ifdef __cplusplus
extern "C" {
#endif

HALIDE_FUNCTION_ATTRS
int test1(struct halide_buffer_t *_buf_buffer, float _alpha, int32_t _beta);

#ifdef __cplusplus
}  // extern "C"
#endif

HALIDE_FUNCTION_ATTRS
inline int test1_th_(at::Tensor &_buf, float _alpha, int32_t _beta) {
    // Check tensors have contiguous memory and are on the correct device
    HLPT_CHECK_CONTIGUOUS(_buf);

    // Wrap tensors in Halide buffers
    Halide::Runtime::Buffer<int32_t> _buf_buffer = Halide::PyTorch::wrap<int32_t>(_buf);

    // Run Halide pipeline
    int err = test1(_buf_buffer, _alpha, _beta);

    AT_ASSERTM(err == 0, "Halide call failed");
    return 0;
}

#include "ATen/cuda/CUDAContext.h"
#include "HalideBuffer.h"
#include "HalidePyTorchCudaHelpers.h"
#include "HalidePyTorchHelpers.h"
#include "torch/extension.h"

struct halide_buffer_t;
struct halide_filter_metadata_t;

#ifndef HALIDE_MUST_USE_RESULT
#ifdef __has_attribute
#if __has_attribute(nodiscard)
#define HALIDE_MUST_USE_RESULT [[nodiscard]]
#elif __has_attribute(warn_unused_result)
#define HALIDE_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define HALIDE_MUST_USE_RESULT
#endif
#else
#define HALIDE_MUST_USE_RESULT
#endif
#endif

#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif



#ifdef __cplusplus
extern "C" {
#endif

HALIDE_FUNCTION_ATTRS
int test1(struct halide_buffer_t *_buf_buffer, float _alpha, int32_t _beta);

#ifdef __cplusplus
}  // extern "C"
#endif

HALIDE_FUNCTION_ATTRS
inline int test1_th_(at::Tensor &_buf, float _alpha, int32_t _beta) {
    // Setup CUDA
    int device_id = at::cuda::current_device();
    CUcontext ctx = 0;
    CUresult res = cuCtxGetCurrent(&ctx);
    AT_ASSERTM(res == 0, "Could not acquire CUDA context");
    cudaStream_t stream = at::cuda::getCurrentCUDAStream(device_id);
    Halide::PyTorch::UserContext user_ctx(device_id, &ctx, &stream);
    void* __user_context = (void*) &user_ctx;

    // Check tensors have contiguous memory and are on the correct device
    HLPT_CHECK_CONTIGUOUS(_buf);
    HLPT_CHECK_DEVICE(_buf, device_id);

    // Wrap tensors in Halide buffers
    Halide::Runtime::Buffer<int32_t> _buf_buffer = Halide::PyTorch::wrap<int32_t>(_buf);

    // Run Halide pipeline
    int err = test1(_buf_buffer, _alpha, _beta);

    AT_ASSERTM(err == 0, "Halide call failed");
    // Make sure data is on device
    AT_ASSERTM(!_buf_buffer.host_dirty(),"device not synchronized for buffer _buf, make sure all update stages are excplicitly computed on GPU.");
    _buf_buffer.device_detach_native();

    return 0;
}
)GOLDEN_CODE";

    if (src != correct_src) {
        int diff = 0;
        while (src[diff] == correct_src[diff]) {
            diff++;
        }
        int diff_end = diff + 1;
        while (diff > 0 && src[diff] != '\n') {
            diff--;
        }
        while (diff_end < (int)src.size() && src[diff_end] != '\n') {
            diff_end++;
        }

        internal_error
            << "Correct source code:\n"
            << correct_src
            << "Actual source code:\n"
            << src
            << "Difference starts at:" << diff << "\n"
            << "Correct: " << correct_src.substr(diff, diff_end - diff) << "\n"
            << "Actual: " << src.substr(diff, diff_end - diff) << "\n";
    }

    std::cout << "CodeGen_PyTorch test passed\n";
}

}  // namespace Internal
}  // namespace Halide
