// GGML's repacked weight layouts, as oracles for the Halide formats with an
// Interleave layout (schemes/layouts.h):
// - ggml_repack: a test-only transcription of make_block_q4_0x4,
//   make_block_q4_0x8, make_block_q8_0x4, make_block_q4_Kx8,
//   make_block_q5_Kx8, make_block_q6_Kx8, make_block_iq4_nlx4,
//   make_block_mxfp4x4, make_block_q1_0x4 (its 8-bit interleave),
//   repack_q4_0_to_q4_0_{4,8}_bl, repack_q8_0_to_q8_0_4_bl,
//   repack_q{4,5,6}_K_to_q{4,5,6}_K_8_bl, repack_iq4_nl_to_iq4_nl_4_bl,
//   repack_mxfp4_to_mxfp4_4_bl and repack_q1_0_to_q1_0_4_bl from GGML v0.26's
//   src/ggml-cpu/repack.cpp (MIT License, Copyright (c) 2023-2024 The ggml
//   authors), for every layout, including those GGML does not pick on this
//   CPU;
// - ggml_repack_live: the bytes GGML itself repacks to, read back from its
//   CPU_REPACK buffer (host memory; the buffer has no get_tensor), which
//   cross-checks the transcription on the layout GGML picks here;
// - relayout: those, AoS as is, or the planar (SoA) layout, which GGML lacks;
// - GGML's x4 activations, which its repacked gemms quantize f32 rows to
//   (GGML repacks no act blocks): quantize_mat, a transcription of
//   ggml_quantize_mat_q8_K_4x8_generic (GGML's on Arm; same source), and
//   ggml_quantize_mat_live, GGML's own (ggml_quantize_mat_t, exported); the
//   layout of its block_q8_Kx4 from q8_K's blocks (make_block_q8_Kx4, the
//   harness's, from its bsums index) is ggml_repack's for q8_K.
#include "harness.h"

#include "ggml-backend.h"

#include <cmath>
#include <cstring>

