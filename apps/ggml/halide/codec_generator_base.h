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

#include "quant_components.h"

namespace ggml_halide {

enum class Direction { Quantize,
                       Dequantize };

// SchemeAndBytes itself now lives in quant_components.h (its `scheme` is
// held as a polymorphic owning handle -- a single leaf, a Compose, or a
// TrustedInverse, whichever the format is; see the make_*() factories
// there) -- moved there so those factories can return it directly instead
// of every Generator switch hand-summing a byte count alongside a bare
// scheme.

template<typename Derived, Direction dir>
class CodecGeneratorBase : public Halide::Generator<Derived> {
public:
    void configure() {
        using namespace Halide;
        SchemeAndBytes sb = static_cast<Derived *>(this)->build_scheme();

        // A structured scheme's encoded form is a first-class 1-D Type::Struct
        // block (one struct per block index); an unported one is the flat 2-D
        // (byte, blk) UInt(8) buffer. block_type.bytes() is the on-disk width in
        // the struct case -- no separately-threaded block_bytes needed.
        const bool structured = sb.block_type.is_struct();

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

        // Bind compute_offline() to a properly-named ImageParam of the packed
        // block's shape up front, instead of letting it mint one named after
        // whatever internal Func produced r.encoded[0]. Only Dequantize below
        // adopts it as a port (named "blocks_in" rather than reusing Quantize's
        // output name "blocks_out"); the two are never both real ports at once,
        // but both objects always exist.
        ImageParam blocks_in = structured ? ImageParam(sb.block_type, 1, "blocks_in") : ImageParam(UInt(8), 2, "blocks_in");

        // Severs `identity` from `input`/encode() entirely: `q.offline`
        // recomputes r.encoded (quantize) from `input`, while `identity`
        // (post-severance) instead reads from `blocks_in` (dequantize).
        ComputeOfflineResult q = Pipeline({identity}).compute_offline(r.encoded, {blocks_in});

        if constexpr (dir == Direction::Quantize) {
            input.dim(0).set_min(0);

            // A thin renamed passthrough gives the compiled Output a clean name
            // (the way `blocks_in` did for the Input side); Halide inlines it.
            Func blocks_out("blocks_out");
            Var byte("byte"), blk("blk");
            if (structured) {
                blocks_out(blk) = q.offline.outputs()[0](blk);
                blocks_out.output_buffer().dim(0).set_min(0);
            } else {
                blocks_out(byte, blk) = q.offline.outputs()[0](byte, blk);
                blocks_out.output_buffer().dim(0).set_bounds(0, sb.block_bytes);
                blocks_out.output_buffer().dim(1).set_min(0);
            }

            this->add_input(input);
            this->add_output(blocks_out);
        } else {
            if (structured) {
                blocks_in.dim(0).set_min(0);
            } else {
                blocks_in.dim(0).set_bounds(0, sb.block_bytes);
                blocks_in.dim(1).set_min(0);
            }
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
