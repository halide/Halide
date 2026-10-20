#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <fstream>
#include <sstream>

using namespace Halide;

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool check(const std::string &what, const std::string &text, const std::string &needle, bool expected) {
    bool found = text.find(needle) != std::string::npos;
    if (found != expected) {
        printf("%s: expected \"%s\" to be %s\n", what.c_str(), needle.c_str(), expected ? "present" : "absent");
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    ImageParam input(Float(32), 2, "input");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");

    // Enough kernels to make the OpenCL source span several 32KB MSZIP chunks.
    std::vector<Func> fs;
    Func prev = BoundaryConditions::repeat_edge(input);
    for (int i = 0; i < 40; i++) {
        Func f("f" + std::to_string(i));
        f(x, y) = sin(prev(x, y)) * prev(x + 1, y) + cos(prev(x, y + 1)) / (prev(x - 1, y - 1) + 2.0f);
        f.compute_root().gpu_tile(x, y, xo, yo, xi, yi, 16, 16);
        fs.push_back(f);
        prev = f;
    }
    Func out = fs.back();

    const std::string dir = Internal::get_test_tmp_dir();
    bool ok = true;
    for (const char *os : {"linux", "windows"}) {
        for (bool compress : {false, true}) {
            Target t(std::string("x86-64-") + os + "-opencl");
            if (compress) {
                t.set_feature(Target::CompressKernels);
            }
            const std::string what = t.to_string();
            const std::string path = dir + "compress_kernels_" + os + (compress ? "_compressed" : "") + ".ll";
            out.compile_to_llvm_assembly(path, {input}, "compress_kernels", t);
            const std::string ll = read_file(path);

            // The OpenCL source is only readable when not compressed.
            ok &= check(what, ll, "__kernel void", !compress);
            ok &= check(what, ll, "opencl_gpu_compressed_kernels", compress);
            ok &= check(what, ll, "define weak i32 @halide_decompress_kernels(", compress);

            if (compress && os == std::string("windows")) {
                // MSZIP magic, and the autolink of the Windows Compression API.
                ok &= check(what, ll, "c\"\\0AQ\\E5\\C0\\18\\00", true);
                ok &= check(what, ll, "!\"/DEFAULTLIB:cabinet.lib\"", true);
                ok &= check(what, ll, "@Decompress(", true);
            } else if (compress) {
                // zlib header for the best compression level.
                ok &= check(what, ll, "c\"x\\DA", true);
                ok &= check(what, ll, "@uncompress(", true);
            }
        }
    }

    if (!ok) {
        return 1;
    }
    printf("Success!\n");
    return 0;
}
