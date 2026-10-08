// GGML's repacked weight layouts, as oracles for the Halide formats with an
// Interleave layout (schemes/layouts.h):
// - ggml_repack: a test-only transcription of make_block_q4_0x4,
//   make_block_q4_0x8 and repack_q4_0_to_q4_0_{4,8}_bl from GGML v0.26's
//   src/ggml-cpu/repack.cpp (MIT License, Copyright (c) 2023-2024 The ggml
//   authors), for every layout, including those GGML does not pick on this
//   CPU;
// - ggml_repack_live: the bytes GGML itself repacks to, read back from its
//   CPU_REPACK buffer (host memory; the buffer has no get_tensor), which
//   cross-checks the transcription on the layout GGML picks here.
#include "harness.h"

#include "ggml-backend.h"

#include <cstring>

namespace gq {

FormatSpec parse_format(const std::string &spec) {
    FormatSpec f;
    std::vector<std::string> t;
    for (size_t i = 0, j; i <= spec.size(); i = j + 1) {
        j = std::min(spec.find('.', i), spec.size());
        t.push_back(spec.substr(i, j - i));
    }
    f.type = t[0];
    for (size_t i = 1; i < t.size(); i++) {
        if (sscanf(t[i].c_str(), "%dx%d", &f.rows, &f.chunk) != 2 && t[i] != "aos") f.codes = t[i];
    }
    return f;
}

namespace {

constexpr int QK4_0 = 32;
struct block_q4_0 {
    uint16_t d;
    uint8_t qs[QK4_0 / 2];
};
template<int N>
struct block_q4_0xN {
    uint16_t d[N];
    uint8_t qs[QK4_0 / 2 * N];
};

block_q4_0xN<4> make_block_q4_0x4(block_q4_0 *in, int blck_size_interleave) {
    block_q4_0xN<4> out;
    for (int i = 0; i < 4; i++) {
        out.d[i] = in[i].d;
    }
    const int end = QK4_0 * 2 / blck_size_interleave;
    if (blck_size_interleave == 8) {
        const uint64_t xor_mask = 0x8888888888888888ULL;
        for (int i = 0; i < end; ++i) {
            int src_id = i % 4;
            int src_offset = (i / 4) * blck_size_interleave;
            int dst_offset = i * blck_size_interleave;
            uint64_t elems;
            memcpy(&elems, &in[src_id].qs[src_offset], sizeof(uint64_t));
            elems ^= xor_mask;
            memcpy(&out.qs[dst_offset], &elems, sizeof(uint64_t));
        }
    } else {  // 4
        const uint32_t xor_mask = 0x88888888;
        for (int i = 0; i < end; ++i) {
            int src_id = i % 4;
            int src_offset = (i / 4) * blck_size_interleave;
            int dst_offset = i * blck_size_interleave;
            uint32_t elems;
            memcpy(&elems, &in[src_id].qs[src_offset], sizeof(uint32_t));
            elems ^= xor_mask;
            memcpy(&out.qs[dst_offset], &elems, sizeof(uint32_t));
        }
    }
    return out;
}

block_q4_0xN<8> make_block_q4_0x8(block_q4_0 *in, unsigned int blck_size_interleave) {
    block_q4_0xN<8> out;
    for (int i = 0; i < 8; i++) {
        out.d[i] = in[i].d;
    }
    const int end = QK4_0 * 4 / blck_size_interleave;
    const uint64_t xor_mask = 0x8888888888888888ULL;
    for (int i = 0; i < end; ++i) {
        int src_id = i % 8;
        int src_offset = (i / 8) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        uint64_t elems;
        memcpy(&elems, &in[src_id].qs[src_offset], sizeof(uint64_t));
        elems ^= xor_mask;
        memcpy(&out.qs[dst_offset], &elems, sizeof(uint64_t));
    }
    return out;
}

// repack_q4_0_to_q4_0_{4,8}_bl
template<int R, typename F>
void repack_rows(const block_q4_0 *src, uint8_t *out, int64_t nrow, int64_t nblocks, F make) {
    auto *dst = (block_q4_0xN<R> *)out;
    block_q4_0 dst_tmp[R];
    for (int64_t b = 0; b < nrow; b += R) {
        for (int64_t x = 0; x < nblocks; x++) {
            for (int i = 0; i < R; i++) {
                dst_tmp[i] = src[x + i * nblocks];
            }
            *dst++ = make(dst_tmp);
        }
        src += R * nblocks;
    }
}

}  // namespace

bool ggml_repack(const FormatSpec &f, ggml_type t, const uint8_t *src, uint8_t *dst, int64_t N, int64_t K) {
    if (t != GGML_TYPE_Q4_0 || f.codes != "i4" || N % f.rows || K % QK4_0) return false;
    auto *s = (const block_q4_0 *)src;
    int c = f.chunk;
    if (f.rows == 4 && (c == 4 || c == 8)) {
        repack_rows<4>(s, dst, N, K / QK4_0, [c](block_q4_0 *in) { return make_block_q4_0x4(in, c); });
    } else if (f.rows == 8 && c == 8) {
        repack_rows<8>(s, dst, N, K / QK4_0, [c](block_q4_0 *in) { return make_block_q4_0x8(in, c); });
    } else {
        return false;
    }
    return true;
}

std::string ggml_repack_live(ggml_type t, const uint8_t *src, std::vector<uint8_t> &dst, int64_t N, int64_t K) {
    ggml_backend_dev_t dev = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_CPU);
    auto get = (ggml_backend_dev_get_extra_bufts_t)ggml_backend_reg_get_proc_address(ggml_backend_dev_backend_reg(dev), "ggml_backend_dev_get_extra_bufts");
    ggml_backend_buffer_type_t buft = nullptr;
    for (auto *b = get ? get(dev) : nullptr; b && *b; b++) {
        if (!strcmp(ggml_backend_buft_name(*b), "CPU_REPACK")) buft = *b;
    }
    if (!buft) return "";
    // W alone in the repack buffer; the activations (not host) stay unallocated.
    ggml_init_params p{ggml_tensor_overhead() * 4, nullptr, true};
    ggml_context *wctx = ggml_init(p), *ctx = ggml_init(p);
    ggml_tensor *W = ggml_new_tensor_2d(wctx, t, K, N);
    ggml_tensor *out = ggml_mul_mat(ctx, W, ggml_new_tensor_2d(ctx, GGML_TYPE_F32, K, 1));
    std::string name;
    take_log();
    ggml_backend_buffer_t buf = ggml_backend_alloc_ctx_tensors_from_buft(wctx, buft);
    if (buf && ggml_backend_dev_supports_op(dev, out)) {
        ggml_backend_tensor_set(W, src, 0, ggml_nbytes(W));
        for (const std::string &l : take_log()) {
            if (auto at = l.find(" with "); l.find("repack tensor ") != std::string::npos && at != std::string::npos) name = l.substr(at + 6, l.find('\n', at) - at - 6);
        }
        dst.assign((const uint8_t *)W->data, (const uint8_t *)W->data + ggml_nbytes(W));
    }
    if (buf) ggml_backend_buffer_free(buf);
    ggml_free(ctx);
    ggml_free(wctx);
    return name;
}

}  // namespace gq
