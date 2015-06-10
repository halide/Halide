#include "RDom.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"

#include "../../src/RDom.h"

#include <string>


void defineRDom()
{

    using Halide::RDom;
    using Halide::RVar;
    namespace h = Halide;
    namespace p = boost::python;
    using p::self;


    //    p::class_<RVar>("RVar",
    //                          "An Image parameter to a halide pipeline. E.g., the input image. \n"
    //                          "Constructor:: \n"
    //                          "  ImageParam(Type t, int dims, name="") \n"
    //                          "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. Supports most of \n"
    //                          "the methods of Image.",
    //                          p::init<h::Type, int, std::string>(p::args("t", "dims", "name"))
    //                          )
    //            .def(p::init<h::Type, int>(p::args("t", "dims")))
    //            .def("name",
    //                 &ImageParam::name,
    //                 p::return_value_policy<p::copy_const_reference>(),
    //                 "Get name of ImageParam.")
    //            .def("set", &ImageParam::set, p::arg("b"),
    //                 "Bind a Buffer, Image, numpy array, or PIL image. Only relevant for jitting.")
    //            .def("get", &ImageParam::get,
    //                 "Get the Buffer that is bound. Only relevant for jitting.");


    //    class RVar(object):
    //        """
    //        A reduction variable represents a single dimension of a reduction
    //        domain (RDom). Don't construct them directly, instead construct an
    //        RDom, and use fields .x, .y, .z, .w to get at the variables. For
    //        single-dimensional reduction domains, you can just cast a
    //        single-dimensional RDom to an RVar.
    //        """
    //        def __new__(cls, *args):
    //            return RVarType(*args)

    //        def min(self):
    //            """
    //            The minimum value that this variable will take on.
    //            """

    //        def extent(self):
    //            """
    //            The number that this variable will take on. The maximum value
    //            of this variable will be min() + extent() - 1.
    //            """

    //        def name(self):
    //            """
    //            The name of this reduction variable.
    //            """


    //    /** A reduction variable represents a single dimension of a reduction
    //     * domain (RDom). Don't construct them directly, instead construct an
    //     * RDom, and use RDom::operator[] to get at the variables. For
    //     * single-dimensional reduction domains, you can just cast a
    //     * single-dimensional RDom to an RVar. */
    //    class RVar {
    //        std::string _name;
    //        Internal::ReductionDomain _domain;
    //        int _index;

    //        const Internal::ReductionVariable &_var() const {
    //            return _domain.domain().at(_index);
    //        }

    //    public:
    //        /** An empty reduction variable. */
    //        RVar() : _name(Internal::make_entity_name(this, "Halide::RVar", 'r')) {}

    //        /** Construct an RVar with the given name */
    //        explicit RVar(const std::string &n) : _name(n) {
    //            // Make sure we don't get a unique name with the same name as
    //            // this later:
    //            Internal::unique_name(n, false);
    //        }

    //        /** Construct a reduction variable with the given name and
    //         * bounds. Must be a member of the given reduction domain. */
    //        RVar(Internal::ReductionDomain domain, int index) :
    //            _domain(domain), _index(index) {
    //        }

    //        /** The minimum value that this variable will take on */
    //        EXPORT Expr min() const;

    //        /** The number that this variable will take on. The maximum value
    //         * of this variable will be min() + extent() - 1 */
    //        EXPORT Expr extent() const;

    //        /** The reduction domain this is associated with. */
    //        EXPORT Internal::ReductionDomain domain() const {return _domain;}

    //        /** The name of this reduction variable */
    //        EXPORT const std::string &name() const;

    //        /** Reduction variables can be used as expressions. */
    //        EXPORT operator Expr() const;
    //    };


    //    p::class_<RDom>("RDom",
    //                          "An Image parameter to a halide pipeline. E.g., the input image. \n"
    //                          "Constructor:: \n"
    //                          "  ImageParam(Type t, int dims, name="") \n"
    //                          "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. Supports most of \n"
    //                          "the methods of Image.",
    //                          p::init<h::Type, int, std::string>(p::args("t", "dims", "name"))
    //                          )
    //            .def(p::init<h::Type, int>(p::args("t", "dims")))
    //            .def("name",
    //                 &ImageParam::name,
    //                 p::return_value_policy<p::copy_const_reference>(),
    //                 "Get name of ImageParam.")
    //            .def("set", &ImageParam::set, p::arg("b"),
    //                 "Bind a Buffer, Image, numpy array, or PIL image. Only relevant for jitting.")
    //            .def("get", &ImageParam::get,
    //                 "Get the Buffer that is bound. Only relevant for jitting.");



    //    class RDom(object):
    //        """
    //        A multi-dimensional domain over which to iterate. Used when
    //        defining functions as reductions. See apps/bilateral_grid.py for an
    //        example of a reduction.

    //        Constructors::

    //          RDom(Expr min, Expr extent, name="")                             -- 1D reduction
    //          RDom(Expr min0, Expr extent0, Expr min1, Expr extent1, name="")  -- 2D reduction
    //          (Similar for 3D and 4D reductions)
    //          RDom(Buffer|ImageParam)                    -- Iterate over all points in the domain

    //        The following global functions can be used for inline reductions::

    //            minimum, maximum, product, sum
    //        """
    //        def __new__(cls, *args):
    //            args = [wrap(x) if not isinstance(x,str) else x for x in args]
    //            return RDomType(*args)

    //        def defined(self):
    //            """
    //            Check if reduction domain is non-NULL.
    //            """

    //        def same_as(self, other):
    //            """
    //            Check if two reduction domains are the same.
    //            """

    //        def dimensions(self):
    //            """
    //            Number of dimensions.
    //            """

    //        x = property(doc="Access dimension 1 of reduction domain.")
    //        y = property(doc="Access dimension 2 of reduction domain.")
    //        z = property(doc="Access dimension 3 of reduction domain.")
    //        w = property(doc="Access dimension 4 of reduction domain.")





    //    /** Construct an undefined reduction domain. */
    //    EXPORT RDom() {}

    //    /** Construct a multi-dimensional reduction domain with the given name. If the name
    //     * is left blank, a unique one is auto-generated. */
    //    // @{
    //    NO_INLINE RDom(const std::vector<std::pair<Expr, Expr>> &ranges, std::string name = "") {
    //        initialize_from_ranges(ranges, name);
    //    }

    //    template <typename... Args>
    //    NO_INLINE RDom(Expr min, Expr extent, Args... args) {
    //        // This should really just be a delegating constructor, but I couldn't make
    //        // that work with variadic template unpacking in visual studio 2013
    //        std::vector<std::pair<Expr, Expr>> ranges;
    //        initialize_from_ranges(ranges, min, extent, args...);
    //    }
    //    // @}

    //    /** Construct a reduction domain that iterates over all points in
    //     * a given Buffer, Image, or ImageParam. Has the same
    //     * dimensionality as the argument. */
    //    // @{
    //    EXPORT RDom(Buffer);
    //    EXPORT RDom(ImageParam);
    //    // @}

    //    /** Construct a reduction domain that wraps an Internal ReductionDomain object. */
    //    EXPORT RDom(Internal::ReductionDomain d);

    //    /** Get at the internal reduction domain object that this wraps. */
    //    Internal::ReductionDomain domain() const {return dom;}

    //    /** Check if this reduction domain is non-NULL */
    //    bool defined() const {return dom.defined();}

    //    /** Compare two reduction domains for equality of reference */
    //    bool same_as(const RDom &other) const {return dom.same_as(other.dom);}

    //    /** Get the dimensionality of a reduction domain */
    //    EXPORT int dimensions() const;

    //    /** Get at one of the dimensions of the reduction domain */
    //    EXPORT RVar operator[](int) const;

    //    /** Single-dimensional reduction domains can be used as RVars directly. */
    //    EXPORT operator RVar() const;

    //    /** Single-dimensional reduction domains can be also be used as Exprs directly. */
    //    EXPORT operator Expr() const;

    //    /** Direct access to the first four dimensions of the reduction
    //     * domain. Some of these variables may be undefined if the
    //     * reduction domain has fewer than four dimensions. */
    //    // @{
    //    RVar x, y, z, w;
    //    // @}


    p::implicitly_convertible<RVar, h::Expr>();
    p::implicitly_convertible<RDom, h::Expr>();
    return;
}
