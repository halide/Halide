#include "Halide.h"

#include <array>
#include <cstring>

using namespace Halide;
using namespace Halide::Internal;

namespace {

// On Windows, raw string literals and binary2cpp-generated arrays can
// contain \r\n line endings (from git checkout with core.autocrlf).
// Normalize to \n so that generated source files have consistent,
// platform-independent line endings.
std::string normalize_line_endings(const char *s) {
    std::string result;
    result.reserve(strlen(s));
    for (; *s; ++s) {
        if (*s != '\r') {
            result += *s;
        }
    }
    return result;
}

// HALIDE_MUST_USE_RESULT defined here is intended to exactly
// duplicate the definition in HalideRuntime.h (so that either or
// both can be present, in any order).
const std::string kDefineMustUseResult = normalize_line_endings(R"INLINE_CODE(#ifndef HALIDE_MUST_USE_RESULT
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
)INLINE_CODE");

}  // namespace

int main(int argc, char **argv) {
    LoweredArgument buffer_arg("buf", Argument::OutputBuffer, Int(32), 3, ArgumentEstimates{});
    LoweredArgument float_arg("alpha", Argument::InputScalar, Float(32), 0, ArgumentEstimates{});
    LoweredArgument int_arg("beta", Argument::InputScalar, Int(32), 0, ArgumentEstimates{});
    LoweredArgument user_context_arg("__user_context", Argument::InputScalar, type_of<const void *>(), 0, ArgumentEstimates{});
    std::vector<LoweredArgument> args = {buffer_arg, float_arg, int_arg, user_context_arg};
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Select::make(alpha > 4.0f, print_when(x < 1, 3), 2);
    Stmt s = Store::make("buf", e, x, Parameter(), const_true(), ModulusRemainder());
    s = LetStmt::make("x", beta + 1, s);
    s = Block::make(s, Free::make("tmp.stack"));
    s = Allocate::make("tmp.stack", Int(32), MemoryType::Stack, {127}, const_true(), s);
    s = Allocate::make("tmp.heap", Int(32), MemoryType::Heap, {43, beta}, const_true(), s);
    Expr buf = Variable::make(Handle(), "buf.buffer");
    s = LetStmt::make("buf", Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern), s);

    Module m("", get_host_target());
    m.append(LoweredFunc("test1", args, s, LinkageType::External));

    std::ostringstream source;
    {
        CodeGen_C cg(source, Target("host"), CodeGen_C::CImplementation);
        cg.compile(m);
    }

    std::string correct_source =
        codegen_c_test_prologue_source() + '\n' +
        codegen_c_test_runtime_header_source() + '\n' +
        codegen_c_test_inlined_c_source() + '\n' +
        '\n' + kDefineMustUseResult + normalize_line_endings(R"GOLDEN_CODE(
#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif



#ifdef __cplusplus
extern "C" {
#endif

HALIDE_FUNCTION_ATTRS
int test1(struct halide_buffer_t *_buf_buffer, float _alpha, int32_t _beta, void const *__user_context) {
 void * const _ucon = const_cast<void *>(__user_context);
 halide_maybe_unused(_ucon);
 auto *_0 = _halide_buffer_get_host(_buf_buffer);
 auto _buf = _0;
 halide_maybe_unused(_buf);
 {
  int64_t _1 = 43;
  int64_t _2 = _1 * _beta;
  if ((_2 > ((int64_t(1) << 31) - 1)) || ((_2 * sizeof(int32_t )) > ((int64_t(1) << 31) - 1)))
  {
   halide_error(_ucon, "32-bit signed overflow computing size of allocation tmp.heap\n");
   return -1;
  } // overflow test tmp.heap
  int64_t _3 = _2;
  int32_t *_tmp_heap = (int32_t  *)halide_malloc(_ucon, sizeof(int32_t )*_3);
  if (!((_tmp_heap != nullptr) || (_3 == 0)))
  {
   int32_t _4 = halide_error_out_of_memory(_ucon);
   return _4;
  }
  HalideFreeHelper<halide_free> _tmp_heap_free(_ucon, _tmp_heap);
  {
   int32_t _tmp_stack[127];
   int32_t _5 = _beta + 1;
   int32_t _6;
   bool _7 = _5 < 1;
   if (_7)
   {
    char b0[1024];
    snprintf(b0, 1024, "%lld%s", (long long)(3), "\n");
    auto *_8 = b0;
    halide_print(_ucon, _8);
    int32_t _9 = 0;
    int32_t _10 = return_second(_9, 3);
    _6 = _10;
   } // if _7
   else
   {
    _6 = 3;
   } // if _7 else
   int32_t _11 = _6;
   float _12 = float_from_bits(1082130432 /* 4 */);
   bool _13 = _alpha > _12;
   int32_t _14 = (int32_t)(_13 ? _11 : 2);
   ((int32_t *)_buf)[_5] = _14;
  } // alloc _tmp_stack
  _tmp_heap_free.free();
 } // alloc _tmp_heap
 return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif

)GOLDEN_CODE");

    const auto compare_srcs = [](const std::string &actual, const std::string &expected) {
        if (actual != expected) {
            int diff = 0;
            while (actual[diff] == expected[diff]) {
                diff++;
            }
            int diff_end = diff + 1;
            while (diff > 0 && actual[diff] != '\n') {
                diff--;
            }
            while (diff_end < (int)actual.size() && actual[diff_end] != '\n') {
                diff_end++;
            }

            internal_error
                << "Correct source code:\n"
                << expected
                << "Actual source code:\n"
                << actual
                << "Difference starts at:\n"
                << "Correct: " << expected.substr(diff, diff_end - diff) << "\n"
                << "Actual: " << actual.substr(diff, diff_end - diff) << "\n";
        }
    };

    compare_srcs(source.str(), correct_source);

    std::ostringstream function_info;
    {
        CodeGen_C cg(function_info, Target("host-no_runtime"), CodeGen_C::CPlusPlusFunctionInfoHeader, "Function/Info/Test");
        cg.compile(m);
    }

    std::string correct_function_info = normalize_line_endings(R"GOLDEN_CODE(#ifndef HALIDE_FUNCTION_INFO__Function___Info___Test
#define HALIDE_FUNCTION_INFO__Function___Info___Test

/* MACHINE GENERATED By Halide. */

#if !(__cplusplus >= 201703L || _MSVC_LANG >= 201703L)
#error "This file requires C++17 or later; please upgrade your compiler."
#endif

#include "HalideRuntime.h"


/**
 * This function returns a constexpr array of information about a Halide-generated
 * function's argument signature (e.g., number of arguments, type of each, etc).
 * While this is a subset of the information provided by the existing _metadata
 * function, it has the distinct advantage of allowing one to use the information
 * it at compile time (rather than runtime). This can be quite useful for producing
 * e.g. automatic call wrappers, etc.
 *
 * For instance, to compute the number of Buffers in a Function, one could do something
 * like:
 *
 *      using namespace HalideFunctionInfo;
 *
 *      template<size_t arg_count>
 *      constexpr size_t count_buffers(const std::array<ArgumentInfo, arg_count> args) {
 *          size_t buffer_count = 0;
 *          for (const auto a : args) {
 *              if (a.kind == InputBuffer || a.kind == OutputBuffer) {
 *                  buffer_count++;
 *              }
 *          }
 *          return buffer_count;
 *      }
 *
 *      constexpr size_t count = count_buffers(metadata_tester_argument_info());
 *
 * The value of `count` will be computed entirely at compile-time, with no runtime
 * impact aside from the numerical value of the constant.
 */

inline constexpr std::array<::HalideFunctionInfo::ArgumentInfo, 4> test1_argument_info() {
 return {{
  {"buf", ::HalideFunctionInfo::OutputBuffer, 3, halide_type_t{halide_type_int, 32}},
  {"alpha", ::HalideFunctionInfo::InputScalar, 0, halide_type_t{halide_type_float, 32}},
  {"beta", ::HalideFunctionInfo::InputScalar, 0, halide_type_t{halide_type_int, 32}},
  {"__user_context", ::HalideFunctionInfo::InputScalar, 0, halide_type_t{halide_type_handle, 64}},
 }};
}
#endif
)GOLDEN_CODE");

    compare_srcs(function_info.str(), correct_function_info);

    printf("Success!\n");
    return 0;
}
