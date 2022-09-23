#ifndef UTILS_H
#define UTILS_H

using namespace Halide;
template<typename T>
Func repeat_edge_x(const T &f) {
    Expr width = f.dim(0).extent();
    Expr x_min = f.dim(0).min();
    return BoundaryConditions::repeat_edge(f, {{x_min, width},{Expr(), Expr()}});
}

#endif
