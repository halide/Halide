#ifndef HALIDE_BOUNDARY_CONDITIONS_H
#define HALIDE_BOUNDARY_CONDITIONS_H

/** \file
 * Support for imposing boundary conditions on Halide::Funcs.
 */

#include <utility>
#include <vector>

#include "Func.h"
#include "IR.h"
#include "Util.h"

namespace Halide {

/** namespace to hold functions for imposing boundary conditions on
 *  Halide Funcs.
 *
 *  All functions in this namespace transform a source Func to a
 *  result Func where the result produces the values of the source
 *  within a given region and a different set of values outside the
 *  given region. A region is an N dimensional box specified by
 *  mins and extents.
 *
 *  Three areas are defined:
 *      The image is the entire set of values in the region.
 *      The edge is the set of pixels in the image but adjacent
 *          to coordinates that are not
 *      The interior is the image minus the edge (and is undefined
 *          if the extent of any region is 1 or less).
 *
 *  If the source Func has more dimensions than are specified, the extra ones
 *  are unmodified.
 *
 *  Numerous options for specifing the outside area are provided,
 *  including replacement with an expression, repeating the edge
 *  samples, mirroring over the edge, and repeating or mirroring the
 *  entire image.
 *
 *  Using these functions to express your boundary conditions is highly
 *  recommended for correctness and performance. Some of these are hard
 *  to get right. The versions here are both understood by bounds
 *  inference, and also judiciously use the 'likely' intrinsic to minimize
 *  runtime overhead.
 *
 */
namespace BoundaryConditions {

namespace Internal {

#if __cplusplus > 199711L // C++11 arbitrary number of args support

inline NO_INLINE void collect_bounds(std::vector<std::pair<Expr, Expr> > &collected_bounds,
                                     Expr min, Expr extent) {
    collected_bounds.push_back(std::make_pair(min, extent));
}

template <typename ...Bounds>
inline NO_INLINE void collect_bounds(std::vector<std::pair<Expr, Expr> > &collected_bounds,
                                     Expr min, Expr extent, Bounds... bounds) {
    collected_bounds.push_back(std::make_pair(min, extent));
    collect_bounds(collected_bounds, bounds...);
}

#endif // C++11 support.

inline const Func &func_like_to_func(const Func &func) {
    return func;
}

template <typename T>
inline NO_INLINE Func func_like_to_func(T func_like) {
    return lambda(_, func_like(_));
}

}

/** Impose a boundary condition such that a given expression is returned
 *  everywhere outside the boundary. Generally the expression will be a
 *  constant, though the code currently allows accessing the arguments
 *  of source.
 *
 *  An ImageParam, Image<T>, or similar can be passed instead of a Func. If this
 *  is done and no bounds are given, the boundaries will be taken from the
 *  min and extent methods of the passed object.
 *
 *  (This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_BORDER
 *   and putting value in the border of the texture.)
 */
// @{
EXPORT Func constant_exterior(const Func &source, Expr value,
                              const std::vector<std::pair<Expr, Expr> > &bounds);

template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value) {
    std::vector<std::pair<Expr, Expr> > object_bounds;
    for (int i = 0; i < func_like.dimensions(); i++) {
        object_bounds.push_back(std::make_pair(Expr(func_like.min(i)), Expr(func_like.extent(i))));
    }

    return constant_exterior(Internal::func_like_to_func(func_like), value, object_bounds);
}

#if __cplusplus > 199711L // C++11 arbitrary number of args support
template <typename T, typename ...Bounds>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_bounds(collected_bounds, bounds...);
    return constant_exterior(Internal::func_like_to_func(func_like), value, collected_bounds);
}
#else
template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Expr min0, Expr extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return constant_exterior(Internal::func_like_to_func(func_like), value, bounds);
}

template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Expr min0, Expr extent0,
                                        Expr min1, Expr extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return constant_exterior(Internal::func_like_to_func(func_like), value, bounds);
}

template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Expr min0, Expr extent0,
                                        Expr min1, Expr extent1,
                                        Expr min2, Expr extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return constant_exterior(Internal::func_like_to_func(func_like), value, bounds);
}

template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Expr min0, Expr extent0,
                                        Expr min1, Expr extent1,
                                        Expr min2, Expr extent2,
                                        Expr min3, Expr extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return constant_exterior(Internal::func_like_to_func(func_like), value, bounds);
}

template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Expr min0, Expr extent0,
                                        Expr min1, Expr extent1,
                                        Expr min2, Expr extent2,
                                        Expr min3, Expr extent3,
                                        Expr min4, Expr extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return constant_exterior(Internal::func_like_to_func(func_like), value, bounds);
}

