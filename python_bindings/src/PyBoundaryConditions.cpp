#include "PyBoundaryConditions.h"

#include <algorithm>

namespace Halide {
namespace PythonBindings {

namespace hb = Halide::BoundaryConditions;

template <typename T, typename S>
inline std::pair<T, S> to_pair(const py::object &iterable) {
    return std::pair<T, S>(py::extract<T>(iterable[0]), py::extract<T>(iterable[1]));
}

template <typename T>
inline std::vector<T> to_vector(const py::object &iterable) {
    return std::vector<T>(py::stl_input_iterator<T>(iterable), py::stl_input_iterator<T>());
}

std::vector<std::pair<Expr, Expr>> inline pyobject_to_bounds(const py::object &pybounds) {
    std::vector<py::object> intermediate = to_vector<py::object>(pybounds);
    std::vector<std::pair<Expr, Expr>> result(intermediate.size());
    std::transform(intermediate.begin(), intermediate.end(), result.begin(), to_pair<Expr, Expr>);
    return result;
}

namespace {

template <typename T>
Func constant_exterior0(T func_like, Expr value) {
    return hb::constant_exterior(func_like, value);
}

Func constant_exterior_bounds(Func func, Expr value, py::object bounds_) {
    return hb::constant_exterior(func, value, pyobject_to_bounds(bounds_));
}

template <typename T = void, typename... Types>
void def_constant_exterior_for_image() {
    py::def("constant_exterior", &constant_exterior0<Buffer<T>>, py::args("source", "value"));
    def_constant_exterior_for_image<Types...>();  // recursive call
}

template <>
void def_constant_exterior_for_image<void>() {  // end of recursion
    // empty
}

}  // namespace

namespace {

template <typename T>
Func repeat_edge0(T func_like) {
    return hb::repeat_edge(func_like);
}

Func repeat_edge_bounds(Func func, py::object bounds_) {
    return hb::repeat_edge(func, pyobject_to_bounds(bounds_));
}

template <typename T = void, typename... Types>
void def_repeat_edge_for_image() {
    py::def("repeat_edge", &repeat_edge0<Buffer<T>>, py::args("source"));
    def_repeat_edge_for_image<Types...>();  // recursive call
}

template <>
void def_repeat_edge_for_image<void>() {  // end of recursion
    // empty
}

}  // namespace

namespace {

template <typename T>
Func repeat_image0(T func_like) {
    return hb::repeat_image(func_like);
}

Func repeat_image_bounds(Func func, py::object bounds_) {
    return hb::repeat_image(func, pyobject_to_bounds(bounds_));
}

template <typename T = void, typename... Types>
void def_repeat_image_for_image() {
    py::def("repeat_image", &repeat_image0<Buffer<T>>, py::args("source"));
    def_repeat_image_for_image<Types...>();  // recursive call
}

template <>
void def_repeat_image_for_image<void>() {  // end of recursion
    // empty
}

}  // namespace

namespace {

template <typename T>
Func mirror_image0(T func_like) {
    return hb::mirror_image(func_like);
}

Func mirror_image_bounds(Func func, py::object bounds_) {
    return hb::mirror_image(func, pyobject_to_bounds(bounds_));
}

template <typename T = void, typename... Types>
void def_mirror_image_for_image() {
    py::def("mirror_image", &mirror_image0<Buffer<T>>, py::args("source"));
    def_mirror_image_for_image<Types...>();  // recursive call
}

template <>
void def_mirror_image_for_image<void>() {  // end of recursion
    // empty
}

}  // namespace

namespace {

template <typename T>
Func mirror_interior0(T func_like) {
    return hb::mirror_interior(func_like);
}

Func mirror_interior_bounds(Func func, py::object bounds_) {
    return hb::mirror_interior(func, pyobject_to_bounds(bounds_));
}

template <typename T = void, typename... Types>
void def_mirror_interior_for_image() {
    py::def("mirror_interior", &mirror_interior0<Buffer<T>>, py::args("source"));
    def_mirror_interior_for_image<Types...>();  // recursive call
}

template <>
void def_mirror_interior_for_image<void>() {  // end of recursion
    // empty
}

}  // namespace

void define_boundary_conditions() {
    // constant_exterior

    py::def("constant_exterior", &constant_exterior0<ImageParam>, py::args("source", "value"),
           "Impose a boundary condition such that a given expression is returned "
           "everywhere outside the boundary. Generally the expression will be a "
           "constant, though the code currently allows accessing the arguments  "
           "of source.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this  "
           "is done and no bounds are given, the boundaries will be taken from the  "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_BORDER  "
           " and putting value in the border of the texture.) ");

    // TODO: need int64, uint64, bool
    def_constant_exterior_for_image<
        uint8_t, uint16_t, uint32_t,
        int8_t, int16_t, int32_t,
        float, double>();

    py::def("constant_exterior", &constant_exterior_bounds, py::args("source", "value", "bounds"));

    // repeat_edge

    py::def("repeat_edge", &repeat_edge0<ImageParam>, py::args("source"),
           "Impose a boundary condition such that the nearest edge sample is returned "
           "everywhere outside the given region.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_EDGE.)");

    def_repeat_edge_for_image<
        uint8_t, uint16_t, uint32_t,
        int8_t, int16_t, int32_t,
        float, double>();

    py::def("repeat_edge", &repeat_edge_bounds, py::args("source", "bounds"));

    // repeat_image

    py::def("repeat_image", &repeat_image0<ImageParam>, py::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_REPEAT.)");

    def_repeat_image_for_image<
        uint8_t, uint16_t, uint32_t,
        int8_t, int16_t, int32_t,
        float, double>();

    py::def("repeat_image", &repeat_image_bounds, py::args("source", "bounds"));

    // mirror_image

    py::def("mirror_image", &mirror_image0<ImageParam>, py::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other, but mirror "
           "them such that adjacent edges are the same.\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_MIRRORED_REPEAT.)");

    def_mirror_image_for_image<
        uint8_t, uint16_t, uint32_t,
        int8_t, int16_t, int32_t,
        float, double>();

    py::def("mirror_image", &mirror_image_bounds, py::args("source", "bounds"));

    // mirror_interior

    py::def("mirror_interior", &mirror_interior0<ImageParam>, py::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other, but mirror "
           "them such that adjacent edges are the same and then overlap the edges.\n\n"
           "This produces an error if any extent is 1 or less. (TODO: check this.)\n\n"
           "An ImageParam, Buffer<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object. "
           "(I do not believe there is a direct GL_TEXTURE_WRAP_* equivalent for this.)");

    def_mirror_interior_for_image<
        uint8_t, uint16_t, uint32_t,
        int8_t, int16_t, int32_t,
        float, double>();

    py::def("mirror_interior", &mirror_interior_bounds, py::args("source", "bounds"));
}

}  // namespace PythonBindings
}  // namespace Halide
