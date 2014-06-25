#ifndef HALIDE_BOUNDARY_CONDITIONS_H
#define HALIDE_BOUNDARY_CONDITIONS_H

/** \file
 * Support for imposing boundary conditions on Halide::Funcs.
 */

#include <utility>
#include <vector>

#include "Func.h"
#include "IR.h"

namespace Halide {

Func boundary_constant_exterior(const Func &source, const Expr &value,
                                const std::vector<std::pair<Expr, Expr> > &bounds);

Func boundary_repeat_edge(const Func &source,
                          const std::vector<std::pair<Expr, Expr> > &bounds);

Func boundary_repeat_image(const Func &source,
                           const std::vector<std::pair<Expr, Expr> > &bounds);


Func boundary_mirror_image(const Func &source,
                           const std::vector<std::pair<Expr, Expr> > &bounds);

Func boundary_mirror_interior(const Func &source,
                              const std::vector<std::pair<Expr, Expr> > &bounds);

#if __cplusplus > 199711L

namespace Internal {

void collect_boundary_bounds(std::vector<std::pair<Expr, Expr> > &collected_bounds,
                             const Expr &min, const Expr &extent) {
    collected_bounds.push_back(std::make_pair(min, extent));
}

template <typename ...Bounds>
void collect_boundary_bounds(std::vector<std::pair<Expr, Expr> > &collected_bounds,
                             const Expr &min, const Expr &extent, Bounds... bounds) {
    collected_bounds.push_back(std::make_pair(min, extent));
    collect_boundary_bounds(collected_bounds, bounds...);
}

}

template <typename ...Bounds>
Func boundary_constant_exterior(const Func &source, const Expr &value, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_boundary_bounds(collected_bounds, bounds...);
    return boundary_constant_exterior(source, value, collected_bounds);
}

template <typename ...Bounds>
Func boundary_repeat_edge(const Func &source, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_boundary_bounds(collected_bounds, bounds...);
    return boundary_repeat_edge(source, collected_bounds);
}

template <typename ...Bounds>
Func boundary_repeat_image(const Func &source, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_boundary_bounds(collected_bounds, bounds...);
    return boundary_repeat_image(source, collected_bounds);
}

template <typename ...Bounds>
Func boundary_mirror_image(const Func &source, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_boundary_bounds(collected_bounds, bounds...);
    return boundary_mirror_image(source, collected_bounds);
}

template <typename ...Bounds>
Func boundary_mirror_interior(const Func &source, Bounds... bounds) {
    std::vector<std::pair<Expr, Expr> > collected_bounds;
    Internal::collect_boundary_bounds(collected_bounds, bounds...);
    return boundary_mirror_interior(source, collected_bounds);
}

#else

Func boundary_constant_exterior(const Func &source,
                                const Expr &value,
                                const Expr &min0, const Expr &extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return boundary_constant_exterior(source, value, bounds);
}

Func boundary_constant_exterior(const Func &source,
                                const Expr &value,
                                const Expr &min0, const Expr &extent0,
                                const Expr &min1, const Expr &extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return boundary_constant_exterior(source, value, bounds);
}

Func boundary_constant_exterior(const Func &source,
                                const Expr &value,
                                const Expr &min0, const Expr &extent0,
                                const Expr &min1, const Expr &extent1,
                                const Expr &min2, const Expr &extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return boundary_constant_exterior(source, value, bounds);
}

Func boundary_constant_exterior(const Func &source,
                                const Expr &value,
                                const Expr &min0, const Expr &extent0,
                                const Expr &min1, const Expr &extent1,
                                const Expr &min2, const Expr &extent2,
                                const Expr &min3, const Expr &extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return boundary_constant_exterior(source, value, bounds);
}

Func boundary_constant_exterior(const Func &source,
                                const Expr &value,
                                const Expr &min0, const Expr &extent0,
                                const Expr &min1, const Expr &extent1,
                                const Expr &min2, const Expr &extent2,
                                const Expr &min3, const Expr &extent3,
                                const Expr &min4, const Expr &extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return boundary_constant_exterior(source, value, bounds);
}

Func boundary_constant_exterior(const Func &source,
                                const Expr &value,
                                const Expr &min0, const Expr &extent0,
                                const Expr &min1, const Expr &extent1,
                                const Expr &min2, const Expr &extent2,
                                const Expr &min3, const Expr &extent3,
                                const Expr &min4, const Expr &extent4,
                                const Expr &min5, const Expr &extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return boundary_constant_exterior(source, value, bounds);
}

Func boundary_repeat_edge(const Func &source,
                          const Expr &min0, const Expr &extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return boundary_repeat_edge(source, bounds);
}

Func boundary_repeat_edge(const Func &source,
                          const Expr &min0, const Expr &extent0,
                          const Expr &min1, const Expr &extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return boundary_repeat_edge(source, bounds);
}

Func boundary_repeat_edge(const Func &source,
                          const Expr &min0, const Expr &extent0,
                          const Expr &min1, const Expr &extent1,
                          const Expr &min2, const Expr &extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return boundary_repeat_edge(source, bounds);
}

Func boundary_repeat_edge(const Func &source,
                          const Expr &min0, const Expr &extent0,
                          const Expr &min1, const Expr &extent1,
                          const Expr &min2, const Expr &extent2,
                          const Expr &min3, const Expr &extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return boundary_repeat_edge(source, bounds);
}

Func boundary_repeat_edge(const Func &source,
                          const Expr &min0, const Expr &extent0,
                          const Expr &min1, const Expr &extent1,
                          const Expr &min2, const Expr &extent2,
                          const Expr &min3, const Expr &extent3,
                          const Expr &min4, const Expr &extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return boundary_repeat_edge(source, bounds);
}

Func boundary_repeat_edge(const Func &source,
                          const Expr &min0, const Expr &extent0,
                          const Expr &min1, const Expr &extent1,
                          const Expr &min2, const Expr &extent2,
                          const Expr &min3, const Expr &extent3,
                          const Expr &min4, const Expr &extent4,
                          const Expr &min5, const Expr &extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return boundary_repeat_edge(source, bounds);
}

Func boundary_repeat_image(const Func &source,
                           const Expr &min0, const Expr &extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return boundary_repeat_image(source, bounds);
}

Func boundary_repeat_image(const Func &source,
                           const Expr &min0, const Expr &extent0,
                           const Expr &min1, const Expr &extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return boundary_repeat_image(source, bounds);
}

Func boundary_repeat_image(const Func &source,
                           const Expr &min0, const Expr &extent0,
                           const Expr &min1, const Expr &extent1,
                           const Expr &min2, const Expr &extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return boundary_repeat_image(source, bounds);
}

Func boundary_repeat_image(const Func &source,
                           const Expr &min0, const Expr &extent0,
                           const Expr &min1, const Expr &extent1,
                           const Expr &min2, const Expr &extent2,
                           const Expr &min3, const Expr &extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return boundary_repeat_image(source, bounds);
}

Func boundary_repeat_image(const Func &source,
                           const Expr &min0, const Expr &extent0,
                           const Expr &min1, const Expr &extent1,
                           const Expr &min2, const Expr &extent2,
                           const Expr &min3, const Expr &extent3,
                           const Expr &min4, const Expr &extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return boundary_repeat_image(source, bounds);
}

Func boundary_repeat_image(const Func &source,
                           const Expr &min0, const Expr &extent0,
                           const Expr &min1, const Expr &extent1,
                           const Expr &min2, const Expr &extent2,
                           const Expr &min3, const Expr &extent3,
                           const Expr &min4, const Expr &extent4,
                           const Expr &min5, const Expr &extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return boundary_repeat_image(source, bounds);
}

Func boundary_mirror_image(const Func &source,
                              const Expr &min0, const Expr &extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return boundary_mirror_image(source, bounds);
}

Func boundary_mirror_image(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return boundary_mirror_image(source, bounds);
}

Func boundary_mirror_image(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return boundary_mirror_image(source, bounds);
}

Func boundary_mirror_image(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2,
                              const Expr &min3, const Expr &extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return boundary_mirror_image(source, bounds);
}

Func boundary_mirror_image(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2,
                              const Expr &min3, const Expr &extent3,
                              const Expr &min4, const Expr &extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return boundary_mirror_image(source, bounds);
}

Func boundary_mirror_image(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2,
                              const Expr &min3, const Expr &extent3,
                              const Expr &min4, const Expr &extent4,
                              const Expr &min5, const Expr &extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return boundary_mirror_image(source, bounds);
}

Func boundary_mirror_interior(const Func &source,
                              const Expr &min0, const Expr &extent0) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    return boundary_mirror_interior(source, bounds);
}

