#include "Halide.h"
#include "halide_test_dirs.h"

#define SPV_ENABLE_UTILITY_CODE
#include <spirv/unified1/spirv.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
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

void check_module(const fs::path &path, uint32_t expected_opcode) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    file.close();
    internal_assert(bytes.size() % sizeof(uint32_t) == 0);
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    internal_assert(words.size() >= 5 && words[0] == SpvMagicNumber);

    std::set<uint32_t> bool_types;
    std::map<uint32_t, uint32_t> type_of;
    bool found = false;
    for (size_t i = 5; i < words.size();) {
        const uint32_t length = words[i] >> 16;
        const SpvOp opcode = (SpvOp)(words[i] & 0xffff);
        internal_assert(length && i + length <= words.size());

        if (opcode == SpvOpTypeBool && length >= 2) {
            bool_types.insert(words[i + 1]);
        } else if (opcode == SpvOpTypeVector && length >= 4 && bool_types.count(words[i + 2])) {
            bool_types.insert(words[i + 1]);
        }
        bool has_result = false, has_result_type = false;
        SpvHasResultAndType(opcode, &has_result, &has_result_type);
        if (has_result && has_result_type && length >= 3) {
            type_of[words[i + 2]] = words[i + 1];
        }

        if (opcode == expected_opcode) {
            found = true;
        }
        if ((opcode == SpvOpIEqual || opcode == SpvOpINotEqual) && length >= 5) {
            for (int operand = 3; operand <= 4; operand++) {
                auto it = type_of.find(words[i + operand]);
                if (it != type_of.end() && bool_types.count(it->second)) {
                    internal_error << "Integer comparison with a boolean operand in " << path << "\n";
                }
            }
        }
        i += length;
    }
    internal_assert(found) << "Missing expected SPIR-V comparison in " << path << "\n";
}

void check_case(const std::string &name, bool not_equal, int vector_width,
                uint32_t expected_opcode) {
    Var x("x"), xo("xo"), xi("xi");
    ImageParam input(Int(32), 1, "input");
    Expr a = input(x) > 0, b = input(x + 1) > 0;
    Func output(name);
    output(x) = not_equal ? cast<int32_t>(a != b) : select(a == b, 1, 0);
    output.gpu_tile(x, xo, xi, 32);
    if (vector_width > 1) {
        output.vectorize(xi, vector_width);
    }

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
        check_module(path, expected_opcode);
        fs::remove(path);
    }
    internal_assert(modules > 0) << "No SPIR-V dump for " << name << "\n";
}

int main(int argc, char **argv) {
    check_case("bool_eq", false, 1, SpvOpLogicalEqual);
    check_case("bool_ne", true, 1, SpvOpLogicalNotEqual);
    check_case("bool_eq_vector", false, 4, SpvOpLogicalEqual);
    check_case("bool_ne_vector", true, 4, SpvOpLogicalNotEqual);
    puts("Success!");
    return 0;
}
