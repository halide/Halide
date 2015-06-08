#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/operators.hpp>
#include <boost/python/str.hpp>
#include <boost/python.hpp>

//#include <Halide.h>
//#include "../../build/include/Halide.h"
#include "../../src/Var.h"
#include "../../src/Expr.h"
#include "../../src/IROperator.h"

#include "../../src/Func.h"

#include "../../src/Type.h"

#include "../../src/Param.h"



#include <string>

namespace h = Halide;
namespace p = boost::python;
using p::self;

char const* greet()
{
    return "hello, world from Halide python bindings";
}

/*
input = ImageParam(UInt(16), 2, 'input')
        x, y = Var('x'), Var('y')

blur_x = Func('blur_x')
        blur_y = Func('blur_y')

        blur_x[x,y] = (input[x,y]+input[x+1,y]+input[x+2,y])/3
        blur_y[x,y] = (blur_x[x,y]+blur_x[x,y+1]+blur_x[x,y+2])/3

        xi, yi = Var('xi'), Var('yi')

        blur_y.tile(x, y, xi, yi, 8, 4).parallel(y).vectorize(xi, 8)
        blur_x.compute_at(blur_y, x).vectorize(x, 8)

        maxval = 255
        in_image = Image(UInt(16), builtin_image('rgb.png'), scale=1.0) # Set scale to 1 so that we only use 0...255 of the UInt(16) range
        eval_func = filter_image(input, blur_y, in_image, disp_time=True, out_dims = (OUT_DIMS[0]-8, OUT_DIMS[1]-8), times=5)
        I = eval_func()
        if len(sys.argv) >= 2:
        I.save(sys.argv[1], maxval)
        else:
        I.show(maxval)
*/

template<typename PythonClass>
void add_operators(PythonClass &class_instance)
{
    using namespace boost::python;

    // FIXME Var + int, Var + float not yet working
    class_instance
            .def(self + self)
            .def(self - self)
            .def(self * self)
            .def(self / self)
            .def(self % self)
            //.def(pow(self, p::other<float>))
            .def(pow(self, self))
            .def(self & self) // and
            .def(self | self) // or
            .def(-self) // neg
            .def(~self) // invert
            .def(self < self)
            .def(self <= self)
            .def(self == self)
            .def(self != self)
            .def(self > self)
            .def(self >= self);

    return;
}

void defineVar()
{
    using Halide::Var;
    auto var_class = p::class_<Var>("Var",
                                    "A Halide variable, to be used when defining functions. It is just" \
                                    "a name, and can be reused in places where no name conflict will" \
                                    "occur. It can be used in the left-hand-side of a function" \
                                    "definition, or as an Expr. As an Expr, it always has type Int(32).\n" \
                                    "\n" \
                                    "Constructors::\n" \
                                    "Var()      -- Construct Var with an automatically-generated unique name\n" \
                                    "Var(name)  -- Construct Var with the given string name.\n",
                                    p::init<std::string>())
            .def(p::init<>())
            //.add_property("name", &Var::name) // "Get the name of a Var.")
            .def("name", &Var::name,
                 boost::python::return_value_policy<boost::python:: copy_const_reference>(),
                 "Get the name of a Var.")
            .def("same_as", &Var::same_as, "Test if two Vars are the same.")
            //.def(self == p::other<Var>())
            .def("implicit", &Var::implicit, "Construct implicit Var from int n.");

    add_operators(var_class);
    return;
}


void defineExpr()
{
    using Halide::Expr;

    auto expr_class = p::class_<Expr>("Expr",
                                      "An expression or fragment of Halide code.\n" \
                                      "One can explicitly coerce most types to Expr via the Expr(x) constructor." \
                                      "The following operators are implemented over Expr, and also other types" \
                                      "such as Image, Func, Var, RVar generally coerce to Expr when used in arithmetic::\n\n" \
                                      "+ - * / % ** & |\n" \
                                      "-(unary) ~(unary)\n" \
                                      " < <= == != > >=\n" \
                                      "+= -= *= /=\n" \
                                      "The following math global functions are also available::\n" \
                                      "Unary:\n" \
                                      "  abs acos acosh asin asinh atan atanh ceil cos cosh exp\n" \
                                      "  fast_exp fast_log floor log round sin sinh sqrt tan tanh\n" \
                                      "Binary:\n" \
                                      "  hypot fast_pow max min pow\n\n" \
                                      "Ternary:\n" \
                                      "  clamp(x, lo, hi)                  -- Clamp expression to [lo, hi]\n" \
                                      "  select(cond, if_true, if_false)   -- Return if_true if cond else if_false\n",
                                      p::init<std::string>())
            .def(p::init<int>()) // Make an expression representing a const 32-bit int (i.e. an IntImm)
            .def(p::init<float>()) // Make an expression representing a const 32-bit float (i.e. a FloatImm)
            .def(p::init<double>()) /* Make an expression representing a const 32-bit float, given a
                                                                                                                                                                                                                                                                             * double. Also emits a warning due to truncation. */
            .def(p::init<std::string>()) // Make an expression representing a const string (i.e. a StringImm)
            .def(p::init<const h::Internal::BaseExprNode *>()) //Expr(const Internal::BaseExprNode *n) : IRHandle(n) {}
            .def("type", &Expr::type); // Get the type of this expression node

    add_operators(expr_class);

    return;
}


