#ifndef HALIDE_APPROXIMATION_COMPONENTS_H
#define HALIDE_APPROXIMATION_COMPONENTS_H

/** \file
 * Reusable Approximation building blocks for typed records, scalar storage,
 * fixed-width bit fields, and block quantization.
 */

#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Approximation.h"
#include "Buffer.h"
#include "IROperator.h"
#include "RDom.h"

namespace Halide {

namespace Internal {

inline std::vector<Var> approximation_component_vars(int dimensions, const std::string &prefix) {
    std::vector<Var> vars;
    vars.reserve(dimensions);
    for (int i = 0; i < dimensions; ++i) {
        vars.emplace_back(prefix + std::to_string(i));
    }
    return vars;
}

inline std::vector<Expr> approximation_component_exprs(const std::vector<Var> &vars) {
    return std::vector<Expr>(vars.begin(), vars.end());
}

}  // namespace Internal

/** Losslessly reshape a flat row into fixed-size records. In block-indexed
 * mode the flat side is `(within, record)` rather than a single flat index. */
class BlockReshape : public Approximation {
public:
    explicit BlockReshape(int block_size, bool block_indexed = false)
        : extents_{block_size}, block_indexed_(block_indexed) {
    }
    explicit BlockReshape(std::vector<int> extents, bool block_indexed = false)
        : extents_(std::move(extents)), block_indexed_(block_indexed) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1) << "BlockReshape::encode expects one input\n";
        Func flat = inputs[0];
        std::vector<Var> dims = block_vars();
        Var blk("blk");
        Expr within = cast<int>(0);
        int stride = 1;
        for (size_t i = 0; i < dims.size(); ++i) {
            within += dims[i] * stride;
            stride *= extents_[i];
        }
        std::vector<Var> args = dims;
        args.push_back(blk);
        Func packed("block_reshape_packed");
        packed(args) = block_indexed_ ? flat(within, blk) : flat(blk * block_size() + within);
        return {{packed}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1) << "BlockReshape::decode expects one input\n";
        Func packed = encoded[0];
        Var k("k"), kk("kk"), blk("blk");
        Expr within = block_indexed_ ? Expr(kk) : k % block_size();
        Expr block = block_indexed_ ? Expr(blk) : k / block_size();
        std::vector<Expr> args;
        Expr rem = within;
        for (int extent : extents_) {
            args.push_back(rem % extent);
            rem /= extent;
        }
        args.push_back(block);
        Func out("block_reshape_unpacked");
        if (block_indexed_) {
            out(kk, blk) = packed(args);
        } else {
            out(k) = packed(args);
        }
        return {{out}, {}};
    }

private:
    std::vector<int> extents_;
    bool block_indexed_;

    int block_size() const {
        int size = 1;
        for (int extent : extents_) {
            size *= extent;
        }
        return size;
    }

    std::vector<Var> block_vars() const {
        std::vector<Var> vars;
        for (size_t i = 0; i < extents_.size(); ++i) {
            vars.emplace_back(extents_.size() == 1 ? "kk" : "d" + std::to_string(i));
        }
        return vars;
    }
};

/** Map consecutive logical Func slots to named fields of an exact struct
 * type. Scalar fields have `record_dimensions` dimensions; array fields have
 * an additional leading element dimension. */