namespace gq {

FormatSpec parse_format(const std::string &s) {
    FormatSpec f;
    std::string spec = s.substr(0, s.find(':'));
    if (spec.size() < s.size()) f.compute = s.substr(spec.size() + 1);
    std::vector<std::string> t;
    for (size_t i = 0, j; i <= spec.size(); i = j + 1) {
        j = std::min(spec.find('.', i), spec.size());
        t.push_back(spec.substr(i, j - i));
    }
    f.type = t[0];
    for (size_t i = 1; i < t.size(); i++) {
        f.acc16 |= t[i] == "acc16";
        if (t[i] == "soa") f.soa = true;
        else if (sscanf(t[i].c_str(), "%dx%d", &f.rows, &f.chunk) != 2 && t[i] != "aos" && t[i] != "acc16")
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

constexpr int QK8_0 = 32;
struct block_q8_0 {
    uint16_t d;
    int8_t qs[QK8_0];
};
struct block_q8_0x4 {
    uint16_t d[4];
    int8_t qs[QK8_0 * 4];
};

block_q8_0x4 make_block_q8_0x4(block_q8_0 *in, unsigned int blck_size_interleave) {
    block_q8_0x4 out;
    for (int i = 0; i < 4; i++) {
        out.d[i] = in[i].d;
    }
    const int end = QK8_0 * 4 / blck_size_interleave;
    for (int i = 0; i < end; ++i) {
        int src_id = i % 4;
        int src_offset = (i / 4) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        memcpy(&out.qs[dst_offset], &in[src_id].qs[src_offset], blck_size_interleave);
    }
    return out;
}

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

// block_iq4_nl {f16 d; u8 qs[16]} and block_mxfp4 {u8 e; u8 qs[16]}: one
// scale field (S) and q4_0's nibbles, which make_block_iq4_nlx4 and
// make_block_mxfp4x4 interleave in 4-byte pieces as they are (no xor).
template<typename S>
struct block_sq4 {
    S d;
    uint8_t qs[QK4_0 / 2];
};
template<typename S>
struct block_sq4x4 {
    S d[4];
    uint8_t qs[QK4_0 * 2];
};

template<typename S>
block_sq4x4<S> make_block_sq4x4(block_sq4<S> *in, unsigned int blck_size_interleave) {
    block_sq4x4<S> out;
    for (int i = 0; i < 4; i++) {
        out.d[i] = in[i].d;
    }
    const int end = QK4_0 * 2 / blck_size_interleave;
    for (int i = 0; i < end; ++i) {
        int src_id = i % 4;
        int src_offset = (i / 4) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        memcpy(&out.qs[dst_offset], &in[src_id].qs[src_offset], sizeof(uint32_t));
    }
    return out;
}

// block_q1_0 {f16 d; u8 qs[16]}: 128 1-bit codes. make_block_q1_0x4 with
// blck_size_interleave 8 (bits, its q1_0_4x8) interleaves the 4 rows' qs a
// byte at a time (its 4-bit interleave shuffles bits: no Interleave layout).
constexpr int QK1_0 = 128;
struct block_q1_0 {
    uint16_t d;
    uint8_t qs[QK1_0 / 8];
};
struct block_q1_0x4 {
    uint16_t d[4];
    uint8_t qs[QK1_0 / 2];
};

block_q1_0x4 make_block_q1_0x4(block_q1_0 *in) {
    block_q1_0x4 out;
    for (int i = 0; i < 4; i++) {
        out.d[i] = in[i].d;
    }
    for (int byte_idx = 0; byte_idx < QK1_0 / 8; ++byte_idx) {
        out.qs[byte_idx * 4 + 0] = in[0].qs[byte_idx];
        out.qs[byte_idx * 4 + 1] = in[1].qs[byte_idx];
        out.qs[byte_idx * 4 + 2] = in[2].qs[byte_idx];
        out.qs[byte_idx * 4 + 3] = in[3].qs[byte_idx];
    }
    return out;
}

constexpr int QK_K = 256;
struct block_q4_K {
    uint16_t d, dmin;
    uint8_t scales[12];
    uint8_t qs[QK_K / 2];
};
struct block_q4_Kx8 {
    uint16_t d[8], dmin[8];
    uint8_t scales[96];
    uint8_t qs[QK_K * 4];
};

block_q4_Kx8 make_block_q4_Kx8(block_q4_K *in, unsigned int blck_size_interleave) {
    block_q4_Kx8 out;
    for (int i = 0; i < 8; i++) {
        out.d[i] = in[i].d;
    }
    for (int i = 0; i < 8; i++) {
        out.dmin[i] = in[i].dmin;
    }
    const int end = QK_K * 4 / blck_size_interleave;
    for (int i = 0; i < end; ++i) {
        int src_id = i % 8;
        int src_offset = (i / 8) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        uint64_t elems;
        memcpy(&elems, &in[src_id].qs[src_offset], blck_size_interleave);
        memcpy(&out.qs[dst_offset], &elems, blck_size_interleave);
    }
    // per sub-block, the 8 rows' scales and mins packed as one block_q4_K's
    uint8_t s[8], m[8];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            s[j] = in[j].scales[i] & 63;
            m[j] = in[j].scales[i + 4] & 63;
        }
        for (int j = 0; j < 4; j++) {
            out.scales[i * 12 + j] = (s[j] & 63) + ((s[j + 4] & 48) << 2);
            out.scales[i * 12 + j + 4] = (m[j] & 63) + ((m[j + 4] & 48) << 2);
            out.scales[i * 12 + j + 8] = (s[j + 4] & 15) + ((m[j + 4] & 15) << 4);
        }
    }
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 8; j++) {
            s[j] = ((in[j].scales[i] & 192) >> 2) | (in[j].scales[i + 8] & 15);
            m[j] = ((in[j].scales[i + 4] & 192) >> 2) | ((in[j].scales[i + 8] & 240) >> 4);
        }
        for (int j = 0; j < 4; j++) {
            out.scales[i * 12 + 48 + j] = (s[j] & 63) + ((s[j + 4] & 48) << 2);
            out.scales[i * 12 + 52 + j] = (m[j] & 63) + ((m[j + 4] & 48) << 2);
            out.scales[i * 12 + 56 + j] = (s[j + 4] & 15) + ((m[j + 4] & 15) << 4);
        }
    }
    return out;
}

