#include "Halide.h"
#include "halide_test_error.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace Halide;

// This test exercises the ability to override halide_get_library_symbol (etc)
// when using JIT code; to do so, it compiles & calls a simple pipeline
// using an OpenCL schedule, since that is known to use these calls
// in a (reasonably) well-defined way and is unlikely to change a great deal
// in the near future; additionally, it doesn't require a particular
// feature in LLVM (unlike, say, Hexagon).

namespace {

int load_library_calls = 0;
int get_library_symbol_calls = 0;

struct LoadLibraryUserContext : JITUserContext {
    std::ostringstream error;

    LoadLibraryUserContext() {
        handlers.custom_error = my_error_handler;
    }

    static void my_error_handler(JITUserContext *u, const char *msg) {
        auto *self = static_cast<LoadLibraryUserContext *>(u);
        self->error << msg;
    }
};

void *my_get_symbol_impl(const char *name) {
    FAIL() << "Saw unexpected call: get_symbol(" << name << ")", nullptr;
}

void *my_load_library_impl(const char *name) {
    load_library_calls++;
    if (!strstr(name, "OpenCL") && !strstr(name, "opencl")) {
        FAIL() << "Saw unexpected call: load_library(" << name << ")", nullptr;
    }
    return nullptr;
}

void *my_get_library_symbol_impl(void *lib, const char *name) {
    get_library_symbol_calls++;
    if (lib != nullptr || strcmp(name, "clGetPlatformIDs") != 0) {
        FAIL() << "Saw unexpected call: get_library_symbol(" << lib << ", " << name << ")", nullptr;
    }
    return nullptr;
}

class LoadLibraryTest : public ::testing::Test {
protected:
    const Target target{get_jit_target_from_environment()};
    LoadLibraryUserContext user_context;
    void SetUp() override {
        if (!target.has_feature(Target::OpenCL)) {
            GTEST_SKIP() << "OpenCL not enabled.";
        }
        load_library_calls = 0;
        get_library_symbol_calls = 0;
    }
};
}  // namespace

TEST_F(LoadLibraryTest, OpenCL) {
    Var x, y, xi, yi;
    Func f;
    f(x, y) = cast<int32_t>(x + y);
    f.gpu_tile(x, y, xi, yi, 8, 8, TailStrategy::Auto, DeviceAPI::OpenCL);

    JITHandlers handlers;
    handlers.custom_get_symbol = my_get_symbol_impl;
    handlers.custom_load_library = my_load_library_impl;
    handlers.custom_get_library_symbol = my_get_library_symbol_impl;
    Internal::JITSharedRuntime::set_default_handlers(handlers);

    Buffer<int32_t> out = f.realize(&user_context, {64, 64}, target);
    EXPECT_GE(load_library_calls, 1);
    EXPECT_GE(get_library_symbol_calls, 1);
    EXPECT_THAT(user_context.error.str(), testing::HasSubstr("OpenCL API not found"));
}