h::Realization func_realize0(h::Func &that, std::vector<int32_t> sizes, const h::Target &target = h::Target())
{
    return that.realize(sizes, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS( func_realize0_overloads, func_realize0, 2, 3)


h::Realization func_realize1(h::Func &that, int x_size=0, int y_size=0, int z_size=0, int w_size=0,
                             const h::Target &target = h::Target())
{
    return that.realize(x_size, y_size, z_size, w_size, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_realize1_overloads, func_realize1, 1, 6)


void func_compile_jit(h::Func &that)
{
    that.compile_jit();
    return;
}

void func_parallel0(h::Func &that, h::VarOrRVar var)
{
    that.parallel(var);
}


void func_parallel1(h::Func &that, h::VarOrRVar var, int factor)
{
    that.parallel(var, factor);
}


void func_vectorize0(h::Func &that, h::VarOrRVar var)
{
    that.vectorize(var);
}

void func_vectorize1(h::Func &that, h::VarOrRVar var, int factor)
{
    that.vectorize(var, factor);
}

h::Func &func_store_at0(h::Func &that, h::Func f, h::Var var)
{
    return that.store_at(f, var);
}

h::Func &func_store_at1(h::Func &that, h::Func f, h::RVar var)
{
    return that.store_at(f, var);
}


void defineFunc()
{
    using Halide::Func;

    auto func_class = p::class_<Func>("Func",
                                      "A halide function. This class represents one stage in a Halide" \
                                      "pipeline, and is the unit by which we schedule things. By default" \
                                      "they are aggressively inlined, so you are encouraged to make lots" \
                                      "of little functions, rather than storing things in Exprs.\n" \
                                      "Constructors::\n\n" \
                                      "  Func()      -- Declare a new undefined function with an automatically-generated unique name\n" \
                                      "  Func(expr)  -- Declare a new function with an automatically-generated unique\n" \
                                      "                 name, and define it to return the given expression (which may\n" \
                                      "                 not contain free variables).\n" \
                                      "  Func(name)  -- Declare a new undefined function with the given name",
                                      p::init<>())
            .def(p::init<std::string>())
            .def(p::init<h::Expr>());
    //.def("set", &Func::set, "Typically one uses f[x, y] = expr to assign to a function. However f.set(expr) can be used also.")
    /*
    func_class.def("allow_race_conditions",
                   &Func::allow_race_conditions,
                   "Specify that race conditions are permitted for this Func, "
                   "which enables parallelizing over RVars even when Halide cannot "
                   "prove that it is safe to do so. Use this with great caution, "
                   "and only if you can prove to yourself that this is safe, as it "
                   "may result in a non-deterministic routine that returns "
                   "different values at different times or on different machines.");*/

    func_class.def("realize",
                   &func_realize1,
                   func_realize1_overloads(
                       p::args("x_size", "y_size", "z_size", "w_size", "target"),
                       "Evaluate this function over some rectangular domain and return"
                       "the resulting buffer. The buffer should probably be instantly"
                       "wrapped in an Image class.\n\n" \
                       "One can use f.realize(Buffer) to realize into an existing buffer."))
            .def("realize", &func_realize0, func_realize0_overloads(
                     p::args("sizes", "target")));


    /*
                            &Func::compile_to_bitcode(self, filename, list_of_Argument, fn_name=""):
                            """
                            Statically compile this function to llvm bitcode, with the
                            given filename (which should probably end in .bc), type
                            signature, and C function name (which defaults to the same name
                                                            as this halide function.
                                                            """

                                                            &Func::compile_to_c(self, filename, list_of_Argument, fn_name=""):
                                                            """
                                                            Statically compile this function to C source code. This is
                                                            useful for providing fallback code paths that will compile on
                                                            many platforms. Vectorization will fail, and parallelization
                                                            will produce serial code.
                                                            """

                                                            &Func::compile_to_file(self, filename_prefix, list_of_Argument, target):
                                                            """
                                                            Various signatures::

                                                            compile_to_file(filename_prefix, list_of_Argument)
                                                            compile_to_file(filename_prefix)
                                                            compile_to_file(filename_prefix, Argument a)
                                                            compile_to_file(filename_prefix, Argument a, Argument b)

                                                            Compile to object file and header pair, with the given
                                                            arguments. Also names the C function to match the first
                                                            argument.
                                                            """
*/

    func_class.def("compile_jit",
                   &func_compile_jit,
                   "Eagerly jit compile the function to machine code. This "
                   "normally happens on the first call to realize. If you're "
                   "running your halide pipeline inside time-sensitive code and "
                   "wish to avoid including the time taken to compile a pipeline, "
                   "then you can call this ahead of time. Returns the raw function "
                   "pointer to the compiled pipeline.");


    func_class.def("debug_to_file",
                   &Func::debug_to_file,
                   p::arg("filename"),
                   "When this function is compiled, include code that dumps its values "
                   "to a file after it is realized, for the purpose of debugging. "
                   "The file covers the realized extent at the point in the schedule that "
                   "debug_to_file appears.\n"
                   "If filename ends in \".tif\" or \".tiff\" (case insensitive) the file "
                   "is in TIFF format and can be read by standard tools.");

    func_class.def("name",
                   &Func::name,
                   boost::python::return_value_policy<boost::python:: copy_const_reference>(),
                   "The name of this function, either given during construction, or automatically generated.");

    func_class.def("value", &Func::value,
                   "The right-hand-side value of the pure definition of this "
                   "function. May be undefined if the function has no pure definition yet.");

    func_class.def("dimensions", &Func::dimensions,
                   "The dimensionality (number of arguments) of this function. Zero if the function is not yet defined.");

    /*
                                                                &Func::__getitem__(self, *args):
                                                                """
                                                                Either calls to the function, or the left-hand-side of a
                                                                reduction definition (see \ref RDom). If the function has
                                                                already been defined, and fewer arguments are given than the
                                                                function has dimensions, then enough implicit vars are added to
                                                                the end of the argument list to make up the difference.
                                                                """

                                                                &Func::split(self, old, outer, inner, factor):
                                                                """
                                                                Split a dimension into inner and outer subdimensions with the
                                                                given names, where the inner dimension iterates from 0 to
                                                                factor-1. The inner and outer subdimensions can then be dealt
                                                                with using the other scheduling calls. It's ok to reuse the old
                                                                variable name as either the inner or outer variable.

                                                                The arguments are all Var instances.
                                                                """

                                                                &Func::fuse(self, inner, outer, fused):
                                                                """
                                                                Join two dimensions into a single fused dimension. The fused
                                                                dimension covers the product of the extents of the inner and
                                                                outer dimensions given.
                                                                """
*/

    func_class.def("parallel",
                   &func_parallel0,
                   p::arg("var"),
                   "Mark a dimension (Var instance) to be traversed in parallel.")
            .def("parallel",
                 &func_parallel1,
                 p::args("var", "factor"));

    func_class.def("vectorize",
                   &func_vectorize1,
                   p::args("var", "factor"),
                   "Split a dimension (Var instance) by the given int factor, then vectorize the "
                   "inner dimension. This is how you vectorize a loop of unknown "
                   "size. The variable to be vectorized should be the innermost "
                   "one. After this call, var refers to the outer dimension of the "
                   "split.")
            .def("vectorize",
                 &func_vectorize0,
                 p::arg("var"));

    /*
                                                                &Func::unroll(self, var, factor=None):
                                                                """
                                                                Split a dimension (Var instance) by the given int factor, then unroll the inner
                                                                dimension. This is how you unroll a loop of unknown size by
                                                                some constant factor. After this call, var refers to the outer
                                                                dimension of the split.
                                                                """

                                                                &Func::bound(self, min_expr, extent_expr):
                                                                """
                                                                Statically declare that the range over which a function should
                                                                be evaluated is given by the second and third arguments. This
                                                                can let Halide perform some optimizations. E.g. if you know
                                                                there are going to be 4 color channels, you can completely
                                                                vectorize the color channel dimension without the overhead of
                                                                splitting it up. If bounds inference decides that it requires
                                                                more of this function than the bounds you have stated, a
                                                                runtime error will occur when you try to run your pipeline.
                                                                """

                                                                &Func::tile(self, x, y, xo, yo, xi, yi, xfactor, yfactor):
                                                                """
                                                                Traverse in tiled order. Two signatures::

                                                                tile(x, y, xi, yi, xfactor, yfactor)
                                                                tile(x, y, xo, yo, xi, yi, xfactor, yfactor)

                                                                Split two dimensions at once by the given factors, and then
                                                                reorder the resulting dimensions to be xi, yi, xo, yo from
                                                                innermost outwards. This gives a tiled traversal.

                                                                The shorter form of tile reuses the old variable names as
                                                                the new outer dimensions.
                                                                """

                                                                &Func::reorder(self, *args):
                                                                """
                                                                Reorder the dimensions (Var arguments) to have the given nesting
                                                                order, from innermost loop order to outermost.
                                                                """
*/


    func_class.def("rename",
                   &Func::rename,
                   p::args("old_name", "new_name"),
                   p::return_internal_reference<1>(),
                   "Rename a dimension. Equivalent to split with a inner size of one.");



    /*
                                                                &Func::gpu_threads(self, *args):
                                                                """
                                                                Tell Halide that the following dimensions correspond to cuda
                                                                thread indices. This is useful if you compute a producer
                                                                function within the block indices of a consumer function, and
                                                                want to control how that function's dimensions map to cuda
                                                                threads. If the selected target is not ptx, this just marks
                                                                those dimensions as parallel.
                                                                """

                                                                &Func::gpu_single_thread(self, *args):
                                                                """
                                                                Tell Halide to run this stage using a single gpu thread and
                                                                block. This is not an efficient use of your GPU, but it can be
                                                                useful to avoid copy-back for intermediate update stages that
                                                                touch a very small part of your Func.
                                                                """

                                                                &Func::gpu_blocks(self, *args):
                                                                """
                                                                Tell Halide that the following dimensions correspond to cuda
                                                                block indices. This is useful for scheduling stages that will
                                                                run serially within each cuda block. If the selected target is
                                                                not ptx, this just marks those dimensions as parallel.
                                                                """

                                                                &Func::gpu(self, block_x, thread_x):
                                                                """
                                                                Three signatures::

                                                                gpu(block_x, thread_x)
                                                                gpu(block_x, block_y, thread_x, thread_y)
                                                                gpu(block_x, block_y, block_z, thread_x, thread_y, thread_z)

                                                                Tell Halide that the following dimensions correspond to cuda
                                                                block indices and thread indices. If the selected target is not
                                                                ptx, these just mark the given dimensions as parallel. The
                                                                dimensions are consumed by this call, so do all other
                                                                unrolling, reordering, etc first.
                                                                """

                                                                &Func::gpu_tile(self, x, x_size):
                                                                """
                                                                Three signatures:

                                                                gpu_tile(x, x_size)
                                                                gpu_tile(x, y, x_size, y_size)
                                                                gpu_tile(x, y, z, x_size, y_size, z_size)

                                                                Short-hand for tiling a domain and mapping the tile indices
                                                                to cuda block indices and the coordinates within each tile to
                                                                cuda thread indices. Consumes the variables given, so do all
                                                                other scheduling first.
                                                                """

                                                                &Func::cuda_threads(self, *args):
                                                                """
                                                                deprecated Old name for #gpu_threads.
                                                                Tell Halide that the following dimensions correspond to cuda
                                                                thread indices. This is useful if you compute a producer
                                                                function within the block indices of a consumer function, and
                                                                want to control how that function's dimensions map to cuda
                                                                threads. If the selected target is not ptx, this just marks
                                                                those dimensions as parallel.
                                                                """

                                                                &Func::cuda_blocks(self, *args):
                                                                """
                                                                deprecated Old name for #cuda_blocks.
                                                                Tell Halide that the following dimensions correspond to cuda
                                                                block indices. This is useful for scheduling stages that will
                                                                run serially within each cuda block. If the selected target is
                                                                not ptx, this just marks those dimensions as parallel.
                                                                """

                                                                &Func::cuda(self, block_x, thread_x):
                                                                """
                                                                deprecated Old name for #cuda.
                                                                Three signatures::

                                                                cuda(block_x, thread_x)
                                                                cuda(block_x, block_y, thread_x, thread_y)
                                                                cuda(block_x, block_y, block_z, thread_x, thread_y, thread_z)

                                                                Tell Halide that the following dimensions correspond to cuda
                                                                block indices and thread indices. If the selected target is not
                                                                ptx, these just mark the given dimensions as parallel. The
                                                                dimensions are consumed by this call, so do all other
                                                                unrolling, reordering, etc first.
                                                                """

                                                                &Func::cuda_tile(self, x, x_size):
                                                                """
                                                                deprecated Old name for #cuda_tile.
                                                                Three signatures:

                                                                cuda_tile(x, x_size)
                                                                cuda_tile(x, y, x_size, y_size)
                                                                cuda_tile(x, y, z, x_size, y_size, z_size)

                                                                Short-hand for tiling a domain and mapping the tile indices
                                                                to cuda block indices and the coordinates within each tile to
                                                                cuda thread indices. Consumes the variables given, so do all
                                                                other scheduling first.
                                                                """

                                                                &Func::reorder_storage(self, *args):
                                                                """
                                                                Scheduling calls that control how the storage for the function
                                                                is laid out. Right now you can only reorder the dimensions.
                                                                """

                                                                &Func::compute_at(self, f, var):
                                                                """
                                                                Compute this function as needed for each unique value of the
                                                                given var (can be a Var or an RVar) for the given calling function f.
                                                                """
*/

    func_class.def("compute_root",
                   &Func::compute_root,
                   p::return_internal_reference<1>(),
                   "Compute all of this function once ahead of time.");


    func_class.def("store_at",
                   &func_store_at0,
                   p::args("f", "var"),
                   p::return_internal_reference<1>(),
                   "Allocate storage for this function within f's loop over "
                   "var (can be a Var or an RVar). Scheduling storage is optional, and can be used to "
                   "separate the loop level at which storage occurs from the loop "
                   "level at which computation occurs to trade off between locality "
                   "and redundant work.")
            .def("store_at",
                 &func_store_at1,
                 p::args("f", "var"),
                 p::return_internal_reference<1>());

    func_class.def("store_root",
                   &Func::store_root,
                   p::return_internal_reference<1>(),
                   "Equivalent to Func.store_at, but schedules storage outside the outermost loop.");

    func_class.def("compute_inline",
                   &Func::compute_inline,
                   p::return_internal_reference<1>(),
                   "Aggressively inline all uses of this function. This is the "
                   "default schedule, so you're unlikely to need to call this. For "
                   "a reduction, that means it gets computed as close to the "
                   "innermost loop as possible.");

    func_class.def("update",
                   &Func::update,
                   "Get a handle on the update step of a reduction for the "
                   "purposes of scheduling it. Only the pure dimensions of the "
                   "update step can be meaningfully manipulated (see RDom).");

    func_class.def("function",
                   &Func::function,
                   "Get a handle on the internal halide function that this Func represents. "
                   "Useful if you want to do introspection on Halide functions.");


    return;
}


void defineTypes()
{

    p::class_<h::Type>("Type", p::no_init);

    p::def("Int", h::Int,
           (p::arg("bits"), p::arg("width")=1),
           "Constructing an signed integer type");

    p::def("UInt", h::UInt,
           (p::arg("bits"), p::arg("width")=1),
           "Constructing an unsigned integer type");

    p::def("Float", h::Float,
           (p::arg("bits"), p::arg("width")=1),
           "Constructing a floating-point type");

    p::def("Bool", h::Bool,
           (p::arg("width")=1),
           "Construct a boolean type");

    return;
}


void defineParam()
{
    // Might create linking problems, if Param.cpp is not included in the python library
    using h::ImageParam;

    p::class_<ImageParam>("ImageParam",
                          "An Image parameter to a halide pipeline. E.g., the input image. \n"
                          "Constructor:: \n"
                          "  ImageParam(Type t, int dims, name="") \n"
                          "The image can be indexed via I[x], I[y,x], etc, which gives a Halide Expr. Supports most of \n"
                          "the methods of Image.",
                          p::init<h::Type, int, std::string>(p::args("t", "dims", "name"))
                          )
            .def(p::init<h::Type, int>(p::args("t", "dims")))
            .def("name",
                 &ImageParam::name,
                 boost::python::return_value_policy<boost::python:: copy_const_reference>(),
                 "Get name of ImageParam.")
            .def("set", &ImageParam::set, p::arg("b"),
                 "Bind a Buffer, Image, numpy array, or PIL image. Only relevant for jitting.")
            .def("get", &ImageParam::get,
                 "Get the Buffer that is bound. Only relevant for jitting.");
    return;
}


h::Expr reinterpret0(h::Type t, h::Expr e)
{
    return h::reinterpret(t, e);
}


void defineOperators()
{
    // defined in IROperator.h

    p::def("max", &h::max,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "\ref Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("min", &h::min,
           p::args("a", "b"),
           "Returns an expression representing the greater of the two "
           "arguments, after doing any necessary type coercion using "
           "\ref Internal::match_types. Vectorizes cleanly on most platforms "
           "(with the exception of integer types on x86 without SSE4).");

    p::def("clamp", &h::clamp,
           p::args("a", "min_val", "max_val"),
           "Clamps an expression to lie within the given bounds. The bounds "
           "are type-cast to match the expression. Vectorizes as well as min/max.");

    //    /** Returns the absolute value of a signed integer or floating-point
    //     * expression. Vectorizes cleanly. Unlike in C, abs of a signed
    //     * integer returns an unsigned integer of the same bit width. This
    //     * means that abs of the most negative integer doesn't overflow. */
    //    inline Expr abs(Expr a) {
    //        user_assert(a.defined())
    //            << "abs of undefined Expr\n";
    //        Type t = a.type();
    //        if (t.is_int()) {
    //            t.code = Type::UInt;
    //        } else if (t.is_uint()) {
    //            user_warning << "Warning: abs of an unsigned type is a no-op\n";
    //            return a;
    //        }
    //        return Internal::Call::make(t, Internal::Call::abs,
    //                                    {a}, Internal::Call::Intrinsic);
    //    }

    //    /** Return the absolute difference between two values. Vectorizes
    //     * cleanly. Returns an unsigned value of the same bit width. There are
    //     * various ways to write this yourself, but they contain numerous
    //     * gotchas and don't always compile to good code, so use this
    //     * instead. */
    //    inline Expr absd(Expr a, Expr b) {
    //        user_assert(a.defined() && b.defined()) << "absd of undefined Expr\n";
    //        Internal::match_types(a, b);
    //        Type t = a.type();

    //        if (t.is_float()) {
    //            // Floats can just use abs.
    //            return abs(a - b);
    //        }

    //        if (t.is_int()) {
    //            // The argument may be signed, but the return type is unsigned.
    //            t.code = Type::UInt;
    //        }

    //        return Internal::Call::make(t, Internal::Call::absd,
    //                                    {a, b},
    //                                    Internal::Call::Intrinsic);
    //    }

    //    /** Returns an expression similar to the ternary operator in C, except
    //     * that it always evaluates all arguments. If the first argument is
    //     * true, then return the second, else return the third. Typically
    //     * vectorizes cleanly, but benefits from SSE41 or newer on x86. */
    //    inline Expr select(Expr condition, Expr true_value, Expr false_value) {

    //        if (as_const_int(condition)) {
    //            // Why are you doing this? We'll preserve the select node until constant folding for you.
    //            condition = cast(Bool(), condition);
    //        }

    //        // Coerce int literals to the type of the other argument
    //        if (as_const_int(true_value)) {
    //            true_value = cast(false_value.type(), true_value);
    //        }
    //        if (as_const_int(false_value)) {
    //            false_value = cast(true_value.type(), false_value);
    //        }

    //        user_assert(condition.type().is_bool())
    //            << "The first argument to a select must be a boolean:\n"
    //            << "  " << condition << " has type " << condition.type() << "\n";

    //        user_assert(true_value.type() == false_value.type())
    //            << "The second and third arguments to a select do not have a matching type:\n"
    //            << "  " << true_value << " has type " << true_value.type() << "\n"
    //            << "  " << false_value << " has type " << false_value.type() << "\n";

    //        return Internal::Select::make(condition, true_value, false_value);
    //    }

    //    /** A multi-way variant of select similar to a switch statement in C,
    //     * which can accept multiple conditions and values in pairs. Evaluates
    //     * to the first value for which the condition is true. Returns the
    //     * final value if all conditions are false. */
    //    // @{
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      select(c2, v2, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      select(c3, v3, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      select(c4, v4, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      select(c5, v5, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      select(c6, v6, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      select(c7, v7, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr c8, Expr v8,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      c7, v7,
    //                      select(c8, v8, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr c8, Expr v8,
    //                       Expr c9, Expr v9,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      c7, v7,
    //                      c8, v8,
    //                      select(c9, v9, default_val));
    //    }
    //    inline Expr select(Expr c1, Expr v1,
    //                       Expr c2, Expr v2,
    //                       Expr c3, Expr v3,
    //                       Expr c4, Expr v4,
    //                       Expr c5, Expr v5,
    //                       Expr c6, Expr v6,
    //                       Expr c7, Expr v7,
    //                       Expr c8, Expr v8,
    //                       Expr c9, Expr v9,
    //                       Expr c10, Expr v10,
    //                       Expr default_val) {
    //        return select(c1, v1,
    //                      c2, v2,
    //                      c3, v3,
    //                      c4, v4,
    //                      c5, v5,
    //                      c6, v6,
    //                      c7, v7,
    //                      c8, v8,
    //                      c9, v9,
    //                      select(c10, v10, default_val));
    //    }
    //    // @}

    // sin, cos, tan @{
    p::def("sin", &h::sin, p::args("x"),
           "Return the sine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("asin", &h::asin, p::args("x"),
           "Return the arcsine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("cos", &h::cos, p::args("x"),
           "Return the cosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("acos", &h::acos, p::args("x"),
           "Return the arccosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("tan", &h::tan, p::args("x"),
           "Return the tangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atan", &h::atan, p::args("x"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atan", &h::atan2, p::args("x", "y"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atan2", &h::atan2, p::args("x", "y"),
           "Return the arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");
    // @}

    // sinh, cosh, tanh @{
    p::def("sinh", &h::sinh, p::args("x"),
           "Return the hyperbolic sine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("asinh", &h::asinh, p::args("x"),
           "Return the hyperbolic arcsine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");


    p::def("cosh", &h::cosh, p::args("x"),
           "Return the hyperbolic cosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("acosh", &h::acosh, p::args("x"),
           "Return the hyperbolic arccosine of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("tanh", &h::tanh, p::args("x"),
           "Return the hyperbolic tangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");

    p::def("atanh", &h::atanh, p::args("x"),
           "Return the hyperbolic arctangent of a floating-point expression. If the argument is "
           "not floating-point, it is cast to Float(32). Does not vectorize well.");
    // @}



    //    /** Return the square root of a floating-point expression. If the
    //     * argument is not floating-point, it is cast to Float(32). Typically
    //     * vectorizes cleanly. */
    //    inline Expr sqrt(Expr x) {
    //        user_assert(x.defined()) << "sqrt of undefined Expr\n";
    //        if (x.type() == Float(64)) {
    //            return Internal::Call::make(Float(64), "sqrt_f64", {x}, Internal::Call::Extern);
    //        } else {
    //            return Internal::Call::make(Float(32), "sqrt_f32", {cast<float>(x)}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Return the square root of the sum of the squares of two
    //     * floating-point expressions. If the argument is not floating-point,
    //     * it is cast to Float(32). Vectorizes cleanly. */
    //    inline Expr hypot(Expr x, Expr y) {
    //        return sqrt(x*x + y*y);
    //    }

    //    /** Return the exponential of a floating-point expression. If the
    //     * argument is not floating-point, it is cast to Float(32). For
    //     * Float(64) arguments, this calls the system exp function, and does
    //     * not vectorize well. For Float(32) arguments, this function is
    //     * vectorizable, does the right thing for extremely small or extremely
    //     * large inputs, and is accurate up to the last bit of the
    //     * mantissa. Vectorizes cleanly. */
    //    inline Expr exp(Expr x) {
    //        user_assert(x.defined()) << "exp of undefined Expr\n";
    //        if (x.type() == Float(64)) {
    //            return Internal::Call::make(Float(64), "exp_f64", {x}, Internal::Call::Extern);
    //        } else {
    //            return Internal::Call::make(Float(32), "exp_f32", {cast<float>(x)}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Return the logarithm of a floating-point expression. If the
    //     * argument is not floating-point, it is cast to Float(32). For
    //     * Float(64) arguments, this calls the system log function, and does
    //     * not vectorize well. For Float(32) arguments, this function is
    //     * vectorizable, does the right thing for inputs <= 0 (returns -inf or
    //     * nan), and is accurate up to the last bit of the
    //     * mantissa. Vectorizes cleanly. */
    //    inline Expr log(Expr x) {
    //        user_assert(x.defined()) << "log of undefined Expr\n";
    //        if (x.type() == Float(64)) {
    //            return Internal::Call::make(Float(64), "log_f64", {x}, Internal::Call::Extern);
    //        } else {
    //            return Internal::Call::make(Float(32), "log_f32", {cast<float>(x)}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Return one floating point expression raised to the power of
    //     * another. The type of the result is given by the type of the first
    //     * argument. If the first argument is not a floating-point type, it is
    //     * cast to Float(32). For Float(32), cleanly vectorizable, and
    //     * accurate up to the last few bits of the mantissa. Gets worse when
    //     * approaching overflow. Vectorizes cleanly. */
    //    inline Expr pow(Expr x, Expr y) {
    //        user_assert(x.defined() && y.defined()) << "pow of undefined Expr\n";

    //        if (const int *i = as_const_int(y)) {
    //            return raise_to_integer_power(x, *i);
    //        }

    //        if (x.type() == Float(64)) {
    //            y = cast<double>(y);
    //            return Internal::Call::make(Float(64), "pow_f64", {x, y}, Internal::Call::Extern);
    //        } else {
    //            x = cast<float>(x);
    //            y = cast<float>(y);
    //            return Internal::Call::make(Float(32), "pow_f32", {x, y}, Internal::Call::Extern);
    //        }
    //    }

    //    /** Evaluate the error function erf. Only available for
    //     * Float(32). Accurate up to the last three bits of the
    //     * mantissa. Vectorizes cleanly. */
    //    inline Expr erf(Expr x) {
    //        user_assert(x.defined()) << "erf of undefined Expr\n";
    //        user_assert(x.type() == Float(32)) << "erf only takes float arguments\n";
    //        return Internal::halide_erf(x);
    //    }

    //    /** Fast approximate cleanly vectorizable log for Float(32). Returns
    //     * nonsense for x <= 0.0f. Accurate up to the last 5 bits of the
    //     * mantissa. Vectorizes cleanly. */
    //    EXPORT Expr fast_log(Expr x);

    //    /** Fast approximate cleanly vectorizable exp for Float(32). Returns
    //     * nonsense for inputs that would overflow or underflow. Typically
    //     * accurate up to the last 5 bits of the mantissa. Gets worse when
    //     * approaching overflow. Vectorizes cleanly. */
    //    EXPORT Expr fast_exp(Expr x);

    //    /** Fast approximate cleanly vectorizable pow for Float(32). Returns
    //     * nonsense for x < 0.0f. Accurate up to the last 5 bits of the
    //     * mantissa for typical exponents. Gets worse when approaching
    //     * overflow. Vectorizes cleanly. */
    //    inline Expr fast_pow(Expr x, Expr y) {
    //        if (const int *i = as_const_int(y)) {
    //            return raise_to_integer_power(x, *i);
    //        }

    //        x = cast<float>(x);
    //        y = cast<float>(y);
    //        return select(x == 0.0f, 0.0f, fast_exp(fast_log(x) * y));
    //    }

    //    /** Fast approximate inverse for Float(32). Corresponds to the rcpps
    //     * instruction on x86, and the vrecpe instruction on ARM. Vectorizes
    //     * cleanly. */
    //    inline Expr fast_inverse(Expr x) {
    //        user_assert(x.type() == Float(32)) << "fast_inverse only takes float arguments\n";
    //        return Internal::Call::make(x.type(), "fast_inverse_f32", {x}, Internal::Call::Extern);
    //    }

    //    /** Fast approximate inverse square root for Float(32). Corresponds to
    //     * the rsqrtps instruction on x86, and the vrsqrte instruction on
    //     * ARM. Vectorizes cleanly. */
    //    inline Expr fast_inverse_sqrt(Expr x) {
    //        user_assert(x.type() == Float(32)) << "fast_inverse_sqrt only takes float arguments\n";
    //        return Internal::Call::make(x.type(), "fast_inverse_sqrt_f32", {x}, Internal::Call::Extern);
    //    }



    p::def("floor", &h::floor, p::args("x"),
           "Return the greatest whole number less than or equal to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("ceil", &h::ceil, p::args("x"),
           "Return the least whole number less than or equal to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("round", &h::round, p::args("x"),
           "Return the whole number closest to a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("trunc", &h::trunc, p::args("x"),
           "Return the integer part of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("fract", &h::fract, p::args("x"),
           "Return the fractional part of a floating-point expression. "
           "If the argument is not floating-point, it is cast to Float(32). "
           "The return value is still in floating point, despite being a whole number. "
           "Vectorizes cleanly");

    p::def("is_nan", &h::is_nan, p::args("x"),
           "Returns true if the argument is a Not a Number (NaN). "
           "Requires a floating point argument.  Vectorizes cleanly.");


    p::def("reinterpret", &reinterpret0, p::args("t, e"),
           "Reinterpret the bits of one value as another type.");

    return;
}


BOOST_PYTHON_MODULE(halide)
{
    using namespace boost::python;
    def("greet", greet);

    defineVar();
    defineExpr();
    defineFunc();
    defineTypes();
    defineParam();
    defineOperators();
}
