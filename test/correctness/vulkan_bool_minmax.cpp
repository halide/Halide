#include "Halide.h"
#include "SpirvIR.h"
#include "halide_test_dirs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;
namespace fs = std::filesystem;

void set_dump_name(const std::string &name) {
#ifdef _WIN32
    internal_assert(_putenv_s("HL_SPIRV_DUMP_FILE", name.c_str()) == 0);
#else
    internal_assert(setenv("HL_SPIRV_DUMP_FILE", name.c_str(), 1) == 0);
#endif
}

void check_case(const std::string &name, Type input_type, bool use_min,
                bool boolean_result, uint32_t expected_opcode) {
    Var x("x"), xo("xo"), xi("xi");
    ImageParam input(input_type, 1, "input");
    Expr a = input(x), b = input(x + 1);
    if (boolean_result) {
        a = a > 0;
        b = b > 0;
    }
    Func output(name);
    Expr value = use_min ? min(a, b) : max(a, b);
    output(x) = boolean_result ? select(value, 1, 0) : value;
    output.gpu_tile(x, xo, xi, 32);

    for (const auto &entry : fs::directory_iterator(".")) {
        const fs::path path = entry.path();
        if (path.extension() == ".spv" && path.filename().string().rfind(name + "_k", 0) == 0) {
            fs::remove(path);
        }
    }
    set_dump_name(name + ".spv");
    output.compile_to_object(get_test_tmp_dir() + name + ".obj", {input}, name,
                             Target("host-vulkan-vk_v10"));

    int modules = 0;
    for (const auto &entry : fs::directory_iterator(".")) {
        const fs::path path = entry.path();
        if (path.extension() != ".spv" || path.filename().string().rfind(name + "_k", 0) != 0) {
            continue;
        }
        ++modules;
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        file.close();
        internal_assert(bytes.size() % sizeof(uint32_t) == 0);
        std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
        std::memcpy(words.data(), bytes.data(), bytes.size());
        internal_assert(words.size() >= 5 && words[0] == SpvMagicNumber);

        bool found = false;
        uint32_t bool_type_id = 0;
        for (size_t i = 5; i < words.size();) {
            const uint32_t length = words[i] >> 16;
            const uint32_t opcode = words[i] & 0xffff;
            internal_assert(length && i + length <= words.size());
            if (opcode == SpvOpTypeBool && length >= 2) bool_type_id = words[i + 1];
            if (boolean_result) {
                if (opcode == expected_opcode) found = true;
                if (opcode == SpvOpExtInst && length >= 5 &&
                    words[i + 1] == bool_type_id &&
                    (words[i + 4] == GLSLstd450UMax || words[i + 4] == GLSLstd450UMin)) {
                    internal_error << "Boolean min/max emitted integer-only GLSL instruction\n";
                }
            } else if (opcode == SpvOpExtInst && length >= 5 &&
                       words[i + 4] == expected_opcode) {
                found = true;
            }
            i += length;
        }
        internal_assert(found) << "Missing expected SPIR-V operation in " << path << "\n";
        fs::remove(path);
    }
    internal_assert(modules > 0) << "No SPIR-V dump for " << name << "\n";
}

int main(int argc, char **argv) {
    check_case("bool_max", Int(32), false, true, SpvOpLogicalOr);
    check_case("bool_min", Int(32), true, true, SpvOpLogicalAnd);
    check_case("uint_max", UInt(32), false, false, GLSLstd450UMax);
    check_case("uint_min", UInt(32), true, false, GLSLstd450UMin);
    check_case("int_max", Int(32), false, false, GLSLstd450SMax);
    check_case("int_min", Int(32), true, false, GLSLstd450SMin);
    check_case("float_max", Float(32), false, false, GLSLstd450FMax);
    check_case("float_min", Float(32), true, false, GLSLstd450FMin);
    puts("Success!");
    return 0;
}