template <typename T>
inline NO_INLINE Func constant_exterior(T func_like, Expr value,
                                        Expr min0, Expr extent0,
                                        Expr min1, Expr extent1,
                                        Expr min2, Expr extent2,
                                        Expr min3, Expr extent3,
                                        Expr min4, Expr extent4,
                                        Expr min5, Expr extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return constant_exterior(Internal::func_like_to_func(func_like), value, bounds);
}
#endif
// @}

/** Impose a boundary condition such that the nearest edge sample is returned
 *  everywhere outside the given region.
 *
 *  An ImageParam, Image<T>, or similar can be passed instead of a Func. If this
 *  is done and no bounds are given, the boundaries will be taken from the
 *  min and extent methods of the passed object.
 *
 *  (This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_EDGE.)
 */
// @{
EXPORT Func repeat_edge(const Func &source,
                        const std::vector<std::pair<Expr, Expr> > &bounds);

template <typename T>
inline NO_INLINE Func repeat_edge(T func_like) {
    std::vector<std::pair<Expr, Expr> > object_bounds;
    for (int i = 0; i < func_like.dimensions(); i++) {
        object_bounds.push_back(std::make_pair(Expr(func_like.min(i)), Expr(func_like.extent(i))));
    }

    return repeat_edge(Internal::func_like_to_func(func_like), object_bounds);
}


