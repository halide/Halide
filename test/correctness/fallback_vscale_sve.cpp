#include "Halide.h"
#include "parse_llvm_ir.h"
#include <fstream>
#include <regex>
#include <string_view>

using namespace Halide;

bool starts_with(std::string_view str, std::string_view prefix) {
    return str.size() >= prefix.size() &&
           str.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(std::string_view str, std::string_view suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool compile_and_check_vscale(Func &f,
                              const std::string &name,
                              const std::string &suffix,
                              const Target &t,
                              int exp_vscale,
                              const std::string &exp_intrin) {

    // Look into llvm-ir and check function attributes for vscale_range
    auto llvm_file_name = name + ".ll";
    f.compile_to_llvm_assembly(llvm_file_name, f.infer_arguments(), t);

    int act_vscale = 0;
    bool intrin_found = false;
    std::regex vscale_regex(R"(vscale_range\(\s*([0-9]+)\s*,\s*([0-9]+)\s*\))");

    for (auto &[func_name, attrs_line] : parse_llvm_ir_attributes_from_file(llvm_file_name)) {
        if (starts_with(func_name, name) && ends_with(func_name, suffix)) {
            // Check vscale_range
            std::smatch match;
            if (std::regex_search(attrs_line, match, vscale_regex) &&
                match[1] == match[2]) {
                act_vscale = std::stoi(match[1]);
            }
        } else if (func_name.find(exp_intrin) != std::string::npos) {
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
    std::stringstream name_ss;
    name_ss << "test_vscale_v" << vectorization_factor
            << "_vb_" << vector_bits;
    const std::string name = name_ss.str();

    Func f(name);
    f(x, y) = absd(x, y);
    f.compute_root().vectorize(x, vectorization_factor);

    Target t("arm-64-linux-sve2-no_asserts-no_runtime-no_bounds_query");
    t.vector_bits = vector_bits;

    // sve or neon
    std::string intrin = exp_vscale > 0 ? "llvm.aarch64.sve.sabd" : "llvm.aarch64.neon.sabd";

    return compile_and_check_vscale(f, name, "", t, exp_vscale, intrin);
}

bool test_streaming_vscale(int vectorization_factor, int vector_bits, int streaming_vector_bits, int exp_vscale) {
    std::stringstream name_ss;
    name_ss << "test_vscale_v" << vectorization_factor
            << "_vb_" << vector_bits
            << "_svb_" << streaming_vector_bits;
    const std::string name = name_ss.str();

    Func f(name);
    f(x, y) = absd(x, y);
    f.compute_root()
        .sme_streaming()  // This extracts streaming task
        .vectorize(x, vectorization_factor);

    Target t("arm-64-linux-no_asserts-no_runtime-no_bounds_query");
    if (vector_bits != 0) {
        t = t.with_feature(Target::SVE2);
        t.vector_bits = vector_bits;
    }
    if (streaming_vector_bits != 0) {
        t = t.with_feature(Target::SME2);
        Target::Feature sme_svl = Target::sme_svl_feature_from_bits(streaming_vector_bits);
        if (sme_svl == Target::FeatureEnd) {
            printf("[%s] Unsupported streaming_vector_bits %d\n", name.c_str(), streaming_vector_bits);
            return false;
        }
        t.set_feature(sme_svl);
    }

    // sve or neon
    std::string intrin = exp_vscale > 0 ? "llvm.aarch64.sve.sabd" : "llvm.aarch64.neon.sabd";

    // Check func for streaming task
    return compile_and_check_vscale(f, name, "_streaming_task", t, exp_vscale, intrin);
}

int main(int argc, char **argv) {

    bool ok = true;

    ok &= test_vscale(4, 128, 1);  // Regular case: <vscale x 4 x ty> with vscale=1
    ok &= test_vscale(3, 128, 0);  // Fallback due to odd vectorization factor
    ok &= test_vscale(8, 512, 4);  // Regular case: <vscale x 2 x ty> with vscale=4
    ok &= test_vscale(4, 512, 0);  // Fallback due to <vscale x 1 x ty>

    // Regular case: <vscale x 2 x ty> with streaming_vscale=4
    ok &= test_streaming_vscale(8, 128, 512, 4);

    // Fallback to non-streaming SVE due to <vscale x 1 x ty>
    // <vscale x 4 x ty> with vscale=1
    ok &= test_streaming_vscale(4, 128, 512, 1);

    // Fallback to non-streaming SVE due to <vscale x 0.5 x ty>
    // And then fallback to NEON due to <vscale x 1 x ty>
    ok &= test_streaming_vscale(2, 256, 512, 0);

    // Fallback to non-streaming SVE due to <vscale x 1 x ty>
    // But the target does not have non-streaming SVE2 feature
    ok &= test_streaming_vscale(4, 0, 512, 0);

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