class StructLayout : public Approximation {
public:
    StructLayout(Type record_type, std::vector<std::string> logical_fields,
                 int record_dimensions = 1)
        : record_type_(record_type), logical_fields_(std::move(logical_fields)),
          record_dimensions_(record_dimensions) {
        user_assert(record_type_.is_struct()) << "StructLayout requires a struct Type\n";
        user_assert(record_dimensions_ > 0) << "StructLayout record dimensionality must be positive\n";
        const StructTypeInfo *info = record_type_.struct_type();
        user_assert(logical_fields_.size() == info->fields.size())
            << "StructLayout requires exactly one logical slot per physical field\n";
        for (const std::string &name : logical_fields_) {
            int matches = 0;
            for (const StructField &field : info->fields) {
                matches += field.name == name;
            }
            user_assert(matches == 1) << "StructLayout: no unique field named '" << name << "'\n";
            int logical_matches = 0;
            for (const std::string &logical_name : logical_fields_) {
                logical_matches += logical_name == name;
            }
            user_assert(logical_matches == 1) << "StructLayout: duplicate logical field '" << name << "'\n";
        }
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == logical_fields_.size())
            << "StructLayout::encode input count does not match logical field count\n";
        const StructTypeInfo *info = record_type_.struct_type();
        std::vector<Var> records = Internal::approximation_component_vars(record_dimensions_, "record");
        std::vector<Expr> record_args = Internal::approximation_component_exprs(records);
        std::vector<Expr> values;
        for (const StructField &field : info->fields) {
            size_t slot = logical_slot(field.name);
            Func input = inputs[slot];
            user_assert(input.outputs() == 1 && input.types()[0] == field.type)
                << "StructLayout field '" << field.name << "' requires exact type " << field.type
                << " but slot " << slot << " has " << input.types()[0] << "\n";
            int extent = field.array_extent.value_or(1);
            user_assert(input.dimensions() == record_dimensions_ + (field.array_extent ? 1 : 0))
                << "StructLayout field '" << field.name << "' has the wrong dimensionality\n";
            for (int element = 0; element < extent; ++element) {
                std::vector<Expr> args = record_args;
                if (field.array_extent) {
                    args.insert(args.begin(), element);
                }
                values.push_back(input(args));
            }
        }
        Func packed("struct_layout_packed");
        packed(records) = pack_struct(record_type_, values);
        return {{packed}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1 && encoded[0].outputs() == 1 &&
                    encoded[0].types()[0] == record_type_)
            << "StructLayout::decode requires one Func of the exact record type\n";
        Func packed = encoded[0];
        std::vector<Var> records = Internal::approximation_component_vars(record_dimensions_, "record");
        std::vector<Expr> record_args = Internal::approximation_component_exprs(records);
        Expr record = packed(record_args);
        std::vector<Func> outputs;
        outputs.reserve(logical_fields_.size());
        for (const std::string &name : logical_fields_) {
            const StructField &physical = physical_field(name);
            Func output("struct_layout_" + name);
            if (physical.array_extent) {
                Var element("element");
                std::vector<Var> args = records;
                args.insert(args.begin(), element);
                output(args) = field(record, name)[element];
            } else {
                output(records) = field(record, name);
            }
            outputs.push_back(output);
        }
        return {outputs, {}};
    }

private:
    Type record_type_;
    std::vector<std::string> logical_fields_;
    int record_dimensions_;

    size_t logical_slot(const std::string &name) const {
        for (size_t i = 0; i < logical_fields_.size(); ++i) {
            if (logical_fields_[i] == name) {
                return i;
            }
        }
        user_error << "StructLayout internal field mapping failure\n";
        return 0;
    }

    const StructField &physical_field(const std::string &name) const {
        for (const StructField &field : record_type_.struct_type()->fields) {
            if (field.name == name) {
                return field;
            }
        }
        user_error << "StructLayout internal physical field failure\n";
        return record_type_.struct_type()->fields[0];
    }
};

/** Explicit numeric conversion between a decoded computation type and the
 * exact type stored in a representation. */
template<typename Decoded, typename Storage>
class StorageCast : public Approximation {
public:
    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1 && inputs[0].types() == std::vector<Type>{type_of<Decoded>()})
            << "StorageCast::encode input type mismatch\n";
        Func input = inputs[0];
        std::vector<Var> args = Internal::approximation_component_vars(input.dimensions(), "cast");
        Func stored("storage_cast_stored");
        stored(args) = strict_float(cast<Storage>(input(Internal::approximation_component_exprs(args))));
        return {{stored}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{type_of<Storage>()})
            << "StorageCast::decode storage type mismatch\n";
        Func input = encoded[0];
        std::vector<Var> args = Internal::approximation_component_vars(input.dimensions(), "cast");
        Func decoded("storage_cast_decoded");
        decoded(args) = strict_float(cast<Decoded>(input(Internal::approximation_component_exprs(args))));
        return {{decoded}, {}};
    }
};

/** Losslessly translate an integral decoded alphabet by a fixed offset into
 * its stored integral alphabet. This is representation policy, not bit
 * packing: e.g. signed q4 codes [-8, 7] become stored nibbles [0, 15] before
 * PlanarFieldPack handles their physical layout. */
