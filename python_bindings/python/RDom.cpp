#include "RDom.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include "add_operators.h"
#include <boost/python.hpp>

#include "../../src/IROperator.h"  // for operations with RVar
#include "../../src/ImageParam.h"
#include "../../src/RDom.h"

#include <string>

namespace h = Halide;
namespace p = boost::python;

void defineRVar() {
    using Halide::RVar;

    auto rvar_class = p::class_<RVar>("RVar",
                                      "A reduction variable represents a single dimension of a reduction "
                                      "domain (RDom). Don't construct them directly, instead construct an "
                                      "RDom, and use RDom::operator[] to get at the variables. For "
                                      "single-dimensional reduction domains, you can just cast a "
                                      "single-dimensional RDom to an RVar.",
                                      p::init<>(p::args("self"), "An empty reduction variable."))
                          .def(p::init<std::string>(p::args("self", "name"), "Construct an RVar with the given name"))

                          .def(p::init<h::Internal::ReductionDomain, int>(
                              p::args("self", "domain", "index"),
                              "Construct a reduction variable with the given name and "
                              "bounds. Must be a member of the given reduction domain."))

                          .def("min", &RVar::min, p::arg("self"),
                               "The minimum value that this variable will take on")
                          .def("extent", &RVar::extent, p::arg("self"),
                               "The number that this variable will take on. "
                               "The maximum value of this variable will be min() + extent() - 1")

                          .def("domain", &RVar::domain, p::arg("self"),
                               "The reduction domain this is associated with.")

                          .def("name", &RVar::name, p::arg("self"),
                               p::return_value_policy<p::copy_const_reference>(),
                               "The name of this reduction variable");

    p::implicitly_convertible<RVar, h::Expr>();

    add_operators(rvar_class);  // define operators with int, rvars, and exprs
    add_operators_with<decltype(rvar_class), h::Expr>(rvar_class);

    return;
}

