#include "Simplify_Internal.h"

namespace Halide {
namespace Internal {

using std::vector;

Expr Simplify::visit(const Shuffle *op, ExprInfo *bounds) {
    if (op->is_extract_element() &&
        (op->vectors[0].as<Ramp>() ||
         op->vectors[0].as<Broadcast>())) {
        // Extracting a single lane of a ramp or broadcast
        if (const Ramp *r = op->vectors[0].as<Ramp>()) {
            return mutate(r->base + op->indices[0] * r->stride, bounds);
        } else if (const Broadcast *b = op->vectors[0].as<Broadcast>()) {
            return mutate(b->value, bounds);
        } else {
            internal_error << "Unreachable";
            return Expr();
        }
    }

    // Mutate the vectors
    vector<Expr> new_vectors;
    bool changed = false;
    for (Expr vector : op->vectors) {
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
        for (Expr e : new_vectors) {
            const Load *load = e.as<Load>();
            if (load && load->name == first_load->name) {
                load_predicates.push_back(load->predicate);
                load_indices.push_back(load->index);
                unpredicated = unpredicated && is_one(load->predicate);
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
                can_collapse &= is_zero(check);
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
                    can_collapse &= is_zero(check);
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
                    if (i_shuffle->slice_begin() != (int)i || i_shuffle->slice_stride() != terms) {
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
                    return Shuffle::make_concat(first_shuffle->vectors);
                }
            }
        }
    } else if (op->is_concat()) {
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
                    can_collapse &= is_zero(check);
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
                if (!is_zero(check)) {
                    can_collapse = false;
                }
            }

            if (can_collapse) {
                return Ramp::make(new_vectors[0], stride, op->indices.size());
            }
        }
    }

    if (!changed) {
        return op;
    } else {
        return Shuffle::make(new_vectors, op->indices);
    }
}

template<typename T>
Expr Simplify::hoist_slice_vector(Expr e) {
    const T *op = e.as<T>();
    internal_assert(op);

    const Shuffle *shuffle_a = op->a.template as<Shuffle>();
    const Shuffle *shuffle_b = op->b.template as<Shuffle>();

    internal_assert(shuffle_a && shuffle_b &&
                    shuffle_a->is_slice() &&
                    shuffle_b->is_slice());

    if (shuffle_a->indices != shuffle_b->indices) {
        return e;
    }

    const std::vector<Expr> &slices_a = shuffle_a->vectors;
    const std::vector<Expr> &slices_b = shuffle_b->vectors;
    if (slices_a.size() != slices_b.size()) {
        return e;
    }

    for (size_t i = 0; i < slices_a.size(); i++) {
        if (slices_a[i].type() != slices_b[i].type()) {
            return e;
        }
    }

    vector<Expr> new_slices;
    for (size_t i = 0; i < slices_a.size(); i++) {
        new_slices.push_back(T::make(slices_a[i], slices_b[i]));
    }

    return Shuffle::make(new_slices, shuffle_a->indices);
}

template Expr Simplify::hoist_slice_vector<Add>(Expr);
template Expr Simplify::hoist_slice_vector<Sub>(Expr);
template Expr Simplify::hoist_slice_vector<Mul>(Expr);
template Expr Simplify::hoist_slice_vector<Min>(Expr);
template Expr Simplify::hoist_slice_vector<Max>(Expr);

}  // namespace Internal
}  // namespace Halide
