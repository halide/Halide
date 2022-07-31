#include "Deinterleave.h"

#include "CSE.h"
#include "Debug.h"
#include "FlattenNestedRamps.h"
#include "IREquality.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "ModulusRemainder.h"
#include "Scope.h"
#include "Simplify.h"
#include "Substitute.h"
#include <numeric>
#include <optional>

namespace Halide {
namespace Internal {

using std::pair;

namespace {

class StoreCollector : public IRMutator {
public:
    const std::string store_name;
    const int store_stride, max_stores;
    std::vector<Stmt> &let_stmts;
    std::vector<Stmt> &stores;

    StoreCollector(const std::string &name, int stride, int ms,
                   std::vector<Stmt> &lets, std::vector<Stmt> &ss)
        : store_name(name), store_stride(stride), max_stores(ms),
          let_stmts(lets), stores(ss), collecting(true) {
    }

private:
    using IRMutator::visit;

    // Don't enter any inner constructs for which it's not safe to pull out stores.
    Stmt visit(const For *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const IfThenElse *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const ProducerConsumer *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const Allocate *op) override {
        collecting = false;
        return op;
    }
    Stmt visit(const Realize *op) override {
        collecting = false;
        return op;
    }

    bool collecting;
    // These are lets that we've encountered since the last collected
    // store. If we collect another store, these "potential" lets
    // become lets used by the collected stores.
    std::vector<Stmt> potential_lets;

