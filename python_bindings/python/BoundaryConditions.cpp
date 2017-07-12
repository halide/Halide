#include "BoundaryConditions.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include <boost/python/stl_iterator.hpp>

#include "Halide.h"

#include <algorithm>
#include <string>
#include <vector>

namespace h = Halide;
namespace hb = Halide::BoundaryConditions;
namespace p = boost::python;

template <typename T, typename S>
inline std::pair<T, S> to_pair(const p::object &iterable) {
    return std::pair<T, S>(p::extract<T>(iterable[0]), p::extract<T>(iterable[1]));
}

template <typename T>
inline std::vector<T> to_vector(const p::object &iterable) {
    return std::vector<T>(p::stl_input_iterator<T>(iterable), p::stl_input_iterator<T>());
}

std::vector<std::pair<h::Expr, h::Expr>> inline pyobject_to_bounds(const p::object &pybounds) {
    std::vector<p::object> intermediate = to_vector<p::object>(pybounds);
    std::vector<std::pair<h::Expr, h::Expr>> result(intermediate.size());
    std::transform(intermediate.begin(), intermediate.end(), result.begin(), to_pair<h::Expr, h::Expr>);
    return result;
}

namespace {

template <typename T>
h::Func constant_exterior0(T func_like, h::Expr value) {
    return hb::constant_exterior(func_like, value);
}

h::Func constant_exterior_bounds(h::Func func, h::Expr value, p::object bounds_) {
    return hb::constant_exterior(func, value, pyobject_to_bounds(bounds_));
}

// C++ fun, variadic template recursive function !
template <typename T = void, typename... Types>
void def_constant_exterior_for_image() {
    p::def("constant_exterior", &constant_exterior0<h::Buffer<T>>, p::args("source", "value"));
    def_constant_exterior_for_image<Types...>();  // recursive call
    return;
}

template <>
void def_constant_exterior_for_image<void>() {  // end of recursion
    return;
}

}  // end of anonymous namespace

namespace {

template <typename T>
h::Func repeat_edge0(T func_like) {
    return hb::repeat_edge(func_like);
}

h::Func repeat_edge_bounds(h::Func func, p::object bounds_) {
    return hb::repeat_edge(func, pyobject_to_bounds(bounds_));
}

// C++ fun, variadic template recursive function !
template <typename T = void, typename... Types>
void def_repeat_edge_for_image() {
    p::def("repeat_edge", &repeat_edge0<h::Buffer<T>>, p::args("source"));
    def_repeat_edge_for_image<Types...>();  // recursive call
    return;
}

template <>
void def_repeat_edge_for_image<void>() {  // end of recursion
    return;
}

}  // end of anonymous namespace

namespace {

template <typename T>
h::Func repeat_image0(T func_like) {
    return hb::repeat_image(func_like);
}

h::Func repeat_image_bounds(h::Func func, p::object bounds_) {
    return hb::repeat_image(func, pyobject_to_bounds(bounds_));
}

// C++ fun, variadic template recursive function !
template <typename T = void, typename... Types>
void def_repeat_image_for_image() {
    p::def("repeat_image", &repeat_image0<h::Buffer<T>>, p::args("source"));
    def_repeat_image_for_image<Types...>();  // recursive call
    return;
}

template <>
void def_repeat_image_for_image<void>() {  // end of recursion
    return;
}

}  // end of anonymous namespace

namespace {

template <typename T>
h::Func mirror_image0(T func_like) {
    return hb::mirror_image(func_like);
}

h::Func mirror_image_bounds(h::Func func, p::object bounds_) {
    return hb::mirror_image(func, pyobject_to_bounds(bounds_));
}

// C++ fun, variadic template recursive function !
template <typename T = void, typename... Types>
void def_mirror_image_for_image() {
    p::def("mirror_image", &mirror_image0<h::Buffer<T>>, p::args("source"));
    def_mirror_image_for_image<Types...>();  // recursive call
    return;
}

template <>
void def_mirror_image_for_image<void>() {  // end of recursion
    return;
}

}  // end of anonymous namespace

namespace {

template <typename T>
h::Func mirror_interior0(T func_like) {
    return hb::mirror_interior(func_like);
}

h::Func mirror_interior_bounds(h::Func func, p::object bounds_) {
    return hb::mirror_interior(func, pyobject_to_bounds(bounds_));
}

// C++ fun, variadic template recursive function !
template <typename T = void, typename... Types>
void def_mirror_interior_for_image() {
    p::def("mirror_interior", &mirror_interior0<h::Buffer<T>>, p::args("source"));
    def_mirror_interior_for_image<Types...>();  // recursive call
    return;
}

template <>
void def_mirror_interior_for_image<void>() {  // end of recursion
    return;
}

}  // end of anonymous namespace

void defineBoundaryConditions() {
    // constant_exterior

    p::def("constant_exterior", &constant_exterior0<h::ImageParam>, p::args("source", "value"),
           "Impose a boundary condition such that a given expression is returned "
           "everywhere outside the boundary. Generally the expression will be a "
           "constant, though the code currently allows accessing the arguments  "
           "of source.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this  "
           "is done and no bounds are given, the boundaries will be taken from the  "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_BORDER  "
           " and putting value in the border of the texture.) ");

    def_constant_exterior_for_image<
        boost::uint8_t, boost::uint16_t, boost::uint32_t,
        boost::int8_t, boost::int16_t, boost::int32_t,
        float, double>();

    p::def("constant_exterior", &constant_exterior_bounds, p::args("source", "value", "bounds"));

    // repeat_edge

    p::def("repeat_edge", &repeat_edge0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the nearest edge sample is returned "
           "everywhere outside the given region.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_EDGE.)");

    def_repeat_edge_for_image<
        boost::uint8_t, boost::uint16_t, boost::uint32_t,
        boost::int8_t, boost::int16_t, boost::int32_t,
        float, double>();

    p::def("repeat_edge", &repeat_edge_bounds, p::args("source", "bounds"));

    // repeat_image

    p::def("repeat_image", &repeat_image0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_REPEAT.)");

    def_repeat_image_for_image<
        boost::uint8_t, boost::uint16_t, boost::uint32_t,
        boost::int8_t, boost::int16_t, boost::int32_t,
        float, double>();

    p::def("repeat_image", &repeat_image_bounds, p::args("source", "bounds"));

    // mirror_image

    p::def("mirror_image", &mirror_image0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other, but mirror "
           "them such that adjacent edges are the same.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_MIRRORED_REPEAT.)");

    def_mirror_image_for_image<
        boost::uint8_t, boost::uint16_t, boost::uint32_t,
        boost::int8_t, boost::int16_t, boost::int32_t,
        float, double>();

    p::def("mirror_image", &mirror_image_bounds, p::args("source", "bounds"));

    // mirror_interior

    p::def("mirror_interior", &mirror_interior0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other, but mirror "
           "them such that adjacent edges are the same and then overlap the edges.\n\n"
           "This produces an error if any extent is 1 or less. (TODO: check this.)\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object. "
           "(I do not believe there is a direct GL_TEXTURE_WRAP_* equivalent for this.)");

    def_mirror_interior_for_image<
        boost::uint8_t, boost::uint16_t, boost::uint32_t,
        boost::int8_t, boost::int16_t, boost::int32_t,
        float, double>();

    p::def("mirror_interior", &mirror_interior_bounds, p::args("source", "bounds"));

    return;
}
