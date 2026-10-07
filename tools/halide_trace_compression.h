#ifndef HALIDE_TRACE_COMPRESSION_H
#define HALIDE_TRACE_COMPRESSION_H

/** \file
 * A lossless compressor for binary trace files (streams of
 * halide_trace_packet_t, as written with HL_TRACE_FILE), used for traces
 * written by Pipeline::halidoscope and read by tools/halidoscope.
 *
 * A compressed trace is trace_compression_magic followed by a sequence of
 * chunks. Each chunk holds a whole number of packets, and is a header of
 * four uint32s (the number of packets, their total size, the size of the
 * range-coded stream, and the size of the raw bit stream) followed by those
 * two streams. Chunks are coded independently, so they can be decoded in
 * parallel, each directly into its place in the output.
 *
 * Only the packet size must be valid for a packet to round-trip. The other
 * fields just guide prediction.
 *
 * Each word of a packet is predicted, and the residual is zigzagged and
 * stored as its bit length (in the adaptive binary range-coded stream)
 * followed by the bits below its leading one (in the raw bit stream).
 * Predictions come from the previous packet with the same size and parent
 * (roughly, the previous packet of the same size from the same thread), from
 * earlier words in the same packet, and for value words, from the last value
 * seen for the same Func and coordinates. Each word position uses whichever
 * predictor would have been best for that position last time, so the decoder
 * can make the same choice. Parents are coded as an index into a
 * move-to-front list, and value vectors that repeat one of the last 16 are
 * coded as an index.
 */

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "HalideRuntime.h"

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace Halide {
namespace Tools {

constexpr char trace_compression_magic[8] = {'H', 'L', 'T', 'R', 'C', 'Z', '0', '2'};

/** The largest packet size the codec supports, in bytes. */
constexpr uint32_t max_trace_packet_size = 1 << 20;

/** Codes a single chunk of trace packets. Use a new TraceCodec for each
 * chunk. */
class TraceCodec {
    struct Model {
        uint16_t p[64];
        Model() {
            std::fill(std::begin(p), std::end(p), 32768);
        }
    };
    struct Ctx {
        std::vector<uint8_t> sel;
        std::vector<Model> len;
    };
    std::vector<std::unique_ptr<Ctx>> ctxs;
    std::vector<std::vector<uint32_t>> prevs = std::vector<std::vector<uint32_t>>(4096);
    std::vector<uint32_t> history = std::vector<uint32_t>(1 << 23);
    uint32_t last_size = 0, parents[64] = {}, recent[16][16] = {}, recent_n[16] = {}, recent_pos = 0;
    Model size_len, parent_len, parent_idx, match_idx;

    uint32_t lo = 0, hi = ~0u, x = 0;
    uint64_t acc = 0;
    int acc_bits = 0;
    std::vector<uint8_t> coded, raw;
    const uint8_t *in_coded = nullptr, *coded_end = nullptr, *in_raw = nullptr, *raw_end = nullptr;

    template<bool dec>
    uint32_t bit(uint32_t y, uint16_t &p) {
        uint32_t mid = lo + (uint32_t)(((uint64_t)(hi - lo) * p) >> 16);
        if (dec) {
            y = x <= mid;
        }
        y ? (hi = mid) : (lo = mid + 1);
        p += y ? (65536 - p) >> 5 : -(p >> 5);
        while (((lo ^ hi) & 0xff000000) == 0) {
            if (dec) {
                x = (x << 8) | (in_coded < coded_end ? *in_coded++ : 0);
            } else {
                coded.push_back(hi >> 24);
            }
            lo <<= 8;
            hi = (hi << 8) | 255;
        }
        return y;
    }

    template<bool dec>
    uint32_t symbol(uint32_t v, int bits, Model &m) {
        uint32_t node = 1;
        for (int k = bits - 1; k >= 0; k--) {
            node = node * 2 + bit<dec>((v >> k) & 1, m.p[node]);
        }
        return node - (1 << bits);
    }

    template<bool dec>
    uint32_t raw_bits(uint32_t v, int bits) {
        if (!dec) {
            acc |= (uint64_t)v << acc_bits;
            for (acc_bits += bits; acc_bits >= 8; acc_bits -= 8, acc >>= 8) {
                raw.push_back((uint8_t)acc);
            }
            return v;
        }
        for (; acc_bits < bits; acc_bits += 8) {
            acc |= (uint64_t)(in_raw < raw_end ? *in_raw++ : 0) << acc_bits;
        }
        v = (uint32_t)(acc & ((1ull << bits) - 1));
        acc >>= bits;
        acc_bits -= bits;
        return v;
    }

    template<bool dec>
    uint32_t residual(uint32_t w, uint32_t pred, Model &m) {
        uint32_t r = w - pred, z = (r << 1) ^ (uint32_t)((int32_t)r >> 31);
        uint32_t len = std::min(symbol<dec>(dec ? 0 : bit_length(z), 6, m), 32u);
        z = len > 1 ? (1u << (len - 1)) | raw_bits<dec>(z & ((1u << (len - 1)) - 1), len - 1) : len;
        return dec ? pred + ((z >> 1) ^ (0u - (z & 1))) : w;
    }

    static uint32_t bit_length(uint32_t z) {
#ifdef _MSC_VER
        unsigned long i;
        return _BitScanReverse(&i, z) ? i + 1 : 0;
#else
        return z ? 32 - __builtin_clz(z) : 0;
#endif
    }

    static uint32_t mix(uint32_t h, uint32_t v) {
        return (h ^ v) * 0x9E3779B1u + (h >> 15);
    }

    static constexpr uint32_t header_words = sizeof(halide_trace_packet_t) / 4;
    static constexpr uint32_t parent_word = offsetof(halide_trace_packet_t, parent_id) / 4;
    static_assert(offsetof(halide_trace_packet_t, size) == 0, "");
    static_assert(sizeof(halide_trace_packet_t) % 4 == 0, "");

    // Codes the packet at w. When decoding, w has room for at most
    // max_words words. Returns false if decoding produced an invalid packet
    // size.
    template<bool dec>
    bool packet(uint32_t *w, size_t max_words) {
        halide_trace_packet_t *p = (halide_trace_packet_t *)w;
        uint32_t size = residual<dec>(dec ? 0 : p->size, last_size, size_len);
        uint32_t n = size / 4;
        if (n < header_words || n > max_trace_packet_size / 4 || n > max_words) {
            return false;
        }
        p->size = last_size = size;
        if (n >= ctxs.size()) {
            ctxs.resize(n + 1);
        }
        if (!ctxs[n]) {
            ctxs[n].reset(new Ctx{std::vector<uint8_t>(n), std::vector<Model>(n)});
        }
        Ctx &c = *ctxs[n];

        uint32_t idx = 63;
        for (uint32_t j = 0; !dec && j < 63 && idx == 63; j++) {
            idx = parents[j] == w[parent_word] ? j : idx;
        }
        idx = symbol<dec>(idx, 6, parent_idx);
        w[parent_word] = idx == 63 ? residual<dec>(w[parent_word], parents[0], parent_len) : parents[idx];
        memmove(parents + 1, parents, std::min(idx, 62u) * 4);
        parents[0] = w[parent_word];
        std::vector<uint32_t> &prev = prevs[mix(n, w[parent_word]) & (prevs.size() - 1)];
        prev.resize(n);

        auto predict = [&](int pred, uint32_t i, uint32_t hist) {
            uint32_t a = w[i - 1], b = i > 1 ? w[i - 2] : 0;
            return pred == 0 ? prev[i] : pred == 1 ? 2 * a - b :
                                     pred == 2     ? a + prev[i] - prev[i - 1] :
                                                     hist;
        };
        auto code_word = [&](uint32_t i, uint32_t num_predictors, uint32_t hist) {
            w[i] = residual<dec>(w[i], predict(c.sel[i], i, hist), c.len[i]);
            uint32_t best = ~0u;
            for (uint32_t pred = 0; pred < num_predictors; pred++) {
                uint32_t r = w[i] - predict(pred, i, hist);
                r = (r << 1) ^ (uint32_t)((int32_t)r >> 31);
                if (r < best) {
                    best = r;
                    c.sel[i] = pred;
                }
            }
        };

        for (uint32_t i = 1; i < header_words; i++) {
            if (i != parent_word) {
                code_word(i, 3, 0);
            }
        }
        // Derive the layout from the header, clamped to the packet.
        uint32_t lanes = std::max<uint32_t>(p->value_bytes() ? p->lanes : 1, 1);
        uint32_t coords = std::min((uint32_t)p->dimensions, n - header_words);
        uint32_t value_start = header_words + coords;
        uint32_t value_words = std::min((p->value_bytes() + 3) / 4, n - value_start);
        const uint32_t *coord = w + header_words;
        for (uint32_t i = header_words; i < value_start; i++) {
            code_word(i, 3, 0);
        }
        // The Func name comes before the values, so they can be keyed on it.
        uint32_t name_hash = p->id;
        for (uint32_t i = value_start + value_words; i < n; i++) {
            w[i] = residual<dec>(w[i], prev[i], c.len[i]);
            name_hash = mix(name_hash, w[i]);
        }
        uint32_t match = 0;
        bool matchable = value_words && value_words <= 16;
        for (uint32_t j = 1; !dec && matchable && j < 16 && !match; j++) {
            uint32_t r = (recent_pos - j) & 15;
            match = recent_n[r] == value_words && !memcmp(recent[r], w + value_start, value_words * 4) ? j : 0;
        }
        if (matchable && (match = symbol<dec>(match, 4, match_idx))) {
            memcpy(w + value_start, recent[(recent_pos - match) & 15], value_words * 4);
        }
        for (uint32_t k = 0; k < value_words; k++) {
            uint32_t lane = k * lanes / value_words, h = mix(name_hash, k * lanes % value_words);
            for (uint32_t d = 0; d < coords / lanes; d++) {
                h = mix(h, coord[d * lanes + lane]);
            }
            uint32_t &hist = history[h & (history.size() - 1)];
            if (!match) {
                code_word(value_start + k, 4, hist);
            }
            hist = w[value_start + k];
        }
        if (matchable) {
            recent_n[recent_pos & 15] = value_words;
            memcpy(recent[recent_pos++ & 15], w + value_start, value_words * 4);
        }
        memcpy(prev.data(), w, n * 4);
        return true;
    }

public:
    /** Appends a chunk encoding count consecutive packets, starting at
     * packets, to out. Each packet must have a valid size. */
    void encode_chunk(const halide_trace_packet_t *packets, uint32_t count, std::vector<uint8_t> &out) {
        coded.clear();
        raw.clear();
        lo = acc = acc_bits = 0;
        hi = ~0u;
        std::vector<uint32_t> buf(max_trace_packet_size / 4);
        const uint8_t *pos = (const uint8_t *)packets;
        for (uint32_t i = 0; i < count; i++) {
            uint32_t size = ((const halide_trace_packet_t *)pos)->size;
            memcpy(buf.data(), pos, size);
            packet<false>(buf.data(), buf.size());
            pos += size;
        }
        raw_bits<false>(0, 7);
        for (int k = 0; k < 4; k++, lo <<= 8) {
            coded.push_back(lo >> 24);
        }
        uint32_t header[4] = {count, (uint32_t)(pos - (const uint8_t *)packets), (uint32_t)coded.size(),
                              (uint32_t)raw.size()};
        out.insert(out.end(), (const uint8_t *)header, (const uint8_t *)(header + 4));
        out.insert(out.end(), coded.begin(), coded.end());
        out.insert(out.end(), raw.begin(), raw.end());
    }

    /** Decodes the chunk [in, end) into out, which must have room for the
     * chunk's decoded size. Returns false if the chunk is corrupt. */
    bool decode_chunk(const uint8_t *in, const uint8_t *end, uint8_t *out) {
        uint32_t header[4];
        if (end - in < (ptrdiff_t)sizeof(header)) {
            return false;
        }
        memcpy(header, in, sizeof(header));
        in_coded = in + sizeof(header);
        if ((uint64_t)header[2] + header[3] != (uint64_t)(end - in_coded) || header[1] % 4) {
            return false;
        }
        coded_end = in_raw = in_coded + header[2];
        raw_end = in_raw + header[3];
        lo = x = acc = acc_bits = 0;
        hi = ~0u;
        for (int k = 0; k < 4; k++) {
            x = (x << 8) | (in_coded < coded_end ? *in_coded++ : 0);
        }
        uint32_t *w = (uint32_t *)out, *w_end = w + header[1] / 4;
        for (uint32_t i = 0; i < header[0]; i++) {
            if (!packet<true>(w, w_end - w)) {
                return false;
            }
            w += w[0] / 4;
        }
        return w == w_end;
    }
};

/** Compresses a stream of trace packets, given as bytes split at arbitrary
 * points (e.g. as read from a pipe). */
class TraceCompressor {
    static constexpr size_t chunk_size = 64 << 20;
    std::vector<uint8_t> pending;
    // The number and total size of the complete packets at the start of
    // pending.
    uint32_t complete_count = 0;
    size_t complete = 0;
    bool started = false;

    void start(std::vector<uint8_t> &out) {
        if (!started) {
            out.insert(out.end(), std::begin(trace_compression_magic), std::end(trace_compression_magic));
            started = true;
        }
    }

    // Encodes the complete packets at the start of pending, from offset begin.
    void encode(size_t begin, std::vector<uint8_t> &out) {
        if (complete_count) {
            std::make_unique<TraceCodec>()->encode_chunk((const halide_trace_packet_t *)(pending.data() + begin),
                                                         complete_count, out);
            complete_count = 0;
        }
    }

public:
    /** Appends the given bytes of the trace stream, and appends any
     * compressed output that is ready to out. Returns false if the stream
     * contains a packet with an invalid size. */
    bool write(const void *data, size_t size, std::vector<uint8_t> &out) {
        start(out);
        pending.insert(pending.end(), (const uint8_t *)data, (const uint8_t *)data + size);
        size_t encoded = 0;
        bool ok = true;
        while (complete + sizeof(halide_trace_packet_t) <= pending.size()) {
            uint32_t packet_size = ((const halide_trace_packet_t *)(pending.data() + complete))->size;
            if (packet_size < sizeof(halide_trace_packet_t) || packet_size % 4 || packet_size > max_trace_packet_size) {
                ok = false;
                break;
            }
            if (complete + packet_size > pending.size()) {
                break;
            }
            complete += packet_size;
            complete_count++;
            if (complete - encoded >= chunk_size) {
                encode(encoded, out);
                encoded = complete;
            }
        }
        pending.erase(pending.begin(), pending.begin() + encoded);
        complete -= encoded;
        return ok;
    }

    /** Appends the rest of the compressed output to out. Returns false if
     * the stream ended partway through a packet. */
    bool finish(std::vector<uint8_t> &out) {
        start(out);
        encode(0, out);
        pending.erase(pending.begin(), pending.begin() + complete);
        complete = 0;
        return pending.empty();
    }
};

inline bool is_compressed_trace(const uint8_t *data, size_t size) {
    return size >= sizeof(trace_compression_magic) &&
           !memcmp(data, trace_compression_magic, sizeof(trace_compression_magic));
}

namespace Internal {

struct TraceChunk {
    const uint8_t *begin, *end;
    size_t out_offset, out_size;
};

// Returns false if the chunk headers are corrupt.
inline bool trace_chunks(const uint8_t *data, size_t size, std::vector<TraceChunk> &chunks) {
    if (!is_compressed_trace(data, size)) {
        return false;
    }
    const uint8_t *end = data + size;
    size_t out_offset = 0;
    for (const uint8_t *pos = data + sizeof(trace_compression_magic); pos < end;) {
        uint32_t header[4];
        if (end - pos < (ptrdiff_t)sizeof(header)) {
            return false;
        }
        memcpy(header, pos, sizeof(header));
        uint64_t chunk_bytes = sizeof(header) + (uint64_t)header[2] + header[3];
        if (chunk_bytes > (uint64_t)(end - pos)) {
            return false;
        }
        chunks.push_back({pos, pos + chunk_bytes, out_offset, header[1]});
        out_offset += header[1];
        pos += chunk_bytes;
    }
    return true;
}

}  // namespace Internal

/** Returns the size of the decompressed trace, or SIZE_MAX if data is not a
 * valid compressed trace. */
inline size_t decompressed_trace_size(const uint8_t *data, size_t size) {
    std::vector<Internal::TraceChunk> chunks;
    if (!Internal::trace_chunks(data, size, chunks)) {
        return std::numeric_limits<size_t>::max();
    }
    return chunks.empty() ? 0 : chunks.back().out_offset + chunks.back().out_size;
}

/** Decompresses a compressed trace into out, which must be 4-byte aligned
 * and have room for decompressed_trace_size bytes. Chunks are decoded in
 * parallel. progress is called from the calling thread with the size of the
 * prefix of out that is complete, each time it grows. Returns false if the
 * trace is corrupt. */
template<typename P>
bool decompress_trace(const uint8_t *data, size_t size, uint8_t *out, P progress) {
    std::vector<Internal::TraceChunk> chunks;
    if (!Internal::trace_chunks(data, size, chunks)) {
        return false;
    }
    enum State : uint8_t { Pending,
                           Done,
                           Failed };
    std::vector<State> state(chunks.size(), Pending);
    size_t num_threads = std::max(1u, std::min((unsigned)chunks.size(), std::thread::hardware_concurrency()));
    std::mutex mutex;
    std::condition_variable cv;
    size_t next = 0;
    bool failed = false;
    auto worker = [&]() {
        while (true) {
            size_t i;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (failed || next >= chunks.size()) {
                    return;
                }
                i = next++;
            }
            const Internal::TraceChunk &c = chunks[i];
            bool ok = std::make_unique<TraceCodec>()->decode_chunk(c.begin, c.end, out + c.out_offset);
            {
                std::lock_guard<std::mutex> lock(mutex);
                state[i] = ok ? Done : Failed;
                failed |= !ok;
            }
            cv.notify_all();
        }
    };
    std::vector<std::thread> threads;
    for (size_t t = 0; t < num_threads; t++) {
        threads.emplace_back(worker);
    }
    for (size_t i = 0; i < chunks.size();) {
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&] { return state[i] != Pending; });
            if (failed) {
                break;
            }
            while (i < chunks.size() && state[i] == Done) {
                i++;
            }
        }
        progress(chunks[i - 1].out_offset + chunks[i - 1].out_size);
    }
    for (std::thread &t : threads) {
        t.join();
    }
    return !failed;
}

/** Decompresses a compressed trace into out. Returns false if the trace is
 * corrupt. */
inline bool decompress_trace(const uint8_t *data, size_t size, std::vector<uint8_t> &out) {
    size_t out_size = decompressed_trace_size(data, size);
    if (out_size == std::numeric_limits<size_t>::max()) {
        return false;
    }
    out.resize(out_size);
    return decompress_trace(data, size, out.data(), [](size_t) {});
}

}  // namespace Tools
}  // namespace Halide

#endif  // HALIDE_TRACE_COMPRESSION_H
