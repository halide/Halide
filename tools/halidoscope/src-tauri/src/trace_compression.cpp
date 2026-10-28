#include "halide_trace_compression.h"

extern "C" bool halide_trace_is_compressed(const uint8_t *data, size_t size) {
    return Halide::Tools::is_compressed_trace(data, size);
}

// Returns the decompressed size of a trace written by Pipeline::halidoscope,
// or SIZE_MAX if data is not a valid compressed trace.
extern "C" size_t halide_trace_decompressed_size(const uint8_t *data, size_t size) {
    return Halide::Tools::decompressed_trace_size(data, size);
}

// Decompresses a trace into out, which must be 4-byte aligned and hold
// halide_trace_decompressed_size bytes, calling progress with the size of the
// complete prefix of out each time it grows. Returns false if the trace is
// corrupt.
extern "C" bool halide_trace_decompress(const uint8_t *data, size_t size, uint8_t *out, void *ctx,
                                        void (*progress)(void *, size_t)) {
    return Halide::Tools::decompress_trace(data, size, out, [&](size_t ready) {
        progress(ctx, ready);
    });
}