struct block_q5_K {
    uint16_t d, dmin;
    uint8_t scales[12];
    uint8_t qh[QK_K / 8];
    uint8_t qs[QK_K / 2];
};
struct block_q5_Kx8 {
    uint16_t d[8], dmin[8];
    uint8_t scales[96];
    uint8_t qh[QK_K];
    uint8_t qs[QK_K * 4];
};

// make_block_q4_Kx8's, with the qh interleaved alike
block_q5_Kx8 make_block_q5_Kx8(block_q5_K *in, unsigned int blck_size_interleave) {
    block_q4_K q4[8];
    for (int i = 0; i < 8; i++) {
        q4[i].d = in[i].d;
        q4[i].dmin = in[i].dmin;
        memcpy(q4[i].scales, in[i].scales, 12);
        memcpy(q4[i].qs, in[i].qs, QK_K / 2);
    }
    block_q4_Kx8 o4 = make_block_q4_Kx8(q4, blck_size_interleave);
    block_q5_Kx8 out;
    memcpy(out.d, o4.d, sizeof(out.d));
    memcpy(out.dmin, o4.dmin, sizeof(out.dmin));
    memcpy(out.scales, o4.scales, sizeof(out.scales));
    memcpy(out.qs, o4.qs, sizeof(out.qs));
    const int end = QK_K * 4 / blck_size_interleave;
    for (int i = 0; i < end / 4; ++i) {
        int src_id = i % 8;
        int src_offset = (i / 8) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        memcpy(&out.qh[dst_offset], &in[src_id].qh[src_offset], blck_size_interleave);
    }
    return out;
}

struct block_q6_K {
    uint8_t ql[QK_K / 2];
    uint8_t qh[QK_K / 4];
    int8_t scales[QK_K / 16];
    uint16_t d;
};
struct block_q6_Kx8 {
    uint16_t d[8];
    int8_t scales[QK_K / 16 * 8];
    uint8_t ql[QK_K / 2 * 8];
    uint8_t qh[QK_K / 4 * 8];
};

block_q6_Kx8 make_block_q6_Kx8(block_q6_K *in, unsigned int blck_size_interleave) {
    block_q6_Kx8 out;
    constexpr int n_blocks = 8;
    for (int i = 0; i < n_blocks; i++) {
        out.d[i] = in[i].d;
    }
    const int end_ls = QK_K * 4 / blck_size_interleave;
    for (int i = 0; i < end_ls; ++i) {
        int src_id = i % n_blocks;
        int src_offset = (i / n_blocks) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        memcpy(&out.ql[dst_offset], &in[src_id].ql[src_offset], blck_size_interleave);
    }
    const int end_hs = end_ls / 2;
    for (int i = 0; i < end_hs; ++i) {
        int src_id = i % n_blocks;
        int src_offset = (i / n_blocks) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        memcpy(&out.qh[dst_offset], &in[src_id].qh[src_offset], blck_size_interleave);
    }
    constexpr int n_scales = QK_K / 16;
    for (int i = 0; i < n_blocks; i++) {
        for (int j = 0; j < n_scales; j++) {
            out.scales[j * n_blocks + i] = in[i].scales[j];
        }
    }
    return out;
}

struct block_q8_K {
    float d;
    int8_t qs[QK_K];
    int16_t bsums[QK_K / 16];
};
struct block_q8_Kx4 {
    float d[4];
    int8_t qs[QK_K * 4];
    int16_t bsums[QK_K / 4];
};

// quantize_mat_q8_K_4x8's layout of 4 q8_K blocks (as make_block_q8_0x4):
// the qs in 8-byte pieces across the rows; the bsums in 4s per row, its
// index (((j & 31) >> 3) << 2) + ((j >> 8) << 4) + ((j >> 6) & 3) of qs[j]
block_q8_Kx4 make_block_q8_Kx4(block_q8_K *in, unsigned int blck_size_interleave) {
    block_q8_Kx4 out;
    for (int i = 0; i < 4; i++) {
        out.d[i] = in[i].d;
    }
    const int end = QK_K * 4 / blck_size_interleave;
    for (int i = 0; i < end; ++i) {
        int src_id = i % 4;
        int src_offset = (i / 4) * blck_size_interleave;
        int dst_offset = i * blck_size_interleave;
        memcpy(&out.qs[dst_offset], &in[src_id].qs[src_offset], blck_size_interleave);
    }
    for (int j = 0; j < QK_K / 4; j++) {
        out.bsums[j] = in[j % 16 / 4].bsums[j / 16 * 4 + j % 4];
    }
    return out;
}

