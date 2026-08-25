#include "Halide.h"
#include "expect_user_error.h"

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

using namespace Halide;

namespace {

bool check(bool condition, const char *message) {
    if (!condition) {
        std::printf("FAIL: %s\n", message);
    }
    return condition;
}

size_t vector_size(const std::vector<Func> &funcs) {
    return funcs.size();
}

}  // namespace

int main(int argc, char **argv) {
    static_assert(std::is_base_of_v<std::vector<Func>, FuncVec>);
    static_assert(std::is_convertible_v<FuncVec, Func>);

    bool success = true;

    FuncVec named("func_vec_stage_", 3);
    success &= check(named.size() == 3, "named constructor created the wrong number of Funcs");
    for (size_t i = 0; i < named.size(); ++i) {
        const std::string expected = "func_vec_stage_" + std::to_string(i);
        success &= check(named[i].name() == expected, "named constructor created an incorrect Func name");
    }

    Func a("func_vec_a");
    Func b("func_vec_b");
    FuncVec funcs{a};
    funcs.push_back(b);
    success &= check(vector_size(funcs) == 2, "FuncVec is not usable as a std::vector<Func>");

    std::vector<Func> base{a};
    FuncVec copied(base);
    FuncVec assigned;
    assigned = base;
    std::vector<Func> round_trip = copied;
    success &= check(assigned.size() == 1 && round_trip.size() == 1,
                     "FuncVec conversion to or from std::vector<Func> failed");

    Func singleton = copied;
    success &= check(singleton.name() == a.name(), "singleton FuncVec converted to the wrong Func");

    Pipeline pipeline(copied);
    Func pipeline_output = pipeline.outputs();
    success &= check(pipeline_output.name() == a.name(), "Pipeline::outputs() did not decay to its singleton Func");

#if HALIDE_WITH_EXCEPTIONS
    FuncVec empty;
    success &= expect_user_error("empty_func_vec", "size 0", [&]() {
        Func f = empty;
        (void)f;
    });
    success &= expect_user_error("multi_func_vec", "size 2", [&]() {
        Func f = funcs;
        (void)f;
    });
#endif

    if (!success) {
        return 1;
    }
    std::printf("Success!\n");
    return 0;
}
