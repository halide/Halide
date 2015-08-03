#include "Func.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include <boost/python.hpp>
#include "add_operators.h"

#include "../../src/Func.h"

#include <boost/format.hpp>

#include "Func_Ref.h"
#include "Func_Stage.h"
#include "Func_VarOrRVar.h"
#include "Func_gpu.h"

#include <vector>
#include <string>

namespace h = Halide;
namespace p = boost::python;


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


void func_realize2(h::Func &that, h::Realization dst, const h::Target &target = h::Target())
{
    that.realize(dst, target);
    return;
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_realize2_overloads, func_realize2, 2, 3)


void func_realize3(h::Func &that, h::Buffer dst, const h::Target &target = h::Target())
{
    that.realize(dst, target);
    return;
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_realize3_overloads, func_realize3, 2, 3)


void func_compile_jit0(h::Func &that)
{
    that.compile_jit();
    return;
}

void func_compile_jit1(h::Func &that, const h::Target &target = h::get_target_from_environment())
{
    that.compile_jit(target);
    return;
}


void func_compile_to_bitcode0(h::Func &that, const std::string &filename,
                              const std::vector<h::Argument> &args,
                              const std::string fn_name = "",
                              const h::Target &target = h::get_target_from_environment())
{
    that.compile_to_bitcode(filename, args, fn_name, target);
    return;
}


BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_bitcode0_overloads, func_compile_to_bitcode0, 3, 5)


void func_compile_to_c0(h::Func &that, const std::string &filename,
                        const std::vector<h::Argument> &args,
                        const std::string fn_name = "",
                        const h::Target &target = h::get_target_from_environment())
{
    that.compile_to_c(filename, args, fn_name, target);
    return;
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_c0_overloads, func_compile_to_c0, 3, 5)


void func_compile_to_file0(h::Func &that, const std::string &filename_prefix,
                           const std::vector<h::Argument> &args,
                           const h::Target &target = h::get_target_from_environment())
{
    that.compile_to_file(filename_prefix, args, target);
    return;
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_file0_overloads, func_compile_to_file0, 3, 4)


void func_compile_to_lowered_stmt0(h::Func &that,
                                   const std::string &filename,
                                   const std::vector<h::Argument> &args,
                                   h::StmtOutputFormat fmt = h::Text,
                                   const h::Target &target = h::get_target_from_environment())
{
    that.compile_to_lowered_stmt(filename, args, fmt, target);
    return;
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_lowered_stmt0_overloads, func_compile_to_lowered_stmt0, 3, 5)

// parallel, vectorize, unroll, tile, and reorder methods are shared with Stage class
// and thus defined as template functions in the header

h::Func &func_store_at0(h::Func &that, h::Func f, h::Var var)
{
    return that.store_at(f, var);
}

h::Func &func_store_at1(h::Func &that, h::Func f, h::RVar var)
{
    return that.store_at(f, var);
}

h::Func &func_compute_at0(h::Func &that, h::Func f, h::Var var)
{
    return that.compute_at(f, var);
}

h::Func &func_compute_at1(h::Func &that, h::Func f, h::RVar var)
{
    return that.compute_at(f, var);
}



void tuple_to_var_expr_vector(
        const std::string debug_name,
        const p::tuple &args_passed,
        std::vector<h::Var> &var_args,
        std::vector<h::Expr> &expr_args)
{
    const size_t args_len = p::len(args_passed);
    for(size_t i=0; i < args_len; i+=1)
    {
        p::object o = args_passed[i];
        p::extract<h::Var> var_extract(o);
        p::extract<h::Expr> expr_extract(o);
        p::extract<boost::int32_t> int32_extract(o);

        const bool is_var = var_extract.check();
        bool expr_added = false;
        if(is_var)
        {
            h::Var v = var_extract();
            var_args.push_back(v);

            expr_args.push_back(v);
            expr_added = true;
        }

        if(not expr_added)
        {
            if(expr_extract.check())
            {
                h::Expr e(expr_extract());
                expr_args.push_back(e);
                expr_added = true;
            }
            else if(not is_var and int32_extract.check())
            { // not var, not expr, but int32
                expr_args.push_back(h::Expr(int32_extract()));
                expr_added = true;
            }
        }


        if(expr_added == false and (args_len > 0))
        {
            for(size_t j=0; j < args_len; j+=1)
            {
                p::object o = args_passed[j];
                const std::string o_str = p::extract<std::string>(p::str(o));
                printf("%s args_passed[%lu] == %s\n", debug_name.c_str(), j, o_str.c_str());
            }
            throw std::invalid_argument(
                        boost::str(boost::format("%s::operator[] only handles "
                                                 "a tuple of Var or a tuple of (convertible to) Expr.") % debug_name));
        }
    }

    return;
}