h::RDom *RDom_constructor0(p::tuple args, std::string name = "") {
    const size_t args_len = p::len(args);
    if ((args_len % 2) != 0) {
        throw std::invalid_argument("RDom constructor expects an even number of Expr inputs");
    }

    std::vector<h::Expr> exprs;

    for (size_t i = 0; i < args_len; i += 1) {
        p::object o = args[i];
        p::extract<h::Expr> expr_extract(o);

        if (expr_extract.check()) {
            exprs.push_back(expr_extract());
        } else {
            for (size_t j = 0; j < args_len; j += 1) {
                p::object o = args[j];
                const std::string o_str = p::extract<std::string>(p::str(o));
                printf("RDom constructor args_passed[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("RDom constructor only handles a list of (convertible to) Expr.");
        }
    }

    assert((exprs.size() % 2) == 0);
    std::vector<std::pair<h::Expr, h::Expr>> ranges;
    for (size_t i = 0; i < exprs.size(); i += 2) {
        ranges.push_back({ exprs[i], exprs[i + 1] });
    }

    return new h::RDom(ranges, name);
}

h::RDom *RDom_constructor1(h::Expr min0, h::Expr extent0,
                           std::string name = "") {
    std::vector<std::pair<h::Expr, h::Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    return new h::RDom(ranges, name);
}

h::RDom *RDom_constructor2(h::Expr min0, h::Expr extent0,
                           h::Expr min1, h::Expr extent1,
                           std::string name = "") {
    std::vector<std::pair<h::Expr, h::Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    ranges.push_back({ min1, extent1 });
    return new h::RDom(ranges, name);
}

h::RDom *RDom_constructor3(h::Expr min0, h::Expr extent0,
                           h::Expr min1, h::Expr extent1,
                           h::Expr min2, h::Expr extent2,
                           std::string name = "") {
    std::vector<std::pair<h::Expr, h::Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    ranges.push_back({ min1, extent1 });
    ranges.push_back({ min2, extent2 });
    return new h::RDom(ranges, name);
}

h::RDom *RDom_constructor4(h::Expr min0, h::Expr extent0,
                           h::Expr min1, h::Expr extent1,
                           h::Expr min2, h::Expr extent2,
                           h::Expr min3, h::Expr extent3,
                           std::string name = "") {
    std::vector<std::pair<h::Expr, h::Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    ranges.push_back({ min1, extent1 });
    ranges.push_back({ min2, extent2 });
    ranges.push_back({ min3, extent3 });
    return new h::RDom(ranges, name);
}

void defineRDom() {
    using Halide::RDom;

    defineRVar();

    // only defined so that python knows what to do with it, not meant to be used by user
    p::class_<h::Internal::ReductionDomain>("_ReductionDomain", p::no_init);

    auto rdom_class = p::class_<RDom>("RDom",
                                      "A multi-dimensional domain over which to iterate. "
                                      "Used when defining functions with update definitions.\n"
                                      "See apps/bilateral_grid.py for an example of a reduction.\n\n"
                                      "Constructors::\n\n"
                                      "  RDom(Expr min, Expr extent, name="
                                      ")                             -- 1D reduction\n"
                                      "  RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, name="
                                      ")  -- 2D reduction\n"
                                      "  (Similar for 3D and 4D reductions)\n"
                                      "  RDom(Buffer|ImageParam)                    -- Iterate over all points in the domain\n"
                                      "The following global functions can be used for inline reductions::\n\n"
                                      "    minimum, maximum, product, sum",
                                      p::init<>(p::arg("self"), "Construct an undefined reduction domain."))
                          .def(p::init<h::Buffer<>>(p::args("self", "buffer"),
                                                   "Construct a reduction domain that iterates over all points in "
                                                   "a given Buffer, Image, or ImageParam. "
                                                   "Has the same dimensionality as the argument."))
                          .def(p::init<h::ImageParam>(p::args("self", "image_param"),
                                                      "Construct a reduction domain that iterates over all points in "
                                                      "a given Buffer, Image, or ImageParam. "
                                                      "Has the same dimensionality as the argument."))
                          .def(p::init<h::Internal::ReductionDomain>(
                              p::args("self", "domain"),
                              "Construct a reduction domain that wraps an Internal ReductionDomain object."))
                          .def("__init__",
                               p::make_constructor(&RDom_constructor0, p::default_call_policies(),
                                                   (p::arg("ranges"), p::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               p::make_constructor(&RDom_constructor1, p::default_call_policies(),
                                                   (p::args("min0", "extent0"),
                                                    p::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               p::make_constructor(&RDom_constructor2, p::default_call_policies(),
                                                   (p::args("min0", "extent0", "min1", "extent1"),
                                                    p::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               p::make_constructor(&RDom_constructor3, p::default_call_policies(),
                                                   (p::args("min0", "extent0", "min1", "extent1",
                                                            "min2", "extent2"),
                                                    p::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               p::make_constructor(&RDom_constructor4, p::default_call_policies(),
                                                   (p::args("min0", "extent0", "min1", "extent1",
                                                            "min2", "extent2"),
                                                    p::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")

                          .def("domain", &RDom::domain, p::arg("self"),
                               "Get at the internal reduction domain object that this wraps.")
                          .def("defined", &RDom::defined, p::arg("self"),
                               "Check if this reduction domain is non-NULL")
                          .def("same_as", &RDom::same_as, p::args("self", "other"),
                               "Compare two reduction domains for equality of reference")
                          .def("dimensions", &RDom::dimensions, p::arg("self"),
                               "Get the dimensionality of a reduction domain")
                          .def("where", &RDom::where, p::args("self", "predicate"),
                               "Add a predicate to the RDom. An RDom may have multiple"
                               "predicates associated with it. An update definition that uses"
                               "an RDom only iterates over the subset points in the domain for"
                               "which all of its predicates are true. The predicate expression"
                               "obeys the same rules as the expressions used on the"
                               "right-hand-side of the corresponding update definition. It may"
                               "refer to the RDom's variables and free variables in the Func's"
                               "update definition. It may include calls to other Funcs, or make"
                               "recursive calls to the same Func. This permits iteration over"
                               "non-rectangular domains, or domains with sizes that vary with"
                               "some free variable, or domains with shapes determined by some"
                               "other Func. ")
                          //"Get at one of the dimensions of the reduction domain"
                          //EXPORT RVar operator[](int) const;

                          //"Single-dimensional reduction domains can be used as RVars directly."
                          //EXPORT operator RVar() const;

                          //"Single-dimensional reduction domains can be also be used as Exprs directly."
                          //EXPORT operator Expr() const;

                          .def_readonly("x", &RDom::x,
                                        "Direct access to the first four dimensions of the reduction domain. "
                                        "Some of these variables may be undefined if the reduction domain has fewer than four dimensions.")
                          .def_readonly("y", &RDom::y)
                          .def_readonly("z", &RDom::z)
                          .def_readonly("w", &RDom::w);

    p::implicitly_convertible<RDom, h::Expr>();
    p::implicitly_convertible<RDom, h::RVar>();

    add_operators(rdom_class);  // define operators with int, rdom and exprs
    add_operators_with<decltype(rdom_class), h::Expr>(rdom_class);

    return;
}
