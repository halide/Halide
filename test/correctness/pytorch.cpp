#include "Halide.h"
#include "halide_test_dirs.h"
#include <fstream>

using namespace Halide;

namespace {

std::string replace_all(const std::string &str, const std::string &find, const std::string &replace) {
    size_t pos = 0;
    std::string result = str;
    while ((pos = result.find(find, pos)) != std::string::npos) {
        result.replace(pos, find.length(), replace);
        pos += replace.length();
    }
    return result;
}

std::string read_entire_file(const std::string &pathname) {
    std::ifstream f(pathname, std::ios::in | std::ios::binary);
    std::string result;

    f.seekg(0, std::ifstream::end);
    size_t size = f.tellg();
    result.resize(size);
    f.seekg(0, std::ifstream::beg);
    f.read(result.data(), result.size());
    if (!f.good()) {
        std::cerr << "Unable to read file: " << pathname;
        exit(1);
    }
    f.close();
    // Normalize file to Unix line endings
    result = replace_all(result, "\r\n", "\n");
    return result;
}

void compare_src(const std::string &src, const std::string &correct_src) {
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

        std::cerr
            << "Correct source code:\n"
            << correct_src
            << "Actual source code:\n"
            << src
            << "Difference starts at:" << diff << "\n"
            << "Correct: " << correct_src.substr(diff, diff_end - diff) << "\n"
            << "Actual: " << src.substr(diff, diff_end - diff) << "\n";

        exit(1);
    }
}

}  // namespace

int main(int argc, char **argv) {
    Param<float> alpha("alpha");
    Param<int32_t> beta("beta");
    Var x("x");

    Func buf("buf");
    buf(x) = cast<int32_t>(alpha + cast<float>(beta));

    {
        // We are using a fixed target here (rather than "host") since
        // we are crosscompiling and want a uniform result everywhere.
        Target t = Target("x86-64-linux");

        std::string pytorch_out = Internal::get_test_tmp_dir() + "pytorch_test1.pytorch.h";
        Internal::ensure_no_file_exists(pytorch_out);

        std::vector<Argument> args{alpha, beta};
        buf.compile_to({{OutputFileType::pytorch_wrapper, pytorch_out}}, args, "test1", t);

        Internal::assert_file_exists(pytorch_out);
        std::string actual = read_entire_file(pytorch_out);

        std::string expected =
            R"GOLDEN_CODE(#include "HalideBuffer.h"
#include "HalidePyTorchHelpers.h"

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
int test1(float _alpha, int32_t _beta, struct halide_buffer_t *_buf_buffer);

HALIDE_FUNCTION_ATTRS
int test1_argv(void **args);

HALIDE_FUNCTION_ATTRS
const struct halide_filter_metadata_t *test1_metadata();

#ifdef __cplusplus
}  // extern "C"
#endif

HALIDE_FUNCTION_ATTRS
inline int test1_th_(float _alpha, int32_t _beta, at::Tensor &_buf) {
    void* __user_context = nullptr;

    // Check tensors have contiguous memory and are on the correct device
    HLPT_CHECK_CONTIGUOUS(_buf);

    // Wrap tensors in Halide buffers
    Halide::Runtime::Buffer<int32_t> _buf_buffer = Halide::PyTorch::wrap<int32_t>(_buf);

    // Run Halide pipeline
    int err = test1(_alpha, _beta, _buf_buffer);

    AT_ASSERTM(err == 0, "Halide call failed");
    return 0;
}
)GOLDEN_CODE";

        compare_src(actual, expected);
    }

    {
        // We are using an explicit target here (rather than "host") to avoid sniffing
        // the system for capabilities; in particular, we don't care what Cuda capabilities
        // the system has, and don't want to initialize Cuda to find out. (Since this test
        // is just a crosscompilation for generated C++ code, this is fine.)
        Target t = Target("x86-64-linux-cuda-user_context");

        std::string pytorch_out = Internal::get_test_tmp_dir() + "pytorch_test2.pytorch.h";
        Internal::ensure_no_file_exists(pytorch_out);

        std::vector<Argument> args{alpha, beta};
        buf.compile_to({{OutputFileType::pytorch_wrapper, pytorch_out}}, args, "test2", t);

        Internal::assert_file_exists(pytorch_out);
        std::string actual = read_entire_file(pytorch_out);

        std::string expected =
            R"GOLDEN_CODE(#include "ATen/cuda/CUDAContext.h"
#include "HalideBuffer.h"
#include "HalidePyTorchHelpers.h"

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
int test2(void const *__user_context, float _alpha, int32_t _beta, struct halide_buffer_t *_buf_buffer);

HALIDE_FUNCTION_ATTRS
int test2_argv(void **args);

HALIDE_FUNCTION_ATTRS
const struct halide_filter_metadata_t *test2_metadata();

#ifdef __cplusplus
}  // extern "C"
#endif

HALIDE_FUNCTION_ATTRS
inline int test2_th_(float _alpha, int32_t _beta, at::Tensor &_buf) {
    // Setup CUDA
    int device_id = at::cuda::current_device();
    CUcontext ctx = 0;
    CUresult res = cuCtxGetCurrent(&ctx);
    AT_ASSERTM(res == 0, "Could not acquire CUDA context");
    cudaStream_t stream = at::cuda::getCurrentCUDAStream(device_id);
    struct UserContext { int device_id; CUcontext *cuda_context; cudaStream_t *stream; } user_ctx;
    user_ctx.device_id = device_id;
    user_ctx.cuda_context = &ctx;
    user_ctx.stream = &stream;
    void* __user_context = (void*) &user_ctx;

    // Check tensors have contiguous memory and are on the correct device
    HLPT_CHECK_CONTIGUOUS(_buf);
    HLPT_CHECK_DEVICE(_buf, device_id);

    // Wrap tensors in Halide buffers
    Halide::Runtime::Buffer<int32_t> _buf_buffer = Halide::PyTorch::wrap_cuda<int32_t>(_buf);

    // Run Halide pipeline
    int err = test2(__user_context, _alpha, _beta, _buf_buffer);

    AT_ASSERTM(err == 0, "Halide call failed");
    // Make sure data is on device
    AT_ASSERTM(!_buf_buffer.host_dirty(),"device not synchronized for buffer _buf, make sure all update stages are explicitly computed on GPU.");
    _buf_buffer.device_detach_native();

    return 0;
}
)GOLDEN_CODE";

        compare_src(actual, expected);
    }

    printf("Success!\n");
    return 0;
}
