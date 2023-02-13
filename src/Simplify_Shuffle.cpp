#include "Deinterleave.h"
#include "IROperator.h"
#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

using std::vector;

Expr Simplify::visit(const Shuffle *op, ExprInfo *bounds) {
    if (op->is_extract_element()) {
        int index = op->indices[0];
        internal_assert(index >= 0);
        for (const Expr &vector : op->vectors) {
            if (index < vector.type().lanes()) {
                if (vector.as<Variable>()) {
                    // If we try to extract_lane of a variable, we'll just get
                    // the same shuffle back.
                    break;
                } else {
                    return extract_lane(mutate(vector, bounds), index);
                }
            }
            index -= vector.type().lanes();
        }
    }

    // Mutate the vectors
    vector<Expr> new_vectors;
    bool changed = false;
    for (const Expr &vector : op->vectors) {
        ExprInfo v_bounds;
        Expr new_vector = mutate(vector, &v_bounds);
        if (!vector.same_as(new_vector)) {
            changed = true;
        }
        if (bounds) {
            if (new_vectors.empty()) {
                *bounds = v_bounds;
            } else {
                bounds->min_defined &= v_bounds.min_defined;
                bounds->max_defined &= v_bounds.max_defined;
                bounds->min = std::min(bounds->min, v_bounds.min);
                bounds->max = std::max(bounds->max, v_bounds.max);
                bounds->alignment = ModulusRemainder::unify(bounds->alignment, v_bounds.alignment);
            }
        }
        new_vectors.push_back(new_vector);
    }

    // Try to convert a load with shuffled indices into a
    // shuffle of a dense load.
    if (const Load *first_load = new_vectors[0].as<Load>()) {
        vector<Expr> load_predicates;
        vector<Expr> load_indices;
        bool unpredicated = true;
        for (const Expr &e : new_vectors) {
            const Load *load = e.as<Load>();
            if (load && load->name == first_load->name) {
                load_predicates.push_back(load->predicate);
                load_indices.push_back(load->index);
                unpredicated = unpredicated && is_const_one(load->predicate);
            } else {
                break;
            }
        }

        if (load_indices.size() == new_vectors.size()) {
            Type t = load_indices[0].type().with_lanes(op->indices.size());
            Expr shuffled_index = Shuffle::make(load_indices, op->indices);
            ExprInfo shuffled_index_info;
            shuffled_index = mutate(shuffled_index, &shuffled_index_info);
            if (shuffled_index.as<Ramp>()) {
                ExprInfo base_info;
                if (const Ramp *r = shuffled_index.as<Ramp>()) {
                    mutate(r->base, &base_info);
                }

                ModulusRemainder alignment =
                    ModulusRemainder::intersect(base_info.alignment, shuffled_index_info.alignment);

                Expr shuffled_predicate;
                if (unpredicated) {
                    shuffled_predicate = const_true(t.lanes());
                } else {
                    shuffled_predicate = Shuffle::make(load_predicates, op->indices);
                    shuffled_predicate = mutate(shuffled_predicate, nullptr);
                }
                t = first_load->type;
                t = t.with_lanes(op->indices.size());
                return Load::make(t, first_load->name, shuffled_index, first_load->image,
                                  first_load->param, shuffled_predicate, alignment);
            }
        }
    }

    // Try to collapse a shuffle of broadcasts into a single
    // broadcast. Note that it doesn't matter what the indices
    // are.
    const Broadcast *b1 = new_vectors[0].as<Broadcast>();
    if (b1) {
        bool can_collapse = true;
        for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
            if (const Broadcast *b2 = new_vectors[i].as<Broadcast>()) {
                Expr check = mutate(b1->value - b2->value, nullptr);
                can_collapse &= is_const_zero(check);
            } else {
                can_collapse = false;
            }
        }
        if (can_collapse) {
            if (op->indices.size() == 1) {
                return b1->value;
            } else {
                return Broadcast::make(b1->value, op->indices.size());
            }
        }
    }

    if (op->is_interleave()) {
        int terms = (int)new_vectors.size();

        // Try to collapse an interleave of ramps into a single ramp.
        const Ramp *r = new_vectors[0].as<Ramp>();
        if (r) {
            bool can_collapse = true;
            for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                // If we collapse these terms into a single ramp,
                // the new stride is going to be the old stride
                // divided by the number of terms, so the
                // difference between two adjacent terms in the
                // interleave needs to be a broadcast of the new
                // stride.
                Expr diff = mutate(new_vectors[i] - new_vectors[i - 1], nullptr);
                const Broadcast *b = diff.as<Broadcast>();
                if (b) {
                    Expr check = mutate(b->value * terms - r->stride, nullptr);
                    can_collapse &= is_const_zero(check);
                } else {
                    can_collapse = false;
                }
            }
            if (can_collapse) {
                return mutate(Ramp::make(r->base, r->stride / terms, r->lanes * terms), bounds);
            }
        }

        // Try to collapse an interleave of slices of vectors from
        // the same vector into a single vector.
        if (const Shuffle *first_shuffle = new_vectors[0].as<Shuffle>()) {
            if (first_shuffle->is_slice()) {
                bool can_collapse = true;
                for (size_t i = 0; i < new_vectors.size() && can_collapse; i++) {
                    const Shuffle *i_shuffle = new_vectors[i].as<Shuffle>();

                    // Check that the current shuffle is a slice...
                    if (!i_shuffle || !i_shuffle->is_slice()) {
                        can_collapse = false;
                        break;
                    }

                    // ... and that it is a slice in the right place...
                    // If the shuffle is a single element, we don't care what the stride is.
                    if (i_shuffle->slice_begin() != (int)i ||
                        (i_shuffle->indices.size() != 1 && i_shuffle->slice_stride() != terms)) {
                        can_collapse = false;
                        break;
                    }

                    if (i > 0) {
                        // ... and that the vectors being sliced are the same.
                        if (first_shuffle->vectors.size() != i_shuffle->vectors.size()) {
                            can_collapse = false;
                            break;
                        }

                        for (size_t j = 0; j < first_shuffle->vectors.size() && can_collapse; j++) {
                            if (!equal(first_shuffle->vectors[j], i_shuffle->vectors[j])) {
                                can_collapse = false;
                            }
                        }
                    }
                }

                if (can_collapse) {
                    // It's possible the slices didn't use all of the vector, in which case we need to slice it.
                    Expr result = Shuffle::make_concat(first_shuffle->vectors);
                    if (result.type().lanes() != op->type.lanes()) {
                        result = Shuffle::make_slice(result, 0, 1, op->type.lanes());
                    }
                    return result;
                }
            }
        }

        // Try to collapse an interleave of a series of extract_bits into a vector reinterpret.
        if (const Call *extract = new_vectors[0].as<Call>()) {
            if (extract->is_intrinsic(Call::extract_bits) &&
                is_const_zero(extract->args[1])) {
                int n = (int)new_vectors.size();
                Expr base = extract->args[0];
                bool can_collapse = base.type().bits() == n * op->type.bits();
                for (int i = 1; can_collapse && i < n; i++) {
                    const Call *c = new_vectors[i].as<Call>();
                    if (!(c->is_intrinsic(Call::extract_bits) &&
                          is_const(c->args[1], i * op->type.bits()) &&
                          equal(base, c->args[0]))) {
                        can_collapse = false;
                    }
                }
                if (can_collapse) {
                    return Reinterpret::make(op->type, base);
                }
            }
        }

    } else if (op->is_concat()) {
        // Bypass concat of a single vector (identity shuffle)
        if (new_vectors.size() == 1) {
            return new_vectors[0];
        }

        // Try to collapse a concat of ramps into a single ramp.
        const Ramp *r = new_vectors[0].as<Ramp>();
        if (r) {
            bool can_collapse = true;
            for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                Expr diff;
                if (new_vectors[i].type().lanes() == new_vectors[i - 1].type().lanes()) {
                    diff = mutate(new_vectors[i] - new_vectors[i - 1], nullptr);
                }

                const Broadcast *b = diff.as<Broadcast>();
                if (b) {
                    Expr check = mutate(b->value - r->stride * new_vectors[i - 1].type().lanes(), nullptr);
                    can_collapse &= is_const_zero(check);
                } else {
                    can_collapse = false;
                }
            }
            if (can_collapse) {
                return Ramp::make(r->base, r->stride, op->indices.size());
            }
        }

        // Try to collapse a concat of scalars into a ramp.
        if (new_vectors[0].type().is_scalar() && new_vectors[1].type().is_scalar()) {
            bool can_collapse = true;
            Expr stride = mutate(new_vectors[1] - new_vectors[0], nullptr);
            for (size_t i = 1; i < new_vectors.size() && can_collapse; i++) {
                if (!new_vectors[i].type().is_scalar()) {
                    can_collapse = false;
                    break;
                }

                Expr check = mutate(new_vectors[i] - new_vectors[i - 1] - stride, nullptr);
                if (!is_const_zero(check)) {
                    can_collapse = false;
                }
            }

            if (can_collapse) {
                return Ramp::make(new_vectors[0], stride, op->indices.size());
            }
        }
    }

    // Pull a widening cast outside of a slice
    if (new_vectors.size() == 1 &&
        op->type.lanes() < new_vectors[0].type().lanes()) {
        if (const Cast *cast = new_vectors[0].as<Cast>()) {
            if (cast->type.bits() > cast->value.type().bits()) {
                return mutate(Cast::make(cast->type.with_lanes(op->type.lanes()),
                                         Shuffle::make({cast->value}, op->indices)),
                              bounds);
            }
        }
    }

    if (op->is_slice() && (new_vectors.size() == 1)) {
        if (const Shuffle *inner_shuffle = new_vectors[0].as<Shuffle>()) {
            // Try to collapse a slice of slice.
            if (inner_shuffle->is_slice() && (inner_shuffle->vectors.size() == 1)) {
                // Indices of the slice are ramp, so nested slice is a1 * (a2 * x + b2) + b1 =
                // = a1 * a2 * x + a1 * b2 + b1.
                return Shuffle::make_slice(inner_shuffle->vectors[0],
                                           op->slice_begin() * inner_shuffle->slice_stride() + inner_shuffle->slice_begin(),
                                           op->slice_stride() * inner_shuffle->slice_stride(),
                                           op->indices.size());
            }
            // Check if we really need to concat all vectors before slicing.
            if (inner_shuffle->is_concat()) {
                int slice_min = op->indices.front();
                int slice_max = op->indices.back();
                int concat_index = 0;
                int new_slice_start = -1;
                vector<Expr> new_concat_vectors;
                for (const auto &v : inner_shuffle->vectors) {
                    // Check if current concat vector overlaps with slice.
                    if ((concat_index >= slice_min && concat_index <= slice_max) ||
                        ((concat_index + v.type().lanes() - 1) >= slice_min && (concat_index + v.type().lanes() - 1) <= slice_max)) {
                        if (new_slice_start < 0) {
                            new_slice_start = concat_index;
                        }
                        new_concat_vectors.push_back(v);
                    }

                    concat_index += v.type().lanes();
                }
                if (new_concat_vectors.size() < inner_shuffle->vectors.size()) {
                    return Shuffle::make_slice(Shuffle::make_concat(new_concat_vectors), op->slice_begin() - new_slice_start, op->slice_stride(), op->indices.size());
                }
            }
        }
    }

    if (!changed) {
        return op;
    } else {
        return Shuffle::make(new_vectors, op->indices);
    }
}

}  // namespace Internal
}  // namespace Halide