#if __cplusplus > 199711L // C++11 arbitrary number of args support
template <typename T, typename ...Bounds>
inline NO_INLINE Func repeat_edge(T func_like, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_bounds(collected_bounds, bounds...);
    return repeat_edge(Internal::func_like_to_func(func_like), collected_bounds);
}
#else
template <typename T>
inline NO_INLINE Func repeat_edge(T func_like,
                                  Expr min0, Expr extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return repeat_edge(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_edge(T func_like,
                                  Expr min0, Expr extent0,
                                  Expr min1, Expr extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return repeat_edge(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_edge(T func_like,
                                  Expr min0, Expr extent0,
                                  Expr min1, Expr extent1,
                                  Expr min2, Expr extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return repeat_edge(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_edge(T func_like,
                                  Expr min0, Expr extent0,
                                  Expr min1, Expr extent1,
                                  Expr min2, Expr extent2,
                                  Expr min3, Expr extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return repeat_edge(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_edge(T func_like,
                                  Expr min0, Expr extent0,
                                  Expr min1, Expr extent1,
                                  Expr min2, Expr extent2,
                                  Expr min3, Expr extent3,
                                  Expr min4, Expr extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return repeat_edge(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_edge(T func_like,
                                  Expr min0, Expr extent0,
                                  Expr min1, Expr extent1,
                                  Expr min2, Expr extent2,
                                  Expr min3, Expr extent3,
                                  Expr min4, Expr extent4,
                                  Expr min5, Expr extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return repeat_edge(Internal::func_like_to_func(func_like), bounds);
}
#endif
// @}

/** Impose a boundary condition such that the entire coordinate space is
 *  tiled with copies of the image abutted against each other.
 *
 *  An ImageParam, Image<T>, or similar can be passed instead of a Func. If this
 *  is done and no bounds are given, the boundaries will be taken from the
 *  min and extent methods of the passed object.
 *
 *  (This is similar to setting GL_TEXTURE_WRAP_* to GL_REPEAT.)
 */
// @{
EXPORT Func repeat_image(const Func &source,
                         const std::vector<std::pair<Expr, Expr> > &bounds);

template <typename T>
inline NO_INLINE Func repeat_image(T func_like) {
    std::vector<std::pair<Expr, Expr> > object_bounds;
    for (int i = 0; i < func_like.dimensions(); i++) {
        object_bounds.push_back(std::make_pair(Expr(func_like.min(i)), Expr(func_like.extent(i))));
    }

    return repeat_image(Internal::func_like_to_func(func_like), object_bounds);
}

#if __cplusplus > 199711L // C++11 arbitrary number of args support
template <typename T, typename ...Bounds>
inline NO_INLINE Func repeat_image(T func_like, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_bounds(collected_bounds, bounds...);
    return repeat_image(Internal::func_like_to_func(func_like), collected_bounds);
}
#else
template <typename T>
inline NO_INLINE Func repeat_image(T func_like,
                                   Expr min0, Expr extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return repeat_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return repeat_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return repeat_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2,
                                   Expr min3, Expr extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return repeat_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2,
                                   Expr min3, Expr extent3,
                                   Expr min4, Expr extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return repeat_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func repeat_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2,
                                   Expr min3, Expr extent3,
                                   Expr min4, Expr extent4,
                                   Expr min5, Expr extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return repeat_image(Internal::func_like_to_func(func_like), bounds);
}
#endif

/** Impose a boundary condition such that the entire coordinate space is
 *  tiled with copies of the image abutted against each other, but mirror
 *  them such that adjacent edges are the same.
 *
 *  An ImageParam, Image<T>, or similar can be passed instead of a Func. If this
 *  is done and no bounds are given, the boundaries will be taken from the
 *  min and extent methods of the passed object.
 *
 *  (This is similar to setting GL_TEXTURE_WRAP_* to GL_MIRRORED_REPEAT.)
 */
// @{
EXPORT Func mirror_image(const Func &source,
                         const std::vector<std::pair<Expr, Expr> > &bounds);

template <typename T>
inline NO_INLINE Func mirror_image(T func_like) {
    std::vector<std::pair<Expr, Expr> > object_bounds;
    for (int i = 0; i < func_like.dimensions(); i++) {
        object_bounds.push_back(std::make_pair(Expr(func_like.min(i)), Expr(func_like.extent(i))));
    }

    return mirror_image(Internal::func_like_to_func(func_like), object_bounds);
}

#if __cplusplus > 199711L // C++11 arbitrary number of args support
template <typename T, typename ...Bounds>
inline NO_INLINE Func mirror_image(T func_like, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_bounds(collected_bounds, bounds...);
    return mirror_image(Internal::func_like_to_func(func_like), collected_bounds);
}
#else
template <typename T>
inline NO_INLINE Func mirror_image(T func_like,
                                   Expr min0, Expr extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return mirror_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return mirror_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return mirror_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2,
                                   Expr min3, Expr extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return mirror_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2,
                                   Expr min3, Expr extent3,
                                   Expr min4, Expr extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return mirror_image(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_image(T func_like,
                                   Expr min0, Expr extent0,
                                   Expr min1, Expr extent1,
                                   Expr min2, Expr extent2,
                                   Expr min3, Expr extent3,
                                   Expr min4, Expr extent4,
                                   Expr min5, Expr extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return mirror_image(Internal::func_like_to_func(func_like), bounds);
}
#endif
// @}

/** Impose a boundary condition such that the entire coordinate space is
 *  tiled with copies of the image abutted against each other, but mirror
 *  them such that adjacent edges are the same and then overlap the edges.
 *
 *  This produces an error if any extent is 1 or less. (TODO: check this.)
 *
 *  An ImageParam, Image<T>, or similar can be passed instead of a Func. If this
 *  is done and no bounds are given, the boundaries will be taken from the
 *  min and extent methods of the passed object.
 *
 *  (I do not believe there is a direct GL_TEXTURE_WRAP_* equivalent for this.)
 */
// @{
EXPORT Func mirror_interior(const Func &source,
                            const std::vector<std::pair<Expr, Expr> > &bounds);

template <typename T>
inline NO_INLINE Func mirror_interior(T func_like) {
    std::vector<std::pair<Expr, Expr> > object_bounds;
    for (int i = 0; i < func_like.dimensions(); i++) {
        object_bounds.push_back(std::make_pair(Expr(func_like.min(i)), Expr(func_like.extent(i))));
    }

    return mirror_interior(Internal::func_like_to_func(func_like), object_bounds);
}

#if __cplusplus > 199711L // C++11 arbitrary number of args support
template <typename T, typename ...Bounds>
inline NO_INLINE Func mirror_interior(T func_like, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_bounds(collected_bounds, bounds...);
    return mirror_interior(Internal::func_like_to_func(func_like), collected_bounds);
}
#else
template <typename T>
inline NO_INLINE Func mirror_interior(T func_like,
                                      Expr min0, Expr extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return mirror_interior(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_interior(T func_like,
                                      Expr min0, Expr extent0,
                                      Expr min1, Expr extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return mirror_interior(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_interior(T func_like,
                                      Expr min0, Expr extent0,
                                      Expr min1, Expr extent1,
                                      Expr min2, Expr extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return mirror_interior(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_interior(T func_like,
                                      Expr min0, Expr extent0,
                                      Expr min1, Expr extent1,
                                      Expr min2, Expr extent2,
                                      Expr min3, Expr extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return mirror_interior(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_interior(T func_like,
                                      Expr min0, Expr extent0,
                                      Expr min1, Expr extent1,
                                      Expr min2, Expr extent2,
                                      Expr min3, Expr extent3,
                                      Expr min4, Expr extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return mirror_interior(Internal::func_like_to_func(func_like), bounds);
}

template <typename T>
inline NO_INLINE Func mirror_interior(T func_like,
                                      Expr min0, Expr extent0,
                                      Expr min1, Expr extent1,
                                      Expr min2, Expr extent2,
                                      Expr min3, Expr extent3,
                                      Expr min4, Expr extent4,
                                      Expr min5, Expr extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return mirror_interior(Internal::func_like_to_func(func_like), bounds);
}
#endif
// @}

}

}

#endif
