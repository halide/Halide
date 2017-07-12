
#include "Param.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include "add_operators.h"
#include <boost/mpl/list.hpp>
#include <boost/python.hpp>

#include "Halide.h"
#include "Type.h"

#include <boost/format.hpp>
#include <string>
#include <vector>

namespace h = Halide;
namespace p = boost::python;

h::Expr imageparam_to_expr_operator0(h::ImageParam &that, p::tuple args_passed) {
    std::vector<h::Expr> expr_args;
    // All ImageParam operator()(...) Expr and Var variants end up building a vector<Expr>
    // all other variants are equivalent to this one
    const size_t args_len = p::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        expr_args.push_back(p::extract<h::Expr>(args_passed[i]));
    }

    return that(expr_args);
}

h::Expr imageparam_to_expr_operator1(h::ImageParam &that, h::Expr an_expr) {
    std::vector<h::Expr> expr_args;
    expr_args.push_back(an_expr);
    // All ImageParam operator()(...) Expr and Var variants end up building a vector<Expr>
    // all other variants are equivalent to this one
    return that(expr_args);
}

std::string imageparam_repr(h::ImageParam &param)  // non-const due to a Halide bug in master (to be fixed)
{
    std::string repr;
    const h::Type &t = param.type();
    if (param.defined()) {
        boost::format f("<halide.ImageParam named '%s' (not yet defined) >");
        repr = boost::str(f % param.name());
    } else {
        boost::format f("<halide.ImageParam named '%s' of type '%s(%i)' and dimensions %i %i %i %i>");
        repr = boost::str(f % param.name() %
                          type_code_to_string(t) % t.bits() %
                          param.dim(0).extent() %
                          param.dim(1).extent() %
                          param.dim(2).extent() %
                          param.dim(3).extent());
    }
    return repr;
}

h::Buffer<> image_param_get(h::ImageParam &param) {
    return param.get();
}

template <typename T>
void image_param_set(h::ImageParam &param, const h::Buffer<T> &im) {
    param.set(im);
}

