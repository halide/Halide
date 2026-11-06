#include "Halide.h"
#include "SpirvIR.h"
#include "halide_test_dirs.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace Halide;
using namespace Halide::Internal;
namespace fs = std::filesystem;

namespace {

void set_dump_name(const std::string &name) {
#ifdef _WIN32
    internal_assert(_putenv_s("HL_SPIRV_DUMP_FILE", name.c_str()) == 0);
#else
    internal_assert(setenv("HL_SPIRV_DUMP_FILE", name.c_str(), 1) == 0);
#endif
}

Expr lazy_if_then_else(const Expr &condition, const Expr &yes, const Expr &no) {
    return Call::make(yes.type(), Call::if_then_else,
                      {condition, yes, no}, Call::PureIntrinsic);
}

Expr lazy_if_then(const Expr &condition, const Expr &yes) {
    return Call::make(yes.type(), Call::if_then_else,
                      {condition, yes}, Call::PureIntrinsic);
}

struct PhiInfo {
    uint32_t result_id = 0;
    uint32_t block_id = 0;
    std::vector<std::pair<uint32_t, uint32_t>> incoming;  // (value_id, parent_block_id)
};

// Validate the SPIR-V module's CFG and OpPhi instructions, ensuring that:
// 1. All OpPhi instructions appear at the start of their basic block.
// 2. Every OpPhi incoming basic block is an actual CFG predecessor of the OpPhi's block.
// 3. The number of incoming pairs matches the number of CFG predecessors.
// 4. Every incoming value defined inside a basic block can reach its incoming parent block.
int validate_spirv_phis(const fs::path &path) {
    std::ifstream file(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    file.close();
    internal_assert(bytes.size() % sizeof(uint32_t) == 0);
    std::vector<uint32_t> words(bytes.size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes.data(), bytes.size());
    internal_assert(words.size() >= 5 && words[0] == SpvMagicNumber);

    bool in_function = false;
    uint32_t current_block_id = 0;
    bool seen_non_phi_in_block = false;

    std::map<uint32_t, std::multiset<uint32_t>> predecessors;
    std::map<uint32_t, std::set<uint32_t>> successors;
    std::map<uint32_t, uint32_t> def_block_of_id;
    std::vector<PhiInfo> phis;

    for (size_t i = 5; i < words.size();) {
        const uint32_t length = words[i] >> 16;
        const uint32_t opcode = words[i] & 0xffff;
        internal_assert(length > 0 && i + length <= words.size());

        if (opcode == SpvOpFunction) {
            in_function = true;
            current_block_id = 0;
        } else if (opcode == SpvOpFunctionEnd) {
            in_function = false;
            current_block_id = 0;
        } else if (opcode == SpvOpLabel) {
            internal_assert(in_function && length >= 2);
            current_block_id = words[i + 1];
            seen_non_phi_in_block = false;
        } else if (in_function && current_block_id != 0) {
            if (opcode == SpvOpPhi) {
                internal_assert(!seen_non_phi_in_block)
                    << "OpPhi found after non-OpPhi instruction in block %" << current_block_id
                    << " in " << path << "\n";
                internal_assert(length >= 5 && (length - 3) % 2 == 0)
                    << "Malformed OpPhi in block %" << current_block_id << " in " << path << "\n";
                PhiInfo phi;
                phi.result_id = words[i + 2];
                phi.block_id = current_block_id;
                def_block_of_id[phi.result_id] = current_block_id;
                for (size_t j = 3; j < length; j += 2) {
                    phi.incoming.emplace_back(words[i + j], words[i + j + 1]);
                }
                phis.push_back(std::move(phi));
            } else {
                if (opcode != SpvOpLine && opcode != SpvOpNoLine && opcode != SpvOpVariable) {
                    seen_non_phi_in_block = true;
                }
                if (opcode == SpvOpBranch) {
                    internal_assert(length >= 2);
                    uint32_t target = words[i + 1];
                    predecessors[target].insert(current_block_id);
                    successors[current_block_id].insert(target);
                } else if (opcode == SpvOpBranchConditional) {
                    internal_assert(length >= 4);
                    uint32_t true_target = words[i + 2];
                    uint32_t false_target = words[i + 3];
                    predecessors[true_target].insert(current_block_id);
                    predecessors[false_target].insert(current_block_id);
                    successors[current_block_id].insert(true_target);
                    successors[current_block_id].insert(false_target);
                } else if (opcode == SpvOpSwitch) {
                    internal_assert(length >= 3);
                    uint32_t default_target = words[i + 2];
                    predecessors[default_target].insert(current_block_id);
                    successors[current_block_id].insert(default_target);
                } else if (length >= 3) {
                    // Most value-producing instructions in a basic block have [word0, type_id, result_id, ...]
                    // Record result_id -> current_block_id when it's not a branch/merge/store/void op.
                    if (opcode != SpvOpSelectionMerge && opcode != SpvOpLoopMerge &&
                        opcode != SpvOpStore && opcode != SpvOpMemoryBarrier &&
                        opcode != SpvOpControlBarrier && opcode != SpvOpReturn &&
                        opcode != SpvOpReturnValue) {
                        def_block_of_id[words[i + 2]] = current_block_id;
                    }
                }
            }
        }
        i += length;
    }

    auto can_reach = [&](uint32_t from_block, uint32_t to_block) {
        if (from_block == to_block) {
            return true;
        }
        std::set<uint32_t> visited;
        std::vector<uint32_t> worklist = {from_block};
        visited.insert(from_block);
        while (!worklist.empty()) {
            uint32_t curr = worklist.back();
            worklist.pop_back();
            for (uint32_t next : successors[curr]) {
                if (next == to_block) {
                    return true;
                }
                if (visited.insert(next).second) {
                    worklist.push_back(next);
                }
            }
        }
        return false;
    };

    for (const PhiInfo &phi : phis) {
        const auto &preds = predecessors[phi.block_id];
        std::multiset<uint32_t> phi_parents;
        for (const auto &[val_id, parent_block_id] : phi.incoming) {
            internal_assert(preds.count(parent_block_id) > 0)
                << "OpPhi %" << phi.result_id << " incoming basic block %" << parent_block_id
                << " is not a predecessor of %" << phi.block_id << " in " << path << "\n";
            phi_parents.insert(parent_block_id);

            auto def_it = def_block_of_id.find(val_id);
            if (def_it != def_block_of_id.end()) {
                internal_assert(can_reach(def_it->second, parent_block_id))
                    << "OpPhi %" << phi.result_id << " incoming value %" << val_id
                    << " defined in block %" << def_it->second
                    << " cannot reach incoming parent block %" << parent_block_id << " in " << path << "\n";
            }
        }
        internal_assert(phi_parents == preds)
            << "OpPhi %" << phi.result_id << " incoming parent blocks do not match CFG predecessors of block %"
            << phi.block_id << " in " << path << "\n";
    }

    return (int)phis.size();
}

void check_spirv_and_run(const std::string &name,
                         Func output,
                         const std::vector<Argument> &compile_args,
                         int expected_min_phis,
                         const Buffer<float> &floats_buf,
                         const Buffer<float> &powers_buf,
                         const std::function<float(float, float)> &ref_fn) {
    for (const auto &entry : fs::directory_iterator(".")) {
        const fs::path path = entry.path();
        if (path.extension() == ".spv" && path.filename().string().rfind(name + "_k", 0) == 0) {
            fs::remove(path);
        }
    }
    set_dump_name(name + ".spv");
    output.compile_to_object(get_test_tmp_dir() + name + ".obj", compile_args, name,
                             Target("host-vulkan-vk_v10"));

    int modules = 0;
    int total_phis = 0;
    for (const auto &entry : fs::directory_iterator(".")) {
        const fs::path path = entry.path();
        if (path.extension() != ".spv" || path.filename().string().rfind(name + "_k", 0) != 0) {
            continue;
        }
        ++modules;
        total_phis += validate_spirv_phis(path);
        fs::remove(path);
    }
    internal_assert(modules > 0) << "No SPIR-V dump for " << name << "\n";
    internal_assert(total_phis >= expected_min_phis)
        << "Expected at least " << expected_min_phis << " OpPhi instructions in " << name
        << ", found " << total_phis << "\n";

    // If the active JIT target has Vulkan enabled, execute on the Vulkan device and verify all elements.
    Target jit_target = get_jit_target_from_environment();
    if (jit_target.has_feature(Target::Vulkan)) {
        Buffer<float> result = output.realize({floats_buf.width()}, jit_target);
        result.copy_to_host();
        for (int i = 0; i < floats_buf.width(); ++i) {
            float expected = ref_fn(floats_buf(i), powers_buf(i));
            float actual = result(i);
            internal_assert( std::fabs(actual - expected) <= 1e-5f )
                << "Mismatch in " << name << " at index " << i
                << ": actual=" << actual << " expected=" << expected << "\n";
        }
    }
}

}  // namespace

int main(int argc, char **argv) {
    const int N = 33;
    Buffer<float> floats_buf(N);
    Buffer<float> powers_buf(N);
    const float power_choices[] = {2.0f, 0.5f, 1.0f, 3.0f, 0.25f, 1.5f};
    for (int i = 0; i < N; ++i) {
        floats_buf(i) = (i + 1) * 0.1f;
        powers_buf(i) = power_choices[i % 6];
    }

    ImageParam floats(Float(32), 1, "floats");
    ImageParam powers(Float(32), 1, "powers");
    floats.set(floats_buf);
    powers.set(powers_buf);

    // 1. Nested in the else arm at multiple depths (Issue #9566 reproducer)
    {
        Var x("x"), block("block"), thread("thread");
        Func output("nested_else");
        Expr v = floats(x), p = powers(x);
        output(x) = lazy_if_then_else(p == 2.0f, v * v,
                    lazy_if_then_else(p == 0.5f, sqrt(v),
                    lazy_if_then_else(p == 1.0f, v, pow(v, p))));
        output.gpu_tile(x, block, thread, 32, TailStrategy::GuardWithIf);

        check_spirv_and_run("nested_else", output, {floats, powers}, 3, floats_buf, powers_buf,
                            [](float v, float p) {
                                return p == 2.0f ? v * v : (p == 0.5f ? std::sqrt(v) : (p == 1.0f ? v : std::pow(v, p)));
                            });
    }

    // 2. Nested in the then arm at multiple depths
    {
        Var x("x"), block("block"), thread("thread");
        Func output("nested_then");
        Expr v = floats(x), p = powers(x);
        output(x) = lazy_if_then_else(p >= 1.0f,
                                      lazy_if_then_else(p == 2.0f, v * v,
                                                        lazy_if_then_else(p == 1.0f, v, v * p)),
                                      sqrt(v));
        output.gpu_tile(x, block, thread, 32, TailStrategy::GuardWithIf);

        check_spirv_and_run("nested_then", output, {floats, powers}, 3, floats_buf, powers_buf,
                            [](float v, float p) {
                                return p >= 1.0f ? (p == 2.0f ? v * v : (p == 1.0f ? v : v * p)) : std::sqrt(v);
                            });
    }

    // 3. Nested in both then and else arms at multiple depths
    {
        Var x("x"), block("block"), thread("thread");
        Func output("nested_both_arms");
        Expr v = floats(x), p = powers(x);
        Expr left = lazy_if_then_else(p < 1.0f,
                                      lazy_if_then_else(p == 0.5f, sqrt(v), v + 1.0f),
                                      lazy_if_then_else(p == 1.0f, v, v * 2.0f));
        Expr right = lazy_if_then_else(p > 2.0f,
                                       lazy_if_then_else(p == 3.0f, v * v * v, v * 3.0f),
                                       lazy_if_then_else(p == 2.0f, v * v, v - 1.0f));
        output(x) = lazy_if_then_else(p <= 1.5f, left, right);
        output.gpu_tile(x, block, thread, 32, TailStrategy::GuardWithIf);

        check_spirv_and_run("nested_both_arms", output, {floats, powers}, 7, floats_buf, powers_buf,
                            [](float v, float p) {
                                if (p <= 1.5f) {
                                    return p < 1.0f ? (p == 0.5f ? std::sqrt(v) : v + 1.0f)
                                                    : (p == 1.0f ? v : v * 2.0f);
                                } else {
                                    return p > 2.0f ? (p == 3.0f ? v * v * v : v * 3.0f)
                                                    : (p == 2.0f ? v * v : v - 1.0f);
                                }
                            });
    }

    // 4. Two-argument Call::if_then_else nested inside three-argument Call::if_then_else
    {
        Var x("x"), block("block"), thread("thread");
        Func output("two_arg_nested");
        Expr v = floats(x), p = powers(x);
        output(x) = lazy_if_then_else(p >= 1.0f,
                                      lazy_if_then(p == 2.0f, v * v),
                                      v);
        output.gpu_tile(x, block, thread, 32, TailStrategy::GuardWithIf);

        check_spirv_and_run("two_arg_nested", output, {floats, powers}, 2, floats_buf, powers_buf,
                            [](float v, float p) {
                                return p >= 1.0f ? (p == 2.0f ? v * v : 0.0f) : v;
                            });
    }

    // 5. Vectorized nested Call::if_then_else (exercises scalarize() and Broadcast condition unwrapping)
    {
        Var x("x"), block("block"), thread("thread"), vec("vec");
        Func output("vectorized_nested");
        Expr v = floats(x), p = powers(x);
        output(x) = lazy_if_then_else(p == 2.0f, v * v,
                                      lazy_if_then_else(p == 0.5f, sqrt(v), v + p));
        output.split(x, x, vec, 4, TailStrategy::GuardWithIf)
            .vectorize(vec)
            .gpu_tile(x, block, thread, 8, TailStrategy::GuardWithIf);

        check_spirv_and_run("vectorized_nested", output, {floats, powers}, 8, floats_buf, powers_buf,
                            [](float v, float p) {
                                return p == 2.0f ? v * v : (p == 0.5f ? std::sqrt(v) : v + p);
                            });
    }

    puts("Success!");
    return 0;
}