template<typename Decoded, typename Storage>
class AdditiveOffset : public Approximation {
    static_assert(std::is_integral_v<Decoded> && std::is_integral_v<Storage>,
                  "AdditiveOffset requires integral types");

public:
    explicit AdditiveOffset(int64_t offset)
        : offset_(offset) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1 && inputs[0].types() == std::vector<Type>{type_of<Decoded>()})
            << "AdditiveOffset::encode input type mismatch\n";
        Func input = inputs[0];
        std::vector<Var> args = Internal::approximation_component_vars(input.dimensions(), "offset");
        std::vector<Expr> call_args = Internal::approximation_component_exprs(args);
        Func stored("additive_offset_stored");
        Expr offset = Internal::make_const(Int(64), offset_);
        stored(args) = cast<Storage>(cast<int64_t>(input(call_args)) + offset);
        return {{stored}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{type_of<Storage>()})
            << "AdditiveOffset::decode storage type mismatch\n";
        Func input = encoded[0];
        std::vector<Var> args = Internal::approximation_component_vars(input.dimensions(), "offset");
        std::vector<Expr> call_args = Internal::approximation_component_exprs(args);
        Func decoded("additive_offset_decoded");
        Expr offset = Internal::make_const(Int(64), offset_);
        decoded(args) = cast<Decoded>(cast<int64_t>(input(call_args)) - offset);
        return {{decoded}, {}};
    }

private:
    int64_t offset_;
};

/** Convert a scalar integral word per record to/from a leading little-endian
 * byte dimension. Decode deliberately uses concat_bits so struct lowering and
 * ordinary byte buffers share the same wide-load optimization path. */
template<typename Word>
class LittleEndianScalarPack : public Approximation {
public:
    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1 && inputs[0].types() == std::vector<Type>{type_of<Word>()})
            << "LittleEndianScalarPack::encode word type mismatch\n";
        Func word = inputs[0];
        std::vector<Var> records = Internal::approximation_component_vars(word.dimensions(), "record");
        std::vector<Expr> record_args = Internal::approximation_component_exprs(records);
        Var byte("byte");
        std::vector<Var> args = records;
        args.insert(args.begin(), byte);
        Func bytes("little_endian_scalar_bytes");
        Expr bits = cast(type_of<Word>(), word(record_args));
        bytes(args) = cast<uint8_t>(bits >> (byte * 8));
        return {{bytes}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{UInt(8)} &&
                    encoded[0].dimensions() >= 2)
            << "LittleEndianScalarPack::decode requires byte arrays per record\n";
        Func bytes = encoded[0];
        std::vector<Var> records = Internal::approximation_component_vars(bytes.dimensions() - 1, "record");
        std::vector<Expr> record_args = Internal::approximation_component_exprs(records);
        std::vector<Expr> pieces;
        for (size_t i = 0; i < sizeof(Word); ++i) {
            std::vector<Expr> args = record_args;
            args.insert(args.begin(), (int)i);
            pieces.push_back(bytes(args));
        }
        Func word("little_endian_scalar_word");
        word(records) = cast<Word>(concat_bits(pieces));
        return {{word}, {}};
    }
};

/** Pack a fixed vector containing exactly two values into an integer word.
 * Decode uses an embedded 8x256 byte-expansion LUT, allowing each source byte
 * to expand through one contiguous eight-byte load. */
template<typename Value>
class BinaryAlphabetPack : public Approximation {
public:
    BinaryAlphabetPack(int vector_size, Type word_type, Value zero_value, Value one_value)
        : vector_size_(vector_size), word_type_(word_type), zero_value_(zero_value), one_value_(one_value),
          expansion_(8, 256) {
        user_assert(word_type_.is_uint() && word_type_.bits() >= vector_size_)
            << "BinaryAlphabetPack word is too small for its vector\n";
        user_assert(vector_size_ > 0 && vector_size_ % 8 == 0)
            << "BinaryAlphabetPack vector size must be a positive multiple of eight\n";
        for (int byte = 0; byte < 256; ++byte) {
            for (int bit = 0; bit < 8; ++bit) {
                expansion_(bit, byte) = (byte & (1 << bit)) ? one_value_ : zero_value_;
            }
        }
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1 && inputs[0].dimensions() >= 2)
            << "BinaryAlphabetPack::encode requires (element, record...)\n";
        Func values = inputs[0];
        int records_n = values.dimensions() - 1;
        std::vector<Var> records = Internal::approximation_component_vars(records_n, "record");
        std::vector<Expr> record_args = Internal::approximation_component_exprs(records);
        RDom bit(0, vector_size_, "binary_bit");
        std::vector<Expr> value_args = record_args;
        value_args.insert(value_args.begin(), bit);
        Expr value = values(value_args);
        Func word("binary_alphabet_word");
        word(records) = cast(word_type_, 0);
        word(records) = word(record_args) |
                        select(value == cast<Value>(one_value_),
                               cast(word_type_, 1) << bit, cast(word_type_, 0));
        return {{word}, {word}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{word_type_})
            << "BinaryAlphabetPack::decode word type mismatch\n";
        Func word = encoded[0];
        std::vector<Var> records = Internal::approximation_component_vars(word.dimensions(), "record");
        std::vector<Expr> record_args = Internal::approximation_component_exprs(records);
        Var element("element");
        Expr bits = word(record_args);
        Expr byte = cast<int32_t>((bits >> ((element / 8) * 8)) & 0xff);
        std::vector<Var> args = records;
        args.insert(args.begin(), element);
        Func values("binary_alphabet_values");
        values(args) = expansion_(element % 8, byte);
        return {{values}, {}};
    }

