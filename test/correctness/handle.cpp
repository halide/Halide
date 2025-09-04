#include "Halide.h"
#include <gtest/gtest.h>

using namespace Halide;

namespace {
// Make a custom strlen so that it always returns a 32-bit int,
// instead of switching based on bit-width.
extern "C" HALIDE_EXPORT_SYMBOL int my_strlen(const char *c) {
    int l = 0;
    while (*c) {
        c++;
        l++;
    }
    return l;
}

HalideExtern_1(int, my_strlen, const char *);

class HandleTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (get_jit_target_from_environment().arch == Target::WebAssembly) {
            GTEST_SKIP() << "WebAssembly JIT does not support Param<> for pointer types.";
        }
    }
};
}  // namespace

TEST_F(HandleTest, PassHandleToExternFunction) {
    // Check we can pass a Handle through to an extern function.
    const char *c_message = "Hello, world!";

    Param<const char *> message;
    message.set(c_message);

    int result = evaluate<int>(my_strlen(message));

    int correct = my_strlen(c_message);
    EXPECT_EQ(result, correct) << "strlen(" << c_message << ")";
}

TEST_F(HandleTest, HandleStorageAsUint64) {
    // Check that storing and loading handles acts like uint64_t
    std::string msg = "hello!\n";
    Func f, g, h;
    Var x;
    f(x) = cast<char *>(msg);
    f.compute_root().vectorize(x, 4);
    g(x) = f(x);
    g.compute_root();
    h(x) = g(x);

    Buffer<char *> im = h.realize({100});

    uint64_t handle = (uint64_t)(im(0));
    if (sizeof(char *) == 4) {
        // On 32-bit systems, the upper four bytes should be zero
        EXPECT_EQ(handle >> 32, 0) << "The upper four bytes of a handle should have been zero on a 32-bit system";
    }
    // As another sanity check, the internal pointer to the string constant should be aligned.
    EXPECT_EQ(handle & 0x3, 0) << "Got handle: " << std::hex << handle << ". A handle should be aligned to at least four bytes";

    for (int i = 0; i < im.width(); i++) {
        EXPECT_EQ(im(i), im(0)) << "im(" << i << ")";
        EXPECT_EQ(std::string(im(i)), msg) << "Handle string content at index " << i;
    }
}
