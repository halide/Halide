#pragma once

// Shared configure()/generate() scaffolding for every *_quant_generators.cpp
// file's Direction-templated Generator (SymmetricCodecGenerator,
// LookupTableCodecGenerator, KQuantCodecGenerator): all three build the
// exact same "real ImageParam -> approximate_by -> compute_offline -> adopt
// one half as a port" pipeline in configure(), differing only in how their
// SchemeAndBytes gets built. This factors that shared body out via CRTP
// (Derived::build_scheme()) -- the same static-polymorphism idiom
// Halide::Generator<T> itself already uses (see its own `T` template
// parameter), not a virtual method: the concrete type is always known at
// compile time, so there's no reason to pay for a vtable. Confirmed safe to
// insert as a base class between a leaf Generator and Halide::Generator<T>:
// GeneratorParam/Input/Output discovery is address-range-based (see
// Generator.cpp's ObjectInstanceRegistry::register_instance/
// instances_in_range), not declaration-order or hierarchy-position based,
// so it doesn't matter which class in the chain declares them.
//
// Usage:
//   class FooCodecGenerator : public CodecGeneratorBase<FooCodecGenerator<dir>, dir> {
//   public:
//       GeneratorParam<...> whatever{...};
//       SchemeAndBytes build_scheme() const { return ::build_scheme(whatever); }
//   };

#include "Halide.h"

namespace ggml_halide {

enum class Direction { Quantize,
                       Dequantize };

struct SchemeAndBytes {
    // The scheme is held as a polymorphic owning handle -- a single leaf, a
    // Compose, or a TrustedInverse, whichever the format is (see the make_*()
    // factories in quant_components.h) -- rather than a concrete Compose, so a
    // one-Approximation scheme needn't be wrapped in a degenerate Compose{x}.
    std::unique_ptr<Halide::Approximation> scheme;
    int block_bytes;
};

template<typename Derived, Direction dir>
class CodecGeneratorBase : public Halide::Generator<Derived> {
public:
    void configure() {
        using namespace Halide;
        SchemeAndBytes sb = static_cast<Derived *>(this)->build_scheme();

        // The "obvious" identity: a real ImageParam (never a placeholder --
        // that's what lets *both* directions share this one call below)
        // flowing through unchanged.
        Var x("x");
        ImageParam input(Float(32), 1, "x");
        Func identity("y");
        identity(x) = input(x);

        ApproximationResult r = Func(input).approximate_by(*sb.scheme, {identity});
        for (Func h : r.handles) {
            h.compute_root();
        }

        // Every scheme here produces a single 2-D uint8 packed byte buffer
        // as its encoded form -- bind compute_offline() to a properly-named
        // ImageParam of that shape up front, instead of letting it mint one
        // named after whatever internal Func happened to produce
        // r.encoded[0] (e.g. "struct_pack_packed"). Only Dequantize below
        // adopts it as a port (named "blocks_in" rather than reusing
        // Quantize's output name "blocks_out" below, so the two don't
        // collide and get uniquified within this same configure() call --
        // they're never both real ports at once, but both objects always
        // exist).
        ImageParam blocks_in(UInt(8), 2, "blocks_in");

        // Severs `identity` from `input`/encode() entirely: `q.offline`
        // recomputes r.encoded (quantize) from `input`, while `identity`
        // (post-severance) instead reads from `blocks_in` (dequantize).
        // Each direction below adopts exactly one of these two independent
        // halves; the other is simply never registered as a port and so
        // never gets compiled in.
        ComputeOfflineResult q = Pipeline({identity}).compute_offline(r.encoded, {blocks_in});

        if constexpr (dir == Direction::Quantize) {
            input.dim(0).set_min(0);

            // q.offline.outputs()[0] is r.encoded[0] itself (an internally-
            // named Func) -- a thin renamed passthrough is the only way to
            // give the compiled Output a clean name, the same way
            // `blocks_in` above did for the Input side; Halide inlines it
            // away, so this costs nothing.
            Func blocks_out("blocks_out");
            Var byte("byte"), blk("blk");
            blocks_out(byte, blk) = q.offline.outputs()[0](byte, blk);
            blocks_out.output_buffer().dim(0).set_bounds(0, sb.block_bytes);
            blocks_out.output_buffer().dim(1).set_min(0);

            this->add_input(input);
            this->add_output(blocks_out);
        } else {
            blocks_in.dim(0).set_bounds(0, sb.block_bytes);
            blocks_in.dim(1).set_min(0);
            identity.output_buffer().dim(0).set_min(0);

            this->add_input(blocks_in);
            this->add_output(identity);
        }
    }

    void generate() {
        // Nothing left to do: configure() already built (and, via
        // add_input/add_output, wired up) the whole pipeline.
    }
};

}  // namespace ggml_halide