private:
    int vector_size_;
    Type word_type_;
    Value zero_value_, one_value_;
    Buffer<Value> expansion_;
};

/** Split a signed code into an unsigned low digit and an additive weighted
 * high contribution. The second parameter recenters the signed code before
 * taking the low digit; decode is simply `code = low + high`. */
class AdditiveRadixSplit : public Approximation {
public:
    AdditiveRadixSplit(int radix, int offset)
        : radix_(radix), offset_(offset) {
        user_assert(radix_ > 1 && offset_ >= 0) << "Invalid AdditiveRadixSplit parameters\n";
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1) << "AdditiveRadixSplit::encode expects one code Func\n";
        Func code = inputs[0];
        std::vector<Var> args = Internal::approximation_component_vars(code.dimensions(), "code");
        std::vector<Expr> call_args = Internal::approximation_component_exprs(args);
        Expr value = cast<int32_t>(code(call_args));
        Expr low_value = (value + offset_) % radix_;
        Func low("additive_radix_low"), high("additive_radix_high");
        low(args) = cast<uint8_t>(low_value);
        high(args) = cast<int8_t>(value - low_value);
        return {{low, high}, {}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 2 && encoded[0].dimensions() == encoded[1].dimensions())
            << "AdditiveRadixSplit::decode expects low and high contributions\n";
        std::vector<Var> args = Internal::approximation_component_vars(encoded[0].dimensions(), "code");
        std::vector<Expr> call_args = Internal::approximation_component_exprs(args);
        Func code("additive_radix_code");
        code(args) = cast<int8_t>(cast<int32_t>(encoded[0](call_args)) +
                                  cast<int32_t>(encoded[1](call_args)));
        return {{code}, {}};
    }

private:
    int radix_, offset_;
};

/** Exact fixed-width planar packing. For `(field_bits, positions)`, one byte
 * contains `8/field_bits` planes, each plane spanning `positions` consecutive
 * elements. This component applies no recentering and no lookup policy. */
class PlanarFieldPack : public Approximation {
public:
    PlanarFieldPack(int field_bits, int positions)
        : field_bits_(field_bits), positions_(positions), planes_(8 / field_bits) {
        user_assert(field_bits_ > 0 && 8 % field_bits_ == 0 && positions_ > 0)
            << "Invalid PlanarFieldPack shape\n";
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1 && inputs[0].dimensions() == 2)
            << "PlanarFieldPack::encode currently requires (element, record)\n";
        Func fields = inputs[0];
        Var position("position"), record("record");
        RDom plane(0, planes_, "plane");
        Expr element = plane * positions_ + position;
        Expr value = cast<uint8_t>(fields(element, record)) & ((1 << field_bits_) - 1);
        Func bytes("planar_field_bytes");
        bytes(position, record) = cast<uint8_t>(0);
        bytes(position, record) = bytes(position, record) |
                                  cast<uint8_t>(value << (plane * field_bits_));
        return {{bytes}, {bytes}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 1 && encoded[0].types() == std::vector<Type>{UInt(8)} &&
                    encoded[0].dimensions() == 2)
            << "PlanarFieldPack::decode currently requires (position, record) bytes\n";
        Func bytes = encoded[0];
        Var element("element"), record("record");
        Expr plane = element / positions_;
        Expr position = element % positions_;
        Func fields("planar_field_values");
        fields(element, record) = cast<uint8_t>((bytes(position, record) >> (plane * field_bits_)) &
                                                ((1 << field_bits_) - 1));
        return {{fields}, {}};
    }

