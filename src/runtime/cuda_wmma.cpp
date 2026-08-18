#include "cuda_wmma.h"

#include "constants.h"
#include "printer.h"
#include "scoped_mutex_lock.h"

namespace Halide {
namespace Runtime {
namespace Internal {
namespace Cuda {

using Halide::Runtime::Internal::Constants::wmma_accumulator_registers;
using Halide::Runtime::Internal::Constants::wmma_build_compare_digits;
using Halide::Runtime::Internal::Constants::wmma_build_element_marker;
using Halide::Runtime::Internal::Constants::wmma_build_index_bits;
using Halide::Runtime::Internal::Constants::wmma_build_mask_digits;
using Halide::Runtime::Internal::Constants::wmma_field_placeholder;
using Halide::Runtime::Internal::Constants::wmma_get_element_marker;
using Halide::Runtime::Internal::Constants::wmma_pack_element_marker;
using Halide::Runtime::Internal::Constants::wmma_xor_element_marker;

// The CUDA API, which cuda.cpp loads and fills in.
// clang-format off
#define CUDA_FN(ret, fn, args)                  extern WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_OPTIONAL(ret, fn, args)         extern WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_3020(ret, fn, fn_3020, args)    extern WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#define CUDA_FN_4000(ret, fn, fn_4000, args)    extern WEAK ret(CUDAAPI *fn) args;  // NOLINT(bugprone-macro-parentheses)
#include "cuda_functions.h"
#undef CUDA_FN
#undef CUDA_FN_OPTIONAL
#undef CUDA_FN_3020
#undef CUDA_FN_4000
// clang-format on

// Also defined in cuda.cpp, where the rest of the error reporting lives.
WEAK const char *get_cuda_error_name(CUresult error);

// Kernels that load a 16x16 matrix out of memory as a fragment and have every
// lane report what it holds in each of its registers. Run on a matrix whose
// entries say where they are, these measure the whole layout. One takes the
// matrix as an accumulator and the other as an A operand, because a fused
// chain has to turn the first into the second.
WEAK const char *wmma_probe_ptx =
    ".version 6.3\n"
    ".target sm_70\n"
    ".address_size 64\n"
    ".visible .entry halide_wmma_probe_c(\n"
    "  .param .u64 src,\n"
    "  .param .u64 dst\n"
    ")\n"
    ".maxntid 32, 1, 1\n"
    "{\n"
    "  .reg .b32 %f<8>;\n"
    "  .reg .b32 %r<4>;\n"
    "  .reg .b64 %rd<8>;\n"
    "  ld.param.u64 %rd1, [src];\n"
    "  ld.param.u64 %rd2, [dst];\n"
    "  cvta.to.global.u64 %rd3, %rd1;\n"
    "  cvta.to.global.u64 %rd4, %rd2;\n"
    "  mov.u32 %r1, 16;\n"
    "  wmma.load.c.sync.aligned.row.m16n16k16.global.f32\n"
    "    {%f0, %f1, %f2, %f3, %f4, %f5, %f6, %f7}, [%rd3], %r1;\n"
    "  mov.u32 %r2, %laneid;\n"
    "  mul.wide.u32 %rd5, %r2, 32;\n"
    "  add.s64 %rd6, %rd4, %rd5;\n"
    "  st.global.b32 [%rd6], %f0;\n"
    "  st.global.b32 [%rd6+4], %f1;\n"
    "  st.global.b32 [%rd6+8], %f2;\n"
    "  st.global.b32 [%rd6+12], %f3;\n"
    "  st.global.b32 [%rd6+16], %f4;\n"
    "  st.global.b32 [%rd6+20], %f5;\n"
    "  st.global.b32 [%rd6+24], %f6;\n"
    "  st.global.b32 [%rd6+28], %f7;\n"
    "  ret;\n"
    "}\n"
    ".visible .entry halide_wmma_probe_a(\n"
    "  .param .u64 src,\n"
    "  .param .u64 dst\n"
    ")\n"
    ".maxntid 32, 1, 1\n"
    "{\n"
    "  .reg .b32 %f<8>;\n"
    "  .reg .b32 %r<4>;\n"
    "  .reg .b64 %rd<8>;\n"
    "  ld.param.u64 %rd1, [src];\n"
    "  ld.param.u64 %rd2, [dst];\n"
    "  cvta.to.global.u64 %rd3, %rd1;\n"
    "  cvta.to.global.u64 %rd4, %rd2;\n"
    "  mov.u32 %r1, 16;\n"
    "  wmma.load.a.sync.aligned.row.m16n16k16.global.f16\n"
    "    {%f0, %f1, %f2, %f3, %f4, %f5, %f6, %f7}, [%rd3], %r1;\n"
    "  mov.u32 %r2, %laneid;\n"
    "  mul.wide.u32 %rd5, %r2, 32;\n"
    "  add.s64 %rd6, %rd4, %rd5;\n"
    "  st.global.b32 [%rd6], %f0;\n"
    "  st.global.b32 [%rd6+4], %f1;\n"
    "  st.global.b32 [%rd6+8], %f2;\n"
    "  st.global.b32 [%rd6+12], %f3;\n"
    "  st.global.b32 [%rd6+16], %f4;\n"
    "  st.global.b32 [%rd6+20], %f5;\n"
    "  st.global.b32 [%rd6+24], %f6;\n"
    "  st.global.b32 [%rd6+28], %f7;\n"
    "  ret;\n"
    "}\n";

constexpr int wmma_tile_width = 16;
// The number of entries in a 16x16 matrix.
constexpr int wmma_entries = wmma_tile_width * wmma_tile_width;
constexpr int warp_lanes = 32;

// Half precision for the small non-negative integers the A probe labels its
// entries with, which are exact in both directions.
WEAK uint16_t half_of_small_int(int value) {
    if (value == 0) {
        return 0;
    }
    int exponent = 25;
    while (value < 0x400) {
        value <<= 1;
        exponent--;
    }
    return (uint16_t)((exponent << 10) | (value & 0x3ff));
}

// The inverse, or -1 if the half doesn't hold such an integer.
WEAK int small_int_of_half(uint16_t h) {
    if (h == 0) {
        return 0;
    }
    int exponent = (h >> 10) & 0x1f;
    if (exponent < 15 || exponent > 25) {
        return -1;
    }
    return ((h & 0x3ff) | 0x400) >> (25 - exponent);
}

// Which lane of the warp holds each entry of a 16x16 single precision
// accumulator, and which of that lane's registers it sits in.
WEAK uint8_t wmma_entry_lane[wmma_entries];
WEAK uint8_t wmma_entry_reg[wmma_entries];

// Which accumulator registers of the same lane feed the two halves of each
// register of an A operand built out of one. Only meaningful when the layouts
// line up lane for lane, which is what wmma_relayout_is_lane_local says.
WEAK uint8_t wmma_a_src_low[wmma_accumulator_registers];
WEAK uint8_t wmma_a_src_high[wmma_accumulator_registers];
WEAK bool wmma_relayout_is_lane_local = false;

WEAK CUcontext wmma_layout_context = nullptr;
WEAK halide_mutex wmma_layout_lock;

// Work out how an A operand would be built from an accumulator holding the
// same matrix. Both are measured, so this only records what it finds, and
// leaves wmma_relayout_is_lane_local false if a lane would have to reach
// outside itself.
WEAK void find_wmma_relayout(const uint16_t *a_held) {
    for (int reg = 0; reg < wmma_accumulator_registers; reg++) {
        for (int lane = 0; lane < 32; lane++) {
            // The two halves of this register of this lane, and where the
            // accumulator keeps them.
            const int halves[2] = {
                small_int_of_half(a_held[(lane * wmma_accumulator_registers + reg) * 2]),
                small_int_of_half(a_held[(lane * wmma_accumulator_registers + reg) * 2 + 1])};
            uint8_t *src[2] = {wmma_a_src_low, wmma_a_src_high};
            for (int half = 0; half < 2; half++) {
                const int entry = halves[half];
                if (entry < 0 || entry >= wmma_entries ||
                    wmma_entry_lane[entry] != lane) {
                    return;
                }
                if (lane == 0) {
                    src[half][reg] = wmma_entry_reg[entry];
                } else if (src[half][reg] != wmma_entry_reg[entry]) {
                    // Which register to take it from would depend on the lane.
                    return;
                }
            }
        }
    }
    wmma_relayout_is_lane_local = true;
}

WEAK int measure_wmma_layout(void *user_context, CUcontext ctx) {
    ScopedMutexLock lock(&wmma_layout_lock);
    if (wmma_layout_context == ctx) {
        return halide_error_code_success;
    }

    CUmodule module = nullptr;
    CUfunction probe_c = nullptr, probe_a = nullptr;
    CUdeviceptr src = 0, dst = 0;

    // The same matrix in both precisions, with every entry labelled with where
    // it is, and room for what one warp reports back about it.
    float c_entries[wmma_entries], c_held[wmma_entries];
    uint16_t a_entries[wmma_entries], a_held[wmma_entries * 2];
    for (int i = 0; i < wmma_entries; i++) {
        c_entries[i] = (float)i;
        a_entries[i] = half_of_small_int(i);
    }

    // Anything that goes wrong here leaves the layout unmeasured, which the
    // caller reports. The allocations are freed on the way out either way.
    CUresult err = cuModuleLoadData(&module, wmma_probe_ptx);
    if (!err) {
        err = cuModuleGetFunction(&probe_c, module, "halide_wmma_probe_c");
    }
    if (!err) {
        err = cuModuleGetFunction(&probe_a, module, "halide_wmma_probe_a");
    }
    if (!err) {
        err = cuMemAlloc(&src, sizeof(c_entries));
    }
    if (!err) {
        // An A operand holds each entry twice, so it reports back twice as
        // much as an accumulator does.
        err = cuMemAlloc(&dst, sizeof(a_held));
    }
    if (!err) {
        err = cuMemcpyHtoD(src, c_entries, sizeof(c_entries));
    }
    if (!err) {
        void *args[] = {&src, &dst};
        err = cuLaunchKernel(probe_c, 1, 1, 1, 32, 1, 1, 0, nullptr, args, nullptr);
    }
    if (!err) {
        err = cuCtxSynchronize();
    }
    if (!err) {
        err = cuMemcpyDtoH(c_held, dst, sizeof(c_held));
    }
    if (!err) {
        err = cuMemcpyHtoD(src, a_entries, sizeof(a_entries));
    }
    if (!err) {
        void *args[] = {&src, &dst};
        err = cuLaunchKernel(probe_a, 1, 1, 1, 32, 1, 1, 0, nullptr, args, nullptr);
    }
    if (!err) {
        err = cuCtxSynchronize();
    }
    if (!err) {
        err = cuMemcpyDtoH(a_held, dst, sizeof(a_held));
    }
    if (src) {
        (void)cuMemFree(src);
    }
    if (dst) {
        (void)cuMemFree(dst);
    }
    if (module) {
        (void)cuModuleUnload(module);
    }
    if (err) {
        error(user_context) << "CUDA error: " << get_cuda_error_name(err)
                            << " measuring the tensor core fragment layout";
        return halide_error_code_gpu_device_error;
    }

    // Each lane reported which entry it holds in each register, so every entry
    // should have been reported exactly once.
    bool found[wmma_entries] = {};
    for (int lane = 0; lane < 32; lane++) {
        for (int reg = 0; reg < wmma_accumulator_registers; reg++) {
            float f = c_held[lane * wmma_accumulator_registers + reg];
            int entry = (int)f;
            if ((float)entry != f || entry < 0 || entry >= wmma_entries || found[entry]) {
                error(user_context) << "CUDA: the tensor core accumulator layout "
                                       "measured on this device is not a permutation "
                                       "of the entries of the matrix. Lane "
                                    << lane << " register " << reg << " holds " << f << ".";
                return halide_error_code_gpu_device_error;
            }
            found[entry] = true;
            wmma_entry_lane[entry] = (uint8_t)lane;
            wmma_entry_reg[entry] = (uint8_t)reg;
        }
    }

    find_wmma_relayout(a_held);

    wmma_layout_context = ctx;
    return halide_error_code_success;
}

// Finish off the markers codegen left in a copy of a PTX module, now that the
// layout they depend on has been measured. Returns the copy, which the caller
// frees, or nullptr on failure, having reported why.
WEAK char *patch_wmma_markers(void *user_context, const char *ptx_src) {
    size_t len = strlen(ptx_src);
    char *patched = (char *)malloc(len + 1);
    if (!patched) {
        error(user_context) << "CUDA: out of memory patching a PTX module";
        return nullptr;
    }
    memcpy(patched, ptx_src, len + 1);

    bool failed = false;
    auto fail = [&](const char *why, size_t where) {
        error(user_context) << "CUDA: could not finish a tensor core instruction. "
                            << why << ":\n"
                            << patched + where;
        failed = true;
        return false;
    };

    // Fill in a field the marker left blank, right aligned, because a leading
    // zero would make PTX read it as octal. Checking that it is blank first
    // catches a marker whose shape has drifted from what this expects.
    auto write_field = [&](size_t at, int width, int value) {
        for (int i = 0; i < width; i++) {
            if (patched[at + i] != wmma_field_placeholder) {
                return fail("A field to fill in is not where it should be", at);
            }
        }
        for (int i = width - 1; i >= 0; i--, value /= 10) {
            patched[at + i] = (value || i == width - 1) ? (char)('0' + value % 10) : ' ';
        }
        return true;
    };

    // The number a marker ends with, which says what is wanted out of the
    // registers it names.
    auto read_index = [&](size_t cursor, int *index) {
        *index = 0;
        size_t digits = 0;
        while (patched[cursor] >= '0' && patched[cursor] <= '9') {
            *index = *index * 10 + (patched[cursor++] - '0');
            digits++;
        }
        return digits ? true : fail("What the marker asks for is not a number", cursor);
    };

    // Rewrite one marker into the instruction it stands for. What is wanted
    // comes first, so one pass over the operands is enough to know which of
    // them to keep as they go past. They get set aside as they do, because the
    // instruction that comes out goes back over the one it replaces, and a
    // register it names may sit under what gets written. It is always shorter,
    // because it names at most two of the registers the marker names them all
    // of, and the slack at the end is padded with spaces.
    //
    // The operands to keep are numbered from the destination, which is always
    // kept, so a register of the fragment is one plus its index.
    auto rewrite = [&](char *marker, const char *name, const char *opcode,
                       int keep_a, int keep_b, int number, const char *tail) {
        // The destination and the registers kept, in the order they come out.
        char kept[3][32];
        int kept_len[3] = {0, 0, 0};

        char *read = marker + strlen(name);
        for (int operand = -1; true; operand++) {
            while (*read == ' ') {
                read++;
            }
            const char *begin = read;
            while (*read && *read != ',' && *read != ';') {
                read++;
            }
            if (!*read) {
                return fail("A marker does not end", marker - patched);
            }
            const char *end = read;
            while (end > begin && end[-1] == ' ') {
                end--;
            }
            // Operand -1 is the number the marker asks with, which the caller
            // has already read.
            const int slot = operand < 0             ? -1 :
                             operand == 0            ? 0 :
                             operand == keep_a       ? 1 :
                             operand == keep_b       ? 2 :
                                                       -1;
            if (slot >= 0) {
                if (end - begin > (int)sizeof(kept[0])) {
                    return fail("A marker names a register that is too long to be one",
                                marker - patched);
                }
                kept_len[slot] = (int)(end - begin);
                memcpy(kept[slot], begin, kept_len[slot]);
            }
            if (*read++ == ';') {
                break;
            }
        }

        char *write = marker;
        bool overrun = false;
        auto put = [&](char c) {
            if (write < read) {
                *write++ = c;
            } else {
                overrun = true;
            }
        };
        auto put_all = [&](const char *s, int n) {
            for (int i = 0; i < n; i++) {
                put(s[i]);
            }
        };
        auto put_str = [&](const char *s) {
            put_all(s, (int)strlen(s));
        };

        put_str(opcode);
        put(' ');
        for (int slot = 0; slot < 3; slot++) {
            if (kept_len[slot]) {
                if (slot) {
                    put(',');
                    put(' ');
                }
                put_all(kept[slot], kept_len[slot]);
            }
        }
        if (number >= 0) {
            put(',');
            put(' ');
            char digits[4];
            int n = 0;
            do {
                digits[n++] = (char)('0' + number % 10);
                number /= 10;
            } while (number && n < (int)sizeof(digits));
            while (n) {
                put(digits[--n]);
            }
        }
        put_str(tail);
        if (overrun) {
            return fail("The instruction a marker becomes does not fit in it",
                        marker - patched);
        }
        while (write < read) {
            *write++ = ' ';
        }
        return true;
    };

    // Every marker of a kind gets found, read and rewritten the same way. All
    // that differs is what the number it asks with means, which is what the
    // body works out.
    auto each_marker = [&](const char *name, auto &&finish) {
        size_t offset = 0;
        while (const char *found = strstr(patched + offset, name)) {
            offset = (found - patched) + strlen(name);
            int index;
            if (!read_index(offset + 1, &index) ||
                !finish(patched + (found - patched), index)) {
                return false;
            }
        }
        return true;
    };

    // A read of one entry, which becomes a shuffle of the register holding it
    // from the lane holding it.
    if (!each_marker(wmma_get_element_marker, [&](char *marker, int entry) {
            if (entry >= wmma_entries) {
                return fail("The entry the marker asks for is not one the matrix has",
                            marker - patched);
            }
            return rewrite(marker, wmma_get_element_marker, "shfl.sync.idx.b32",
                           1 + wmma_entry_reg[entry], -1, wmma_entry_lane[entry],
                           ", 31, -1;");
        })) {
        return free(patched), nullptr;
    }

    // A register of an A operand, which becomes a convert and pack of the two
    // registers of the accumulator that feed it, the high half first.
    if (!each_marker(wmma_pack_element_marker, [&](char *marker, int reg) {
            if (reg >= wmma_accumulator_registers) {
                return fail("The register the marker asks for is not one the operand "
                            "has",
                            marker - patched);
            }
            if (!wmma_relayout_is_lane_local) {
                return fail("This device spreads an accumulator and an A operand over "
                            "the lanes of a warp differently, so one can't be built "
                            "from the other in place. Stage it through shared memory "
                            "instead",
                            marker - patched);
            }
            return rewrite(marker, wmma_pack_element_marker, "cvt.rn.f16x2.f32",
                           1 + wmma_a_src_high[reg], 1 + wmma_a_src_low[reg], -1, ";");
        })) {
        return free(patched), nullptr;
    }

    // A register of an accumulator built out of a vector spread along one axis
    // of the matrix. Which entry of the vector a lane wants varies from lane to
    // lane, so what gets filled in is which bit of the lane index drives each
    // level of the select tree.
    size_t offset = 0;
    while (const char *marker = strstr(patched + offset, wmma_build_element_marker)) {
        int packed;
        if (!read_index((marker - patched) + strlen(wmma_build_element_marker) + 1,
                        &packed)) {
            return free(patched), nullptr;
        }
        const int reg = packed & 15, axis = packed >> 4;
        if (reg >= wmma_accumulator_registers) {
            fail("The register the marker asks for is not one the accumulator has",
                 marker - patched);
            return free(patched), nullptr;
        }

        // Which entry of the vector each lane holds in that register. Every
        // lane holds exactly one entry of the matrix per register.
        int index[warp_lanes];
        for (int e = 0; e < wmma_entries; e++) {
            if (wmma_entry_reg[e] == reg) {
                index[wmma_entry_lane[e]] =
                    axis ? e % wmma_tile_width : e / wmma_tile_width;
            }
        }

        size_t semicolon = marker - patched;
        for (int b = 0; b < wmma_build_index_bits; b++) {
            // Either a bit of the lane index drives this level of the tree, or
            // the same half of it is taken whatever the lane.
            int mask = 0, compare = index[0] >> b & 1;
            for (int s = 0; s < 5 && mask == 0; s++) {
                bool matches = true;
                for (int lane = 0; lane < warp_lanes; lane++) {
                    matches &= (index[lane] >> b & 1) == (lane >> s & 1);
                }
                mask = matches ? 1 << s : 0;
            }
            if (mask == 0) {
                for (int lane = 0; lane < warp_lanes; lane++) {
                    if ((index[lane] >> b & 1) != compare) {
                        fail("This device spreads an accumulator over the lanes of "
                             "a warp in a way that doesn't let a value broadcast "
                             "along one axis of the matrix be selected lane by "
                             "lane. Stage it through shared memory instead",
                             marker - patched);
                        return free(patched), nullptr;
                    }
                }
            } else {
                compare = 0;
            }

            // The mask ends the and, and the comparand the setp that follows.
            const int width[2] = {wmma_build_mask_digits, wmma_build_compare_digits};
            const int value[2] = {mask, compare};
            for (int half = 0; half < 2; half++) {
                while (patched[semicolon] && patched[semicolon] != ';') {
                    semicolon++;
                }
                if (!patched[semicolon] || semicolon < (size_t)width[half]) {
                    fail("A select tree is not shaped the way it should be",
                         marker - patched);
                    return free(patched), nullptr;
                }
                if (!write_field(semicolon - width[half], width[half], value[half])) {
                    return free(patched), nullptr;
                }
                semicolon++;
            }
        }
        offset = semicolon;
    }

    // A step of a reduction along an axis of the matrix, which becomes a
    // butterfly shuffle of the register holding the entry to combine with. If
    // that entry is one this lane already holds, the mask is zero, which makes
    // the shuffle a read of the lane's own register.
    if (!each_marker(
            wmma_xor_element_marker,
            [&](char *marker, int packed) {
                const int reg = packed & 15, bit = (packed >> 4) & 7, axis = packed >> 7;
                if (reg >= wmma_accumulator_registers) {
                    return fail("The register the marker asks for is not one the "
                                "accumulator has",
                                marker - patched);
                }

                // Where the entry each lane holds in that register comes from.
                // For this to be one instruction every lane has to find it in
                // the same register of the lane it swaps with, and every pair
                // of lanes has to differ by the same amount, which is what
                // makes the swap a butterfly. The row is the high half of an
                // entry's index and the column the low.
                const int flip = axis ? 0 : 4;
                int src_reg = -1, mask = -1;
                for (int e = 0; e < wmma_entries; e++) {
                    if (wmma_entry_reg[e] != reg) {
                        continue;
                    }
                    const int from = e ^ (1 << (bit + flip));
                    const int swap = wmma_entry_lane[e] ^ wmma_entry_lane[from];
                    if (src_reg < 0) {
                        src_reg = wmma_entry_reg[from];
                        mask = swap;
                    } else if (src_reg != wmma_entry_reg[from] || mask != swap) {
                        src_reg = -1;
                        break;
                    }
                }
                if (src_reg < 0) {
                    return fail("This device spreads an accumulator over the lanes "
                                "of a warp in a way that doesn't let entries be "
                                "exchanged along an axis of the matrix in one step, "
                                "so a reduction along one can't happen where the "
                                "fragments sit. Stage it through shared memory "
                                "instead",
                                marker - patched);
                }

                return rewrite(marker, wmma_xor_element_marker, "shfl.sync.bfly.b32",
                               1 + src_reg, -1, mask, ", 31, -1;");
            })) {
        return free(patched), nullptr;
    }

    if (failed) {
        return free(patched), nullptr;
    }
    return patched;
}

WEAK const char *finish_wmma_markers(void *user_context, CUcontext ctx,
                                     const char *ptx_src) {
    if (!strstr(ptx_src, wmma_get_element_marker) &&
        !strstr(ptx_src, wmma_pack_element_marker) &&
        !strstr(ptx_src, wmma_build_element_marker) &&
        !strstr(ptx_src, wmma_xor_element_marker)) {
        return ptx_src;
    }
    if (measure_wmma_layout(user_context, ctx)) {
        return nullptr;
    }
    return patch_wmma_markers(user_context, ptx_src);
}

}  // namespace Cuda
}  // namespace Internal
}  // namespace Runtime
}  // namespace Halide
