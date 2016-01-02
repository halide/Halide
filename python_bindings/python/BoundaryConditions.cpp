#include "BoundaryConditions.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Lambda.h" // needed by BoundaryConditions.h
#include "../../src/BoundaryConditions.h"
#include "../../src/Func.h"

#include <string>

namespace h = Halide;
namespace hb = Halide::BoundaryConditions;
namespace p = boost::python;

namespace {

template<typename T>
h::Func constant_exterior0(T func_like, h::Expr value)
{
    return hb::constant_exterior(func_like, value);
}

// C++ fun, variadic template recursive function !
template<typename T=void, typename ...Types>
void def_constant_exterior_for_image()
{
    p::def("constant_exterior", &constant_exterior0<h::Image<T>>, p::args("source", "value"));
    def_constant_exterior_for_image<Types...>(); // recursive call
    return;
}

template<>
void def_constant_exterior_for_image<void>()
{ // end of recursion
    return;
}

} // end of anonymous namespace

namespace {


template<typename T>
h::Func repeat_edge0(T func_like)
{
    return hb::repeat_edge(func_like);
}

// C++ fun, variadic template recursive function !
template<typename T=void, typename ...Types>
void def_repeat_edge_for_image()
{
    p::def("repeat_edge", &repeat_edge0<h::Image<T>>, p::args("source"));
    def_repeat_edge_for_image<Types...>(); // recursive call
    return;
}

template<>
void def_repeat_edge_for_image<void>()
{ // end of recursion
    return;
}

} // end of anonymous namespace

namespace {

template<typename T>
h::Func repeat_image0(T func_like)
{
    return hb::repeat_image(func_like);
}

// C++ fun, variadic template recursive function !
template<typename T=void, typename ...Types>
void def_repeat_image_for_image()
{
    p::def("repeat_image", &repeat_image0<h::Image<T>>, p::args("source"));
    def_repeat_image_for_image<Types...>(); // recursive call
    return;
}

template<>
void def_repeat_image_for_image<void>()
{ // end of recursion
    return;
}

} // end of anonymous namespace

namespace {


template<typename T>
h::Func mirror_image0(T func_like)
{
    return hb::mirror_image(func_like);
}

// C++ fun, variadic template recursive function !
template<typename T=void, typename ...Types>
void def_mirror_image_for_image()
{
    p::def("mirror_image", &mirror_image0<h::Image<T>>, p::args("source"));
    def_mirror_image_for_image<Types...>(); // recursive call
    return;
}

template<>
void def_mirror_image_for_image<void>()
{ // end of recursion
    return;
}

} // end of anonymous namespace

namespace {

template<typename T>
h::Func mirror_interior0(T func_like)
{
    return hb::mirror_interior(func_like);
}

// C++ fun, variadic template recursive function !
template<typename T=void, typename ...Types>
void def_mirror_interior_for_image()
{
    p::def("mirror_interior", &mirror_interior0<h::Image<T>>, p::args("source"));
    def_mirror_interior_for_image<Types...>(); // recursive call
    return;
}

template<>
void def_mirror_interior_for_image<void>()
{ // end of recursion
    return;
}

} // end of anonymous namespace

void defineBoundaryConditions()
{
    

    // Other variants of constant_exterior exist, but not yet implemented

    p::def("constant_exterior", &constant_exterior0<h::ImageParam>, p::args("source", "value"),
           "Impose a boundary condition such that a given expression is returned "
           "everywhere outside the boundary. Generally the expression will be a "
           "constant, though the code currently allows accessing the arguments  "
           "of source.\n\n"
           "An ImageParam, Image<T>, or similar can be passed instead of a Func. If this  "
           "is done and no bounds are given, the boundaries will be taken from the  "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_BORDER  "
           " and putting value in the border of the texture.) ");

    def_constant_exterior_for_image<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double >();

    // The following variant is not yet implemented
    //    EXPORT Func constant_exterior(const Func &source, Expr value,
    //                                  const std::vector<std::pair<Expr, Expr>> &bounds);


    p::def("repeat_edge", &repeat_edge0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the nearest edge sample is returned "
           "everywhere outside the given region.\n\n"
           "An ImageParam, Image<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_CLAMP_TO_EDGE.)");

    def_repeat_edge_for_image<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double >();

    // The following variant is not yet implemented
    //    EXPORT Func repeat_edge(const Func &source,
    //                            const std::vector<std::pair<Expr, Expr>> &bounds);



    p::def("repeat_image", &repeat_image0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other.\n\n"
           "An ImageParam, Image<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_REPEAT.)");

    def_repeat_image_for_image<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double >();

    //The following variant is not yet implemented
    //        EXPORT Func repeat_image(const Func &source,
    //                                 const std::vector<std::pair<Expr, Expr>> &bounds);


    p::def("mirror_image", &mirror_image0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other, but mirror "
           "them such that adjacent edges are the same.\n\n"
           "An ImageParam, Image<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object.\n\n"
           "(This is similar to setting GL_TEXTURE_WRAP_* to GL_MIRRORED_REPEAT.)");

    def_mirror_image_for_image<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double >();

    // The following variant is not yet implemented
    //        EXPORT Func mirror_image(const Func &source,
    //                                 const std::vector<std::pair<Expr, Expr>> &bounds);


    p::def("mirror_interior", &mirror_interior0<h::ImageParam>, p::args("source"),
           "Impose a boundary condition such that the entire coordinate space is "
           "tiled with copies of the image abutted against each other, but mirror "
           "them such that adjacent edges are the same and then overlap the edges.\n\n"
           "This produces an error if any extent is 1 or less. (TODO: check this.)\n\n"
           "An ImageParam, Image<T>, or similar can be passed instead of a Func. If this "
           "is done and no bounds are given, the boundaries will be taken from the "
           "min and extent methods of the passed object. "
           "(I do not believe there is a direct GL_TEXTURE_WRAP_* equivalent for this.)");

    def_mirror_interior_for_image<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double >();

    // The following variant is not yet implemented
    //    EXPORT Func mirror_interior(const Func &source,
    //                                const std::vector<std::pair<Expr, Expr>> &bounds);

    return;
}

