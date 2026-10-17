#pragma once

// Harness for GGML-format quantized kernels. Every kernel computes
//   out(n, m) = sum_k W(k, n) * A(k, m)    (out stored as out[m * N + n])
// with W in a ggml weight type and A in an activation type; vec_dot is the
// N = M = 1 case. Providers (GGML CPU/Metal/BLAS, Halide) are registered in
// one table and compared against an f64 oracle and each other.

#include "HalideRuntime.h"
#include "ggml-cpu.h"
#include "ggml.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace gq {

enum Op { GQ_vec_dot = 1,
          GQ_mul_mat = 2,
          GQ_codec = 4 };

struct Shape {
    std::string model, layer;
    int64_t K, N, M;
};

// What a kernel does to the numbers, so that the oracle tolerance is derived
// from it rather than tuned (see oracle.cpp).
struct Precision {
    int acc_bits = 24;                      // accumulator significand bits (24: f32, 11: f16)
    int64_t chain = 0;                      // terms per accumulator before an f32 tree reduction; 0 = K
    int operand_bits = 0;                   // operands rounded to this many bits before multiplying; 0 = exact
    ggml_type act_quant = GGML_TYPE_COUNT;  // activations re-quantized internally to this type
    int64_t act_block = 0;                  // ... with 8-bit absmax blocks of this size (0: act_quant's)
    int64_t w_block = 0;                    // weights re-quantized to 8-bit absmax blocks of this size
};

// Raw case data, in GGML's on-disk layouts.
struct Inputs {
    ggml_type wt, at;
    Shape s;
    int threads;
    const std::vector<uint8_t> *w;  // N rows of ggml_row_size(wt, K) bytes; w[0] holds the first copy
    int copies;                     // distinct weight copies cycled through (>1: cache-cold)
    const std::vector<uint8_t> *a;  // M rows of ggml_row_size(at, K) bytes
};

struct Kernel {
    virtual ~Kernel() = default;
    virtual void run(int reps) = 0;     // timed; must be synchronous
    virtual void read(float *out) = 0;  // N * M results of the last run
    std::string path;                   // what the provider dispatched
    Precision prec;
    int batch = 1;  // ops computed per rep
};

struct Provider {
    std::string name;
    int ops;            // mask of Op
    bool timed = true;  // false: checked build, correctness only
    // nullptr if the provider does not support the case.
    std::function<std::unique_ptr<Kernel>(const Inputs &)> make;
};

std::vector<Provider> &providers();
void register_ggml_providers();
void register_halide_providers();

// vec_dot through GGML's CPU ABI (ggml_vec_dot_t); shared by GGML and Halide.
std::unique_ptr<Kernel> vec_dot_kernel(ggml_vec_dot_t f, const Inputs &in);

// Oracle (oracle.cpp): f64 dot products of GGML's own decoding (to_float) of
// the inputs, on a sample of the outputs.
struct Reference {
    int64_t K;
    std::vector<int64_t> ns, ms;  // sampled outputs (all of them for small shapes)
    std::vector<float> w, a, a0;  // decoded sampled rows of W and A; A before conversion to in.at
    std::vector<double> out;      // [m * ns.size() + n]
};
Reference reference(const Inputs &in, const std::vector<float> &a_f32);
// max over sampled outputs of |got - ref| / tolerance; <= 1 passes.
double error_ratio(const Inputs &in, const Reference &ref, const Precision &p, const float *got);

// Shapes (shapes.cpp).
std::vector<Shape> shapes(const std::string &preset, int op);

// Timing (bench.cpp).
struct Timing {
    double median, lo, hi;  // ns per rep; [lo, hi] is a 95% CI of the median
    int samples, reps;
};
std::vector<Timing> time_interleaved(const std::vector<Kernel *> &ks, int rounds, double min_sample_ms);

// Codecs (codec.cpp): a format's Halide quantize (row of f32 -> blocks) and
// dequantize (blocks -> f32), checked bitwise against GGML's reference
// quantizer (ggml_quantize_chunk) and to_float, and timed against them.
// Halide functions are called through their _argv entry points: quantize
// (x, ports...), dequantize (ports..., y), with one buffer per encoded port.
using Argv = int (*)(void **);
struct CodecRow {
    std::string type;
    Argv quantize[2];  // [checked, bench]
    Argv dequantize[2];
    const halide_filter_metadata_t *(*metadata)();  // of quantize: arguments 1... are the ports
    Argv scaled[2];                                 // checked quantize, dequantize with scaled codes
};
std::vector<CodecRow> &codec_rows();
struct CodecResult {
    std::string type, dir, provider, path;
    int64_t K;
    std::string detail;  // empty: bit-exact
    bool timed;
    Timing t;
    int64_t rows = 1;  // of K values each
};
std::vector<CodecResult> run_codecs(const std::vector<int64_t> &Ks, const std::vector<std::string> &filters, bool check, int rounds, double min_ms);

// A Halide format "<type>[.<codes>][.<rows>x<chunk>|.soa]" (schemes/schemes.h).
struct FormatSpec {
    std::string type, codes;
    int rows = 1, chunk = 0;
    bool soa = false;
};
FormatSpec parse_format(const std::string &spec);
// N rows of K values of type t from GGML's blocks to f's layout: as is (AoS),
// GGML's repack (rows > 1), or planar (soa, for blocks of an fp16 scale and
// then codes: the codes of every block, then their scales, one array per
// port); false if f has no such layout.
bool relayout(const FormatSpec &f, ggml_type t, const uint8_t *src, uint8_t *dst, int64_t N, int64_t K);

// A dense buffer over host memory.
struct HBuf {
    halide_buffer_t b{};
    halide_dimension_t d[4]{};
    HBuf(const void *host, halide_type_t t, const std::vector<int64_t> &extents);
    HBuf(const HBuf &) = delete;
};
// A kernel's encoded ports, its arguments [first, first + n) in `md`, over
// `bytes` in port order: each port's records are `records` (e.g. [blocks,
// rows]); a port with one more dimension holds its elements first, as many
// as fill `record_bytes` with the other ports' records.
std::vector<std::unique_ptr<HBuf>> port_buffers(const halide_filter_metadata_t *md, int first, int n, const uint8_t *bytes,
                                                const std::vector<int64_t> &records, int64_t record_bytes);
// GGML's repack (repack.cpp) of N rows of K values of type t, from its blocks
// to f's layout; false if GGML has no such layout.
bool ggml_repack(const FormatSpec &f, ggml_type t, const uint8_t *src, uint8_t *dst, int64_t N, int64_t K);
// The bytes GGML's CPU_REPACK buffer holds for the same weights; returns its
// layout's name (e.g. q4_0_4x8), empty if it does not repack them.
std::string ggml_repack_live(ggml_type t, const uint8_t *src, std::vector<uint8_t> &dst, int64_t N, int64_t K);

// Logging: GGML log lines captured since the last call.
std::vector<std::string> take_log();
int probe_metal(int argc, char **argv);
extern bool verbose;
extern std::string self_exe;
// Hold the Halide thread pool's keep-awake count while timing Halide kernels
// (default true; --no-keep-awake).
extern bool halide_keep_awake;

}  // namespace gq