p::object func_getitem_operator0(h::Func &that, p::tuple args_passed)
{
    std::vector<h::Var> var_args;
    std::vector<h::Expr> expr_args;
    const size_t args_len = p::len(args_passed);

    tuple_to_var_expr_vector("Func", args_passed, var_args, expr_args);

    p::object return_object;

    // We prioritize Args over Expr variant
    if(var_args.size() == args_len)
    {
        h::FuncRefVar ret = that(var_args);

        p::copy_non_const_reference::apply<h::FuncRefVar &>::type converter;
        PyObject* obj = converter( ret );
        return_object = p::object( p::handle<>( obj ) );
    }
    else
    {   user_assert(expr_args.size() == args_len)
                << "Not all func_getitem_operator0 arguments where converted to Expr "
                << "( expr_args.size() " << expr_args.size() << "!= args_len " << args_len << ")";
        h::FuncRefExpr ret = that(expr_args);

        p::copy_non_const_reference::apply<h::FuncRefExpr &>::type converter;
        PyObject* obj = converter( ret );
        return_object = p::object( p::handle<>( obj ) );
    }


    if(false)
    {
        printf("func_getitem_operator0 returns %s\n",
               static_cast<std::string>(p::extract<std::string>(p::str(return_object))).c_str());
    }
    return return_object;
}


p::object func_getitem_operator1(h::Func &that, p::object arg_passed)
{
    p::tuple args_passed;
    p::extract<p::tuple> tuple_extract(arg_passed);
    if(tuple_extract.check())
    {
        args_passed = tuple_extract();
    }
    else if(arg_passed.is_none())
    {
        // args_passed tuple is left empty
    }
    else
    {
        args_passed = p::make_tuple(arg_passed);
    }

    return func_getitem_operator0(that, args_passed);
}



template <typename T>
h::Stage func_setitem_operator0(h::Func &that, p::tuple args_passed, T right_hand)
{
    std::vector<h::Var> var_args;
    std::vector<h::Expr> expr_args;
    const size_t args_len = p::len(args_passed);

    tuple_to_var_expr_vector("Func", args_passed, var_args, expr_args);

    // We prioritize Args
    if(var_args.size() == args_len)
    {
        h::FuncRefVar ret = that(var_args);
        h::Stage s = (ret = right_hand);
        return s;
    }
    else
    {   user_assert(expr_args.size() == args_len) << "Not all func_setitem_operator0 arguments where converted to Expr";

        h::FuncRefExpr ret = that(expr_args);
        h::Stage s = (ret = right_hand);
        return s;
    }
}

template <typename T>
h::Stage func_setitem_operator1(h::Func &that, p::object arg_passed, T right_hand)
{
    p::tuple args_passed;
    p::extract<p::tuple> tuple_extract(arg_passed);
    if(tuple_extract.check())
    {
        args_passed = tuple_extract();
    }
    else if(arg_passed.is_none())
    {
        // args_passed tuple is left empty
    }
    else
    {
        args_passed = p::make_tuple(arg_passed);
    }

    return func_setitem_operator0(that, args_passed, right_hand);
}


std::string func_repr(const h::Func &func)
{
    std::string repr;
    boost::format f("<halide.Func '%s'>");
    repr = boost::str(f % func.name());
    return repr;
}