Func boundary_mirror_interior(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    return boundary_mirror_interior(source, bounds);
}

Func boundary_mirror_interior(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    return boundary_mirror_interior(source, bounds);
}

Func boundary_mirror_interior(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2,
                              const Expr &min3, const Expr &extent3) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    return boundary_mirror_interior(source, bounds);
}

Func boundary_mirror_interior(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2,
                              const Expr &min3, const Expr &extent3,
                              const Expr &min4, const Expr &extent4) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    return boundary_mirror_interior(source, bounds);
}

Func boundary_mirror_interior(const Func &source,
                              const Expr &min0, const Expr &extent0,
                              const Expr &min1, const Expr &extent1,
                              const Expr &min2, const Expr &extent2,
                              const Expr &min3, const Expr &extent3,
                              const Expr &min4, const Expr &extent4,
                              const Expr &min5, const Expr &extent5) {
    std::vector<std::pair<Expr, Expr> > bounds;
    bounds.push_back(std::make_pair(min0, extent0));
    bounds.push_back(std::make_pair(min1, extent1));
    bounds.push_back(std::make_pair(min2, extent2));
    bounds.push_back(std::make_pair(min3, extent3));
    bounds.push_back(std::make_pair(min4, extent4));
    bounds.push_back(std::make_pair(min5, extent5));
    return boundary_mirror_interior(source, bounds);
}

#endif

}

#endif
