#include "Halide.h"

// A loop vectorized with VectorizeIn::SWAR inside a CUDA kernel computes on
// its lanes packed into a 64-bit register. PTX registers are scalar, so the
// lanes of a SIMD-vectorized loop are each extracted, computed on and put
// back. Here every thread decodes the low nibbles of 8 bytes into 8 signed
// codes, staged in shared memory: a load, a few word operations and a store.
// The decode widens each byte before masking it and narrows the result, as a
// decode written without regard to lanes does, which must not stop the lanes
// from packing.

using namespace Halide;
using namespace Halide::Internal;

namespace {

// Find the GPU kernel source embedded in a compiled module.
class FindGPUSource : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) override {
        if (op->image.defined() &&
            ends_with(op->image.name(), "_gpu_source_kernels")) {
            const char *data = (const char *)op->image.data();
            source += std::string(data, data + op->image.number_of_elements());
        }
    }

public:
    std::string source;
};

int count(const std::string &src, const std::string &pattern) {
    int n = 0;
    for (size_t i = src.find(pattern); i != std::string::npos; i = src.find(pattern, i + 1)) {
        n++;
    }
    return n;
}

Expr decode(const Expr &byte) {
    Expr nibble = cast<uint8_t>((cast<uint32_t>(byte) >> 4) % 16);
    return cast<int8_t>(cast<int32_t>(nibble) - 8);
}

// The kernel source of a pipeline compiled for CUDA.
std::string compile_to_ptx(Func out) {
    Module m = out.compile_to_module(out.infer_arguments(), out.name(),
                                     Target("x86-64-linux-cuda-cuda_capability_70"));
    FindGPUSource finder;
    for (const auto &lf : m.functions()) {
        lf.body.accept(&finder);
    }
    return finder.source;
}

// Nothing here says where a thread's 8 bytes start: the input's rows are an
// odd number of bytes apart and the output starts at an odd coordinate, so
// most of the words are not at multiples of 8 bytes, which PTX requires of a
// 64-bit access. Each word is loaded and stored a byte at a time instead, and
// is still computed on whole.
bool unaligned() {
    ImageParam in(UInt(8), 2, "in");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi"), xv("xv");
    Func out("out");
    out(x, y) = decode(in(x, y));
    out.split(x, x, xv, 8, TailStrategy::RoundUp)
        .gpu_tile(x, y, xo, yo, xi, yi, 8, 4, TailStrategy::RoundUp)
        .vectorize(xv, VectorizeIn::SWAR);

    const std::string ptx = compile_to_ptx(out);
    auto bytes = [&](const std::string &access) {
        int n = 0;
        for (const char *type : {".b8", ".u8", ".nc.b8", ".nc.u8"}) {
            n += count(ptx, access + type);
        }
        return n;
    };
    const int loads = count(ptx, "ld.global"), stores = count(ptx, "st.global");
    const int lane_ops = count(ptx, "cvt.u8") + count(ptx, "cvt.s8") + count(ptx, "prmt");
    if (loads == 0 || stores == 0 || bytes("ld.global") != loads || bytes("st.global") != stores || lane_ops != 0) {
        std::cout << "Expected every load and store of an unaligned word to be of single bytes, "
                  << "and no operations on bytes between them, but found " << loads << " loads ("
                  << bytes("ld.global") << " of bytes), " << stores << " stores ("
                  << bytes("st.global") << " of bytes) and " << lane_ops
                  << " operations on bytes in:\n"
                  << ptx << "\n";
        return false;
    }

    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        return true;
    }
    const int W = 64, H = 8, x0 = 3, y0 = 1;
    Buffer<uint8_t> input(W + 7, H + 2);
    input.fill([](int x, int y) { return (uint8_t)(x * 37 + y * 101 + (x * y) % 7); });
    in.set(input);
    Buffer<int8_t> result(W, H);
    result.set_min(x0, y0);
    out.realize(result, t);
    result.copy_to_host();
    for (int yy = y0; yy < y0 + H; yy++) {
        for (int xx = x0; xx < x0 + W; xx++) {
            int expected = (input(xx, yy) >> 4) - 8;
            if (result(xx, yy) != expected) {
                printf("unaligned: out(%d, %d) = %d instead of %d\n", xx, yy, result(xx, yy), expected);
                return false;
            }
        }
    }
    return true;
}

}  // namespace

int main(int argc, char **argv) {
    ImageParam in(UInt(8), 2, "in");
    Var x("x"), y("y"), xo("xo"), yo("yo"), xi("xi"), yi("yi");
    Func f("f"), out("out");
    f(x, y) = decode(in(x, y));
    out(x, y) = cast<int32_t>(f(x, y)) * f(63 - x, y);

    // Rows are 64 bytes apart and tiles start at multiples of 64, so each
    // thread's 8 bytes start at a multiple of 8 (given an input that is
    // aligned to them).
    in.set_host_alignment(8);
    in.dim(0).set_min(0);
    in.dim(1).set_min(0).set_stride(64);
    out.output_buffer().dim(0).set_min(0);
    out.gpu_tile(x, y, xo, yo, xi, yi, 64, 4, TailStrategy::RoundUp);
    f.compute_at(out, xo)
        .store_in(MemoryType::GPUShared)
        .split(x, xo, xi, 8)
        .vectorize(xi, VectorizeIn::SWAR)
        .gpu_threads(xo, y);

    const std::string ptx = compile_to_ptx(out);
    // One 64-bit load of the input and one 64-bit store per thread, and no
    // byte taken out of a register on the way.
    const int loads = count(ptx, "ld.global"), words = count(ptx, "st.shared.b64") + count(ptx, "st.shared.u64");
    const int stores = count(ptx, "st.shared"), bytes = count(ptx, "cvt.u8") + count(ptx, "cvt.s8") + count(ptx, "prmt");
    if (loads != 1 || words != 1 || stores != 1 || bytes != 0) {
        std::cout << "Expected one load of the input, one 64-bit store to shared memory and "
                  << "no operations on bytes, but found " << loads << " loads, " << stores
                  << " stores (" << words << " of 64 bits) and " << bytes
                  << " operations on bytes in:\n"
                  << ptx << "\n";
        return 1;
    }

    if (!unaligned()) {
        return 1;
    }

    Target t = get_jit_target_from_environment();
    if (!t.has_feature(Target::CUDA)) {
        printf("[SKIP] The instructions are as expected, but running them needs CUDA.\n");
        return 0;
    }
    const int W = 64, H = 8;
    Buffer<uint8_t> input(W, H);
    input.fill([](int x, int y) { return (uint8_t)(x * 37 + y * 101 + (x * y) % 7); });
    in.set(input);
    Buffer<int32_t> result = out.realize({W, H}, t);
    result.copy_to_host();
    for (int yy = 0; yy < H; yy++) {
        for (int xx = 0; xx < W; xx++) {
            int expected = ((input(xx, yy) >> 4) - 8) * ((input(63 - xx, yy) >> 4) - 8);
            if (result(xx, yy) != expected) {
                printf("out(%d, %d) = %d instead of %d\n", xx, yy, result(xx, yy), expected);
                return 1;
            }
        }
    }

    printf("Success!\n");
    return 0;
}
