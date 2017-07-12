#include "Func.h"

// to avoid compiler confusion, python.hpp must be include before Halide headers
#include "add_operators.h"
#include <boost/python.hpp>

#include "Halide.h"
#include "Image.h"

#include <boost/format.hpp>

#include "Expr.h"
#include "Func_Ref.h"
#include "Func_Stage.h"
#include "Func_VarOrRVar.h"
#include "Func_gpu.h"

#include <string>
#include <vector>

namespace h = Halide;
namespace p = boost::python;

p::object realization_to_python_object(const h::Realization &r) {
    if (r.size() == 1) {
        return buffer_to_python_object(r[0]);
    } else {
        p::list elts;
        for (size_t i = 0; i < r.size(); i++) {
            elts.append(buffer_to_python_object(r[i]));
        }
        return p::tuple(elts);
    }
}

h::Realization python_object_to_realization(p::object obj) {
    std::vector<h::Buffer<>> buffers;
    p::extract<p::tuple> tuple_extract(obj);
    if (tuple_extract.check()) {
        p::tuple tup = tuple_extract();
        for (ssize_t i = 0; i < p::len(tup); i++) {
            buffers.push_back(python_object_to_buffer(tup[i]));
        }
    } else {
        buffers.push_back(python_object_to_buffer(obj));
    }
    return h::Realization(buffers);
}

template <typename... Args>
p::object func_realize(h::Func &f, Args... args) {
    return realization_to_python_object(f.realize(args...));
}

template <typename... Args>
void func_realize_into(h::Func &f, Args... args) {
    f.realize(args...);
}

template <typename... Args>
void func_realize_tuple(h::Func &f, p::tuple obj, Args... args) {
    f.realize(python_object_to_realization(obj), args...);
}

void func_compile_jit0(h::Func &that) {
    that.compile_jit();
    return;
}

void func_compile_jit1(h::Func &that, const h::Target &target = h::get_target_from_environment()) {
    that.compile_jit(target);
    return;
}