    Expr visit(const Load *op) override {
        if (!collecting) {
            return op;
        }

        // If we hit a load from the buffer we're trying to collect
        // stores for, stop collecting to avoid reordering loads and
        // stores from the same buffer.
        if (op->name == store_name) {
            collecting = false;
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const Store *op) override {
        if (!collecting) {
            return op;
        }

        // By default, do nothing.
        Stmt stmt = op;

        if (stores.size() >= (size_t)max_stores) {
            // Already have enough stores.
            collecting = false;
            return stmt;
        }

        // Make sure this Store doesn't do anything that causes us to
        // stop collecting.
        stmt = IRMutator::visit(op);
        if (!collecting) {
            return stmt;
        }

        if (op->name != store_name) {
            // Not a store to the buffer we're looking for.
            return stmt;
        }

        const Ramp *r = op->index.as<Ramp>();
        if (!r || !is_const(r->stride, store_stride)) {
            // Store doesn't store to the ramp we're looking
            // for. Can't interleave it. Since we don't want to
            // reorder stores, stop collecting.
            collecting = false;
            return stmt;
        }

        // This store is good, collect it and replace with a no-op.
        stores.emplace_back(op);
        stmt = Evaluate::make(0);

        // Because we collected this store, we need to save the
        // potential lets since the last collected store.
        let_stmts.insert(let_stmts.end(), potential_lets.begin(), potential_lets.end());
        potential_lets.clear();
        return stmt;
    }

    Expr visit(const Call *op) override {
        if (!op->is_pure()) {
            // Avoid reordering calls to impure functions
            collecting = false;
            return op;
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const LetStmt *op) override {
        if (!collecting) {
            return op;
        }

        // Walk inside the let chain
        Stmt stmt = IRMutator::visit(op);

        // If we're still collecting, we need to save the entire let chain as potential lets.
        if (collecting) {
            Stmt body;
            do {
                potential_lets.emplace_back(op);
                body = op->body;
            } while ((op = body.as<LetStmt>()));
        }
        return stmt;
    }

    Stmt visit(const Block *op) override {
        if (!collecting) {
            return op;
        }

        Stmt first = mutate(op->first);
        Stmt rest = op->rest;
        // We might have decided to stop collecting during mutation of first.
        if (collecting) {
            rest = mutate(rest);
        }
        return Block::make(first, rest);
    }
};

Stmt collect_strided_stores(const Stmt &stmt, const std::string &name, int stride, int max_stores,
                            std::vector<Stmt> lets, std::vector<Stmt> &stores) {

    StoreCollector collect(name, stride, max_stores, lets, stores);
    return collect.mutate(stmt);
}

// Essentially, broadcast the mask, i.e. take each mask element, and repeat it
// \p scale times, consequtively. No reordering of mask elements happens!
std::vector<char> upscale_mask(const std::vector<char> &src_mask, int scale) {
    internal_assert(scale > 1) << "Expected to at least double the mask.";
    std::vector<char> mask;
    mask.reserve(scale * src_mask.size());
    for (char src_mask_elt : src_mask) {
        std::fill_n(std::back_inserter(mask), /*count=*/scale,
                    /*value=*/src_mask_elt);
    }
    return mask;
}

// Compute the elements of the specified ramp/linear space.
std::vector<int> ramp_to_indices(int starting_lane, int lane_stride,
                                 int new_lanes) {
    std::vector<int> indices;
    indices.reserve(new_lanes);
    for (int i = 0; i < new_lanes; i++) {
        indices.emplace_back(starting_lane + lane_stride * i);
    }
    return indices;
}

// What are the (in-order) demanded indices?
std::vector<int> mask_to_indices(const std::vector<char> &mask) {
    std::vector<int> indices;
    indices.reserve(mask.size());
    for (int src_lane = 0; src_lane != (int)mask.size(); ++src_lane) {
        if (mask[src_lane]) {
            indices.emplace_back(src_lane);
        }
    }
    return indices;
}

// Which indices are demanded at all?
template<typename T, typename UnaryOperation, typename UnaryPredicate>
std::vector<char> indices_to_mask(const std::vector<T> &unordered_elts,
                                  int index_end, UnaryOperation get_index,
                                  UnaryPredicate pred) {
    internal_assert(index_end > 0) << "Size should be positive";
    std::vector<char> mask(index_end, 0);
    for (const T &elt : unordered_elts) {
        if (!pred(elt)) {
            continue;
        }
        int index = get_index(elt);
        internal_assert((unsigned)index < mask.size()) << "Wrong size hint.";
        mask[index] = 1;
    }
    return mask;
}

std::vector<char> indices_to_mask(const std::vector<int> &indices,
                                  int index_end) {
    return indices_to_mask(
        indices, index_end,
        /*get_index=*/[](const int &index) { return index; },
        /*pred=*/[](const int &) { return true; });
}

std::vector<char> indices_to_mask(const std::vector<int> &indices) {
    internal_assert(!indices.empty()) << "No indices?";
    return indices_to_mask(indices, 1 + indices.back());
}

// Suppose the \p indices are indices of 32-bit lanes. If we want to instead
// have 16-bit lanes, we'll need to similarly adjust the index granularity.
// NOTE: this operation is precise and not lossy.
std::vector<int> upscale_indices(const std::vector<int> &src_indices,
                                 int scale) {
    std::vector<int> indices;
    indices.reserve(scale * src_indices.size());
    for (int src_index : src_indices) {
        for (int subelt = 0; subelt != scale; ++subelt) {
            indices.emplace_back(scale * src_index + subelt);
        }
    }
    return indices;
}

struct IndexXFormMapping {
    // WARNING: if you modify `indices`, you will need to
    //          manually call `recompute_translation()`!
    std::vector<int> indices;
    std::vector<std::optional<int>> translation;

    template<typename T, typename UnaryOperation, typename UnaryPredicate>
    static IndexXFormMapping get(const std::vector<T> &unordered_elts,
                                 int index_end, UnaryOperation get_index,
                                 UnaryPredicate pred) {
        std::vector<char> index_mask =
            indices_to_mask(unordered_elts, index_end, get_index, pred);

        IndexXFormMapping m{mask_to_indices(index_mask)};
        m.recompute_translation();
        return m;
    }

    void recompute_translation() {
        translation.clear();
        if (indices.empty()) {
            return;
        }
        // An inverse of the `indices`, given original index, contains position
        // of said index within `indices` vector, if present.
        translation.resize(1 + indices.back(), 0);
        for (int index_idx = 0; index_idx != (int)indices.size(); ++index_idx) {
            translation[indices[index_idx]] = index_idx;
        }
    };

    template<typename T, typename UnaryOperation>
    static IndexXFormMapping get(const std::vector<T> &unordered_elts,
                                 int index_end, UnaryOperation get_index) {
        return IndexXFormMapping::get(unordered_elts, index_end, get_index,
                                      /*pred=*/[](const T &) { return true; });
    }
};

struct DecomposedIndex {
    int outer, inner;
};

// How many lanes are there *before* the 0'th lane of the i'th vector?
std::vector<int> get_num_lanes_total_in_preceding_vectors(
    const std::vector<int> &vector_lengths) {
    std::vector<int> accumulated_vector_length;
    // This is not quite the `std::exclusive_scan()`, it produces one more elt,
    // which is the total number of elements in all of the vectors.
    accumulated_vector_length.reserve(1 + vector_lengths.size());
    accumulated_vector_length.emplace_back(0);
    for (int vector_length : vector_lengths) {
        accumulated_vector_length.emplace_back(
            accumulated_vector_length.back() + vector_length);
    }
    return accumulated_vector_length;
};

// Shuffle indices are specified as-if the src vectors are concatenated.
// Decompose that into source vector index and lane within said vector.
std::vector<DecomposedIndex>
decompose_shuffle_indices(const std::vector<int> &vector_lengths,
                          const std::vector<int> &indices) {
    // How many lanes are there *before* the 0'th lane of the i'th vector?
    const std::vector<int> num_lanes_total_in_preceding_vectors =
        get_num_lanes_total_in_preceding_vectors(vector_lengths);

    // Construct a lookup table, that is indexed by the index,
    // and contains the (index of the) vector, to which this index belongs to.
    std::vector<int> lane_to_vector_map;
    lane_to_vector_map.reserve(num_lanes_total_in_preceding_vectors.back());
    for (int vector_index = 0; vector_index != (int)vector_lengths.size();
         ++vector_index) {
        int vector_length = vector_lengths[vector_index];
        std::fill_n(std::back_inserter(lane_to_vector_map),
                    /*num=*/vector_length,
                    /*value=*/vector_index);
    }

    std::vector<DecomposedIndex> decomposed_indices;
    decomposed_indices.reserve(indices.size());
    for (int index : indices) {
        internal_assert((unsigned)index < lane_to_vector_map.size())
            << "Shuffle mask out of bounds.\n";
        DecomposedIndex &i = decomposed_indices.emplace_back();
        i.outer = lane_to_vector_map[index];
        i.inner = index - num_lanes_total_in_preceding_vectors[i.outer];
    }

    return decomposed_indices;
}

std::vector<int> recompose_shuffle_indices(
    const std::vector<int> &vector_lengths,
    const std::vector<DecomposedIndex> &decomposed_indices) {
    // How many lanes are there *before* the 0'th lane of the i'th vector?
    const std::vector<int> num_lanes_total_in_preceding_vectors =
        get_num_lanes_total_in_preceding_vectors(vector_lengths);

    // And *finally*, recompose the indices.
    std::vector<int> indices;
    indices.reserve(decomposed_indices.size());
    for (const DecomposedIndex &i : decomposed_indices) {
        internal_assert((unsigned)i.outer < vector_lengths.size())
            << "Out of bounds vector index";
        indices.emplace_back(i.inner +
                             num_lanes_total_in_preceding_vectors[i.outer]);
    }

    return indices;
}

std::vector<int> get_vector_lane_counts(const std::vector<Expr> &vectors) {
    std::vector<int> lanes;
    lanes.reserve(vectors.size());
    std::transform(vectors.begin(), vectors.end(), std::back_inserter(lanes),
                   [](const Expr &vec) { return vec.type().lanes(); });
    return lanes;
}

int minimal_adjacent_difference(const std::vector<int> &indices) {
    int min_difference = std::numeric_limits<int>::max();
    std::adjacent_find(indices.begin(), indices.end(),
                       [&min_difference](int a, int b) {
                           min_difference = std::min(min_difference, b - a);
                           return false;
                       });
    return min_difference;
}

// Which 1-st order polynominal goes through all of the specified \p indices,
// while going through the minimal amount of the indices *NOT* specified here?
std::optional<std::pair<int /*base*/, int /*min_step*/>>
fit_affine(const std::vector<int> &indices) {
    if (indices.empty()) {
        return {};
    }
    int base = indices.front();
    int min_step =
        indices.size() < 2 ? 1 : minimal_adjacent_difference(indices);
    return std::make_pair(base, min_step);
}

// Return the 1-st order polynominal that exactly specifies \p indices.
// Note: there might not be such a polynominal.
std::optional<std::pair<int /*base*/, int /*step*/>>
as_affine(const std::vector<int> &indices) {
    if (indices.empty()) {
        return {};
    }
    int base = indices.front();
    int step = indices.size() < 2 ? 1 : indices[1] - indices[0];
    auto step_mismatch = [step](int a, int b) {
        int curr_step = b - a;
        return curr_step != step;
    };
    if (std::adjacent_find(std::next(indices.begin()), indices.end(),
                           step_mismatch) != indices.end()) {
        return {};
    }
    return std::make_pair(base, step);
}

// Count of the elements in a linear space from \p a to \b with \p step.
int compute_linspace_length(int a, int b, int step) {
    int distance = b - a;
    internal_assert(distance >= 0 && distance % step == 0)
        << "Unexpected linear space.";
    int num_hops = distance / step;
    return 1 + num_hops;
}

struct DemandedLanes {
    DemandedLanes(const std::vector<int> &indices_)
        : indices(indices_) {
    }

    auto begin() const {
        return indices.begin();
    }
    auto end() const {
        return indices.end();
    }
    auto size() const {
        return indices.size();
    }
    auto empty() const {
        return indices.empty();
    }
    int operator[](int i) const {
        return indices[i];
    }

    std::vector<int> indices;  // Do *NOT* mutate in-place!
};

class Deinterleaver : public IRGraphMutator {
public:
    Deinterleaver(DemandedLanes lanes_, const Scope<> &lets)
        : lanes(std::move(lanes_)), external_lets(lets) {
    }

    using IRGraphMutator::mutate;

    Expr mutate(const Expr &e) override {
        // FIXME: is there really only always-on assertions?
        internal_assert(true ||
                        mask_to_indices(indices_to_mask(
                            lanes.indices, /*index_end=*/e.type().lanes())) ==
                            lanes.indices)
            << "Invalid demanded lane 'mask'.";
        internal_assert(!lanes.empty()) << "No lanes demanded from this Expr.";
        // If all lanes are demanded, we're done.
        if ((int)lanes.size() == e.type().lanes()) {
            return e;
        }
        Type expected_type = e.type().with_lanes(lanes.size());
        Expr m_e = IRGraphMutator::mutate(e);
        internal_assert(m_e.type() == expected_type)
            << "Mutated into an unexpected type.";
        return m_e;
    }

private:
    DemandedLanes lanes;

    // lets for which we have even and odd lane specializations
    const Scope<> &external_lets;

    using IRMutator::visit;

    Expr visit(const VectorReduce *op) override {
        Expr in;
        {
            int factor = op->value.type().lanes() / op->type.lanes();
            std::vector<int> input_lanes =
                upscale_indices(lanes.indices, factor);
            ScopedValue<DemandedLanes> old_lanes(lanes, input_lanes);
            in = mutate(op->value);
        }
        return VectorReduce::make(op->op, in, lanes.size());
    }

    Expr visit(const Broadcast *op) override {
        // `Broadcast` preservation is best-effort, in the sense that we don't
        // want to keep the `Broadcast` if we will `Shuffle` it later, so just
        // let the generic `Shuffle` handling deal with it.
        return mutate(Shuffle::make_broadcast(op->value, op->lanes));
    }

    Expr visit(const Select *op) override {
        Expr condition = op->condition;
        if (!condition.type().is_scalar()) {
            condition = mutate(condition);
        }
        return Select::make(condition, mutate(op->true_value),
                            mutate(op->false_value));
    }

    Expr visit(const Load *op) override {
        Type t = op->type.with_lanes(lanes.size());
        ModulusRemainder align = op->alignment;
        // TODO: Figure out the alignment of every nth lane
        std::optional<std::pair<int /*base*/, int /*step*/>> lanes_affine =
            as_affine(lanes.indices);
        if (!lanes_affine || lanes_affine->first != 0) {
            align = ModulusRemainder();
        }
        return Load::make(t, op->name, mutate(op->index), op->image, op->param,
                          mutate(op->predicate), align);
    }

    Expr visit(const Ramp *op) override {
        // How many lanes are there in the base/stride source vectors?
        std::vector<int> input_vectors_lane_counts;
        input_vectors_lane_counts.reserve(op->lanes);
        std::fill_n(std::back_inserter(input_vectors_lane_counts),
                    /*num=*/op->lanes, /*value=*/op->base.type().lanes());

        // Shuffle indices are specified for the evaluated Ramp,
        // with flattened vectors. Decompose that into the actual Ramp lane
        // and lane within base/stride vectors.
        std::vector<DecomposedIndex> decomposed_input_lanes =
            decompose_shuffle_indices(input_vectors_lane_counts, lanes.indices);

        // Which lanes of the base/stride source vector are demanded *at all*?
        IndexXFormMapping input_lane_xform = IndexXFormMapping::get(
            decomposed_input_lanes, op->base.type().lanes(),
            [](const DecomposedIndex &i) { return i.inner; });

        Expr base = op->base;
        Expr stride = op->stride;

        // Now that we know which lanes of the src vector are demanded, recurse.
        for (Expr *e : {&base, &stride}) {
            ScopedValue<DemandedLanes> old_lanes(lanes,
                                                 input_lane_xform.indices);
            *e = mutate(*e);
        }

        // Which Ramp lanes are actually demanded?
        IndexXFormMapping ramp_lanes_xform = IndexXFormMapping::get(
            decomposed_input_lanes, op->lanes,
            [](const DecomposedIndex &i) { return i.outer; });

        // Which 1-st order polynominal best describes the demanded ramp lanes?
        // This is an optimization problem, we want to minimize the number of
        // to-be-discarded ramp lanes, by maximizing the step, as long as that
        // does *NOT* lead to *NOT* computing actually-demanded ramp lanes...
        std::pair<int /*base*/, int /*min_step*/> ramp_lanes_affine =
            *fit_affine(ramp_lanes_xform.indices);

        // And here's the catch. The best-fit 1-st order polynominal describing
        // demanded ramp lanes may end up producing some ramp lanes that we
        // did not originally demand. We must expect them to be there.
        ramp_lanes_xform.indices = ramp_to_indices(
            ramp_lanes_affine.first, ramp_lanes_affine.second,
            compute_linspace_length(ramp_lanes_xform.indices.front(),
                                    ramp_lanes_xform.indices.back(),
                                    ramp_lanes_affine.second));
        ramp_lanes_xform.recompute_translation();  // !!!

        // Refresh vector lane count knowledge after mutating source vectors.
        input_vectors_lane_counts.clear();
        std::fill_n(std::back_inserter(input_vectors_lane_counts),
                    /*num=*/ramp_lanes_xform.indices.size(),
                    /*value=*/input_lane_xform.indices.size());

        // Now that we (might have) dropped some of the ramp lanes
        // of the source vector, and maybe mutated said source vector,
        // update the decomposed shuffle mask.
        for (DecomposedIndex &i : decomposed_input_lanes) {
            i.outer = *ramp_lanes_xform.translation[i.outer];
            i.inner = *input_lane_xform.translation[i.inner];
        }

        // If some of the ramp lanes aren't demanded at all,
        // we shouldn't produce them.
        if (int ramp_base = ramp_lanes_affine.first; ramp_base != 0) {
            base += cast(base.type(), ramp_base) * stride;
        }
        if (int ramp_step = ramp_lanes_affine.second; ramp_step != 1) {
            stride *= cast(stride.type(), ramp_step);
        }

        // Now that we are done with mutation, let's reconstruct the indices.
        std::vector<int> new_lanes = recompose_shuffle_indices(
            input_vectors_lane_counts, decomposed_input_lanes);

        // Now, we can recreate the hopefully-smaller Ramp.
        Expr in;
        if (int lanes = ramp_lanes_xform.indices.size(); lanes != 1) {
            in = Ramp::make(base, stride, lanes);
        } else {
            in = base;
        }

        // But, it is possible that we *still* don't demand all of the produced
        // lanes, in which case we need a Shuffle after all.
        if (in.type().lanes() != (int)new_lanes.size()) {
            in = Shuffle::make({in}, new_lanes);
        }

        return in;
    }

    Expr visit(const Variable *op) override {
        if (std::optional<std::pair<int /*base*/, int /*step*/>> lanes_affine =
                as_affine(lanes.indices);
            external_lets.contains(op->name) && lanes_affine) {
            Type t = op->type.with_lanes(lanes.size());
            int starting_lane = lanes[0];
            int lane_stride = lanes_affine->second;
            if (starting_lane == 0 && lane_stride == 2) {
                return Variable::make(t, op->name + ".even_lanes", op->image,
                                      op->param, op->reduction_domain);
            } else if (starting_lane == 1 && lane_stride == 2) {
                return Variable::make(t, op->name + ".odd_lanes", op->image,
                                      op->param, op->reduction_domain);
            } else if (starting_lane == 0 && lane_stride == 3) {
                return Variable::make(t, op->name + ".lanes_0_of_3", op->image,
                                      op->param, op->reduction_domain);
            } else if (starting_lane == 1 && lane_stride == 3) {
                return Variable::make(t, op->name + ".lanes_1_of_3", op->image,
                                      op->param, op->reduction_domain);
            } else if (starting_lane == 2 && lane_stride == 3) {
                return Variable::make(t, op->name + ".lanes_2_of_3", op->image,
                                      op->param, op->reduction_domain);
            }
        }

        // Uh-oh, we don't know how to deinterleave this vector expression.
        // Make llvm do it.
        return Shuffle::make({op}, lanes.indices);
    }

    Expr visit(const Cast *op) override {
        Type t = op->type.with_lanes(lanes.size());
        return Cast::make(t, mutate(op->value));
    }

    Expr visit(const Reinterpret *op) override {
        Type src_ty = op->value.type();
        int src_lanes = src_ty.lanes();
        Type tgt_ty = op->type;
        int tgt_lanes = tgt_ty.lanes();

        if (src_lanes == tgt_lanes) {
            // If the number of lanes wasn't changing, just recurse further.
            Type t = tgt_ty.with_lanes(lanes.size());
            return Reinterpret::make(t, mutate(op->value));
        }

        // Otherwise, since `Reinterpret` never changes the total bit count,
        // but the lane count change, so must the width of the lanes, and to
        // simplify further logic, it is best to do so with 'uint'-typed lanes.
        if (!src_ty.is_uint() || !tgt_ty.is_uint()) {
            auto reinterpret = [](Expr e, Type t, halide_type_code_t code) {
                t = t.with_code(code);
                if (e.type() != t) {
                    e = Reinterpret::make(t, e);
                }
                return e;
            };

            Expr in = op->value;
            // Prepare - ensure that the type is `uint`.
            in = reinterpret(in, in.type(), Type::UInt);
            // Perform the actual lane splitting/merging, in `uint`.
            in = reinterpret(in, tgt_ty, Type::UInt);
            // Recover - convert to the expected output type.
            in = reinterpret(in, tgt_ty, tgt_ty.code());
            return mutate(in);
        }

        auto [min_lanes, max_lanes] = std::minmax(src_lanes, tgt_lanes);

        // Likewise, we really need an integral scaling factor.
        // If it is not, we, again, want to expand.
        if (max_lanes % min_lanes != 0) {
            // We want to avoid the precision loss when back-transforming the
            // demanded bits @ tgt_ty into demanded bits @ src_ty, so we want
            // such an immediate type, that has N times more lanes than tgt_ty,
            // while *also* having M times more lanes than src_ty,
            // while minimizing N and M.
            int lanes_lcm = std::lcm(src_lanes, tgt_lanes);  // Optimal.
            internal_assert(lanes_lcm % tgt_ty.lanes() == 0);
            int lane_prescale = lanes_lcm / tgt_ty.lanes();
            internal_assert(tgt_ty.bits() % lane_prescale == 0);
            Type intermediate_ty =
                tgt_ty.with_lanes(lane_prescale * tgt_ty.lanes())
                    .with_bits(tgt_ty.bits() / lane_prescale);
            internal_assert(intermediate_ty.lanes() == lanes_lcm &&
                            lanes_lcm % src_lanes == 0 &&
                            lanes_lcm % tgt_lanes == 0);

            Expr in = op->value;  // of src_ty.
            in = Reinterpret::make(intermediate_ty, in);
            in = Reinterpret::make(tgt_ty, in);
            return mutate(in);
        }

        int scale = max_lanes / min_lanes;

        if (tgt_lanes < src_lanes) {
            // Source vector's lanes are more fine-grained, which means,
            // we only need to round-trip demanded lanes and recurse into it.
            std::vector<int> input_lanes =
                upscale_indices(lanes.indices, scale);
            Type t =
                op->type.with_lanes(lanes.size());  // Before `ScopedValue`!
            ScopedValue<DemandedLanes> old_lanes(lanes, input_lanes);
            Expr in = mutate(op->value);
            return Reinterpret::make(t, in);
        }

        // The ugly case.
        // We are reinterpreting from a vector with *less* fine-grained lanes,
        // so round-trip of the demanded lanes is pessimistic, in the sense that
        // if we only partially demand some of the (wider) lanes,
        // we need to manually drop non-demanded (narrow) lanes.

        // Into how many fine-grained lanes was each coarse-grained lane split?
        std::vector<int> input_vector_lane_subpartition_count;
        input_vector_lane_subpartition_count.reserve(src_lanes);
        std::fill_n(std::back_inserter(input_vector_lane_subpartition_count),
                    /*num=*/src_lanes, /*value=*/scale);

        std::vector<DecomposedIndex> decomposed_input_lanes =
            decompose_shuffle_indices(input_vector_lane_subpartition_count,
                                      lanes.indices);

        // Which coarse-grained lanes of the source vector are demanded at all?
        IndexXFormMapping input_lane_xform = IndexXFormMapping::get(
            decomposed_input_lanes, src_lanes,
            [](const DecomposedIndex &i) { return i.outer; });

        Expr in;
        // Now that we know which lanes of input vector are demanded, recurse.
        {
            ScopedValue<DemandedLanes> old_lanes(lanes,
                                                 input_lane_xform.indices);
            in = mutate(op->value);
        }

        // Now that we (might've) dropped some of the lanes of the input vector,
        // update the decomposed shuffle mask.
        for (DecomposedIndex &i : decomposed_input_lanes) {
            i.outer = *input_lane_xform.translation[i.outer];
        }

        // Refresh vector lane count knowledge after mutating input vector.
        input_vector_lane_subpartition_count.clear();
        std::fill_n(std::back_inserter(input_vector_lane_subpartition_count),
                    /*num=*/in.type().lanes(), /*value=*/scale);

        // Now that we are done with mutation, let's reconstruct the indices.
        std::vector<int> input_lanes = recompose_shuffle_indices(
            input_vector_lane_subpartition_count, decomposed_input_lanes);

        Type t = in.type();
        internal_assert(t.bits() % scale == 0);
        t = t.with_lanes(scale * t.lanes()).with_bits(t.bits() / scale);
        in = Reinterpret::make(t, in);

        // But, it is possible that we *still* don't demand all of the produced
        // lanes, in which case we need a Shuffle after all.
        if (in.type().lanes() != (int)input_lanes.size()) {
            in = Shuffle::make({in}, input_lanes);
        }

        return in;
    }

    Expr visit(const Call *op) override {
        Type t = op->type.with_lanes(lanes.size());

        // Vector calls are always parallel across the lanes, so we
        // can just deinterleave the args.

        // Beware of intrinsics for which this is not true!

        std::vector<Expr> args;
        args.reserve(op->args.size());

        if (op->is_intrinsic(Call::signed_integer_overflow)) {
            // Just keep the (scalar) argument.
            args = op->args;
        } else if (op->is_intrinsic(Call::unsafe_promise_clamped) ||
                   op->is_intrinsic(Call::promise_clamped)) {
            // Only mutate the non-scalar arguments for these.
            for (Expr e : op->args) {
                if (e.type().is_vector()) {
                    e = mutate(e);
                }
                args.emplace_back(e);
            }
        } else {
            args = mutate(op->args);
        }
        return Call::make(t, op->name, args, op->call_type, op->func,
                          op->value_index, op->image, op->param);
    }

    Expr visit(const Shuffle *op) override {
        // NOTE: we don't try to deduplicate `op->vectors` here. It is unclear,
        // however, whether or not doing so here would be beneficial.

        // Backtransform the demanded lanes through the shuffle,
        // what is the recomputed shuffle mask?
        std::vector<int> input_lanes;
        input_lanes.reserve(lanes.size());
        std::transform(lanes.begin(), lanes.end(),
                       std::back_inserter(input_lanes),
                       [&](int lane) { return op->indices[lane]; });

        // And how many lanes are there in each of the source vectors?
        std::vector<int> input_vector_lane_counts =
            get_vector_lane_counts(op->vectors);

        // Shuffle indices are specified as-if the src vectors are concatenated.
        // Decompose that into source vector index and lane within said vector.
        std::vector<DecomposedIndex> decomposed_input_lanes =
            decompose_shuffle_indices(input_vector_lane_counts, input_lanes);

        // Preserving ordering, which source vectors are actually demanded?
        IndexXFormMapping vector_xform = IndexXFormMapping::get(
            decomposed_input_lanes, op->vectors.size(),
            [](const DecomposedIndex &i) { return i.outer; });

        // Preserving ordering, per each demanded source vector,
        // which lanes of said vector are actually demanded?
        std::vector<IndexXFormMapping> vectors_lane_xform;
        vectors_lane_xform.reserve(vector_xform.indices.size());
        for (int source_vector_index : vector_xform.indices) {
            vectors_lane_xform.emplace_back(IndexXFormMapping::get(
                decomposed_input_lanes,
                input_vector_lane_counts[source_vector_index],
                [](const DecomposedIndex &i) { return i.inner; },
                [source_vector_index](const DecomposedIndex &i) {
                    return i.outer == source_vector_index;
                }));
        }

        // Now that we know which input vectors are demanded,
        // and which lanes are demanded from them, recurse into them.
        std::vector<Expr> input_vectors;
        input_vectors.reserve(vector_xform.indices.size());
        for (int vector_index = 0;
             vector_index != (int)vector_xform.indices.size(); ++vector_index) {
            int source_vector_index = vector_xform.indices[vector_index];
            Expr source_vector = op->vectors[source_vector_index];
            const std::vector<int> &source_vector_lanes =
                vectors_lane_xform[vector_index].indices;
            ScopedValue<DemandedLanes> old_lanes(lanes, source_vector_lanes);
            input_vectors.emplace_back(mutate(source_vector));
        }

        // Refresh vector lane count knowledge after mutating input vectors.
        input_vector_lane_counts = get_vector_lane_counts(input_vectors);

        // Now that we (might have) dropped some of the source input vectors,
        // and maybe mutated the others, update the decomposed shuffle mask.
        for (DecomposedIndex &i : decomposed_input_lanes) {
            i.outer = *vector_xform.translation[i.outer];
            i.inner = *vectors_lane_xform[i.outer].translation[i.inner];
        }

        // Now that we are done with mutation, let's reconstruct the indices.
        input_lanes = recompose_shuffle_indices(input_vector_lane_counts,
                                                decomposed_input_lanes);

        // ... and construct the new shuffle.
        Expr in = Shuffle::make(input_vectors, input_lanes);
        const auto *new_op = in.as<Shuffle>();

        // If we ended up with just a single input vector, it's possible that
        // the shuffle simplified into a no-op identity (concat) shuffle,
        // in which case just directly return the only input vector.
        if (new_op->vectors.size() == 1 && new_op->is_concat()) {
            return new_op->vectors[0];
        }

        // Likewise, try to recognize simple variants of `Broadcast`-of-shuffle.
        // This is mainly to recover from `visit(const Broadcast *op)`.
        for (int num_lanes_to_broadcast = 1,
                 lanes_total = (int)new_op->indices.size();
             num_lanes_to_broadcast != lanes_total; ++num_lanes_to_broadcast) {
            if (lanes_total % num_lanes_to_broadcast != 0) {
                continue;  // Can't broadcast this number of lanes.
            }
            int broadcast_factor = lanes_total / num_lanes_to_broadcast;
            // If we split `new_op->indices` into `broadcast_factor` chunks,
            // are all chunks fully identical?
            bool mismatch = false;
            for (int replica = 1; !mismatch && replica != broadcast_factor;
                 ++replica) {
                mismatch |= !std::equal(
                    new_op->indices.begin() + 0 * num_lanes_to_broadcast,
                    new_op->indices.begin() + 1 * num_lanes_to_broadcast,
                    new_op->indices.begin() + replica * num_lanes_to_broadcast);
            }
            if (mismatch) {
                continue;  // Perhaps smaller broadcast factor will succeed?
            }
            std::vector<int> in_lanes(num_lanes_to_broadcast);
            std::iota(in_lanes.begin(), in_lanes.end(), 0);
            ScopedValue<DemandedLanes> old_lanes(lanes, in_lanes);
            Expr vec_to_broadcast = mutate(in);
            return Broadcast::make(vec_to_broadcast, broadcast_factor);
        }

        // We can't do anything more here, just return the new shuffle.
        return in;
    }
};

Expr deinterleave(Expr e, int starting_lane, int lane_stride, int new_lanes, const Scope<> &lets) {
    e = substitute_in_all_lets(e);
    Deinterleaver d(ramp_to_indices(starting_lane, lane_stride, new_lanes),
                    lets);
    e = d.mutate(e);
    e = common_subexpression_elimination(e);
    return simplify(e);
}
}  // namespace

Expr extract_odd_lanes(const Expr &e, const Scope<> &lets) {
    internal_assert(e.type().lanes() % 2 == 0);
    return deinterleave(e, 1, 2, e.type().lanes() / 2, lets);
}

Expr extract_even_lanes(const Expr &e, const Scope<> &lets) {
    internal_assert(e.type().lanes() % 2 == 0);
    return deinterleave(e, 0, 2, (e.type().lanes() + 1) / 2, lets);
}

Expr extract_even_lanes(const Expr &e) {
    internal_assert(e.type().lanes() % 2 == 0);
    Scope<> lets;
    return extract_even_lanes(e, lets);
}

Expr extract_odd_lanes(const Expr &e) {
    internal_assert(e.type().lanes() % 2 == 0);
    Scope<> lets;
    return extract_odd_lanes(e, lets);
}

Expr extract_mod3_lanes(const Expr &e, int lane, const Scope<> &lets) {
    internal_assert(e.type().lanes() % 3 == 0);
    return deinterleave(e, lane, 3, (e.type().lanes() + 2) / 3, lets);
}

Expr extract_lane(const Expr &e, int lane) {
    Scope<> lets;
    return deinterleave(e, lane, e.type().lanes(), 1, lets);
}

namespace {

class Interleaver : public IRMutator {
    Scope<> vector_lets;

