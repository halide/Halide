#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <regex>
#include <sstream>

using namespace Halide;

// Large constant lookup tables are placed away from page edges on Apple arm64
// targets (see CodeGen_LLVM::place_lookup_tables). Check the placement in the
// generated IR, and check that the lookups still produce the right values.

namespace {

constexpr int page = 16384, margin = 4096;

// A pipeline that looks up a value in each of some tables of the given sizes
// (in bytes).
Func make_pipeline(ImageParam &idx, const std::vector<int> &sizes) {
    Var x("x");
    Func f("f");
    Expr e = cast<uint16_t>(0);
    for (size_t t = 0; t < sizes.size(); t++) {
        Buffer<uint16_t> lut(sizes[t] / 2, "lut" + std::to_string(t));
        for (int i = 0; i < lut.width(); i++) {
            lut(i) = (uint16_t)(i * 37 + 11 * t + 1);
        }
        e += lut(cast<int>(idx(x)) % lut.width());
    }
    f(x) = e;
    return f;
}

std::string compile_to_ll(Func f, const std::vector<Argument> &args, const Target &t) {
    const std::string file = Internal::get_test_tmp_dir() + "constant_blob_placement.ll";
    Internal::ensure_no_file_exists(file);
    f.compile_to_llvm_assembly(file, args, "placement_pipeline", t.with_feature(Target::NoRuntime));
    Internal::assert_file_exists(file);
    std::ifstream in(file);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Check that the IR has a page-aligned region with the given number of
// tables, each of which keeps the margin from any page boundary it doesn't
// cross, or no such region if num_tables is zero.
bool check_region(const std::string &ll, int num_tables, int expected_size, const char *what) {
    std::smatch m;
    if (!std::regex_search(ll, m, std::regex("\n@halide_lookup_tables = private constant <\\{[^}]*\\}> <\\{(.*)\\}>, section \"__TEXT,__halide_const\", align 16384\n"))) {
        if (num_tables == 0 && ll.find("__halide_const") == std::string::npos) {
            return true;
        }
        printf("%s: lookup table region %s\n", what, num_tables ? "not found" : "unexpected");
        return false;
    }
    if (num_tables == 0) {
        printf("%s: unexpected lookup table region\n", what);
        return false;
    }
    std::string fields = m[1].str();
    std::regex field("\\[(\\d+) x i8\\] (zeroinitializer|c\")");
    int pos = 0, tables = 0;
    for (auto it = std::sregex_iterator(fields.begin(), fields.end(), field); it != std::sregex_iterator(); it++) {
        const int size = std::stoi((*it)[1].str());
        if ((*it)[2].str() != "zeroinitializer") {
            tables++;
            const int begin = pos % page, last = (pos + size - 1) % page;
            if (begin < margin || last >= page - margin) {
                printf("%s: table at [%d, %d) of the region is too close to a page edge\n", what, pos, pos + size);
                return false;
            }
        }
        pos += size;
    }
    if (tables != num_tables || pos != expected_size) {
        printf("%s: found %d tables in %d bytes instead of %d tables in %d bytes\n",
               what, tables, pos, num_tables, expected_size);
        return false;
    }
    return true;
}

bool check(const char *target, const std::vector<int> &sizes, int num_tables, int expected_size) {
    ImageParam idx(UInt(16), 1, "idx");
    Func f = make_pipeline(idx, sizes);
    return check_region(compile_to_ll(f, {idx}, Target(target)), num_tables, expected_size, target);
}

}  // namespace

int main(int argc, char **argv) {
    // Tables of more than 1 KB are placed. Two small ones fit in one page. A
    // 12 KB one can't fit in a page with the margins, so it must cross into
    // the next one.
    if (!check("arm-64-macos", {4096}, 1, 8192) ||
        !check("arm-64-ios", {2048, 1024, 4096}, 2, 10240) ||
        !check("arm-64-macos", {12288, 4096, 2048, 512}, 3, 26624) ||
        !check("arm-64-macos", {16384, 32768}, 2, 53248) ||
        // Small tables don't need it.
        !check("arm-64-macos", {1024, 512}, 0, 0) ||
        // Only Apple arm64 targets have the problem.
        !check("arm-64-linux", {4096}, 0, 0) ||
        !check("x86-64-macos", {4096}, 0, 0)) {
        return 1;
    }

    // GPU kernel sources are embedded as buffers too, but aren't lookup tables.
    {
        ImageParam in(Float(32), 1, "in");
        Var x("x"), xo("xo"), xi("xi");
        Func g("g");
        g(x) = in(x) * 2.0f + sqrt(in(x)) + sin(in(x)) * cos(in(x));
        g.gpu_tile(x, xo, xi, 64);
        const std::string ll = compile_to_ll(g, {in}, Target("arm-64-macos-metal"));
        if (ll.find("_gpu_source_kernels") == std::string::npos ||
            !check_region(ll, 0, 0, "arm-64-macos-metal")) {
            return 1;
        }
    }

    // The data must still be read from the right place.
    const std::vector<int> sizes = {12288, 4096, 2048, 512, 16384};
    ImageParam idx(UInt(16), 1, "idx");
    Func f = make_pipeline(idx, sizes);
    Buffer<uint16_t> in(8192);
    for (int i = 0; i < in.width(); i++) {
        in(i) = (uint16_t)((i * 7919) % 8192);
    }
    idx.set(in);
    Buffer<uint16_t> out = f.realize({in.width()});
    for (int i = 0; i < in.width(); i++) {
        uint16_t correct = 0;
        for (size_t t = 0; t < sizes.size(); t++) {
            correct += (uint16_t)((in(i) % (sizes[t] / 2)) * 37 + 11 * t + 1);
        }
        if (out(i) != correct) {
            printf("out(%d) = %d instead of %d\n", i, out(i), correct);
            return 1;
        }
    }

    printf("Success!\n");
    return 0;
}
