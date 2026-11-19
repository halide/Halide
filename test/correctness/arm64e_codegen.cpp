#include "Halide.h"
#include "halide_test_dirs.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace Halide;

namespace {

uint32_t read_u32_le(const char *bytes, size_t offset) {
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

bool check_macho_header_span(const char *obj_bytes, size_t obj_size,
                             const std::string &label,
                             uint32_t expected_cpusubtype) {
    if (obj_size < 12) {
        fprintf(stderr, "[%s] Object file too small (%zu bytes)\n",
                label.c_str(), obj_size);
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
                label.c_str(), magic, cputype, cpusubtype, expected_cpusubtype);
        return false;
    }
    return true;
}

bool check_macho_header(const std::vector<char> &obj_bytes,
                        const std::string &label,
                        uint32_t expected_cpusubtype) {
    return check_macho_header_span(obj_bytes.data(), obj_bytes.size(), label, expected_cpusubtype);
}

bool check_archive_macho_members(const std::vector<char> &ar_bytes,
                                 const std::string &label,
                                 uint32_t expected_cpusubtype,
                                 size_t expected_obj_members) {
    if (ar_bytes.size() < 8 || std::memcmp(ar_bytes.data(), "!<arch>\n", 8) != 0) {
        fprintf(stderr, "[%s] Missing BSD archive magic\n", label.c_str());
        return false;
    }

    size_t offset = 8;
    size_t obj_members = 0;
    while (offset < ar_bytes.size()) {
        if (ar_bytes.size() - offset < 60) {
            fprintf(stderr, "[%s] Truncated archive member header at offset %zu\n",
                    label.c_str(), offset);
            return false;
        }
        const char *hdr = ar_bytes.data() + offset;
        if (hdr[58] != '`' || hdr[59] != '\n') {
            fprintf(stderr, "[%s] Invalid archive member fmag at offset %zu\n",
                    label.c_str(), offset);
            return false;
        }
        std::string size_str(hdr + 48, 10);
        const size_t payload_size = static_cast<size_t>(std::strtoull(size_str.c_str(), nullptr, 10));
        offset += 60;
        if (payload_size > ar_bytes.size() - offset) {
            fprintf(stderr, "[%s] Truncated archive member payload at offset %zu\n",
                    label.c_str(), offset);
            return false;
        }

        std::string name(hdr, 16);
        size_t data_offset = offset;
        size_t data_size = payload_size;
        if (name.compare(0, 3, "#1/") == 0) {
            const size_t ext_len = static_cast<size_t>(std::strtoull(name.c_str() + 3, nullptr, 10));
            if (ext_len > payload_size) {
                fprintf(stderr, "[%s] Invalid BSD extended name length %zu\n",
                        label.c_str(), ext_len);
                return false;
            }
            name.assign(ar_bytes.data() + offset, ext_len);
            data_offset += ext_len;
            data_size -= ext_len;
        }

        if (name.compare(0, 9, "__.SYMDEF") != 0 && name.compare(0, 1, "/") != 0) {
            const size_t nul = name.find('\0');
            const std::string clean_name = (nul == std::string::npos) ? name : name.substr(0, nul);
            if (!check_macho_header_span(ar_bytes.data() + data_offset, data_size,
                                         label + ":" + clean_name, expected_cpusubtype)) {
                return false;
            }
            ++obj_members;
        }

        offset += payload_size + (payload_size & 1u);
    }

    if (obj_members != expected_obj_members) {
        fprintf(stderr, "[%s] Expected %zu object members in archive, saw %zu\n",
                label.c_str(), expected_obj_members, obj_members);
        return false;
    }
    return true;
}

int check_arm64e_target(const std::string &target_str,
                        uint32_t expected_cpusubtype,
                        bool expect_x1) {
    Target target(target_str);
    if (!target.supported()) {
        printf("[SKIP] Target %s is not supported in this build of Halide.\n", target_str.c_str());
        return 0;
    }

    const std::string tmp = Internal::get_test_tmp_dir();
    const std::string ll_path = tmp + "arm64e_codegen_" + target_str + ".ll";
    const std::string asm_path = tmp + "arm64e_codegen_" + target_str + ".s";
    const std::string obj_path = tmp + "arm64e_codegen_" + target_str + ".o";
    const std::string static_lib_path = tmp + "arm64e_codegen_" + target_str + ".a";
    const std::string rt_obj_path = tmp + "arm64e_codegen_rt_" + target_str + ".o";
    const std::string mt_lib_path = tmp + "arm64e_codegen_mt_" + target_str + ".a";

    Internal::ensure_no_file_exists(ll_path);
    Internal::ensure_no_file_exists(asm_path);
    Internal::ensure_no_file_exists(obj_path);
    Internal::ensure_no_file_exists(static_lib_path);
    Internal::ensure_no_file_exists(rt_obj_path);
    Internal::ensure_no_file_exists(mt_lib_path);

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
        {OutputFileType::static_library, static_lib_path},
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
    const char *expected_prologue_pac = expect_x1 ? "pacibsppc" : "pacibsp";
    const char *expected_epilogue_ret = expect_x1 ? "retabsppc" : "retab";
    if (!require_contains(asm_text, expected_prologue_pac, target_str, "assembly") ||
        !require_contains(asm_text, expected_epilogue_ret, target_str, "assembly") ||
        !require_contains(asm_text, "blraaz", target_str, "assembly") ||
        !require_contains(asm_text, "paciza", target_str, "assembly") ||
        !require_contains(asm_text, "@AUTH(ia,0)", target_str, "assembly")) {
        return 1;
    }

    if (expect_x1) {
        if (!require_contains(ll_text, "\"branch-protection-pauth-lr\"", target_str, "LLVM IR") ||
            !require_contains(ll_text, "\"target-cpu\"=\"apple-m5\"", target_str, "LLVM IR") ||
            !require_contains(ll_text, "+pauth-lr", target_str, "LLVM IR") ||
            !require_contains(ll_text, "+cpa", target_str, "LLVM IR") ||
            !require_contains(asm_text, "addpt", target_str, "assembly")) {
            return 1;
        }
    }

    const std::vector<char> obj_bytes = Internal::read_entire_file(obj_path);
    if (!check_macho_header(obj_bytes, target_str, expected_cpusubtype)) {
        return 1;
    }

    const std::vector<char> static_lib_bytes = Internal::read_entire_file(static_lib_path);
    if (!check_archive_macho_members(static_lib_bytes, target_str + " (.a)", expected_cpusubtype, 1)) {
        return 1;
    }

    compile_standalone_runtime(rt_obj_path, target);
    const std::vector<char> rt_obj_bytes = Internal::read_entire_file(rt_obj_path);
    if (!check_macho_header(rt_obj_bytes, target_str + " (runtime)", expected_cpusubtype)) {
        return 1;
    }

    // Verify compile_multitarget preserves word-1 target features (ARM64e / ARM64eX1)
    // across all subtargets, the indirect-call wrapper, and the shared runtime member.
    Target specialized_target = target
                                    .with_feature(expect_x1 ? Target::ARMv88a : Target::ARMDotProd)
                                    .with_feature(Target::ARM64e);
    compile_multitarget(
        "f_arm64e_mt",
        {{OutputFileType::static_library, mt_lib_path}},
        {specialized_target, target},
        {},
        [&](const std::string &name, const Target &t) {
            return f.compile_to_module({in}, name, t);
        });
    const std::vector<char> mt_lib_bytes = Internal::read_entire_file(mt_lib_path);
    if (!check_archive_macho_members(mt_lib_bytes, target_str + " (multitarget .a)", expected_cpusubtype, 4)) {
        return 1;
    }

#ifdef HALIDE_WITH_EXCEPTIONS
    {
        std::vector<Target> mismatched_targets = {Target(target.os, target.arch, target.bits)};
        if (expect_x1) {
            mismatched_targets.emplace_back(target.os, target.arch, target.bits, Target::ProcessorGeneric,
                                            std::vector<Target::Feature>{Target::ARM64e});
        }
        for (size_t idx = 0; idx < mismatched_targets.size(); ++idx) {
            bool threw = false;
            const std::string bad_mt_path =
                tmp + "arm64e_codegen_bad_mt_" + std::to_string(idx) + "_" + target_str + ".a";
            Internal::ensure_no_file_exists(bad_mt_path);
            try {
                compile_multitarget(
                    "f_arm64e_bad_mt",
                    {{OutputFileType::static_library, bad_mt_path}},
                    {target, mismatched_targets[idx]},
                    {},
                    [&](const std::string &name, const Target &t) {
                        return f.compile_to_module({in}, name, t);
                    });
            } catch (const Halide::Error &) {
                threw = true;
            }
            if (!threw) {
                fprintf(stderr, "[%s] Expected mixed-ABI compile_multitarget with %s to fail\n",
                        target_str.c_str(), mismatched_targets[idx].to_string().c_str());
                return 1;
            }
        }
    }
#endif

    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    constexpr uint32_t kCpuSubtypeArm64eVersioned = 0x80000002u;
    constexpr uint32_t kCpuSubtypeArm64eX1Versioned = 0x8000000cu;

    if (check_arm64e_target("arm-64-ios-arm64e", kCpuSubtypeArm64eVersioned, false) != 0 ||
        check_arm64e_target("arm-64-macos-arm64e", kCpuSubtypeArm64eVersioned, false) != 0 ||
        check_arm64e_target("arm-64-ios-arm64e_x1", kCpuSubtypeArm64eX1Versioned, true) != 0 ||
        check_arm64e_target("arm-64-macos-arm64e_x1", kCpuSubtypeArm64eX1Versioned, true) != 0) {
        return 1;
    }

    printf("Success!\n");
    return 0;
}