    using IRMutator::visit;

    bool should_deinterleave = false;
    int num_lanes;

    Expr deinterleave_expr(const Expr &e) {
        std::vector<Expr> exprs;
        for (int i = 0; i < num_lanes; i++) {
            Scope<> lets;
            exprs.emplace_back(deinterleave(e, i, num_lanes, e.type().lanes() / num_lanes, lets));
        }
        return Shuffle::make_interleave(exprs);
    }

    template<typename T, typename Body>
    Body visit_lets(const T *op) {
        // Visit an entire chain of lets in a single method to conserve stack space.
        struct Frame {
            const T *op;
            Expr new_value;
            ScopedBinding<> binding;
            Frame(const T *op, Expr v, Scope<void> &scope)
                : op(op),
                  new_value(std::move(v)),
                  binding(new_value.type().is_vector(), scope, op->name) {
            }
        };
        std::vector<Frame> frames;
        Body result;

        do {
            result = op->body;
            frames.emplace_back(op, mutate(op->value), vector_lets);
        } while ((op = result.template as<T>()));

        result = mutate(result);

        for (auto it = frames.rbegin(); it != frames.rend(); it++) {
            Expr value = std::move(it->new_value);

            result = T::make(it->op->name, value, result);

            // For vector lets, we may additionally need a let defining the even and odd lanes only
            if (value.type().is_vector()) {
                if (value.type().lanes() % 2 == 0) {
                    result = T::make(it->op->name + ".even_lanes", extract_even_lanes(value, vector_lets), result);
                    result = T::make(it->op->name + ".odd_lanes", extract_odd_lanes(value, vector_lets), result);
                }
                if (value.type().lanes() % 3 == 0) {
                    result = T::make(it->op->name + ".lanes_0_of_3", extract_mod3_lanes(value, 0, vector_lets), result);
                    result = T::make(it->op->name + ".lanes_1_of_3", extract_mod3_lanes(value, 1, vector_lets), result);
                    result = T::make(it->op->name + ".lanes_2_of_3", extract_mod3_lanes(value, 2, vector_lets), result);
                }
            }
        }

        return result;
    }

