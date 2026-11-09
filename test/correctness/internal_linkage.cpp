#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

int main(int argc, char **argv) {
    ImageParam input(UInt(8), 2, "input");
    Var x("x"), y("y");
    Func f("f");
    f(x, y) = input(x, y) * 2;
    f.parallel(y);

    // The closure for the parallel loop is an internal function, so it must
    // not be visible outside of the object file.
    const std::string ll = Internal::get_test_tmp_dir() + "internal_linkage.ll";
    Internal::ensure_no_file_exists(ll);
    f.compile_to_llvm_assembly(ll, {input}, "linkage_pipeline", get_host_target().with_feature(Target::NoRuntime));
    Internal::assert_file_exists(ll);

    std::ifstream in(ll);
    std::stringstream ss;
    ss << in.rdbuf();

    bool found_closure = false, found_entry = false;
    std::string line;
    while (std::getline(ss, line)) {
        if (line.rfind("define ", 0) != 0) {
            continue;
        }
        const bool is_internal = line.find("define internal ") == 0;
        if (line.find("@linkage_pipeline_par_for_") != std::string::npos) {
            found_closure = true;
            if (!is_internal) {
                printf("Parallel closure is not internal:\n%s\n", line.c_str());
                return 1;
            }
        } else if (line.find("@linkage_pipeline(") != std::string::npos) {
            found_entry = true;
            if (is_internal) {
                printf("Entry point is internal:\n%s\n", line.c_str());
                return 1;
            }
        }
    }
    if (!found_closure || !found_entry) {
        printf("Did not find the %s in %s\n", found_closure ? "entry point" : "parallel closure", ll.c_str());
        return 1;
    }

    printf("Success!\n");
    return 0;
}