void func_compile_to_bitcode0(h::Func &that, const std::string &filename,
                              p::list args,
                              const std::string fn_name = "",
                              const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_bitcode(filename, args_vec, fn_name, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_bitcode0_overloads, func_compile_to_bitcode0, 3, 5)

void func_compile_to_object0(h::Func &that, const std::string &filename,
                             p::list args,
                             const std::string fn_name = "",
                             const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_object(filename, args_vec, fn_name, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_object0_overloads, func_compile_to_object0, 3, 5)

void func_compile_to_header0(h::Func &that, const std::string &filename,
                             p::list args,
                             const std::string fn_name = "",
                             const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_header(filename, args_vec, fn_name, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_header0_overloads, func_compile_to_header0, 3, 5)

void func_compile_to_assembly0(h::Func &that, const std::string &filename,
                               p::list args,
                               const std::string fn_name = "",
                               const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_assembly(filename, args_vec, fn_name, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_assembly0_overloads, func_compile_to_assembly0, 3, 5)

void func_compile_to_c0(h::Func &that, const std::string &filename,
                        p::list args,
                        const std::string fn_name = "",
                        const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_c(filename, args_vec, fn_name, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_c0_overloads, func_compile_to_c0, 3, 5)

void func_compile_to_file0(h::Func &that, const std::string &filename_prefix,
                           p::list args,
                           const std::string fn_name = "",
                           const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_file(filename_prefix, args_vec, fn_name, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_file0_overloads, func_compile_to_file0, 3, 5)

void func_compile_to_lowered_stmt0(h::Func &that,
                                   const std::string &filename,
                                   p::list args,
                                   h::StmtOutputFormat fmt = h::Text,
                                   const h::Target &target = h::get_target_from_environment()) {
    auto args_vec = python_collection_to_vector<h::Argument>(args);
    that.compile_to_lowered_stmt(filename, args_vec, fmt, target);
}

BOOST_PYTHON_FUNCTION_OVERLOADS(func_compile_to_lowered_stmt0_overloads, func_compile_to_lowered_stmt0, 3, 5)

// parallel, vectorize, unroll, tile, and reorder methods are shared with Stage class
// and thus defined as template functions in the header

h::Func &func_store_at0(h::Func &that, h::Func f, h::Var var) {
    return that.store_at(f, var);
}

h::Func &func_store_at1(h::Func &that, h::Func f, h::RVar var) {
    return that.store_at(f, var);
}

h::Func &func_compute_at0(h::Func &that, h::Func f, h::Var var) {
    return that.compute_at(f, var);
}

h::Func &func_compute_at1(h::Func &that, h::Func f, h::RVar var) {
    return that.compute_at(f, var);
}

h::FuncRef func_getitem_operator(h::Func &func, p::object arg) {
    return func(python_tuple_to_expr_vector(arg));
}

h::Stage func_setitem_operator(h::Func &func, p::object lhs, p::object rhs) {
    return (func(python_tuple_to_expr_vector(lhs)) =
                h::Tuple(python_tuple_to_expr_vector(rhs)));
}

std::string func_repr(const h::Func &func) {
    std::string repr;
    boost::format f("<halide.Func '%s'>");
    repr = boost::str(f % func.name());
    return repr;
}

void func_define_extern0(h::Func &that,
                         const std::string &function_name,
                         p::list params,
                         h::Type output_type,
                         int dimensionality) {
    auto params_vec = python_collection_to_vector<h::ExternFuncArgument>(params);
    return that.define_extern(function_name, params_vec, output_type, dimensionality);
}

void func_define_extern1(h::Func &that,
                         const std::string &function_name,
                         p::list params,
                         p::list types,
                         int dimensionality) {
    auto params_vec = python_collection_to_vector<h::ExternFuncArgument>(params);
    auto types_vec = python_collection_to_vector<h::Type>(types);
    return that.define_extern(function_name, params_vec, types_vec, dimensionality);
}

p::tuple func_output_types(h::Func &func) {
    p::list elts;
    for (h::Type t : func.output_types()) {
        elts.append(t);
    }
    return p::tuple(elts);
}

void defineFunc() {

    using Halide::Func;
    using namespace func_and_stage_implementation_details;

    p::enum_<h::StmtOutputFormat>("StmtOutputFormat")
        .value("Text", h::StmtOutputFormat::Text)
        .value("HTML", h::StmtOutputFormat::HTML)
        .export_values();

    auto func_class =
        p::class_<Func>("Func",
                        "A halide function. This class represents one stage in a Halide"
                        "pipeline, and is the unit by which we schedule things. By default"
                        "they are aggressively inlined, so you are encouraged to make lots"
                        "of little functions, rather than storing things in Exprs.\n"
                        "Constructors::\n\n"
                        "  Func()      -- Declare a new undefined function with an automatically-generated unique name\n"
                        "  Func(expr)  -- Declare a new function with an automatically-generated unique\n"
                        "                 name, and define it to return the given expression (which may\n"
                        "                 not contain free variables).\n"
                        "  Func(name)  -- Declare a new undefined function with the given name",
                        p::init<>(p::arg("self")))
            .def(p::init<std::string>(p::arg("self")))
            .def(p::init<h::Expr>(p::arg("self")));

    func_class
        .def("allow_race_conditions", &Func::allow_race_conditions,
             p::return_internal_reference<1>(),
             "Specify that race conditions are permitted for this Func, "
             "which enables parallelizing over RVars even when Halide cannot "
             "prove that it is safe to do so. Use this with great caution, "
             "and only if you can prove to yourself that this is safe, as it "
             "may result in a non-deterministic routine that returns "
             "different values at different times or on different machines.");

    const char *realize_doc =
        "Evaluate this function over some rectangular domain and return"
        "the resulting buffer.";

    const char *realize_into_doc =
        "Evaluate this function into the given buffer.";

    func_class
        .def("realize", &func_realize<>,
             p::args("self"),
             realize_doc)
        .def("realize", &func_realize<h::Target>,
             p::args("self", "target"),
             realize_doc)
        .def("realize", &func_realize<int>,
             p::args("self", "x"),
             realize_doc)
        .def("realize", &func_realize<int, h::Target>,
             p::args("self", "x", "target"),
             realize_doc)
        .def("realize", &func_realize<int, int>,
             p::args("self", "x", "y"),
             realize_doc)
        .def("realize", &func_realize<int, int, h::Target>,
             p::args("self", "x", "y", "target"),
             realize_doc)
        .def("realize", &func_realize<int, int, int>,
             p::args("self", "x", "y", "z"),
             realize_doc)
        .def("realize", &func_realize<int, int, int, h::Target>,
             p::args("self", "x", "y", "z", "target"),
             realize_doc)
        .def("realize", &func_realize<int, int, int, int>,
             p::args("self", "x", "y", "z", "w"),
             realize_doc)
        .def("realize", &func_realize<int, int, int, int, h::Target>,
             p::args("self", "x", "y", "z", "w", "target"),
             realize_doc)
        .def("realize", &func_realize_into<h::Buffer<uint8_t>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<uint8_t>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<uint16_t>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<uint16_t>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<uint32_t>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<uint32_t>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<int8_t>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<int8_t>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<int16_t>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<int16_t>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<int32_t>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<int32_t>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<float>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<float>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<double>>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_into<h::Buffer<double>, h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc)
        .def("realize", &func_realize_tuple<>,
             p::args("self", "output"),
             realize_into_doc)
        .def("realize", &func_realize_tuple<h::Target>,
             p::args("self", "output", "target"),
             realize_into_doc);

    func_class.def("compile_to_bitcode", &func_compile_to_bitcode0,
                   func_compile_to_bitcode0_overloads(
                       p::args("self", "filename", "args", "fn_name", "target"),
                       "Statically compile this function to llvm bitcode, with the "
                       "given filename (which should probably end in .bc), type "
                       "signature, and C function name (which defaults to the same name "
                       "as this halide function."));

    func_class.def("compile_to_object", &func_compile_to_object0,
                   func_compile_to_object0_overloads(
                       p::args("self", "filename", "args", "fn_name", "target"),
                       "Statically compile this function to an object file, with the "
                       "given filename (which should probably end in .o or .obj), type "
                       "signature, and C function name (which defaults to the same name "
                       "as this halide function. You probably don't want to use this "
                       "directly; call compile_to_file instead."));

    func_class.def("compile_to_header", &func_compile_to_header0,
                   func_compile_to_header0_overloads(
                       p::args("self", "filename", "args", "fn_name", "target"),
                       "Emit a header file with the given filename for this "
                       "function. The header will define a function with the type "
                       "signature given by the second argument, and a name given by the "
                       "third. The name defaults to the same name as this halide "
                       "function. You don't actually have to have defined this function "
                       "yet to call this. You probably don't want to use this directly; "
                       "call compile_to_file instead. "));

    func_class.def("compile_to_assembly", &func_compile_to_assembly0,
                   func_compile_to_assembly0_overloads(
                       p::args("self", "filename", "args", "fn_name", "target"),
                       " Statically compile this function to text assembly equivalent "
                       " to the object file generated by compile_to_object. This is "
                       " useful for checking what Halide is producing without having to "
                       " disassemble anything, or if you need to feed the assembly into "
                       " some custom toolchain to produce an object file (e.g. iOS) "));

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
                       p::args("self", "filename_prefix", "args", "fn_name", "target"),
                       "Compile to object file and header pair, with the given arguments. "
                       "The name defaults to the same name as the Halide Func."));

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
                   "has not been been defined. Returns a tuple with one element for "
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

    func_class.def("rvars", &Func::rvars, (p::arg("self"), p::arg("idx") = 0),
                   "Get the reduction variables for an update definition, if there is one.");

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

    func_class.def("output_types", &func_output_types, p::arg("self"),
                   "Get the types of the outputs of this Func.");

    func_class.def("outputs", &Func::outputs, p::arg("self"),
                   "Get the number of outputs of this Func. Corresponds to the "
                   "size of the Tuple this Func was defined to return.");

    func_class.def("extern_function_name", &Func::extern_function_name, p::arg("self"),
                   p::return_value_policy<p::copy_const_reference>(),
                   "Get the name of the extern function called for an extern definition.");

    func_class.def("dimensions", &Func::dimensions, p::arg("self"),
                   "The dimensionality (number of arguments) of this function. Zero if the function is not yet defined.");

    func_class.def("__getitem__", &func_getitem_operator,
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
                   "the end of the argument list to make up the difference. (see \\ref Var::implicit)");

    func_class
        .def("__setitem__", &func_setitem_operator);

    // FIXME should share these definitions with Stage instead of having copy and paste code

    func_class.def("split", &func_split<Func>, p::args("self", "old", "outer", "inner", "factor"),
                   p::return_internal_reference<1>(),
                   "Split a dimension into inner and outer subdimensions with the "
                   "given names, where the inner dimension iterates from 0 to "
                   "factor-1. The inner and outer subdimensions can then be dealt "
                   "with using the other scheduling calls. It's ok to reuse the old "
                   "variable name as either the inner or outer variable.")
        .def("fuse", &Func::fuse, p::args("self", "inner", "outer", "fused"),
             p::return_internal_reference<1>(),
             "Join two dimensions into a single fused dimenion. The fused "
             "dimension covers the product of the extents of the inner and "
             "outer dimensions given.")
        .def("serial", &Func::serial, p::args("self", "var"),
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

    func_class.def("bound", &Func::bound, p::args("self", "var", "min", "extent"),
                   p::return_internal_reference<1>(),
                   "Statically declare that the range over which a function should "
                   "be evaluated is given by the second and third arguments. This "
                   "can let Halide perform some optimizations. E.g. if you know "
                   "there are going to be 4 color channels, you can completely "
                   "vectorize the color channel dimension without the overhead of "
                   "splitting it up. If bounds inference decides that it requires "
                   "more of this function than the bounds you have stated, a "
                   "runtime error will occur when you try to run your pipeline. ");

    func_class.def("tile", &func_tile0<Func>, p::args("self", "x", "y", "xo", "yo", "xi", "yi", "xfactor", "yfactor"),
                   p::return_internal_reference<1>(),
                   "Split two dimensions at once by the given factors, and then "
                   "reorder the resulting dimensions to be xi, yi, xo, yo from "
                   "innermost outwards. This gives a tiled traversal.");

    func_class.def("tile", &func_tile1<Func>, p::args("self", "x", "y", "xi", "yi", "xfactor", "yfactor"),
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
        .def("reorder", &func_reorder1<Func>, (p::arg("self"), p::arg("v0"), p::arg("v1") = p::object(),
                                               p::arg("v2") = p::object(), p::arg("v3") = p::object(),
                                               p::arg("v4") = p::object(), p::arg("v5") = p::object()),
             p::return_internal_reference<1>(),
             "Reorder variables to have the given nesting order, "
             "from innermost out");

    func_class.def("rename", &Func::rename, p::args("self", "old_name", "new_name"),
                   p::return_internal_reference<1>(),
                   "Rename a dimension. Equivalent to split with a inner size of one.");

    const std::string reorder_storage_doc =
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
                                                               p::arg("v2") = p::object(), p::arg("v3") = p::object(),
                                                               p::arg("v4") = p::object(), p::arg("v5") = p::object()),
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

    func_class.def("update", &Func::update, (p::arg("self"), p::arg("idx") = 0),
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

    defineStage();
    defineVarOrRVar();
    defineFuncRef();
}