void func_define_extern0(h::Func &that,const std::string &function_name,
                         const std::vector<h::ExternFuncArgument> &params,
                         h::Type output_type,
                         int dimensionality) {
    return that.define_extern(function_name, params, output_type, dimensionality);
}

void func_define_extern1(h::Func &that,const std::string &function_name,
                         const std::vector<h::ExternFuncArgument> &params,
                         const std::vector<h::Type> &output_types,
                         int dimensionality) {
    return that.define_extern(function_name, params, output_types, dimensionality);
}


void defineFunc()
{
    using Halide::Func;
    using namespace func_and_stage_implementation_details;

    p::enum_<h::StmtOutputFormat>("StmtOutputFormat")
            .value("Text", h::StmtOutputFormat::Text)
            .value("HTML", h::StmtOutputFormat::HTML)
            .export_values()
            ;

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
                                      p::init<>(p::arg("self")))
            .def(p::init<std::string>(p::arg("self")))
            .def(p::init<h::Expr>(p::arg("self")));
    //.def("set", &Func::set, "Typically one uses f[x, y] = expr to assign to a function. However f.set(expr) can be used also.")

    func_class.def("allow_race_conditions", &Func::allow_race_conditions,
                   p::return_internal_reference<1>(),
                   "Specify that race conditions are permitted for this Func, "
                   "which enables parallelizing over RVars even when Halide cannot "
                   "prove that it is safe to do so. Use this with great caution, "
                   "and only if you can prove to yourself that this is safe, as it "
                   "may result in a non-deterministic routine that returns "
                   "different values at different times or on different machines.");

    func_class.def("realize", &func_realize1,
                   func_realize1_overloads(
                       p::args("self", "x_size", "y_size", "z_size", "w_size", "target"),
                       "Evaluate this function over some rectangular domain and return"
                       "the resulting buffer. The buffer should probably be instantly"
                       "wrapped in an Image class.\n\n" \
                       "One can use f.realize(Buffer) to realize into an existing buffer."))
            .def("realize", &func_realize0, func_realize0_overloads(
                     p::args("self", "sizes", "target")))
            .def("realize", &func_realize3, func_realize3_overloads(
                     p::args("self", "dst", "target"),
                     "Evaluate this function into an existing allocated buffer or "
                     "buffers. If the buffer is also one of the arguments to the "
                     "function, strange things may happen, as the pipeline isn't "
                     "necessarily safe to run in-place. If you pass multiple buffers, "
                     "they must have matching sizes."))
            .def("realize", &func_realize2, func_realize2_overloads(
                     p::args("self", "dst", "target")));


    func_class.def("compile_to_bitcode", &func_compile_to_bitcode0,
                   func_compile_to_bitcode0_overloads(
                       p::args("self", "filename", "args", "fn_name", "target"),
                       "Statically compile this function to llvm bitcode, with the "
                       "given filename (which should probably end in .bc), type "
                       "signature, and C function name (which defaults to the same name "
                       "as this halide function."));

    func_class.def("compile_to_c", &func_compile_to_c0,
                   func_compile_to_c0_overloads(
                       p::args("self", "filename", "args", "fn_name", "target"),
                       "Statically compile this function to C source code. This is "
                       "useful for providing fallback code paths that will compile on "
                       "many platforms. Vectorization will fail, and parallelization "
                       "will produce serial code."));

    func_class.def("compile_to_file",
                   &func_compile_to_file0,
                   func_compile_to_file0_overloads(
                       p::args("self", "filename_prefix", "args", "target"),
                       "Compile to object file and header pair, with the given arguments. "
                       "Also names the C function to match the first argument."));

    func_class.def("compile_jit", &func_compile_jit1, p::args("self", "target"),
                   "Eagerly jit compile the function to machine code. This "
                   "normally happens on the first call to realize. If you're "
                   "running your halide pipeline inside time-sensitive code and "
                   "wish to avoid including the time taken to compile a pipeline, "
                   "then you can call this ahead of time. Returns the raw function "
                   "pointer to the compiled pipeline.")
            .def("compile_jit", &func_compile_jit0, p::arg("self"));

    func_class.def("debug_to_file", &Func::debug_to_file, p::args("self", "filename"),
                   "When this function is compiled, include code that dumps its values "
                   "to a file after it is realized, for the purpose of debugging. "
                   "The file covers the realized extent at the point in the schedule that "
                   "debug_to_file appears.\n"
                   "If filename ends in \".tif\" or \".tiff\" (case insensitive) the file "
                   "is in TIFF format and can be read by standard tools.");

    func_class.def("compile_to_lowered_stmt", &func_compile_to_lowered_stmt0,
                   func_compile_to_lowered_stmt0_overloads(
                       p::args("self", "filename", "args", "fmt", "target"),
                       "Write out an internal representation of lowered code. Useful "
                       "for analyzing and debugging scheduling. Can emit html or plain text."));

    func_class.def("print_loop_nest", &Func::print_loop_nest, p::arg("self"),
                   "Write out the loop nests specified by the schedule for this "
                   "Function. Helpful for understanding what a schedule is doing.");


    func_class.def("name", &Func::name, p::arg("self"),
                   p::return_value_policy<p::copy_const_reference>(),
                   "The name of this function, either given during construction, or automatically generated.");

    func_class.def("args", &Func::args, p::arg("self"),
                   "Get the pure arguments.");

    func_class.def("value", &Func::value, p::arg("self"),
                   "The right-hand-side value of the pure definition of this "
                   "function. Causes an error if there's no pure definition, or if "
                   "the function is defined to return multiple values.");

    func_class.def("values", &Func::values, p::arg("self"),
                   "The values returned by this function. An error if the function "
                   "has not been been defined. Returns a Tuple with one element for "
                   "functions defined to return a single value.");

    func_class.def("defined", &Func::defined, p::arg("self"),
                   "Does this function have at least a pure definition.");

    func_class.def("update_args", &Func::update_args, (p::arg("self"), p::arg("idx") = 0),
                   p::return_value_policy<p::copy_const_reference>(),
                   "Get the left-hand-side of the update definition. An empty "
                   "vector if there's no update definition. If there are "
                   "multiple update definitions for this function, use the "
                   "argument to select which one you want.");

    func_class.def("update_value", &Func::update_value, (p::arg("self"), p::arg("idx") = 0),
                   "Get the right-hand-side of an update definition. An error if "
                   "there's no update definition. If there are multiple "
                   "update definitions for this function, use the argument to "
                   "select which one you want.");

    func_class.def("update_values", &Func::update_values, (p::arg("self"), p::arg("idx") = 0),
                   "Get the right-hand-side of an update definition for "
                   "functions that returns multiple values. An error if there's no "
                   "update definition. Returns a Tuple with one element for "
                   "functions that return a single value.");

    func_class.def("reduction_domain", &Func::reduction_domain, (p::arg("self"), p::arg("idx") = 0),
                   "Get the reduction domain for an update definition, if there is one.");

    func_class
            .def("has_update_definition", &Func::has_update_definition, p::arg("self"),
                 "Does this function have at least one update definition?")
            .def("num_update_definitions", &Func::num_update_definitions, p::arg("self"),
                 "How many update definitions does this function have?");

    func_class.def("is_extern", &Func::is_extern, p::arg("self"),
                   "Is this function an external stage? That is, was it defined "
                   "using define_extern?");

    func_class.def("define_extern", &func_define_extern0,
                   p::args("self", "function_name", "params", "output_type", "dimensionality"),
                   "Add an extern definition for this Func. This lets you define a "
                   "Func that represents an external pipeline stage. You can, for "
                   "example, use it to wrap a call to an extern library such as "
                   "fftw.")
            .def("define_extern", &func_define_extern1,
                 p::args("self", "function_name", "params", "output_types", "dimensionality"));

    func_class.def("output_types", &Func::output_types, p::arg("self"),
                   p::return_value_policy<p::copy_const_reference>(),
                   "Get the types of the outputs of this Func.");

    func_class.def("outputs", &Func::outputs, p::arg("self"),
                   "Get the number of outputs of this Func. Corresponds to the "
                   "size of the Tuple this Func was defined to return.");

    func_class.def("extern_function_name", &Func::extern_function_name, p::arg("self"),
                   p::return_value_policy<p::copy_const_reference>(),
                   "Get the name of the extern function called for an extern definition.");

    func_class.def("dimensions", &Func::dimensions, p::arg("self"),
                   "The dimensionality (number of arguments) of this function. Zero if the function is not yet defined.");


    func_class.def("__getitem__", &func_getitem_operator0,
                   "If received a tuple of Vars\n\n"
                   "Construct either the left-hand-side of a definition, or a call "
                   "to a functions that happens to only contain vars as "
                   "arguments. If the function has already been defined, and fewer "
                   "arguments are given than the function has dimensions, then "
                   "enough implicit vars are added to the end of the argument list "
                   "to make up the difference (see \\ref Var::implicit)\n\n"
                   "If received a tuple of Expr\n\n"
                   "Either calls to the function, or the left-hand-side of a "
                   "update definition (see \\ref RDom). If the function has "
                   "already been defined, and fewer arguments are given than the "
                   "function has dimensions, then enough implicit vars are added to "
                   "the end of the argument list to make up the difference. (see \\ref Var::implicit)")
            .def("__getitem__", &func_getitem_operator1); // handles the case where a single index object is given

    func_class
            .def("__setitem__", &func_setitem_operator0<h::FuncRefVar>)
            .def("__setitem__", &func_setitem_operator0<h::FuncRefExpr>)
            .def("__setitem__", &func_setitem_operator0<h::Expr>)
            .def("__setitem__", &func_setitem_operator0<h::Tuple>)
            .def("__setitem__", &func_setitem_operator1<h::FuncRefVar>) // handles the case where a single index object is given
            .def("__setitem__", &func_setitem_operator1<h::FuncRefExpr>)
            .def("__setitem__", &func_setitem_operator1<h::Expr>)
            .def("__setitem__", &func_setitem_operator1<h::Tuple>);

    /*
    &Func::__getitem__(self, *args):
    """
    Either calls to the function, or the left-hand-side of a
    reduction definition (see \\ref RDom). If the function has
    already been defined, and fewer arguments are given than the
    function has dimensions, then enough implicit vars are added to
    the end of the argument list to make up the difference.
    """
*/

    // FIXME should share these definitions with Stage instead of having copy and paste code

    func_class.def("split", &Func::split, p::args("self", "old", "outer", "inner", "factor"),
                   p::return_internal_reference<1>(),
                   "Split a dimension into inner and outer subdimensions with the "
                   "given names, where the inner dimension iterates from 0 to "
                   "factor-1. The inner and outer subdimensions can then be dealt "
                   "with using the other scheduling calls. It's ok to reuse the old "
                   "variable name as either the inner or outer variable.")
            .def("fuse", &Func::fuse,  p::args("self", "inner", "outer",  "fused"),
                 p::return_internal_reference<1>(),
                 "Join two dimensions into a single fused dimenion. The fused "
                 "dimension covers the product of the extents of the inner and "
                 "outer dimensions given.")
            .def("serial", &Func::serial, p::args("self","var"),
                 p::return_internal_reference<1>(),
                 "Mark a dimension to be traversed serially. This is the default.");

    func_class.def("parallel", &func_parallel0<Func>, p::args("self", "var"),
                   p::return_internal_reference<1>(),
                   "Mark a dimension (Var instance) to be traversed in parallel.")
            .def("parallel", &func_parallel1<Func>, p::args("self", "var", "factor"),
                 p::return_internal_reference<1>());

    func_class.def("vectorize", &func_vectorize1<Func>, p::args("self", "var", "factor"),
                   p::return_internal_reference<1>(),
                   "Split a dimension (Var instance) by the given int factor, then vectorize the "
                   "inner dimension. This is how you vectorize a loop of unknown "
                   "size. The variable to be vectorized should be the innermost "
                   "one. After this call, var refers to the outer dimension of the "
                   "split.")
            .def("vectorize", &func_vectorize0<Func>, p::args("self", "var"),
                 p::return_internal_reference<1>());

    func_class.def("unroll", &func_unroll1<Func>, p::args("self", "var", "factor"),
                   p::return_internal_reference<1>(),
                   "Split a dimension by the given factor, then unroll the inner "
                   "dimension. This is how you unroll a loop of unknown size by "
                   "some constant factor. After this call, var refers to the outer "
                   "dimension of the split.")
            .def("unroll", &func_unroll0<Func>, p::args("self", "var"),
                 p::return_internal_reference<1>());

    func_class.def("bound", &Func::bound,  p::args("self", "var", "min", "extent"),
                   p::return_internal_reference<1>(),
                   "Statically declare that the range over which a function should "
                   "be evaluated is given by the second and third arguments. This "
                   "can let Halide perform some optimizations. E.g. if you know "
                   "there are going to be 4 color channels, you can completely "
                   "vectorize the color channel dimension without the overhead of "
                   "splitting it up. If bounds inference decides that it requires "
                   "more of this function than the bounds you have stated, a "
                   "runtime error will occur when you try to run your pipeline. ");

    func_class.def("tile", &func_tile0<Func>,  p::args("self", "x", "y", "xo", "yo", "xi", "yi", "xfactor", "yfactor"),
                   p::return_internal_reference<1>(),
                   "Split two dimensions at once by the given factors, and then "
                   "reorder the resulting dimensions to be xi, yi, xo, yo from "
                   "innermost outwards. This gives a tiled traversal.")
            .def("tile", &func_tile1<Func>,  p::args("self", "x", "y", "xi", "yi", "xfactor", "yfactor"),
                 p::return_internal_reference<1>(),
                 "A shorter form of tile, which reuses the old variable names as the new outer dimensions");


    func_class.def("reorder", &func_reorder0<Func, p::tuple>, p::args("self", "vars"),
                   p::return_internal_reference<1>(),
                   "Reorder variables to have the given nesting order, "
                   "from innermost out")
            .def("reorder", &func_reorder0<Func, p::list>, p::args("self", "vars"),
                 p::return_internal_reference<1>(),
                 "Reorder variables to have the given nesting order, "
                 "from innermost out")
            .def("reorder", &func_reorder1<Func>, (p::arg("self"), p::arg("v0"), p::arg("v1")=p::object(),
                                                   p::arg("v2")=p::object(), p::arg("v3")=p::object(),
                                                   p::arg("v4")=p::object(), p::arg("v5")=p::object()),
                 p::return_internal_reference<1>(),
                 "Reorder variables to have the given nesting order, "
                 "from innermost out");

    func_class.def("rename", &Func::rename, p::args("self", "old_name", "new_name"),
                   p::return_internal_reference<1>(),
                   "Rename a dimension. Equivalent to split with a inner size of one.");

    const std::string reorder_storage_doc = \
            "Specify how the storage for the function is laid out. These "
            "calls let you specify the nesting order of the dimensions. For "
            "example, foo.reorder_storage(y, x) tells Halide to use "
            "column-major storage for any realizations of foo, without "
            "changing how you refer to foo in the code. You may want to do "
            "this if you intend to vectorize across y. When representing "
            "color images, foo.reorder_storage(c, x, y) specifies packed "
            "storage (red, green, and blue values adjacent in memory), and "
            "foo.reorder_storage(x, y, c) specifies planar storage (entire "
            "red, green, and blue images one after the other in memory).\n\n"
            "If you leave out some dimensions, those remain in the same "
            "positions in the nesting order while the specified variables "
            "are reordered around them.";
    func_class.def("reorder_storage", &func_reorder_storage0<Func, p::tuple>, p::args("self", "dims"),
                   p::return_internal_reference<1>(), reorder_storage_doc.c_str())
            .def("reorder_storage", &func_reorder_storage0<Func, p::list>, p::args("self", "dims"),
                 p::return_internal_reference<1>(), reorder_storage_doc.c_str())
            .def("reorder_storage", &func_reorder_storage1<Func>, (p::arg("self"), p::arg("v0"), p::arg("v1"),
                                                                   p::arg("v2")=p::object(), p::arg("v3")=p::object(),
                                                                   p::arg("v4")=p::object(), p::arg("v5")=p::object()),
                 p::return_internal_reference<1>(), reorder_storage_doc.c_str());


    func_class.def("compute_at", &func_compute_at0, p::args("self", "f", "var"),
                   p::return_internal_reference<1>(),
                   "Compute this function as needed for each unique value of the "
                   "given var (can be a Var or an RVar) for the given calling function f.")
            .def("compute_at", &func_compute_at1, p::args("self", "f", "var"),
                 p::return_internal_reference<1>());

    func_class.def("compute_root", &Func::compute_root, p::arg("self"),
                   p::return_internal_reference<1>(),
                   "Compute all of this function once ahead of time.");


    func_class.def("store_at", &func_store_at0, p::args("self", "f", "var"),
                   p::return_internal_reference<1>(),
                   "Allocate storage for this function within f's loop over "
                   "var (can be a Var or an RVar). Scheduling storage is optional, and can be used to "
                   "separate the loop level at which storage occurs from the loop "
                   "level at which computation occurs to trade off between locality "
                   "and redundant work.")
            .def("store_at", &func_store_at1, p::args("self", "f", "var"),
                 p::return_internal_reference<1>());

    func_class.def("store_root", &Func::store_root, p::arg("self"),
                   p::return_internal_reference<1>(),
                   "Equivalent to Func.store_at, but schedules storage outside the outermost loop.");

    func_class.def("compute_inline", &Func::compute_inline, p::arg("self"),
                   p::return_internal_reference<1>(),
                   "Aggressively inline all uses of this function. This is the "
                   "default schedule, so you're unlikely to need to call this. For "
                   "a reduction, that means it gets computed as close to the "
                   "innermost loop as possible.");

    func_class.def("update", &Func::update, (p::arg("self"), p::arg("idx")=0),
                   "Get a handle on the update step of a reduction for the "
                   "purposes of scheduling it. Only the pure dimensions of the "
                   "update step can be meaningfully manipulated (see RDom).");

    func_class.def("function", &Func::function, p::arg("self"),
                   "Get a handle on the internal halide function that this Func represents. "
                   "Useful if you want to do introspection on Halide functions.")
            .def("trace_loads", &Func::trace_loads, p::arg("self"),
                 p::return_internal_reference<1>(),
                 "Trace all loads from this Func by emitting calls to "
                 "halide_trace. If the Func is inlined, this has no effect.")
            .def("trace_stores", &Func::trace_stores, p::arg("self"),
                 p::return_internal_reference<1>(),
                 "Trace all stores to the buffer backing this Func by emitting "
                 "calls to halide_trace. If the Func is inlined, this call has no effect.")
            .def("trace_realizations", &Func::trace_realizations, p::arg("self"),
                 p::return_internal_reference<1>(),
                 "Trace all realizations of this Func by emitting calls to halide_trace.");

    func_class.def("specialize", &Func::specialize, p::args("self", "condition"),
                   "Specialize a Func. This creates a special-case version of the "
                   "Func where the given condition is true. The most effective "
                   "conditions are those of the form param == value, and boolean "
                   "Params. See C++ documentation for more details.");

    func_class.def("__repr__", &func_repr, p::arg("self"));


    defineFuncGpuMethods(func_class);

    p::implicitly_convertible<Func, h::Expr>();

    defineStage();
    defineVarOrRVar();
    defineFuncRef();

    return;
}
