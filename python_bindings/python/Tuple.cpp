#include "Tuple.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>

#include "../../src/Tuple.h"
#include "../../src/Func.h" // for FuncRefVar, FuncRefExpr

#include <vector>
#include <string>

namespace h = Halide;

h::Tuple tuple_select0(h::Tuple condition, const h::Tuple &true_value, const h::Tuple &false_value) {

    return tuple_select(condition, true_value, false_value);
}

h::Tuple tuple_select1(h::Expr condition, const h::Tuple &true_value, const h::Tuple &false_value) {
    return tuple_select(condition, true_value, false_value);
}


h::Expr tuple_getitem0(h::Tuple &that, const size_t x)
{
    return that[x];
}

h::Buffer realization_getitem0(h::Realization &that, const size_t x)
{
    return that[x];
}


void defineRealization()
{
    using Halide::Realization;
    namespace p = boost::python;
    

    //auto realization_class =
    p::class_<Realization>("Realization",
                           "Funcs with Tuple values return multiple buffers when you realize "
                           "them. Tuples are to Exprs as Realizations are to Buffers.",
                           p::init< std::vector<h::Buffer> >
                           ( // BuffersVector is defined in Buffer.cpp
                             p::args("self", "buffers"),
                             "Construct a Realization from a vector of Buffers"))

            //    "Construct a Realization from some Buffers."
            //    template<typename ...Args>
            //    Realization(Buffer a, Buffer b, Args... args) : buffers({a, b})

            //    "Single-element realizations are implicitly castable to Buffers."
            //    Realization::operator Buffer() const

            //            "Construct a Tuple from a function reference."
            //            Tuple(const FuncRefVar &);
            //            Tuple(const FuncRefExpr &);

            .def("size", &Realization::size, p::arg("self"),
                 "The number of buffers in the Realization.")

            //.def("__getitem__", &Realization::Buffer &operator[], p::args("self", "x"),
            //     "Get a reference to one of the buffers.")

            .def("__getitem__", &realization_getitem0, p::args("self", "x"),
                 "Get one of the buffers.")

            .def("as_vector", &Realization::as_vector, p::arg("self"),
                 p::return_value_policy<p::copy_const_reference>(),
                 "Treat the Realization as a vector of Buffers")
            ;

    // "Single-element realizations are implicitly castable to Buffers."
    p::implicitly_convertible<Realization, h::Buffer>();

    return;
}


void defineTuple()
{
    using Halide::Tuple;
    namespace p = boost::python;
    

    //auto tuple_class =
    p::class_<Tuple>("Tuple",
                     "Create a small array of Exprs for defining and calling functions with multiple outputs.",
                     p::no_init)

            .def(p::init< std::vector<h::Expr> >( // ExprsVector is defined in Expr.cpp
                                                  p::args("self", "exprs"),
                                                  "Construct a Tuple from a vector of Exprs"))
            //    "Construct a Tuple from some Exprs."
            //    template<typename ...Args>
            //    Tuple(Expr a, Expr b, Args... args)

            //            "Construct a Tuple from a function reference."
            //            Tuple(const FuncRefVar &);
            //            Tuple(const FuncRefExpr &);

            .def("size", &Tuple::size, p::arg("self"),
                 "The number of elements in the tuple.")

            //.def("__getitem__", &Tuple::Expr &operator[], p::args("self", "x"),
            //     "Get a reference to an element.")

            .def("__getitem__", &tuple_getitem0, p::args("self", "x"),
                 "Get a copy of an element.")

            .def("as_vector", &Tuple::as_vector, p::arg("self"),
                 p::return_value_policy<p::copy_const_reference>(),
                 "Treat the tuple as a vector of Exprs")
            ;


    p::implicitly_convertible<h::FuncRefVar, Tuple>();
    p::implicitly_convertible<h::FuncRefExpr, Tuple>();


    defineRealization();

    p::def("tuple_select", &tuple_select0, p::args("condition", "true_value", "false_value"),
           "Equivalents of some standard operators for tuples.");

    p::def("tuple_select", &tuple_select1, p::args("condition", "true_value", "false_value"),
           "Equivalents of some standard operators for tuples.");

    return;
}
