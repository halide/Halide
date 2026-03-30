#include "Halide.h"
#include <fstream>
#include <regex>

using namespace Halide;

bool compile_and_check_vscale(Func &f,
                              const std::string &name,
                              const Target &t,
                              int exp_vscale,
                              const std::string &exp_intrin) {

    // Look into llvm-ir and check function attributes for vscale_range
    auto llvm_file_name = name + ".ll";
    f.compile_to_llvm_assembly(llvm_file_name, f.infer_arguments(), t);

    Internal::assert_file_exists(llvm_file_name);
    std::ifstream llvm_file;
    llvm_file.open(llvm_file_name);
    std::string line;
    // Pattern to extract "n" and "m" in "vscale_range(n,m)"
    std::regex vscale_regex(R"(vscale_range\(\s*([0-9]+)\s*,\s*([0-9]+)\s*\))");

    int act_vscale = 0;
    bool intrin_found = false;

    while (getline(llvm_file, line)) {
        // Check vscale_range
        std::smatch match;
        if (std::regex_search(line, match, vscale_regex) && match[1] == match[2]) {
            act_vscale = std::stoi(match[1]);
        }
        // Check intrin
        if (line.find(exp_intrin) != std::string::npos) {
            intrin_found = true;
        }
    }

    if (act_vscale != exp_vscale) {
        printf("[%s] Found vscale_range %d, while expected %d\n", name.c_str(), act_vscale, exp_vscale);
        return false;
    }
    if (!intrin_found) {
        printf("[%s] Cannot find expected intrin %s\n", name.c_str(), exp_intrin.c_str());
        return false;
    }
    return true;
}

Var x("x"), y("y");

bool test_vscale(int vectorization_factor, int vector_bits, int exp_vscale) {
    Func f("f");
    f(x, y) = absd(x, y);
    f.compute_root().vectorize(x, vectorization_factor);

    Target t("arm-64-linux-sve2-no_asserts-no_runtime-no_bounds_query");
    t.vector_bits = vector_bits;

    std::stringstream name;
    name << "test_vscale_v" << vectorization_factor << "_vector_bits_" << vector_bits;

    // sve or neon
    std::string intrin = exp_vscale > 0 ? "llvm.aarch64.sve.sabd" : "llvm.aarch64.neon.sabd";

    return compile_and_check_vscale(f, name.str(), t, exp_vscale, intrin);
}

int main(int argc, char **argv) {

    bool ok = true;

    ok &= test_vscale(4, 128, 1);  // Regular case: <vscale x 4 x ty> with vscale=1
    ok &= test_vscale(3, 128, 0);  // Fallback due to odd vectorization factor
    ok &= test_vscale(8, 512, 4);  // Regular case: <vscale x 2 x ty> with vscale=4
    ok &= test_vscale(4, 512, 0);  // Fallback due to <vscale x 1 x ty>

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
