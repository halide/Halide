#include "PyParam.h"


#include "PyBinaryOperators.h"
#include "PyType.h"

namespace Halide {
namespace PythonBindings {

Expr imageparam_to_expr_operator0(ImageParam &that, py::tuple args_passed) {
    std::vector<Expr> expr_args;
    // All ImageParam operator()(...) Expr and Var variants end up building a vector<Expr>
    // all other variants are equivalent to this one
    const size_t args_len = py::len(args_passed);
    for (size_t i = 0; i < args_len; i += 1) {
        expr_args.push_back(py::extract<Expr>(args_passed[i]));
    }

    return that(expr_args);
}

Expr imageparam_to_expr_operator1(ImageParam &that, Expr an_expr) {
    std::vector<Expr> expr_args;
    expr_args.push_back(an_expr);
    // All ImageParam operator()(...) Expr and Var variants end up building a vector<Expr>
    // all other variants are equivalent to this one
    return that(expr_args);
}

std::string imageparam_repr(const ImageParam &param) {
    std::ostringstream o;
    o << "<halide.ImageParam '" <<param.name() << "'";
    if (!param.defined()) {
        o << " (undefined)";
    } else {
        // TODO: add dimensions to this
        o << " type " << halide_type_to_string(param.type());
    }
    o << ">";
    return o.str();
}

Buffer<> image_param_get(ImageParam &param) {
    return param.get();
}

template <typename T>
void image_param_set(ImageParam &param, const Buffer<T> &im) {
    param.set(im);
}

void define_image_param() {
    auto image_param_class =
        py::class_<ImageParam>("ImageParam",
                              "An Image parameter to a halide pipeline. E.g., the input image. \n"
                              "Constructor:: \n"
                              "  ImageParam(Type t, int dims, name="
                              ") \n"
                              "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. "
                              "Supports most of the methods of Image.",
                              py::init<Type, int, std::string>(py::args("self", "t", "dims", "name")))
            .def(py::init<Type, int>(py::args("self", "t", "dims")))
            .def("name", &ImageParam::name, py::arg("self"),
                 py::return_value_policy<py::copy_const_reference>(),
                 "Get name of ImageParam.")

            .def("dimensions", &ImageParam::dimensions, py::arg("self"),
                 "Get the dimensionality of this image parameter")
            .def("channels", &ImageParam::channels, py::arg("self"),
                 "Get an expression giving the extent in dimension 2, "
                 "which by convention is the channel-count of the image")

            .def("width", &ImageParam::width, py::arg("self"),
                 "Get an expression giving the extent in dimension 0, which by "
                 "convention is the width of the image")
            .def("height", &ImageParam::height, py::arg("self"),
                 "Get an expression giving the extent in dimension 1, which by "
                 "convention is the height of the image")

            .def("left", &ImageParam::left, py::arg("self"),
                 "Get an expression giving the minimum coordinate in dimension 0, which "
                 "by convention is the coordinate of the left edge of the image")
            .def("right", &ImageParam::right, py::arg("self"),
                 "Get an expression giving the maximum coordinate in dimension 0, which "
                 "by convention is the coordinate of the right edge of the image")
            .def("top", &ImageParam::top, py::arg("self"),
                 "Get an expression giving the minimum coordinate in dimension 1, which "
                 "by convention is the top of the image")
            .def("bottom", &ImageParam::bottom, py::arg("self"),
                 "Get an expression giving the maximum coordinate in dimension 1, which "
                 "by convention is the bottom of the image")

            .def("set", &image_param_set<uint8_t>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<uint16_t>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<uint32_t>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<int8_t>, py::args("self", "im"),
                 "Bind a buffer to this IageParam. Only relevant for jitting.")
            .def("set", &image_param_set<int16_t>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<int32_t>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<float>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("set", &image_param_set<double>, py::args("self", "im"),
                 "Bind a buffer to this ImageParam. Only relevant for jitting.")
            .def("get", &image_param_get, py::arg("self"),
                 "Get the buffer bound to this ImageParam. Only relevant for jitting.")
            .def("__getitem__", &imageparam_to_expr_operator0, py::args("self", "tuple"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit).\n\n"
                 "Call with: [x], [x,y], [x,y,z], or [x,y,z,w]")
            .def("__getitem__", &imageparam_to_expr_operator1, py::args("self", "expr"),
                 "Construct an expression which loads from this image. "
                 "The location is extended with enough implicit variables to match "
                 "the dimensionality of the image (see \\ref Var::implicit).\n\n"
                 "Call with: [x], [x,y], [x,y,z], or [x,y,z,w]")

            .def("__repr__", &imageparam_repr, py::arg("self"));

    py::implicitly_convertible<ImageParam, Argument>();

    // "Using a param as the argument to an external stage treats it as an Expr"
    //py::implicitly_convertible<ImageParam, ExternFuncArgument>();
}

// TODO: unimplemented?
void define_output_image_param() {

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
}

Expr param_as_expr(Param<> &that) {
    return static_cast<Expr>(that);
}

py::object param_get(const Param<> &param) {
    const Type t = param.type();
    // My kingdom for a Type Visitor pattern!
    if (t == UInt(1)) return py::object(param.get<bool>());
    if (t == UInt(8)) return py::object(param.get<uint8_t>());
    if (t == UInt(16)) return py::object(param.get<uint16_t>());
    if (t == UInt(32)) return py::object(param.get<uint32_t>());
    if (t == UInt(64)) return py::object(param.get<uint64_t>());
    if (t == Int(8)) return py::object(param.get<int8_t>());
    if (t == Int(16)) return py::object(param.get<int16_t>());
    if (t == Int(32)) return py::object(param.get<int32_t>());
    if (t == Int(64)) return py::object(param.get<int64_t>());
    if (t == Float(32)) return py::object(param.get<float>());
    if (t == Float(64)) return py::object(param.get<double>());
    throw std::runtime_error("Unsupported type in get");
    return py::object();
}

void param_set(Param<> &param, py::object value) {
    const Type t = param.type();
    if (t == UInt(1)) { param.set<bool>(py::extract<bool>(value)); return; }
    if (t == UInt(8)) { param.set<uint8_t>(py::extract<uint8_t>(value)); return; }
    if (t == UInt(16)) { param.set<uint16_t>(py::extract<uint16_t>(value)); return; }
    if (t == UInt(32)) { param.set<uint32_t>(py::extract<uint32_t>(value)); return; }
    if (t == UInt(64)) { param.set<uint64_t>(py::extract<uint64_t>(value)); return; }
    if (t == Int(8)) { param.set<int8_t>(py::extract<int8_t>(value)); return; }
    if (t == Int(16)) { param.set<int16_t>(py::extract<int16_t>(value)); return; }
    if (t == Int(32)) { param.set<int32_t>(py::extract<int32_t>(value)); return; }
    if (t == Int(64)) { param.set<int64_t>(py::extract<int64_t>(value)); return; }
    if (t == Float(32)) { param.set<float>(py::extract<float>(value)); return; }
    if (t == Float(64)) { param.set<double>(py::extract<double>(value)); return; }
    throw std::runtime_error("Unsupported type in set");
}

std::string param_repr(const Param<> &param) {
    std::ostringstream o;
    o << "<halide.Param '" <<param.name() << "'"
      << " type " << halide_type_to_string(param.type()) << ">";
    return o.str();
}

std::shared_ptr<Param<>> param_ctor_type_value(const Type &type, py::object value) {
    auto p = std::shared_ptr<Param<>>(new Param<>(type));
    param_set(*p, value);
    return p;
}

std::shared_ptr<Param<>> param_ctor_type_name(const Type &type, const std::string &name) {
    auto p = std::shared_ptr<Param<>>(new Param<>(type, name));
    return p;
}

std::shared_ptr<Param<>> param_ctor_type_name_value(const Type &type, const std::string &name, py::object value) {
    auto p = std::shared_ptr<Param<>>(new Param<>(type, name));
    param_set(*p, value);
    return p;
}

// TODO: add variants for initing with min/max as well (no tests exist for those in Python yet)

void define_param() {
    auto param_class =
        py::class_<Param<>>("Param", py::no_init);
    param_class
        .def(py::init<Type>())
        .def(py::init<Type, std::string>())
        .def("__init__", py::make_constructor(&param_ctor_type_value, py::default_call_policies(), py::args("type", "value")))
        .def("__init__", py::make_constructor(&param_ctor_type_name, py::default_call_policies(), py::args("type", "name")))
        .def("__init__", py::make_constructor(&param_ctor_type_name_value, py::default_call_policies(), py::args("type", "name", "value")))

        .def("name", &Param<>::name, py::arg("self"),
             py::return_value_policy<py::copy_const_reference>(),
             "Get the name of this parameter")
        .def("is_explicit_name", &Param<>::is_explicit_name, py::arg("self"),
             "Return true iff the name was explicitly specified in the ctor (vs autogenerated).")

        .def("get", &param_get)
        .def("set", &param_set)

        .def("type", &Param<>::type, py::arg("self"),
             "Get the halide type of T")

        .def("set_range", &Param<>::set_range, py::args("self", "min", "max"),
             "Get or set the possible range of this parameter. "
             "Use undefined Exprs to mean unbounded.")
        .def("set_min_value", &Param<>::set_min_value, py::args("self", "min"),
             "Get or set the possible range of this parameter. "
             "Use undefined Exprs to mean unbounded.")
        .def("set_max_value", &Param<>::set_max_value, py::args("self", "max"),
             "Get or set the possible range of this parameter. "
             "Use undefined Exprs to mean unbounded.")
        .def("min_value", &Param<>::min_value, py::arg("self"))
        .def("max_value", &Param<>::max_value, py::arg("self"))

        .def("expr", &param_as_expr)

        //            "Using a param as the argument to an external stage treats it
        //            "as an Expr"
        //            operator ExternFuncArgument() const

        //            "Construct the appropriate argument matching this parameter,
        //            "for the purpose of generating the right type signature when
        //            "statically compiling halide pipelines."
        //            operator Argument() const

        .def("__repr__", &param_repr, py::arg("self"));

    py::implicitly_convertible<Param<>, Argument>();
    //py::implicitly_convertible<Param<>, ExternFuncArgument>();
    py::implicitly_convertible<Param<>, Expr>();

    add_binary_operators_with<int>(param_class);
    add_binary_operators_with<float>(param_class);
    add_binary_operators_with<Expr>(param_class);

    py::def("user_context_value", &user_context_value,
           "Returns an Expr corresponding to the user context passed to "
           "the function (if any). It is rare that this function is necessary "
           "(e.g. to pass the user context to an extern function written in C).");

    define_image_param();
    define_output_image_param();
}

}  // namespace PythonBindings
}  // namespace Halide
