#include "PyRDom.h"

#include "PyBinaryOperators.h"

namespace Halide {
namespace PythonBindings {

void define_rvar() {
    auto rvar_class = py::class_<RVar>("RVar",
                                      "A reduction variable represents a single dimension of a reduction "
                                      "domain (RDom). Don't construct them directly, instead construct an "
                                      "RDom, and use RDom::operator[] to get at the variables. For "
                                      "single-dimensional reduction domains, you can just cast a "
                                      "single-dimensional RDom to an RVar.",
                                      py::init<>(py::args("self"), "An empty reduction variable."))
                          .def(py::init<std::string>(py::args("self", "name"), "Construct an RVar with the given name"))

                          .def(py::init<Internal::ReductionDomain, int>(
                              py::args("self", "domain", "index"),
                              "Construct a reduction variable with the given name and "
                              "bounds. Must be a member of the given reduction domain."))

                          .def("min", &RVar::min, py::arg("self"),
                               "The minimum value that this variable will take on")
                          .def("extent", &RVar::extent, py::arg("self"),
                               "The number that this variable will take on. "
                               "The maximum value of this variable will be min() + extent() - 1")

                          .def("domain", &RVar::domain, py::arg("self"),
                               "The reduction domain this is associated with.")

                          .def("name", &RVar::name, py::arg("self"),
                               py::return_value_policy<py::copy_const_reference>(),
                               "The name of this reduction variable");

    py::implicitly_convertible<RVar, Expr>();

    add_binary_operators(rvar_class);  // define operators with int, rvars, and exprs
    add_binary_operators_with<Expr>(rvar_class);
}

RDom *RDom_constructor0(py::tuple args, std::string name = "") {
    const size_t args_len = py::len(args);
    if ((args_len % 2) != 0) {
        throw std::invalid_argument("RDom constructor expects an even number of Expr inputs");
    }

    std::vector<Expr> exprs;

    for (size_t i = 0; i < args_len; i += 1) {
        py::object o = args[i];
        py::extract<Expr> expr_extract(o);

        if (expr_extract.check()) {
            exprs.push_back(expr_extract());
        } else {
            for (size_t j = 0; j < args_len; j += 1) {
                py::object o = args[j];
                const std::string o_str = py::extract<std::string>(py::str(o));
                printf("RDom constructor args_passed[%lu] == %s\n", j, o_str.c_str());
            }
            throw std::invalid_argument("RDom constructor only handles a list of (convertible to) Expr.");
        }
    }

    assert((exprs.size() % 2) == 0);
    std::vector<std::pair<Expr, Expr>> ranges;
    for (size_t i = 0; i < exprs.size(); i += 2) {
        ranges.push_back({ exprs[i], exprs[i + 1] });
    }

    return new RDom(ranges, name);
}

RDom *RDom_constructor1(Expr min0, Expr extent0,
                           std::string name = "") {
    std::vector<std::pair<Expr, Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    return new RDom(ranges, name);
}

RDom *RDom_constructor2(Expr min0, Expr extent0,
                           Expr min1, Expr extent1,
                           std::string name = "") {
    std::vector<std::pair<Expr, Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    ranges.push_back({ min1, extent1 });
    return new RDom(ranges, name);
}

RDom *RDom_constructor3(Expr min0, Expr extent0,
                           Expr min1, Expr extent1,
                           Expr min2, Expr extent2,
                           std::string name = "") {
    std::vector<std::pair<Expr, Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    ranges.push_back({ min1, extent1 });
    ranges.push_back({ min2, extent2 });
    return new RDom(ranges, name);
}

RDom *RDom_constructor4(Expr min0, Expr extent0,
                           Expr min1, Expr extent1,
                           Expr min2, Expr extent2,
                           Expr min3, Expr extent3,
                           std::string name = "") {
    std::vector<std::pair<Expr, Expr>> ranges;
    ranges.push_back({ min0, extent0 });
    ranges.push_back({ min1, extent1 });
    ranges.push_back({ min2, extent2 });
    ranges.push_back({ min3, extent3 });
    return new RDom(ranges, name);
}

void define_rdom() {
    define_rvar();

    // only defined so that python knows what to do with it, not meant to be used by user
    py::class_<Internal::ReductionDomain> dummy("_ReductionDomain", py::no_init);

    auto rdom_class = py::class_<RDom>("RDom",
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
                                      py::init<>(py::arg("self"), "Construct an undefined reduction domain."))
                          .def(py::init<Buffer<>>(py::args("self", "buffer"),
                                                   "Construct a reduction domain that iterates over all points in "
                                                   "a given Buffer, Image, or ImageParam. "
                                                   "Has the same dimensionality as the argument."))
                          .def(py::init<OutputImageParam>(py::args("self", "image_param"),
                                                      "Construct a reduction domain that iterates over all points in "
                                                      "a given Buffer, Image, or ImageParam. "
                                                      "Has the same dimensionality as the argument."))
                          .def(py::init<Internal::ReductionDomain>(
                              py::args("self", "domain"),
                              "Construct a reduction domain that wraps an Internal ReductionDomain object."))
                          .def("__init__",
                               py::make_constructor(&RDom_constructor0, py::default_call_policies(),
                                                   (py::arg("ranges"), py::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               py::make_constructor(&RDom_constructor1, py::default_call_policies(),
                                                   (py::args("min0", "extent0"),
                                                    py::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               py::make_constructor(&RDom_constructor2, py::default_call_policies(),
                                                   (py::args("min0", "extent0", "min1", "extent1"),
                                                    py::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               py::make_constructor(&RDom_constructor3, py::default_call_policies(),
                                                   (py::args("min0", "extent0", "min1", "extent1",
                                                            "min2", "extent2"),
                                                    py::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")
                          .def("__init__",
                               py::make_constructor(&RDom_constructor4, py::default_call_policies(),
                                                   (py::args("min0", "extent0", "min1", "extent1",
                                                            "min2", "extent2"),
                                                    py::arg("name") = "")),
                               "Construct a multi-dimensional reduction domain with the given name. "
                               "If the name is left blank, a unique one is auto-generated.")

                          .def("domain", &RDom::domain, py::arg("self"),
                               "Get at the internal reduction domain object that this wraps.")
                          .def("defined", &RDom::defined, py::arg("self"),
                               "Check if this reduction domain is non-NULL")
                          .def("same_as", &RDom::same_as, py::args("self", "other"),
                               "Compare two reduction domains for equality of reference")
                          .def("dimensions", &RDom::dimensions, py::arg("self"),
                               "Get the dimensionality of a reduction domain")
                          .def("where", &RDom::where, py::args("self", "predicate"),
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

    py::implicitly_convertible<RDom, Expr>();
    py::implicitly_convertible<RDom, RVar>();

    add_binary_operators(rdom_class);  // define operators with int, rdom and exprs
    add_binary_operators_with<Expr>(rdom_class);
}

}  // namespace PythonBindings
}  // namespace Halide