    Expr visit(const Let *op) override {
        return visit_lets<Let, Expr>(op);
    }

    Stmt visit(const LetStmt *op) override {
        return visit_lets<LetStmt, Stmt>(op);
    }

    Expr visit(const Ramp *op) override {
        if (op->stride.type().is_vector() &&
            is_const_one(op->stride) &&
            !op->base.as<Ramp>() &&
            !op->base.as<Broadcast>()) {
            // We have a ramp with a computed vector base.  If we
            // deinterleave we'll get ramps of stride 1 with a
            // computed scalar base.
            should_deinterleave = true;
            num_lanes = op->stride.type().lanes();
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Mod *op) override {
        const Ramp *r = op->a.as<Ramp>();
        for (int i = 2; i <= 4; ++i) {
            if (r &&
                is_const(op->b, i) &&
                (r->type.lanes() % i) == 0) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Div *op) override {
        const Ramp *r = op->a.as<Ramp>();
        for (int i = 2; i <= 4; ++i) {
            if (r &&
                is_const(op->b, i) &&
                (r->type.lanes() % i) == 0 &&
                r->type.lanes() > i) {
                should_deinterleave = true;
                num_lanes = i;
                break;
            }
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Call *op) override {
        if (!op->is_pure() &&
            !op->is_intrinsic(Call::unsafe_promise_clamped) &&
            !op->is_intrinsic(Call::promise_clamped)) {
            // deinterleaving potentially changes the order of execution.
            should_deinterleave = false;
        }
        return IRMutator::visit(op);
    }

    Expr visit(const Load *op) override {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        bool should_deinterleave_idx = should_deinterleave;

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        bool should_deinterleave_predicate = should_deinterleave;

        Expr expr;
        if (should_deinterleave_idx && (should_deinterleave_predicate || is_const_one(predicate))) {
            // If we want to deinterleave both the index and predicate
            // (or the predicate is one), then deinterleave the
            // resulting load.
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
            expr = deinterleave_expr(expr);
        } else if (should_deinterleave_idx) {
            // If we only want to deinterleave the index and not the
            // predicate, deinterleave the index prior to the load.
            idx = deinterleave_expr(idx);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else if (should_deinterleave_predicate) {
            // Similarly, deinterleave the predicate prior to the load
            // if we don't want to deinterleave the index.
            predicate = deinterleave_expr(predicate);
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else if (!idx.same_as(op->index) || !predicate.same_as(op->index)) {
            expr = Load::make(op->type, op->name, idx, op->image, op->param, predicate, op->alignment);
        } else {
            expr = op;
        }

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;
        return expr;
    }

    Stmt visit(const Store *op) override {
        bool old_should_deinterleave = should_deinterleave;
        int old_num_lanes = num_lanes;

        should_deinterleave = false;
        Expr idx = mutate(op->index);
        if (should_deinterleave) {
            idx = deinterleave_expr(idx);
        }

        should_deinterleave = false;
        Expr value = mutate(op->value);
        if (should_deinterleave) {
            value = deinterleave_expr(value);
        }

        should_deinterleave = false;
        Expr predicate = mutate(op->predicate);
        if (should_deinterleave) {
            predicate = deinterleave_expr(predicate);
        }

        Stmt stmt = Store::make(op->name, value, idx, op->param, predicate, op->alignment);

        should_deinterleave = old_should_deinterleave;
        num_lanes = old_num_lanes;

        return stmt;
    }

    HALIDE_NEVER_INLINE Stmt gather_stores(const Block *op) {
        const LetStmt *let = op->first.as<LetStmt>();
        const Store *store = op->first.as<Store>();

        // Gather all the let stmts surrounding the first.
        std::vector<Stmt> let_stmts;
        while (let) {
            let_stmts.emplace_back(let);
            store = let->body.as<Store>();
            let = let->body.as<LetStmt>();
        }

        // There was no inner store.
        if (!store) {
            return Stmt();
        }

        const Ramp *r0 = store->index.as<Ramp>();

        // It's not a store of a ramp index.
        if (!r0) {
            return Stmt();
        }

        const int64_t *stride_ptr = as_const_int(r0->stride);

        // The stride isn't a constant or is <= 1
        if (!stride_ptr || *stride_ptr <= 1) {
            return Stmt();
        }

        const int64_t stride = *stride_ptr;
        const int lanes = r0->lanes;
        const int64_t expected_stores = stride;

        // Collect the rest of the stores.
        std::vector<Stmt> stores;
        stores.emplace_back(store);
        Stmt rest = collect_strided_stores(op->rest, store->name,
                                           stride, expected_stores,
                                           let_stmts, stores);

        // Check the store collector didn't collect too many
        // stores (that would be a bug).
        internal_assert(stores.size() <= (size_t)expected_stores);

        // Not enough stores collected.
        if (stores.size() != (size_t)expected_stores) {
            return Stmt();
        }

        // Too many stores and lanes to represent in a single vector
        // type.
        int max_bits = sizeof(halide_type_t::lanes) * 8;
        // mul_would_overflow is for signed types, but vector lanes
        // are unsigned, so add a bit.
        max_bits++;
        if (mul_would_overflow(max_bits, stores.size(), lanes)) {
            return Stmt();
        }

        Type t = store->value.type();
        Expr base;
        std::vector<Expr> args(stores.size());
        std::vector<Expr> predicates(stores.size());

        int min_offset = 0;
        std::vector<int> offsets(stores.size());

        std::string load_name;
        Buffer<> load_image;
        Parameter load_param;
        for (size_t i = 0; i < stores.size(); ++i) {
            const Ramp *ri = stores[i].as<Store>()->index.as<Ramp>();
            internal_assert(ri);

            // Mismatched store vector laness.
            if (ri->lanes != lanes) {
                return Stmt();
            }

            Expr diff = simplify(ri->base - r0->base);
            const int64_t *offs = as_const_int(diff);

            // Difference between bases is not constant.
            if (!offs) {
                return Stmt();
            }

            offsets[i] = *offs;
            if (*offs < min_offset) {
                min_offset = *offs;
            }
        }

        // Gather the args for interleaving.
        for (size_t i = 0; i < stores.size(); ++i) {
            int j = offsets[i] - min_offset;
            if (j == 0) {
                base = stores[i].as<Store>()->index.as<Ramp>()->base;
            }

            // The offset is not between zero and the stride.
            if (j < 0 || (size_t)j >= stores.size()) {
                return Stmt();
            }

            // We already have a store for this offset.
            if (args[j].defined()) {
                return Stmt();
            }

            args[j] = stores[i].as<Store>()->value;
            predicates[j] = stores[i].as<Store>()->predicate;
        }

        // One of the stores should have had the minimum offset.
        internal_assert(base.defined());

        // Generate a single interleaving store.
        t = t.with_lanes(lanes * stores.size());
        Expr index = Ramp::make(base, make_one(base.type()), t.lanes());
        Expr value = Shuffle::make_interleave(args);
        Expr predicate = Shuffle::make_interleave(predicates);
        Stmt new_store = Store::make(store->name, value, index, store->param, predicate, ModulusRemainder());

        // Continue recursively into the stuff that
        // collect_strided_stores didn't collect.
        Stmt stmt = Block::make(new_store, mutate(rest));

        // Rewrap the let statements we pulled off.
        while (!let_stmts.empty()) {
            const LetStmt *let = let_stmts.back().as<LetStmt>();
            stmt = LetStmt::make(let->name, let->value, stmt);
            let_stmts.pop_back();
        }

        // Success!
        return stmt;
    }

    Stmt visit(const Block *op) override {
        Stmt s = gather_stores(op);
        if (s.defined()) {
            return s;
        } else {
            Stmt first = mutate(op->first);
            Stmt rest = mutate(op->rest);
            if (first.same_as(op->first) && rest.same_as(op->rest)) {
                return op;
            } else {
                return Block::make(first, rest);
            }
        }
    }

public:
    Interleaver() = default;
};

}  // namespace

Stmt rewrite_interleavings(const Stmt &s) {
    return Interleaver().mutate(s);
}

namespace {

template<typename UnaryFunction>
void for_each_mask(UnaryFunction fun) {
    int max_width = 8;
    std::vector<char> mask;
    mask.reserve(max_width);
    for (int curr_width = 1; curr_width <= max_width; ++curr_width) {
        mask.resize(curr_width);
        for (unsigned pat = 0; (pat >> curr_width) == 0; ++pat) {
            for (int bit = 0; bit < curr_width; ++bit) {
                mask[bit] = (pat >> bit) != 0;
            }
            fun(mask);
        }
    }
}

void check(Expr a, const Expr &even, const Expr &odd) {
    a = simplify(a);
    Expr correct_even = extract_even_lanes(a);
    Expr correct_odd = extract_odd_lanes(a);
    if (!equal(correct_even, even)) {
        internal_error << correct_even << " != " << even << "\n";
    }
    if (!equal(correct_odd, odd)) {
        internal_error << correct_odd << " != " << odd << "\n";
    }
}
}  // namespace

void deinterleave_vector_test() {
    for_each_mask([](std::vector<char> mask) {
        while (!mask.empty() && !mask.back()) {
            mask.pop_back();
        }
        if (mask.empty()) {
            return;
        }
        for (int scale = 2; scale <= 4; ++scale) {
            const auto a =
                indices_to_mask(upscale_indices(mask_to_indices(mask), scale));
            const auto b = upscale_mask(mask, scale);
            internal_assert(a == b) << "Not equivalent?";
        }
    });

    std::pair<Expr, Expr> result;
    Expr x = Variable::make(Int(32), "x");
    Expr ramp = Ramp::make(x + 4, 3, 8);
    Expr ramp_a = Ramp::make(x + 4, 6, 4);
    Expr ramp_b = Ramp::make(x + 7, 6, 4);
    check(ramp, ramp_a, ramp_b);

    Expr broadcast = Broadcast::make(x + 4, 16);
    Expr broadcast_a = Broadcast::make(x + 4, 8);
    const Expr &broadcast_b = broadcast_a;
    check(broadcast, broadcast_a, broadcast_b);

    check(Load::make(ramp.type(), "buf", ramp, Buffer<>(), Parameter(), const_true(ramp.type().lanes()), ModulusRemainder()),
          Load::make(ramp_a.type(), "buf", ramp_a, Buffer<>(), Parameter(), const_true(ramp_a.type().lanes()), ModulusRemainder()),
          Load::make(ramp_b.type(), "buf", ramp_b, Buffer<>(), Parameter(), const_true(ramp_b.type().lanes()), ModulusRemainder()));

    Expr vec_x = Variable::make(Int(32, 4), "vec_x");
    Expr vec_y = Variable::make(Int(32, 4), "vec_y");
    check(Shuffle::make({vec_x, vec_y}, {0, 4, 2, 6, 4, 2, 3, 7, 1, 2, 3, 4}),
          Shuffle::make({vec_x, Shuffle::make_extract_element(vec_y, 0)},
                        {0, 2, 4, 3, 1, 3}),
          Shuffle::make({Shuffle::make_extract_element(vec_x, 2),
                         Shuffle::make({vec_y}, {0, 2, 3})},
                        {1, 2, 0, 3, 0, 1}));

    {
        std::vector<Expr> v;
        for (int i = 0; i != 8; ++i) {
            v.emplace_back(Variable::make(Int(32), "v." + std::to_string(i)));
        }
        Expr x = Shuffle::make_concat(v);
        x = VectorReduce::make(VectorReduce::Or, x, 4);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(
            x,
            VectorReduce::make(
                VectorReduce::Or,
                Shuffle::make_concat({v[0], v[1], v[2], v[3], v[6], v[7]}), 3),
            VectorReduce::make(
                VectorReduce::Or,
                Shuffle::make_concat({v[0], v[1], v[4], v[5], v[6], v[7]}), 3));
    }

    {
        Expr v = Variable::make(Int(32), "v");
        x = Broadcast::make(v, 4);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x, Broadcast::make(v, 3), Broadcast::make(v, 3));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 3; ++i) {
            vars.emplace_back(
                Variable::make(Int(32), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        Expr x = Broadcast::make(v, 2);
        x = Shuffle::make({x}, {0, 0, 1, 0, 2, 0, 0, 2, 1, 2, 2, 2});
        check(x, Broadcast::make(v, 2),
              Shuffle::make({Ramp::make(vars[0], vars[2] - vars[0], 2)},
                            {0, 0, 0, 1, 1, 1}));
    }

    {
        Expr c = Variable::make(Bool(), "c");

        std::vector<Expr> true_vals;
        for (int i = 0; i != 4; ++i) {
            true_vals.emplace_back(
                Variable::make(Int(32), "t." + std::to_string(i)));
        }
        Expr t = Shuffle::make_concat(true_vals);

        std::vector<Expr> false_vals;
        for (int i = 0; i != 4; ++i) {
            false_vals.emplace_back(
                Variable::make(Int(32), "f." + std::to_string(i)));
        }
        Expr f = Shuffle::make_concat(false_vals);

        Expr x = Select::make(c, t, f);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Select::make(c,
                           Shuffle::make_concat(
                               {true_vals[0], true_vals[1], true_vals[3]}),
                           Shuffle::make_concat(
                               {false_vals[0], false_vals[1], false_vals[3]})),
              Select::make(c,
                           Shuffle::make_concat(
                               {true_vals[0], true_vals[2], true_vals[3]}),
                           Shuffle::make_concat(
                               {false_vals[0], false_vals[2], false_vals[3]})));
    }

    {
        std::vector<Expr> cond_vals;
        for (int i = 0; i != 4; ++i) {
            cond_vals.emplace_back(
                Variable::make(Bool(), "c." + std::to_string(i)));
        }
        Expr c = Shuffle::make_concat(cond_vals);

        std::vector<Expr> true_vals;
        for (int i = 0; i != 4; ++i) {
            true_vals.emplace_back(
                Variable::make(Int(32), "t." + std::to_string(i)));
        }
        Expr t = Shuffle::make_concat(true_vals);

        std::vector<Expr> false_vals;
        for (int i = 0; i != 4; ++i) {
            false_vals.emplace_back(
                Variable::make(Int(32), "f." + std::to_string(i)));
        }
        Expr f = Shuffle::make_concat(false_vals);

        Expr x = Select::make(c, t, f);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Select::make(Shuffle::make_concat(
                               {cond_vals[0], cond_vals[1], cond_vals[3]}),
                           Shuffle::make_concat(
                               {true_vals[0], true_vals[1], true_vals[3]}),
                           Shuffle::make_concat(
                               {false_vals[0], false_vals[1], false_vals[3]})),
              Select::make(Shuffle::make_concat(
                               {cond_vals[0], cond_vals[2], cond_vals[3]}),
                           Shuffle::make_concat(
                               {true_vals[0], true_vals[2], true_vals[3]}),
                           Shuffle::make_concat(
                               {false_vals[0], false_vals[2], false_vals[3]})));
    }

    {
        std::vector<Expr> indices;
        for (int i = 0; i != 4; ++i) {
            indices.emplace_back(
                Variable::make(Int(32), "index." + std::to_string(i)));
        }
        Expr index = Shuffle::make_concat(indices);

        std::vector<Expr> predicates;
        for (int i = 0; i != 4; ++i) {
            predicates.emplace_back(
                Variable::make(Int(32), "predicate." + std::to_string(i)));
        }
        Expr predicate = Shuffle::make_concat(predicates);

        Expr x = Load::make(Int(32, 4), {}, index, {}, {}, predicate, {});
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Load::make(
                  Int(32, 3), {},
                  Shuffle::make_concat({indices[0], indices[1], indices[3]}),
                  {}, {},
                  Shuffle::make_concat(
                      {predicates[0], predicates[1], predicates[3]}),
                  {}),
              Load::make(
                  Int(32, 3), {},
                  Shuffle::make_concat({indices[0], indices[2], indices[3]}),
                  {}, {},
                  Shuffle::make_concat(
                      {predicates[0], predicates[2], predicates[3]}),
                  {}));
    }

    {
        Expr base = Variable::make(Int(32), "base");
        Expr stride = Variable::make(Int(32), "stride");
        Expr x = Ramp::make(base, stride, 16);
        x = Shuffle::make({x}, {3, 2, 5, 2, 9, 2, 11, 2});
        check(x,
              Shuffle::make({Ramp::make((stride * 3) + base, stride * 2, 5)},
                            {0, 1, 3, 4}),
              Broadcast::make((stride * 2) + base, 4));
    }

    {
        std::vector<Expr> bases;
        for (int i = 0; i != 4; ++i) {
            bases.emplace_back(
                Variable::make(Int(32), "base." + std::to_string(i)));
        }
        Expr base = Shuffle::make_concat(bases);

        std::vector<Expr> strides;
        for (int i = 0; i != 4; ++i) {
            strides.emplace_back(
                Variable::make(Int(32), "stride." + std::to_string(i)));
        }
        Expr stride = Shuffle::make_concat(strides);

        Expr x = Ramp::make(base, stride, 2);
        x = Shuffle::make({x}, {0, 0, 2, 1, 3, 3, 4, 4, 6, 5, 7, 7});
        check(
            x,
            Ramp::make(
                Shuffle::make_concat({bases[0], bases[2], bases[3]}),
                Shuffle::make_concat({strides[0], strides[2], strides[3]}), 2),
            Ramp::make(
                Shuffle::make_concat({bases[0], bases[1], bases[3]}),
                Shuffle::make_concat({strides[0], strides[1], strides[3]}), 2));
    }

    {
        std::vector<Expr> bases;
        for (int i = 0; i != 4; ++i) {
            bases.emplace_back(
                Variable::make(Int(32), "base." + std::to_string(i)));
        }
        Expr base = Shuffle::make_concat(bases);

        std::vector<Expr> strides;
        for (int i = 0; i != 4; ++i) {
            strides.emplace_back(
                Variable::make(Int(32), "stride." + std::to_string(i)));
        }
        Expr stride = Shuffle::make_concat(strides);

        Expr x = Ramp::make(base, stride, 16);
        x = Shuffle::make(
            {x}, {1, 3, 2, 3, 4, 3, 6, 3, 8, 3, 9, 3, 16, 3, 17, 3, 18, 3});
        check(
            x,
            Shuffle::make(
                {Ramp::make(
                    Shuffle::make_concat({bases[0], bases[1], bases[2]}),
                    Shuffle::make_concat({strides[0], strides[1], strides[2]}),
                    5)},
                {1, 2, 3, 5, 6, 7, 12, 13, 14}),
            Broadcast::make(bases[3], 9));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 4; ++i) {
            vars.emplace_back(
                Variable::make(Int(32), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Cast::make(Int(64, 4), v);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Cast::make(Int(64, 3),
                         Shuffle::make_concat({vars[0], vars[1], vars[3]})),
              Cast::make(Int(64, 3),
                         Shuffle::make_concat({vars[0], vars[2], vars[3]})));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 4; ++i) {
            vars.emplace_back(
                Variable::make(Int(32), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Reinterpret::make(Float(32, 4), v);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Reinterpret::make(Float(32, 3), Shuffle::make_concat(
                                                  {vars[0], vars[1], vars[3]})),
              Reinterpret::make(
                  Float(32, 3),
                  Shuffle::make_concat({vars[0], vars[2], vars[3]})));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 8; ++i) {
            vars.emplace_back(
                Variable::make(Int(16), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Reinterpret::make(Float(32, 4), v);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Reinterpret::make(Float(32, 3), Shuffle::make_concat(
                                                  {vars[0], vars[1], vars[2],
                                                   vars[3], vars[6], vars[7]})),
              Reinterpret::make(
                  Float(32, 3),
                  Shuffle::make_concat(
                      {vars[0], vars[1], vars[4], vars[5], vars[6], vars[7]})));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 9; ++i) {
            vars.emplace_back(Variable::make(Int(8), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Reinterpret::make(Int(12, 6), v);
        x = Shuffle::make({x}, {0, 2, 1, 3});
        check(x,
              Reinterpret::make(Int(12, 2), Shuffle::make_concat(
                                                {vars[0], vars[1], vars[2]})),
              Reinterpret::make(Int(12, 2), Shuffle::make_concat(
                                                {vars[3], vars[4], vars[5]})));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 9; ++i) {
            vars.emplace_back(Variable::make(Int(8), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Reinterpret::make(Int(12, 6), v);
        x = Shuffle::make({x}, {0, 1, 3, 2});
        check(x,
              Reinterpret::make(
                  Int(12, 2),
                  Shuffle::make(
                      {Reinterpret::make(UInt(4, 8), Shuffle::make_concat(
                                                         {vars[0], vars[1],
                                                          vars[4], vars[5]}))},
                      {0, 1, 2, 5, 6, 7})),
              Reinterpret::make(
                  Int(12, 2),
                  Shuffle::make_slice(
                      Reinterpret::make(
                          UInt(4, 8), Shuffle::make_concat({vars[1], vars[2],
                                                            vars[3], vars[4]})),
                      1, 1, 6)));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 6; ++i) {
            vars.emplace_back(
                Variable::make(Int(12), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Reinterpret::make(Int(8, 9), v);
        x = Shuffle::make({x}, {0, 2, 1, 3, 3, 4, 4, 5});
        check(x,
              Reinterpret::make(
                  Int(8, 4),
                  Shuffle::make(
                      {Reinterpret::make(UInt(4, 12), Shuffle::make_concat(
                                                          {vars[0], vars[1],
                                                           vars[2], vars[3]}))},
                      {0, 1, 2, 3, 6, 7, 8, 9})),
              Reinterpret::make(
                  Int(8, 4),
                  Shuffle::make_slice(
                      Reinterpret::make(
                          UInt(4, 9),
                          Shuffle::make_concat({vars[1], vars[2], vars[3]})),
                      1, 1, 8)));
    }

    {
        std::vector<Expr> vars;
        for (int i = 0; i != 4; ++i) {
            vars.emplace_back(
                Variable::make(Int(32), "v." + std::to_string(i)));
        }
        Expr v = Shuffle::make_concat(vars);
        x = Call::make(Int(32, 4), Call::IntrinsicOp::abs, {v},
                       Call::PureIntrinsic);
        x = Shuffle::make({x}, {0, 0, 1, 2, 3, 3});
        check(x,
              Call::make(Int(32, 3), Call::IntrinsicOp::abs,
                         {Shuffle::make_concat({vars[0], vars[1], vars[3]})},
                         Call::PureIntrinsic),
              Call::make(Int(32, 3), Call::IntrinsicOp::abs,
                         {Shuffle::make_concat({vars[0], vars[2], vars[3]})},
                         Call::PureIntrinsic));
    }

    std::cout << "deinterleave_vector test passed" << std::endl;
}

}  // namespace Internal
}  // namespace Halide