inline int nearest_int(float fval) {
    float val = fval + 12582912.f;
    int i;
    memcpy(&i, &val, sizeof(int));
    return (i & 0x007fffff) - 0x00400000;
}

void ggml_quantize_mat_q8_K_4x8_generic(const float *x, void *vy, int64_t k) {
    const int nb = k / QK_K;
    block_q8_Kx4 *y = (block_q8_Kx4 *)vy;
    const int blck_size_interleave = 8;
    float srcv[4][QK_K];
    float iscale[4];
    for (int i = 0; i < nb; i++) {
        for (int row_iter = 0; row_iter < 4; row_iter++) {
            float amax = 0.0f;
            float max = 0;
            for (int j = 0; j < QK_K; j++) {
                srcv[row_iter][j] = x[row_iter * k + i * QK_K + j];
                if (amax < fabsf(srcv[row_iter][j])) {
                    amax = fabsf(srcv[row_iter][j]);
                    max = srcv[row_iter][j];
                }
            }
            iscale[row_iter] = amax ? -127.f / max : 0;
            y[i].d[row_iter] = amax ? 1 / iscale[row_iter] : 0;
        }
        for (int j = 0; j < QK_K / 4; j++) {
            y[i].bsums[j] = 0;
        }
        for (int j = 0; j < QK_K * 4; j++) {
            int src_offset = (j / (4 * blck_size_interleave)) * blck_size_interleave;
            int src_id = (j % (4 * blck_size_interleave)) / blck_size_interleave;
            src_offset += (j % blck_size_interleave);
            int index = (((j & 31) >> 3) << 2) + ((j >> 8) << 4) + ((j >> 6) & 3);
            float x0 = srcv[src_id][src_offset] * iscale[src_id];
            y[i].qs[j] = nearest_int(x0);
            y[i].bsums[index] += y[i].qs[j];
        }
    }
}