private:
    int field_bits_, positions_, planes_;
};

enum class BlockRoundingMode {
    Nearest,
    TruncateHalfUpWithOffset,
    SignOnly,
    NearestEvenClampedHigh
};

enum class BlockScaleAnchor {
    AbsMax,
    ExtremeSignedValue,
    MeanAbs,
    ExtremeSignedValueTwoStep
};

inline Expr approximation_nearest_int(Expr value) {
    Expr rounded = value + 12582912.0f;
    Expr bits = reinterpret<int32_t>(rounded);
    return (bits & 0x007fffff) - 0x00400000;
}

/** Symmetric per-block int8 quantization with explicit rounding and scale
 * selection policies. */
class SymmetricBlockQuantize : public Approximation {
public:
    SymmetricBlockQuantize(int block_size, int qmax, BlockRoundingMode rounding, BlockScaleAnchor anchor)
        : block_size_(block_size), qmax_(qmax), rounding_(rounding), anchor_(anchor) {
    }

    EncodeResult encode(std::vector<Func> inputs) override {
        user_assert(inputs.size() == 1) << "SymmetricBlockQuantize::encode expects one block Func\n";
        Func block = inputs[0];
        Var kk("kk"), blk("blk");
        RDom r(0, block_size_, "r");
        Func stat("symmetric_quantize_stat"), scale("symmetric_quantize_scale"), reciprocal("symmetric_quantize_reciprocal");
        auto define_extreme = [&]() {
            stat(blk) = Tuple(0.0f, 0.0f);
            Expr value = block(r, blk);
            Expr take = abs(value) > stat(blk)[0];
            stat(blk) = Tuple(select(take, abs(value), stat(blk)[0]),
                              select(take, value, stat(blk)[1]));
        };
        if (anchor_ == BlockScaleAnchor::AbsMax) {
            stat(blk) = 0.0f;
            stat(blk) = max(stat(blk), abs(block(r, blk)));
            scale(blk) = stat(blk) / (float)qmax_;
            reciprocal(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else if (anchor_ == BlockScaleAnchor::ExtremeSignedValue) {
            define_extreme();
            scale(blk) = stat(blk)[1] * (-1.0f / (float)qmax_);
            reciprocal(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else if (anchor_ == BlockScaleAnchor::MeanAbs) {
            stat(blk) = 0.0f;
            stat(blk) += abs(block(r, blk));
            scale(blk) = stat(blk) / (float)block_size_;
            reciprocal(blk) = select(scale(blk) != 0.0f, 1.0f / scale(blk), 0.0f);
        } else {
            define_extreme();
            reciprocal(blk) = select(stat(blk)[0] == 0.0f, 0.0f,
                                     (-1.0f * (float)qmax_) / stat(blk)[1]);
            scale(blk) = select(reciprocal(blk) != 0.0f, 1.0f / reciprocal(blk), 0.0f);
        }
        Expr scaled = block(kk, blk) * reciprocal(blk);
        Func codes("symmetric_quantize_codes");
        if (rounding_ == BlockRoundingMode::Nearest) {
            codes(kk, blk) = cast<int8_t>(round(scaled));
        } else if (rounding_ == BlockRoundingMode::TruncateHalfUpWithOffset) {
            Expr raw = cast<int32_t>(cast<int8_t>(scaled + (float)qmax_ + 0.5f));
            codes(kk, blk) = cast<int8_t>(min(raw, 2 * qmax_ - 1) - qmax_);
        } else if (rounding_ == BlockRoundingMode::SignOnly) {
            codes(kk, blk) = cast<int8_t>(select(block(kk, blk) >= 0.0f, 1, -1));
        } else {
            codes(kk, blk) = cast<int8_t>(min(qmax_, approximation_nearest_int(scaled)));
        }
        return {{codes, scale}, {stat}};
    }

    DecodeResult decode(std::vector<Func> encoded) override {
        user_assert(encoded.size() == 2) << "SymmetricBlockQuantize::decode expects codes and scale\n";
        Var kk("kk"), blk("blk");
        Func dequantized("symmetric_dequantized");
        dequantized(kk, blk) = cast<float>(encoded[0](kk, blk)) * encoded[1](blk);
        return {{dequantized}, {}};
    }

private:
    int block_size_, qmax_;
    BlockRoundingMode rounding_;
    BlockScaleAnchor anchor_;
};

}  // namespace Halide

#endif
