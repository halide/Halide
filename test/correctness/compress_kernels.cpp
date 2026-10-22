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

// Decode the first n bytes of the LLVM IR string constant (c"...") that
// starts at pos, in which non-printable bytes are written as \XX.
std::vector<uint8_t> ll_string_bytes(const std::string &ll, size_t pos, int n) {
    std::vector<uint8_t> bytes;
    pos += 2;  // c"
    while ((int)bytes.size() < n && pos < ll.size() && ll[pos] != '"') {
        if (ll[pos] == '\\') {
            bytes.push_back((uint8_t)std::stoi(ll.substr(pos + 1, 2), nullptr, 16));
            pos += 3;
        } else {
            bytes.push_back((uint8_t)ll[pos]);
            pos += 1;
        }
    }
    return bytes;
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
                // The first MSZIP block: a u32 size below 64KB, and the "CK" signature.
                std::vector<uint8_t> first_block;
                size_t at = ll.find("gpu_compressed_kernels.data");
                if (at != std::string::npos) {
                    at = ll.find("c\"", at);
                }
                if (at != std::string::npos) {
                    first_block = ll_string_bytes(ll, at, 6);
                }
                if (first_block.size() != 6 || first_block[2] != 0 || first_block[3] != 0 ||
                    first_block[4] != 'C' || first_block[5] != 'K') {
                    printf("%s: compressed kernels do not start with an MSZIP block\n", what.c_str());
                    ok = false;
                }
                // The autolink of the Windows Compression API.
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