// repack_q4_0_to_q4_0_{4,8}_bl, repack_q8_0_to_q8_0_4_bl,
// repack_q{4,5,6}_K_to_q{4,5,6}_K_8_bl, repack_iq4_nl_to_iq4_nl_4_bl,
// repack_mxfp4_to_mxfp4_4_bl, repack_q1_0_to_q1_0_4_bl
template<int R, typename B, typename F>
void repack_rows(const B *src, uint8_t *out, int64_t nrow, int64_t nblocks, F make) {
    auto *dst = (decltype(make(nullptr)) *)out;
    B dst_tmp[R];
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
    int c = f.chunk;
    if (t == GGML_TYPE_Q8_0 && f.codes.empty() && f.rows == 4 && (c == 4 || c == 8) && N % 4 == 0 && K % QK8_0 == 0) {
        repack_rows<4>((const block_q8_0 *)src, dst, N, K / QK8_0, [c](block_q8_0 *in) { return make_block_q8_0x4(in, c); });
        return true;
    }
    if (t == GGML_TYPE_Q8_K && f.codes.empty() && f.rows == 4 && c == 8 && N % 4 == 0 && K % QK_K == 0) {
        repack_rows<4>((const block_q8_K *)src, dst, N, K / QK_K, [c](block_q8_K *in) { return make_block_q8_Kx4(in, c); });
        return true;
    }
    if (t == GGML_TYPE_Q4_K && f.codes.empty() && f.rows == 8 && c == 8 && N % 8 == 0 && K % QK_K == 0) {
        repack_rows<8>((const block_q4_K *)src, dst, N, K / QK_K, [c](block_q4_K *in) { return make_block_q4_Kx8(in, c); });
        return true;
    }
    if (t == GGML_TYPE_IQ4_NL && f.codes.empty() && f.rows == 4 && c == 4 && N % 4 == 0 && K % QK4_0 == 0) {
        repack_rows<4>((const block_sq4<uint16_t> *)src, dst, N, K / QK4_0, [c](block_sq4<uint16_t> *in) { return make_block_sq4x4(in, c); });
        return true;
    }
    if (t == GGML_TYPE_MXFP4 && f.codes.empty() && f.rows == 4 && c == 4 && N % 4 == 0 && K % QK4_0 == 0) {
        repack_rows<4>((const block_sq4<uint8_t> *)src, dst, N, K / QK4_0, [c](block_sq4<uint8_t> *in) { return make_block_sq4x4(in, c); });
        return true;
    }
    if (t == GGML_TYPE_Q1_0 && f.codes.empty() && f.rows == 4 && c == 1 && N % 4 == 0 && K % QK1_0 == 0) {
        repack_rows<4>((const block_q1_0 *)src, dst, N, K / QK1_0, [](block_q1_0 *in) { return make_block_q1_0x4(in); });
        return true;
    }
    if (t == GGML_TYPE_Q5_K && f.codes.empty() && f.rows == 8 && c == 8 && N % 8 == 0 && K % QK_K == 0) {
        repack_rows<8>((const block_q5_K *)src, dst, N, K / QK_K, [c](block_q5_K *in) { return make_block_q5_Kx8(in, c); });
        return true;
    }
    if (t == GGML_TYPE_Q6_K && f.codes.empty() && f.rows == 8 && c == 8 && N % 8 == 0 && K % QK_K == 0) {
        repack_rows<8>((const block_q6_K *)src, dst, N, K / QK_K, [c](block_q6_K *in) { return make_block_q6_Kx8(in, c); });
        return true;
    }
    if (t != GGML_TYPE_Q4_0 || f.codes != "i4" || N % f.rows || K % QK4_0) return false;
    auto *s = (const block_q4_0 *)src;
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

bool rowwise_format(const FormatSpec &f) {
    return f.type == "qsi8cx" || f.type == "qai8dx";
}

std::vector<std::unique_ptr<HBuf>> rowwise_ports(const halide_filter_metadata_t *md, int first, int n, const FormatSpec &f,
                                                 int64_t K, int64_t R, std::vector<std::vector<uint8_t>> &mem) {
    std::vector<std::unique_ptr<HBuf>> bs;
    for (int i = first; i < first + n; i++) {
        halide_type_t t = md->arguments[i].type;
        std::vector<int64_t> e{R};
        if (md->arguments[i].dimensions == 3) e = {32 * f.rows, K / 32, R / f.rows};
        int64_t count = 1;
        for (int64_t x : e)
            count *= x;
        mem.emplace_back(count * t.bytes());
        bs.push_back(std::make_unique<HBuf>(mem.back().data(), t, e));
    }
    return bs;
}

bool quantize_mat(const FormatSpec &f, ggml_type t, const float *x, uint8_t *dst, int64_t N, int64_t K) {
    if (t != GGML_TYPE_Q8_K || !f.codes.empty() || f.rows != 4 || f.chunk != 8 || N % 4 || K % QK_K) return false;
    for (int64_t r = 0; r < N; r += 4) {
        ggml_quantize_mat_q8_K_4x8_generic(x + r * K, dst + r / 4 * (K / QK_K) * sizeof(block_q8_Kx4), K);
    }
    return true;
}

}  // namespace gq

// (GQ_RELAYOUT_ONLY: without the live GGML references, which need libggml,
// for apps/ggml/llama's ggml-cpu)
#ifndef GQ_RELAYOUT_ONLY

// GGML's (src/ggml-cpu/repack.cpp: the gemm's act quantizer, by its chunk)
template<int64_t INTER_SIZE, ggml_type PARAM_TYPE>
void ggml_quantize_mat_t(const float *x, void *vy, int64_t nrow, int64_t n_per_row);

namespace gq {

bool ggml_quantize_mat_live(const FormatSpec &f, ggml_type t, const float *x, uint8_t *dst, int64_t N, int64_t K) {
    if (t != GGML_TYPE_Q8_K || !f.codes.empty() || f.rows != 4 || f.chunk != 8 || N % 4 || K % QK_K) return false;
    for (int64_t r = 0; r < N; r += 4) {
        ggml_quantize_mat_t<8, GGML_TYPE_Q8_K>(x + r * K, dst + r / 4 * (K / QK_K) * sizeof(block_q8_Kx4), 4, K);
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

#endif
