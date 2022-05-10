#include "HalideBuffer.h"
#include "HalideRuntime.h"
#include "multitarget.h"
#include <atomic>
#include <string>
#include <tuple>

using namespace Halide::Runtime;

void my_error_handler(void *user_context, const char *message) {
    // Don't use the word "error": if CMake sees it in the output
    // from an add_custom_command() on Windows, it can decide that
    // the command failed, regardless of error code.
    printf("Saw: (%s)\n", message);
}

std::pair<std::string, bool> get_env_variable(char const *env_var_name) {
    if (env_var_name) {
        size_t read = 0;
#ifdef _MSC_VER
        char lvl[32];
        if (getenv_s(&read, lvl, env_var_name) != 0) read = 0;
#else
        char *lvl = getenv(env_var_name);
        read = (lvl) ? 1 : 0;
#endif
        if (read) {
            return {std::string(lvl), true};
        }
    }
    return {"", false};
}

bool use_noboundsquery_feature() {
    auto [value, read] = get_env_variable("HL_MULTITARGET_TEST_USE_NOBOUNDSQUERY_FEATURE");
    if (!read) {
        return false;
    }
    return std::stoi(value) != 0;
}

static std::atomic<int> can_use_count{0};

int my_can_use_target_features(int count, const uint64_t *features) {
    can_use_count += 1;
    const int word = halide_target_feature_no_bounds_query / 64;
    const int bit = halide_target_feature_no_bounds_query % 64;
    if (features[word] & (1ULL << bit)) {
        if (use_noboundsquery_feature()) {
            return 1;
        } else {
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    const int W = 32, H = 32;
    Buffer<uint32_t, 2> output(W, H);

    halide_set_error_handler(my_error_handler);
    halide_set_custom_can_use_target_features(my_can_use_target_features);

    const auto *md = HalideTest::multitarget_metadata();
    printf("Observed metadata target is: %s\n", md->target);

    // multitarget_metadata() also uses can_use_target and will return
    // a pointer to metadata that contains the actual Target string of the
    // filter we're going to execute. Verify that it is what we expect.
    bool expected_nbq = use_noboundsquery_feature();
    bool actual_nbq = strstr(md->target, "no_bounds_query") != nullptr;
    if (expected_nbq != actual_nbq) {
        printf("Expected to have no_bounds_query=%d, but actually ran with no_bounds_query=%d\n", expected_nbq, actual_nbq);
        return -1;
    }

    const std::string expected_name = "multitarget";
    if (md->name != expected_name) {
        printf("Expected metadata name to be '%s', but saw '%s'\n", expected_name.c_str(), md->name);
        return -1;
    }

    if (HalideTest::multitarget(output) != 0) {
        printf("Error at multitarget\n");
        return -1;
    }

    // Verify output.
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint32_t expected = use_noboundsquery_feature() ? 0xdeadbeef : 0xf00dcafe;
            const uint32_t actual = output(x, y);
            if (actual != expected) {
                printf("Error at %d, %d: expected %x, got %x\n", x, y, expected, actual);
                return -1;
            }
        }
    }

    // halide_can_use_target_features() should be called exactly twice (once
    // for the real call, once for the call to multitarget_metadata()), with the
    // results cached; call these a few more times to verify.
    for (int i = 0; i < 10; ++i) {
        if (HalideTest::multitarget(output) != 0) {
            printf("Error at multitarget\n");
            return -1;
        }
        (void)HalideTest::multitarget_metadata();
    }
    if (can_use_count != 2) {
        printf("Error: halide_can_use_target_features was called %d times!\n", (int)can_use_count);
        return -1;
    }

    {
        // Verify that the multitarget wrapper code propagates nonzero error
        // results back to the caller properly.
        Buffer<uint8_t, 2> bad_type(W, H);
        int result = HalideTest::multitarget(bad_type);
        if (result != halide_error_code_bad_type) {
            printf("Error: expected to fail with halide_error_code_bad_type (%d) but actually got %d!\n", (int)halide_error_code_bad_type, result);
            return -1;
        }
    }

    printf("Saw %x for no_bounds_query=%d\n", output(0, 0), use_noboundsquery_feature());
    printf("Success!\n");
    return 0;
}
