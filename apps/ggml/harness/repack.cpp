// GGML's repacked weight layouts, as oracles for the Halide formats with an
// Interleave layout (schemes/layouts.h):
// - ggml_repack: a test-only transcription of make_block_q4_0x4,
//   make_block_q4_0x8 and repack_q4_0_to_q4_0_{4,8}_bl from GGML v0.26's
//   src/ggml-cpu/repack.cpp (MIT License, Copyright (c) 2023-2024 The ggml
//   authors), for every layout, including those GGML does not pick on this
//   CPU;
// - ggml_repack_live: the bytes GGML itself repacks to, read back from its
//   CPU_REPACK buffer (host memory; the buffer has no get_tensor), which
//   cross-checks the transcription on the layout GGML picks here;
// - relayout: those, AoS as is, or the planar (SoA) layout, which GGML lacks.
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
        if (t[i] == "soa") f.soa = true;
        else if (sscanf(t[i].c_str(), "%dx%d", &f.rows, &f.chunk) != 2 && t[i] != "aos")
            f.codes = t[i];
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

bool relayout(const FormatSpec &f, ggml_type t, const uint8_t *src, uint8_t *dst, int64_t N, int64_t K) {
    if (f.rows > 1) return ggml_repack(f, t, src, dst, N, K);
    size_t bs = ggml_type_size(t), n = N * (K / ggml_blck_size(t));
    if (!f.soa) {
        memcpy(dst, src, bs * n);
    } else if (t == GGML_TYPE_Q4_0 || t == GGML_TYPE_Q8_0) {  // {fp16 d; codes}
        for (size_t i = 0; i < n; i++) {
            memcpy(dst + i * (bs - 2), src + i * bs + 2, bs - 2);
            memcpy(dst + n * (bs - 2) + i * 2, src + i * bs, 2);
        }
    } else {
        return false;
    }
    return true;
}

HBuf::HBuf(const void *host, halide_type_t t, const std::vector<int64_t> &extents) {
    b.host = (uint8_t *)host;
    b.type = t;
    b.dimensions = (int)extents.size();
    b.dim = d;
    for (int i = 0, stride = 1; i < b.dimensions; stride *= (int)extents[i++])
        d[i] = {0, (int32_t)extents[i], stride, 0};
}

std::vector<std::unique_ptr<HBuf>> port_buffers(const halide_filter_metadata_t *md, int first, int n, const uint8_t *bytes,
                                                const std::vector<int64_t> &records, int64_t record_bytes) {
    auto size = [](halide_type_t t) { return (int64_t)t.bytes(); };
    int64_t rest = record_bytes, count = 1;
    for (int64_t r : records)
        count *= r;
    for (int i = first; i < first + n; i++) {
        if (md->arguments[i].dimensions == (int)records.size()) rest -= size(md->arguments[i].type);
    }
    std::vector<std::unique_ptr<HBuf>> bs;
    for (int i = first; i < first + n; i++) {
        halide_type_t t = md->arguments[i].type;
        std::vector<int64_t> e = records;
        if (md->arguments[i].dimensions > (int)records.size()) e.insert(e.begin(), rest / size(t));
        bs.push_back(std::make_unique<HBuf>(bytes, t, e));
        bytes += count * size(t) * (e.size() > records.size() ? e[0] : 1);
    }
    return bs;
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
