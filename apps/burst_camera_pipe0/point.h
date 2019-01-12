/*
    Adapted (with permission) from https://github.com/timothybrooks/hdr-plus
*/

#ifndef BURST_CAMERA_PIPE_POINT_H_
#define BURST_CAMERA_PIPE_POINT_H_

#include "Halide.h"

/*
 * struct Point -- An abstraction used to store the x and y offsets of alignment together (among other things).
 * This helps reduce computation when finding the minimum offset for a given tile and cuts down on redundant code.
 */
typedef struct Point {

    Halide::Expr x, y;

    // Construct default
    Point() : x(Halide::cast<int16_t>(0)), y(Halide::cast<int16_t>(0)) {}

    // Construct from a Tuple
    Point(Halide::Tuple t) : x(Halide::cast<int16_t>(t[0])), y(Halide::cast<int16_t>(t[1])) {}

    // Construct from a pair of Exprs
    Point(Halide::Expr x, Halide::Expr y) : x(Halide::cast<int16_t>(x)), y(Halide::cast<int16_t>(y)) {}

    // Construct from a call to a Func by treating it as a Tuple
    Point(Halide::FuncRef t) : Point(Halide::Tuple(t)) {}

    // Convert to a Tuple
    operator Halide::Tuple() const {
        return {x, y};
    }

    // Point addition
    Point operator+(const Point &other) const {
        return {x + other.x, y + other.y};
    }

    // Point subtraction
    Point operator-(const Point &other) const {
        return {x - other.x, y - other.y};
    }

    // Scalar multiplication
    Point operator*(const int n) const {
        return {n * x, n * y};
    }
} P;

// Scalar multiplication on the left
inline Point operator*(const int n, const Point p) {
    return p * n;
}

// Point negation
inline Point operator-(const Point p) {
    return Point(-p.x, -p.y);
}

// Integrate Point with Halide::print for debugging
inline Point print(const Point p) {
    return Point(Halide::print(p.x, p.y), p.y);
}

// Integrate Point with Halide::print_when for debugging
template <typename... Args>
inline Point print_when(Halide::Expr condition, const Point p, Args&&... args) {
    return Point(Halide::print_when(condition, p.x, p.y, args...), p.y);
}

// Integrate Point with Halide::select
inline Point select(Halide::Expr condition, const Point true_value, const Point false_value) {
    return Point(Halide::select(condition, true_value.x, false_value.x), Halide::select(condition, true_value.y, false_value.y));
}

// Integrate Point with Halide::clamp
inline Point clamp(const Point p, const Point min_p, const Point max_p) {
    return Point(Halide::clamp(p.x, min_p.x, max_p.x), Halide::clamp(p.y, min_p.y, max_p.y));
}

#endif