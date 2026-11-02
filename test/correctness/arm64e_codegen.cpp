#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Halide;

namespace {

uint32_t read_u32_le(const std::vector<char> &bytes, size_t offset) {
    uint32_t val = 0;
    val |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 0])) << 0;
    val |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 1])) << 8;
    val |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 2])) << 16;
    val |= static_cast<uint32_t>(static_cast<uint8_t>(bytes[offset + 3])) << 24;
    return val;
}

bool require_contains(const std::string &haystack, const std::string &needle,
                      const std::string &target_str, const char *kind) {
    if (haystack.find(needle) == std::string::npos) {
        fprintf(stderr, "[%s] Expected %s to contain '%s'\n",
                target_str.c_str(), kind, needle.c_str());
        return false;
    }
    return true;
}

bool check_macho_header(const std::vector<char> &obj_bytes,
                        const std::string &target_str,
                        uint32_t expected_cpusubtype) {
    if (obj_bytes.size() < 12) {
        fprintf(stderr, "[%s] Object file too small (%zu bytes)\n",
                target_str.c_str(), obj_bytes.size());
        return false;
    }

    constexpr uint32_t kMachOMagic64 = 0xfeedfacfu;
    constexpr uint32_t kCpuTypeArm64 = 0x0100000cu;
    const uint32_t magic = read_u32_le(obj_bytes, 0);
    const uint32_t cputype = read_u32_le(obj_bytes, 4);
    const uint32_t cpusubtype = read_u32_le(obj_bytes, 8);

    if (magic != kMachOMagic64 || cputype != kCpuTypeArm64 || cpusubtype != expected_cpusubtype) {
        fprintf(stderr,
                "[%s] Unexpected Mach-O header: magic=0x%08x cputype=0x%08x cpusubtype=0x%08x "
                "(expected cpusubtype=0x%08x)\n",
                target_str.c_str(), magic, cputype, cpusubtype, expected_cpusubtype);
        return false;
    }
    return true;
}

int check_arm64e_target(const std::string &target_str, uint32_t expected_cpusubtype) {
    Target target(target_str);
    if (!target.supported()) {
        printf("[SKIP] Target %s is not supported in this build of Halide.\n", target_str.c_str());
        return 0;
    }

    const std::string tmp = Internal::get_test_tmp_dir();
    const std::string ll_path = tmp + "arm64e_codegen_" + target_str + ".ll";
    const std::string asm_path = tmp + "arm64e_codegen_" + target_str + ".s";
    const std::string obj_path = tmp + "arm64e_codegen_" + target_str + ".o";
    const std::string rt_obj_path = tmp + "arm64e_codegen_rt_" + target_str + ".o";

    Internal::ensure_no_file_exists(ll_path);
    Internal::ensure_no_file_exists(asm_path);
    Internal::ensure_no_file_exists(obj_path);
    Internal::ensure_no_file_exists(rt_obj_path);

    ImageParam in(Int(32), 2, "in");
    Var x("x"), y("y");
    Func f("f_arm64e");
    f(x, y) = in(x, y) * 3 + x + y;
    // Schedule a parallel loop with the linked Halide runtime so codegen emits
    // function-pointer signing (both inline paciza and @AUTH(ia,0) relocations)
    // and authenticated indirect calls (blraaz).
    f.parallel(y).vectorize(x, 4);

    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::llvm_assembly, ll_path},
        {OutputFileType::assembly, asm_path},
        {OutputFileType::object, obj_path},
    };
    f.compile_to(outputs, {in}, "f_arm64e", target);

    const std::vector<char> ll_bytes = Internal::read_entire_file(ll_path);
    const std::string ll_text(ll_bytes.begin(), ll_bytes.end());
    if (!require_contains(ll_text, "\"ptrauth-returns\"", target_str, "LLVM IR") ||
        !require_contains(ll_text, "\"ptrauth-calls\"", target_str, "LLVM IR") ||
        !require_contains(ll_text, "\"ptrauth-auth-traps\"", target_str, "LLVM IR") ||
        !require_contains(ll_text, "ptrauth (ptr @", target_str, "LLVM IR") ||
        !require_contains(ll_text, "[ \"ptrauth\"(i32 0, i64 0) ]", target_str, "LLVM IR")) {
        return 1;
    }

    const std::vector<char> asm_bytes = Internal::read_entire_file(asm_path);
    const std::string asm_text(asm_bytes.begin(), asm_bytes.end());
    if (!require_contains(asm_text, "pacibsp", target_str, "assembly") ||
        !require_contains(asm_text, "retab", target_str, "assembly") ||
        !require_contains(asm_text, "blraaz", target_str, "assembly") ||
        !require_contains(asm_text, "paciza", target_str, "assembly") ||
        !require_contains(asm_text, "@AUTH(ia,0)", target_str, "assembly")) {
        return 1;
    }

    const std::vector<char> obj_bytes = Internal::read_entire_file(obj_path);
    if (!check_macho_header(obj_bytes, target_str, expected_cpusubtype)) {
        return 1;
    }

    compile_standalone_runtime(rt_obj_path, target);
    const std::vector<char> rt_obj_bytes = Internal::read_entire_file(rt_obj_path);
    if (!check_macho_header(rt_obj_bytes, target_str + " (runtime)", expected_cpusubtype)) {
        return 1;
    }

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    constexpr uint32_t kCpuSubtypeArm64eVersioned = 0x80000002u;

    if (check_arm64e_target("arm-64-ios-arm64e", kCpuSubtypeArm64eVersioned) != 0 ||
        check_arm64e_target("arm-64-macos-arm64e", kCpuSubtypeArm64eVersioned) != 0) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
