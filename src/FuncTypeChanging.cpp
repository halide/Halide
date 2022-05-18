#include "FuncTypeChanging.h"

namespace Halide {

static bool operator==(const Var &a, const Var &b) {
    return a.same_as(b);
}

namespace FuncTypeChanging {

// NOTE: Precondition: `chunk_idx u< num_chunks`!
static Expr get_nth_chunk(const Expr &value, const Type &chunk_type,
                          const Expr &chunk_idx, ChunkOrder chunk_order) {
    int num_bits_total = value.type().bits();
    int num_bits_per_chunk = chunk_type.bits();
    int num_chunks = num_bits_total / num_bits_per_chunk;
    user_assert(num_bits_total > num_bits_per_chunk &&
                num_bits_total % num_bits_per_chunk == 0 && num_chunks > 1)
        << "Input value must evenly partition into several chunks.\n";

    Expr low_chunk_idx = chunk_order == ChunkOrder::LowestFirst ?
                             chunk_idx :
                             (num_chunks - 1) - chunk_idx;
    Expr num_low_padding_bits = num_bits_per_chunk * low_chunk_idx;
    Expr chunk_bits = extract_bits(value, num_low_padding_bits,
                                   make_unsigned(num_bits_per_chunk));
    return cast(chunk_type, chunk_bits);
}

static Expr concatenate_chunks(std::vector<Expr> chunks,
                               ChunkOrder chunk_order) {
    const Type chunk_type = chunks.front().type();
    const int chunk_width = chunk_type.bits();
    Type final_type = chunk_type.with_bits(chunk_width * chunks.size());

    if (chunk_order != ChunkOrder::LowestFirst) {
        std::reverse(std::begin(chunks), std::end(chunks));
    }

    Expr res = Internal::make_zero(final_type);
    for (size_t chunk_idx = 0; chunk_idx != chunks.size(); ++chunk_idx) {
        Expr wide_chunk = cast(final_type, chunks[chunk_idx]);  // zero ext
        Expr positioned_chunk = wide_chunk << (chunk_width * chunk_idx);
        res = res | positioned_chunk;
    }

    return res;
}

static Func narrow(const Func &wide_input, const Type &dst_type, int num_chunks,
                   const Var &dim, const std::string &name,
                   ChunkOrder chunk_order) {
    const std::vector<Var> dims = wide_input.args();
    user_assert(count(begin(dims), end(dims), dim) == 1)
        << "Expected dimension " << dim << " to represent "
        << "exactly one function argument!\n";

    Expr wide_elt_idx = dim / num_chunks;
    Expr chunk_idx = make_unsigned(dim % num_chunks);

    std::vector<Expr> args;
    args.reserve(dims.size());
    std::transform(dims.begin(), dims.end(), std::back_inserter(args),
                   [dim, wide_elt_idx](const Var &input_dim) {
                       return input_dim.same_as(dim) ? wide_elt_idx : input_dim;
                   });

    Func narrowed(name);
    narrowed(dims) =
        get_nth_chunk(wide_input(args), dst_type, chunk_idx, chunk_order);

    return narrowed;
}

static Func widen(const Func &narrow_input, const Type &dst_type,
                  int num_chunks, const Var &dim, const std::string &name,
                  ChunkOrder chunk_order) {
    const std::vector<Var> dims = narrow_input.args();
    user_assert(count(begin(dims), end(dims), dim) == 1)
        << "Expected dimension " << dim << " to represent "
        << "exactly one function argument!\n";

    auto dim_index =
        std::distance(begin(dims), std::find(begin(dims), end(dims), dim));

    std::vector<Expr> baseline_args;
    baseline_args.reserve(dims.size());
    std::transform(dims.begin(), dims.end(), std::back_inserter(baseline_args),
                   [](const Var &input_dim) { return input_dim; });

    std::vector<Expr> chunks;
    chunks.reserve(num_chunks);
    std::generate_n(
        std::back_inserter(chunks), num_chunks,
        [&chunks, baseline_args, dim_index, num_chunks, dim, narrow_input]() {
            int chunk_idx = chunks.size();
            std::vector<Expr> args = baseline_args;
            args[dim_index] = (num_chunks * dim) + chunk_idx;
            return narrow_input(args);
        });

    Func widened(name);
    widened(dims) = concatenate_chunks(chunks, chunk_order);

    return widened;
}

Func change_type(const Func &input, const Type &dst_type, const Var &dim,
                 const std::string &name, ChunkOrder chunk_order) {
    const Type &src_type = input.output_type();
    int src_width = src_type.bits();
    int dst_width = dst_type.bits();
    bool is_widening = dst_width > src_width;
    auto [min_width, max_width] = std::minmax(src_width, dst_width);
    int num_chunks = max_width / min_width;
    user_assert(src_type.with_bits(dst_width) == dst_type &&
                src_type.is_uint() && src_width != dst_width &&
                max_width % min_width == 0 && num_chunks > 1)
        << "The source type " << src_type << " and destination type "
        << dst_type << " must be similar uint types with different widths, "
        << "larger width must be an integral multiple of the smaller width.\n";

    return is_widening ?
               widen(input, dst_type, num_chunks, dim, name, chunk_order) :
               narrow(input, dst_type, num_chunks, dim, name, chunk_order);
}

}  // namespace FuncTypeChanging

}  // namespace Halide