void defineImageParam() {
    using Halide::ImageParam;

    auto image_param_class =
        p::class_<ImageParam>("ImageParam",
                              "An Image parameter to a halide pipeline. E.g., the input image. \n"
                              "Constructor:: \n"
                              "  ImageParam(Type t, int dims, name="
                              ") \n"
                              "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. "
                              "Supports most of the methods of Image.",
                              p::init<h::Type, int, std::string>(p::args("self", "t", "dims", "name")))
            .def(p::init<h::Type, int>(p::args("self", "t", "dims")))
            .def("name", &ImageParam::name, p::arg("self"),
                 p::return_value_policy<p::copy_const_reference>(),
                 "Get name of ImageParam.")

            .def("dimensions", &ImageParam::dimensions, p::arg("self"),
                 "Get the dimensionality of this image parameter")
            .def("channels", &ImageParam::channels, p::arg("self"),
                 "Get an expression giving the extent in dimension 2, "
                 "which by convention is the channel-count of the image")

            .def("width", &ImageParam::width, p::arg("self"),
                 "Get an expression giving the extent in dimension 0, which by "
                 "convention is the width of the image")
            .def("height", &ImageParam::height, p::arg("self"),
                 "Get an expression giving the extent in dimension 1, which by "
                 "convention is the height of the image")

            .def("left", &ImageParam::left, p::arg("self"),
                 "Get an expression giving the minimum coordinate in dimension 0, which "
                 "by convention is the coordinate of the left edge of the image")
            .def("right", &ImageParam::right, p::arg("self"),
                 "Get an expression giving the maximum coordinate in dimension 0, which "
                 "by convention is the coordinate of the right edge of the image")
            .def("top", &ImageParam::top, p::arg("self"),
                 "Get an expression giving the minimum coordinate in dimension 1, which "
                 "by convention is the top of the image")
            .def("bottom", &ImageParam::bottom, p::arg("self"),
                 "Get an expression giving the maximum coordinate in dimension 1, which "
                 "by convention is the bottom of the image")

            .def("set", &image_param_set<uint8_t>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<uint16_t>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<uint32_t>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<int8_t>, p::args("self", "im"),
                 "Bind a buffer to this IageParam. Only relevant for jitting.")
            .def("set", &image_param_set<int16_t>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<int32_t>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<float>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<double>, p::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("get", &image_param_get, p::arg("self"),
                 "Get the buffer bound to this ImageParam. Only relevant for jitting.")
            .def("__getitem__", &imageparam_to_expr_operator0, p::args("self", "tuple"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit).\n\n"
                 "Call with: [x], [x,y], [x,y,z], or [x,y,z,w]")
            .def("__getitem__", &imageparam_to_expr_operator1, p::args("self", "expr"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit).\n\n"
                 "Call with: [x], [x,y], [x,y,z], or [x,y,z,w]")

            .def("__repr__", &imageparam_repr, p::arg("self"));

    p::implicitly_convertible<ImageParam, h::Argument>();

    // "Using a param as the argument to an external stage treats it as an Expr"
    //p::implicitly_convertible<ImageParam, h::ExternFuncArgument>();

    return;
}

void defineOutputImageParam() {

    //"A handle on the output buffer of a pipeline. Used to make static
    // "promises about the output size and stride."
    //class OutputImageParam {
    //protected:
    //    "A reference-counted handle on the internal parameter object"
    //    Internal::Parameter param;

    //    void add_implicit_args_if_placeholder(std::vector<Expr> &args,
    //                                          Expr last_arg,
    //                                          int total_args,
    //                                          bool *placeholder_seen) const;
    //public:

    //    "Construct a NULL image parameter handle."
    //    OutputImageParam() {}

    //    "Virtual destructor. Does nothing."
    //    EXPORT virtual ~OutputImageParam();

    //    "Construct an OutputImageParam that wraps an Internal Parameter object."
    //    EXPORT OutputImageParam(const Internal::Parameter &p);

    //    "Get the name of this Param"
    //    EXPORT const std::string &name() const;

    //    "Get the type of the image data this Param refers to"
    //    EXPORT Type type() const;

    //    "Is this parameter handle non-NULL"
    //    EXPORT bool defined();

    //    "Get an expression representing the minimum coordinates of this image
    //     "parameter in the given dimension."
    //    EXPORT Expr min(int x) const;

    //    "Get an expression representing the extent of this image
    //     "parameter in the given dimension"
    //    EXPORT Expr extent(int x) const;

    //    "Get an expression representing the stride of this image in the
    //     "given dimension"
    //    EXPORT Expr stride(int x) const;

    //    "Set the extent in a given dimension to equal the given
    //     "expression. Images passed in that fail this check will generate
    //     "a runtime error. Returns a reference to the ImageParam so that
    //     "these calls may be chained.
    //     *
    //     "This may help the compiler generate better
    //     "code. E.g:
    //     \code
    //     im.set_extent(0, 100);
    //     \endcode
    //     "tells the compiler that dimension zero must be of extent 100,
    //     "which may result in simplification of boundary checks. The
    //     "value can be an arbitrary expression:
    //     \code
    //     im.set_extent(0, im.extent(1));
    //     \endcode
    //     "declares that im is a square image (of unknown size), whereas:
    //     \code
    //     im.set_extent(0, (im.extent(0)/32)*32);
    //     \endcode
    //     "tells the compiler that the extent is a multiple of 32."
    //    EXPORT OutputImageParam &set_extent(int dim, Expr extent);

    //    "Set the min in a given dimension to equal the given
    //     "expression. Setting the mins to zero may simplify some
    //     "addressing math."
    //    EXPORT OutputImageParam &set_min(int dim, Expr min);

    //    "Set the stride in a given dimension to equal the given
    //     "value. This is particularly helpful to set when
    //     "vectorizing. Known strides for the vectorized dimension
    //     "generate better code."
    //    EXPORT OutputImageParam &set_stride(int dim, Expr stride);

    //    "Set the min and extent in one call."
    //    EXPORT OutputImageParam &set_bounds(int dim, Expr min, Expr extent);

    //    "Get the dimensionality of this image parameter"
    //    EXPORT int dimensions() const;

    //    "Get an expression giving the minimum coordinate in dimension 0, which
    //     "by convention is the coordinate of the left edge of the image"
    //    EXPORT Expr left() const;

    //    "Get an expression giving the maximum coordinate in dimension 0, which
    //     "by convention is the coordinate of the right edge of the image"
    //    EXPORT Expr right() const;

    //    "Get an expression giving the minimum coordinate in dimension 1, which
    //     "by convention is the top of the image"
    //    EXPORT Expr top() const;

    //    "Get an expression giving the maximum coordinate in dimension 1, which
    //     "by convention is the bottom of the image"
    //    EXPORT Expr bottom() const;

    //    "Get an expression giving the extent in dimension 0, which by
    //     "convention is the width of the image"
    //    EXPORT Expr width() const;

    //    "Get an expression giving the extent in dimension 1, which by
    //     "convention is the height of the image"
    //    EXPORT Expr height() const;

    //    "Get an expression giving the extent in dimension 2, which by
    //     "convention is the channel-count of the image"
    //    EXPORT Expr channels() const;

    //    "Get at the internal parameter object representing this ImageParam."
    //    EXPORT Internal::Parameter parameter() const;

    //    "Construct the appropriate argument matching this parameter,
    //     "for the purpose of generating the right type signature when
    //     "statically compiling halide pipelines."
    //    EXPORT virtual operator Argument() const;

    //    "Using a param as the argument to an external stage treats it
    //     "as an Expr"
    //    EXPORT operator ExternFuncArgument() const;
    //};

    return;
}

template <typename T>
h::Expr param_as_expr(h::Param<T> &that) {
    return static_cast<h::Expr>(that);
}

template <typename T>
std::string param_repr(const h::Param<T> &param) {
    std::string repr;
    const h::Type &t = param.type();
    boost::format f("<halide.Param named '%s' of type '%s(%i)'>");
    repr = boost::str(f % param.name() % type_code_to_string(t) % t.bits());

    return repr;
}

template <typename T>
void defineParam_impl(const std::string suffix, const h::Type type) {
    using Halide::Param;

    auto param_class =
        p::class_<Param<T>>(("Param" + suffix).c_str(),
                            "A scalar parameter to a halide pipeline. If you're jitting, this "
                            "should be bound to an actual value of type T using the set method "
                            "before you realize the function uses this. If you're statically "
                            "compiling, this param should appear in the argument list.",
                            p::init<>(
                                p::arg("self"),
                                "Construct a scalar parameter of type T with a unique auto-generated name"));
    param_class
        .def(p::init<T>(
            p::args("self", "val"),
            "Construct a scalar parameter of type T an initial value of "
            "'val'. Only triggers for scalar types."))
        .def(p::init<std::string>(
            p::args("self", "name"), "Construct a scalar parameter of type T with the given name."))
        .def(p::init<std::string, T>(
            p::args("self", "name", "val"),
            "Construct a scalar parameter of type T with the given name "
            "and an initial value of 'val'."))
        .def(p::init<T, h::Expr, h::Expr>(
            p::args("self", "val", "min", "max"),
            "Construct a scalar parameter of type T with an initial value of 'val' "
            "and a given min and max."))
        .def(p::init<std::string, T, h::Expr, h::Expr>(
            p::args("self", "name", "val", "min", "max"),
            "Construct a scalar parameter of type T with the given name "
            "and an initial value of 'val' and a given min and max."))

        .def("name", &Param<T>::name, p::arg("self"),
             p::return_value_policy<p::copy_const_reference>(),
             "Get the name of this parameter")
        .def("is_explicit_name", &Param<T>::is_explicit_name, p::arg("self"),
             "Return true iff the name was explicitly specified in the ctor (vs autogenerated).")

        .def("get", &Param<T>::get, p::arg("self"),
             "Get the current value of this parameter. Only meaningful when jitting.")
        .def("set", &Param<T>::set, p::args("self", "val"),
             "Set the current value of this parameter. Only meaningful when jitting")
        //            .def("get_address", &Param<T>::get_address, p::arg("self"),
        //                 "Get a pointer to the location that stores the current value of
        //                 "this parameter. Only meaningful for jitting.")

        .def("type", &Param<T>::type, p::arg("self"),
             "Get the halide type of T")

        .def("set_range", &Param<T>::set_range, p::args("self", "min", "max"),
             "Get or set the possible range of this parameter. "
             "Use undefined Exprs to mean unbounded.")
        .def("set_min_value", &Param<T>::set_min_value, p::args("self", "min"),
             "Get or set the possible range of this parameter. "
             "Use undefined Exprs to mean unbounded.")
        .def("set_max_value", &Param<T>::set_max_value, p::args("self", "max"),
             "Get or set the possible range of this parameter. "
             "Use undefined Exprs to mean unbounded.")
        .def("get_min_value", &Param<T>::get_min_value, p::arg("self"))
        .def("get_max_value", &Param<T>::get_max_value, p::arg("self"))

        .def("expr", &param_as_expr<T>, p::arg("self"),
             "You can use this parameter as an expression in a halide "
             "function definition")

        //            "You can use this parameter as an expression in a halide
        //            "function definition"
        //            operator Expr() const

        //            "Using a param as the argument to an external stage treats it
        //            "as an Expr"
        //            operator ExternFuncArgument() const

        //            "Construct the appropriate argument matching this parameter,
        //            "for the purpose of generating the right type signature when
        //            "statically compiling halide pipelines."
        //            operator Argument() const

        .def("__repr__", &param_repr<T>, p::arg("self"));

    p::implicitly_convertible<Param<T>, h::Argument>();
    //p::implicitly_convertible<Param<T>, h::ExternFuncArgument>();
    p::implicitly_convertible<Param<T>, h::Expr>();

    typedef decltype(param_class) pc_t;
    add_operators_with<pc_t, int>(param_class);
    add_operators_with<pc_t, float>(param_class);
    add_operators_with<pc_t, h::Expr>(param_class);

    add_operators_with<pc_t, Param<uint8_t>>(param_class);
    add_operators_with<pc_t, Param<uint16_t>>(param_class);
    add_operators_with<pc_t, Param<uint32_t>>(param_class);

    add_operators_with<pc_t, Param<int8_t>>(param_class);
    add_operators_with<pc_t, Param<int16_t>>(param_class);
    add_operators_with<pc_t, Param<int32_t>>(param_class);

    add_operators_with<pc_t, Param<float>>(param_class);
    add_operators_with<pc_t, Param<double>>(param_class);

    return;
}

template <typename T, typename... Args>
p::object create_param_object(Args... args) {
    typedef h::Param<T> ParamType;
    typedef typename p::manage_new_object::apply<ParamType *>::type converter_t;
    converter_t converter;
    PyObject *obj = converter(new ParamType(args...));
    return p::object(p::handle<>(obj));
}

struct end_of_recursion_t {};  // dummy helper type

// C++ fun, variadic template recursive function !
template <typename T = end_of_recursion_t, typename... Types>
p::object create_param0_impl(h::Type type, std::string name) {
    if (h::type_of<T>() == type) {
        if (name != "") {
            return create_param_object<T>(name);
        } else {
            return create_param_object<T>();
        }
    } else {
        return create_param0_impl<Types...>(type, name);  // keep recursing
    }
}

template <>
p::object create_param0_impl<end_of_recursion_t>(h::Type type, std::string /*name*/) {  // end of recursion, did not find a matching type
    printf("create_param0_impl<end_of_recursion_t> received %s\n", type_repr(type).c_str());
    throw std::invalid_argument("ParamFactory::create_param0_impl received type not handled");
    return p::object();
}

//template<>
//struct h::type_of_helper<end_of_recursion_t> {
//    operator h::Type() {
//        return h::Type(); // call default constructor
//    }
//};

//// C++ fun, variadic template recursive function !
//template<bool UseExprs, typename T=end_of_recursion_t, typename ...Types>
//p::object create_param1_impl(h::Type type, std::string name, p::object val, h::Expr min, h::Expr max)
//{
//    if(std::is_same<T, end_of_recursion_t>::value)
//    {
//        // end of recursion, did not find a matching type
//        printf("create_param1_impl<end_of_recursion_t> received %s\n", type_repr(type).c_str());
//        throw std::invalid_argument("ParamFactory::create_param1_impl received type not handled");
//        return p::object();
//    }

//    if(h::type_of<T>() == type)
//    {
//        p::extract<T> val_extract(val);

//        if(val_extract.check())
//        {
//            T true_val = val_extract();
//            if(UseExprs == true)
//            {
//                if(name != "")
//                {
//                    return create_param_object<T>(name, true_val, min, max);
//                }
//                else
//                {
//                    return create_param_object<T>(true_val, min, max);
//                }
//            }
//            else
//            { // UseExprs == false
//                if(name != "")
//                {
//                    return create_param_object<T>(name, true_val);
//                }
//                else
//                {
//                    return create_param_object<T>(true_val);
//                }
//            }
//        }
//        else
//        {
//            printf("create_param1_impl type == %s\n", type_repr(type).c_str());
//            const std::string val_str = p::extract<std::string>(p::str(val));
//            printf("create_param1_impl val == %s\n", val_str.c_str());
//            throw std::invalid_argument("ParamFactory::create_param1_impl called with "
//                                        "a value that could not be converted to the given type");
//        }
//    }
//    else
//    {
//        return create_param1_impl<UseExprs, Types...>(type, name, val, min, max); // keep recursing
//    }
//}

typedef boost::mpl::list<boost::uint8_t, boost::uint16_t, boost::uint32_t,
                         boost::int8_t, boost::int16_t, boost::int32_t,
                         float, double>
    pixel_types_t;

// C++ fun, variadic template recursive function !
// (if you wonder why struct::operator() and not a function,
// see http://artofsoftware.org/2012/12/20/c-template-function-partial-specialization )

template <typename PixelTypes, typename... Args>
struct create_param1_impl_t {
    p::object operator()(h::Type type, p::object val, Args... args) {
        typedef typename boost::mpl::front<PixelTypes>::type pixel_t;
        if (h::type_of<pixel_t>() == type) {
            p::extract<pixel_t> val_extract(val);
            if (val_extract.check()) {
                pixel_t true_val = val_extract();
                return call_create_param_object<pixel_t>(true_val, args...);
            } else {
                printf("create_param1_impl type == %s\n", type_repr(type).c_str());
                const std::string val_str = p::extract<std::string>(p::str(val));
                printf("create_param1_impl val == %s\n", val_str.c_str());
                throw std::invalid_argument("ParamFactory::create_param1_impl called with "
                                            "a value that could not be converted to the given type");
            }
        } else {  // keep recursing
            typedef typename boost::mpl::pop_front<PixelTypes>::type pixels_types_tail_t;
            return create_param1_impl_t<pixels_types_tail_t, Args...>()(type, val, args...);
        }
    }

    template <typename T>
    p::object call_create_param_object(T true_val) {
        return create_param_object<T>(true_val);
    }

    template <typename T>
    p::object call_create_param_object(T true_val, std::string name) {
        return create_param_object<T>(name, true_val);
    }

    template <typename T>
    p::object call_create_param_object(T true_val, std::string name, h::Expr min, h::Expr max) {
        return create_param_object<T>(name, true_val, min, max);
    }

    template <typename T>
    p::object call_create_param_object(T true_val, h::Expr min, h::Expr max) {
        return create_param_object<T>(true_val, min, max);
    }

    //    template<typename T, typename ...Args2>
    //    p::object call_create_param_object(T true_val, Args2... args)
    //    {
    //        throw std::runtime_error("create_param1_impl_t was called with parameters types not yet handled");
    //        return p::object();
    //    }
};

template <typename... Args>
struct create_param1_impl_t<boost::mpl::l_end::type, Args...> {
    p::object operator()(h::Type type, p::object val, Args... args) {
        // end of recursion, did not find a matching type
        printf("create_param1_impl<end_of_recursion_t> received %s\n", type_repr(type).c_str());
        throw std::invalid_argument("ParamFactory::create_param1_impl received type not handled");
        return p::object();
    }
};

struct ParamFactory {
    static p::object create_param0(h::Type type) {
        return create_param0_impl<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double>(type, "");
    }

    static p::object create_param1(h::Type type, std::string name) {
        return create_param0_impl<
            boost::uint8_t, boost::uint16_t, boost::uint32_t,
            boost::int8_t, boost::int16_t, boost::int32_t,
            float, double>(type, name);
    }

    static p::object create_param2(h::Type type, p::object val) {
        return create_param1_impl_t<pixel_types_t>()(type, val);
    }

    static p::object create_param3(h::Type type, std::string name, p::object val) {
        return create_param1_impl_t<pixel_types_t, std::string>()(type, val, name);
    }

    static p::object create_param4(h::Type type, p::object val, h::Expr min, h::Expr max) {
        return create_param1_impl_t<pixel_types_t, h::Expr, h::Expr>()(type, val, min, max);
    }

    static p::object create_param5(h::Type type, std::string name, p::object val, h::Expr min, h::Expr max) {
        return create_param1_impl_t<pixel_types_t, std::string, h::Expr, h::Expr>()(type, val, name, min, max);
    }
};

void defineParam() {
    // Might create linking problems, if Param.cpp is not included in the python library

    defineParam_impl<uint8_t>("_uint8", h::UInt(8));
    defineParam_impl<uint16_t>("_uint16", h::UInt(16));
    defineParam_impl<uint32_t>("_uint32", h::UInt(32));

    defineParam_impl<int8_t>("_int8", h::Int(8));
    defineParam_impl<int16_t>("_int16", h::Int(16));
    defineParam_impl<int32_t>("_int32", h::Int(32));

    defineParam_impl<float>("_float32", h::Float(32));
    defineParam_impl<double>("_float64", h::Float(64));

    // "Param" will look as a class, but instead it will be simply a factory method
    // Order of definitions matter, the last defined method is attempted first
    // Here it is important to try "type, name" before "type, val"
    p::def("Param", &ParamFactory::create_param5, p::args("type", "name", "val", "min", "max"),
           "Construct a scalar parameter of type T with the given name "
           "and an initial value of 'val' and a given min and max.");
    p::def("Param", &ParamFactory::create_param4, p::args("type", "val", "min", "max"),
           "Construct a scalar parameter of type T with an initial value of 'val' "
           "and a given min and max.");
    p::def("Param", &ParamFactory::create_param3, p::args("type", "name", "val"),
           "Construct a scalar parameter of type T with the given name "
           "and an initial value of 'val'.");
    p::def("Param", &ParamFactory::create_param2, p::args("type", "val"),
           "Construct a scalar parameter of type T an initial value of "
           "'val'. Only triggers for scalar types.");
    p::def("Param", &ParamFactory::create_param1, p::args("type", "name"),
           "Construct a scalar parameter of type T with the given name.");
    p::def("Param", &ParamFactory::create_param0, p::args("type"),
           "Construct a scalar parameter of type T with a unique auto-generated name");

    ;

    p::def("user_context_value", &h::user_context_value,
           "Returns an Expr corresponding to the user context passed to "
           "the function (if any). It is rare that this function is necessary "
           "(e.g. to pass the user context to an extern function written in C).");

    defineImageParam();
    defineOutputImageParam();
    return;
}
