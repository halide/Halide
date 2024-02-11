#include <array>
#include <iostream>
#include <limits>

#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "Deinterleave.h"
#include "FindIntrinsics.h"
#include "IROperator.h"
#include "Lerp.h"
#include "Param.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Type.h"
#include "Util.h"
#include "Var.h"

namespace Halide {
namespace Internal {

using std::map;
using std::ostream;
using std::ostringstream;
using std::string;
using std::vector;

extern "C" unsigned char halide_internal_initmod_inlined_c[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntime_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeCuda_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeHexagonHost_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeMetal_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenCL_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeQurt_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeD3D12Compute_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeWebGPU_h[];
extern "C" unsigned char halide_c_template_CodeGen_C_prologue[];
extern "C" unsigned char halide_c_template_CodeGen_C_vectors[];

namespace {

// HALIDE_MUST_USE_RESULT defined here is intended to exactly
// duplicate the definition in HalideRuntime.h (so that either or
// both can be present, in any order).
const char *const kDefineMustUseResult = R"INLINE_CODE(#ifndef HALIDE_MUST_USE_RESULT
#ifdef __has_attribute
#if __has_attribute(nodiscard)
#define HALIDE_MUST_USE_RESULT [[nodiscard]]
#elif __has_attribute(warn_unused_result)
#define HALIDE_MUST_USE_RESULT __attribute__((warn_unused_result))
#else
#define HALIDE_MUST_USE_RESULT
#endif
#else
#define HALIDE_MUST_USE_RESULT
#endif
#endif
)INLINE_CODE";

const char *const constexpr_argument_info_docs = R"INLINE_CODE(
/**
 * This function returns a constexpr array of information about a Halide-generated
 * function's argument signature (e.g., number of arguments, type of each, etc).
 * While this is a subset of the information provided by the existing _metadata
 * function, it has the distinct advantage of allowing one to use the information
 * it at compile time (rather than runtime). This can be quite useful for producing
 * e.g. automatic call wrappers, etc.
 *
 * For instance, to compute the number of Buffers in a Function, one could do something
 * like:
 *
 *      using namespace HalideFunctionInfo;
 *
 *      template<size_t arg_count>
 *      constexpr size_t count_buffers(const std::array<ArgumentInfo, arg_count> args) {
 *          size_t buffer_count = 0;
 *          for (const auto a : args) {
 *              if (a.kind == InputBuffer || a.kind == OutputBuffer) {
 *                  buffer_count++;
 *              }
 *          }
 *          return buffer_count;
 *      }
 *
 *      constexpr size_t count = count_buffers(metadata_tester_argument_info());
 *
 * The value of `count` will be computed entirely at compile-time, with no runtime
 * impact aside from the numerical value of the constant.
 */

)INLINE_CODE";

class TypeInfoGatherer : public IRGraphVisitor {
private:
    using IRGraphVisitor::include;
    using IRGraphVisitor::visit;

    void include_type(const Type &t) {
        if (t.is_vector()) {
            if (t.is_bool()) {
                // bool vectors are always emitted as uint8 in the C++ backend
                // TODO: on some architectures, we could do better by choosing
                // a bitwidth that matches the other vectors in use; EliminateBoolVectors
                // could be used for this with a bit of work.
                vector_types_used.insert(UInt(8).with_lanes(t.lanes()));
            } else if (!t.is_handle()) {
                // Vector-handle types can be seen when processing (e.g.)
                // require() statements that are vectorized, but they
                // will all be scalarized away prior to use, so don't emit
                // them.
                vector_types_used.insert(t);
                if (t.is_int()) {
                    // If we are including an int-vector type, also include
                    // the same-width uint-vector type; there are various operations
                    // that can use uint vectors for intermediate results (e.g. lerp(),
                    // but also Mod, which can generate a call to abs() for int types,
                    // which always produces uint results for int inputs in Halide);
                    // it's easier to just err on the side of extra vectors we don't
                    // use since they are just type declarations.
                    vector_types_used.insert(t.with_code(halide_type_uint));
                }
            }
        }
    }

    void include_lerp_types(const Type &t) {
        if (t.is_vector() && t.is_int_or_uint() && (t.bits() >= 8 && t.bits() <= 32)) {
            include_type(t.widen());
        }
    }

protected:
    void include(const Expr &e) override {
        include_type(e.type());
        IRGraphVisitor::include(e);
    }

    // GCC's __builtin_shuffle takes an integer vector of
    // the size of its input vector. Make sure this type exists.
    void visit(const Shuffle *op) override {
        vector_types_used.insert(Int(32, op->vectors[0].type().lanes()));
        IRGraphVisitor::visit(op);
    }

    void visit(const For *op) override {
        for_types_used.insert(op->for_type);
        IRGraphVisitor::visit(op);
    }

    void visit(const Ramp *op) override {
        include_type(op->type.with_lanes(op->lanes));
        IRGraphVisitor::visit(op);
    }

    void visit(const Broadcast *op) override {
        include_type(op->type.with_lanes(op->lanes));
        IRGraphVisitor::visit(op);
    }

    void visit(const Cast *op) override {
        include_type(op->type);
        IRGraphVisitor::visit(op);
    }

    void visit(const Call *op) override {
        include_type(op->type);
        if (op->is_intrinsic(Call::lerp)) {
            // lower_lerp() can synthesize wider vector types.
            for (const auto &a : op->args) {
                include_lerp_types(a.type());
            }
        } else if (op->is_intrinsic()) {
            Expr lowered = lower_intrinsic(op);
            if (lowered.defined()) {
                lowered.accept(this);
                return;
            }
        }

        IRGraphVisitor::visit(op);
    }

public:
    std::set<ForType> for_types_used;
    std::set<Type> vector_types_used;
};

}  // namespace

CodeGen_C::CodeGen_C(ostream &s, const Target &t, OutputKind output_kind, const std::string &guard)
    : IRPrinter(s), id("$$ BAD ID $$"), target(t), output_kind(output_kind) {

    if (output_kind == CPlusPlusFunctionInfoHeader) {
        // If it's a header, emit an include guard.
        stream << "#ifndef HALIDE_FUNCTION_INFO_" << c_print_name(guard) << "\n"
               << "#define HALIDE_FUNCTION_INFO_" << c_print_name(guard) << "\n";
        stream << R"INLINE_CODE(
/* MACHINE GENERATED By Halide. */

#if !(__cplusplus >= 201703L || _MSVC_LANG >= 201703L)
#error "This file requires C++17 or later; please upgrade your compiler."
#endif

#include "HalideRuntime.h"

)INLINE_CODE";

        return;
    }

    if (is_header()) {
        // If it's a header, emit an include guard.
        stream << "#ifndef HALIDE_" << c_print_name(guard) << "\n"
               << "#define HALIDE_" << c_print_name(guard) << "\n"
               << "#include <stdint.h>\n"
               << "\n"
               << "// Forward declarations of the types used in the interface\n"
               << "// to the Halide pipeline.\n"
               << "//\n";
        if (target.has_feature(Target::NoRuntime)) {
            stream << "// For the definitions of these structs, include HalideRuntime.h\n";
        } else {
            stream << "// Definitions for these structs are below.\n";
        }
        stream << "\n"
               << "// Halide's representation of a multi-dimensional array.\n"
               << "// Halide::Runtime::Buffer is a more user-friendly wrapper\n"
               << "// around this. Its declaration is in HalideBuffer.h\n"
               << "struct halide_buffer_t;\n"
               << "\n"
               << "// Metadata describing the arguments to the generated function.\n"
               << "// Used to construct calls to the _argv version of the function.\n"
               << "struct halide_filter_metadata_t;\n"
               << "\n";
        // We just forward declared the following types:
        forward_declared.insert(type_of<halide_buffer_t *>().handle_type);
        forward_declared.insert(type_of<halide_filter_metadata_t *>().handle_type);
    } else if (is_extern_decl()) {
        // Extern decls to be wrapped inside other code (eg python extensions);
        // emit the forward decls with a minimum of noise. Note that we never
        // mess with legacy buffer types in this case.
        stream << "struct halide_buffer_t;\n"
               << "struct halide_filter_metadata_t;\n"
               << "\n";
        forward_declared.insert(type_of<halide_buffer_t *>().handle_type);
        forward_declared.insert(type_of<halide_filter_metadata_t *>().handle_type);
    } else {
        // Include declarations of everything generated C source might want
        stream
            << halide_c_template_CodeGen_C_prologue << "\n"
            << halide_internal_runtime_header_HalideRuntime_h << "\n"
            << halide_internal_initmod_inlined_c << "\n";
        stream << "\n";
    }

    stream << kDefineMustUseResult << "\n";

    // Throw in a default (empty) definition of HALIDE_FUNCTION_ATTRS
    // (some hosts may define this to e.g. __attribute__((warn_unused_result)))
    stream << "#ifndef HALIDE_FUNCTION_ATTRS\n";
    stream << "#define HALIDE_FUNCTION_ATTRS\n";
    stream << "#endif\n";
}

CodeGen_C::~CodeGen_C() {
    set_name_mangling_mode(NameMangling::Default);

    if (is_header()) {
        if (!target.has_feature(Target::NoRuntime)) {
            stream << "\n"
                   << "// The generated object file that goes with this header\n"
                   << "// includes a full copy of the Halide runtime so that it\n"
                   << "// can be used standalone. Declarations for the functions\n"
                   << "// in the Halide runtime are below.\n";
            if (target.os == Target::Windows) {
                stream
                    << "//\n"
                    << "// The inclusion of this runtime means that it is not legal\n"
                    << "// to link multiple Halide-generated object files together.\n"
                    << "// This problem is Windows-specific. On other platforms, we\n"
                    << "// use weak linkage.\n";
            } else {
                stream
                    << "//\n"
                    << "// The runtime is defined using weak linkage, so it is legal\n"
                    << "// to link multiple Halide-generated object files together,\n"
                    << "// or to clobber any of these functions with your own\n"
                    << "// definition.\n";
            }
            stream << "//\n"
                   << "// To generate an object file without a full copy of the\n"
                   << "// runtime, use the -no_runtime target flag. To generate a\n"
                   << "// standalone Halide runtime to use with such object files\n"
                   << "// use the -r flag with any Halide generator binary, e.g.:\n"
                   << "// $ ./my_generator -r halide_runtime -o . target=host\n"
                   << "\n"
                   << halide_internal_runtime_header_HalideRuntime_h << "\n";
            if (target.has_feature(Target::CUDA)) {
                stream << halide_internal_runtime_header_HalideRuntimeCuda_h << "\n";
            }
            if (target.has_feature(Target::HVX)) {
                stream << halide_internal_runtime_header_HalideRuntimeHexagonHost_h << "\n";
            }
            if (target.has_feature(Target::Metal)) {
                stream << halide_internal_runtime_header_HalideRuntimeMetal_h << "\n";
            }
            if (target.has_feature(Target::OpenCL)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenCL_h << "\n";
            }
            if (target.has_feature(Target::D3D12Compute)) {
                stream << halide_internal_runtime_header_HalideRuntimeD3D12Compute_h << "\n";
            }
            if (target.has_feature(Target::WebGPU)) {
                stream << halide_internal_runtime_header_HalideRuntimeWebGPU_h << "\n";
            }
        }
        stream << "#endif\n";
    }
}

void CodeGen_C::add_platform_prologue() {
    // nothing
}

void CodeGen_C::add_vector_typedefs(const std::set<Type> &vector_types) {
    if (!vector_types.empty()) {
        // Voodoo fix: on at least one config (our arm32 buildbot running gcc 5.4),
        // emitting this long text string was regularly garbled in a predictable pattern;
        // flushing the stream before or after heals it. Since C++ codegen is rarely
        // on a compilation critical path, we'll just band-aid it in this way.
        stream << std::flush;
        stream << halide_c_template_CodeGen_C_vectors;
        stream << std::flush;

        for (const auto &t : vector_types) {
            string name = print_type(t, DoNotAppendSpace);
            string scalar_name = print_type(t.element_of(), DoNotAppendSpace);
            stream << "#if halide_cpp_use_native_vector(" << scalar_name << ", " << t.lanes() << ")\n";
            stream << "using " << name << " = NativeVector<" << scalar_name << ", " << t.lanes() << ">;\n";
            stream << "using " << name << "_ops = NativeVectorOps<" << scalar_name << ", " << t.lanes() << ">;\n";
            // Useful for debugging which Vector implementation is being selected
            // stream << "#pragma message \"using NativeVector for " << t << "\"\n";
            stream << "#else\n";
            stream << "using " << name << " = CppVector<" << scalar_name << ", " << t.lanes() << ">;\n";
            stream << "using " << name << "_ops = CppVectorOps<" << scalar_name << ", " << t.lanes() << ">;\n";
            // Useful for debugging which Vector implementation is being selected
            // stream << "#pragma message \"using CppVector for " << t << "\"\n";
            stream << "#endif\n";
        }
    }

    using_vector_typedefs = true;
}

void CodeGen_C::set_name_mangling_mode(NameMangling mode) {
    if (extern_c_open && mode != NameMangling::C) {
        stream << R"INLINE_CODE(
#ifdef __cplusplus
}  // extern "C"
#endif

)INLINE_CODE";
        extern_c_open = false;
    } else if (!extern_c_open && mode == NameMangling::C) {
        stream << R"INLINE_CODE(
#ifdef __cplusplus
extern "C" {
#endif

)INLINE_CODE";
        extern_c_open = true;
    }
}

string CodeGen_C::print_type(Type type, AppendSpaceIfNeeded space_option) {
    return type_to_c_type(type, space_option == AppendSpace);
}

string CodeGen_C::print_reinterpret(Type type, const Expr &e) {
    ostringstream oss;
    if (type.is_handle() || e.type().is_handle()) {
        // Use a c-style cast if either src or dest is a handle --
        // note that although Halide declares a "Handle" to always be 64 bits,
        // the source "handle" might actually be a 32-bit pointer (from
        // a function parameter), so calling reinterpret<> (which just memcpy's)
        // would be garbage-producing.
        oss << "(" << print_type(type) << ")";
    } else {
        oss << "reinterpret<" << print_type(type) << ">";
    }
    // If we are generating a typed nullptr, just emit that as a literal, with no intermediate,
    // to avoid ugly code like
    //
    //      uint64_t _32 = (uint64_t)(0ull);
    //      auto *_33 = (void *)(_32);
    //
    // and instead just do
    //      auto *_33 = (void *)(nullptr);
    if (type.is_handle() && is_const_zero(e)) {
        oss << "(nullptr)";
    } else {
        oss << "(" << print_expr(e) << ")";
    }
    return oss.str();
}

string CodeGen_C::print_name(const string &name) {
    return c_print_name(name);
}

namespace {
class ExternCallPrototypes : public IRGraphVisitor {
    struct NamespaceOrCall {
        const Call *call;  // nullptr if this is a subnamespace
        std::map<string, NamespaceOrCall> names;
        NamespaceOrCall(const Call *call = nullptr)
            : call(call) {
        }
    };
    std::map<string, NamespaceOrCall> c_plus_plus_externs;
    std::map<string, const Call *> c_externs;
    std::set<std::string> processed;
    std::set<std::string> internal_linkage;
    std::set<std::string> destructors;

    using IRGraphVisitor::visit;

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);

        if (!processed.count(op->name)) {
            if (op->call_type == Call::Extern || op->call_type == Call::PureExtern) {
                c_externs.insert({op->name, op});
            } else if (op->call_type == Call::ExternCPlusPlus) {
                std::vector<std::string> namespaces;
                std::string name = extract_namespaces(op->name, namespaces);
                std::map<string, NamespaceOrCall> *namespace_map = &c_plus_plus_externs;
                for (const auto &ns : namespaces) {
                    auto insertion = namespace_map->insert({ns, NamespaceOrCall()});
                    namespace_map = &insertion.first->second.names;
                }
                namespace_map->insert({name, NamespaceOrCall(op)});
            }
            processed.insert(op->name);
        }

        if (op->is_intrinsic(Call::register_destructor)) {
            internal_assert(op->args.size() == 2);
            const StringImm *fn = op->args[0].as<StringImm>();
            internal_assert(fn);
            destructors.insert(fn->value);
        }
    }

    void visit(const Allocate *op) override {
        IRGraphVisitor::visit(op);
        if (!op->free_function.empty()) {
            destructors.insert(op->free_function);
        }
    }

    void emit_function_decl(ostream &stream, const Call *op, const std::string &name) const {
        // op->name (rather than the name arg) since we need the fully-qualified C++ name
        if (internal_linkage.count(op->name)) {
            stream << "static ";
        }
        stream << type_to_c_type(op->type, /* append_space */ true) << name << "(";
        if (function_takes_user_context(name)) {
            stream << "void *";
            if (!op->args.empty()) {
                stream << ", ";
            }
        }
        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) {
                stream << ", ";
            }
            if (op->args[i].as<StringImm>()) {
                stream << "const char *";
            } else {
                stream << type_to_c_type(op->args[i].type(), true);
            }
        }
        stream << ");\n";
    }

    void emit_namespace_or_call(ostream &stream, const NamespaceOrCall &ns_or_call, const std::string &name) const {
        if (ns_or_call.call == nullptr) {
            stream << "namespace " << name << " {\n";
            for (const auto &ns_or_call_inner : ns_or_call.names) {
                emit_namespace_or_call(stream, ns_or_call_inner.second, ns_or_call_inner.first);
            }
            stream << "} // namespace " << name << "\n";
        } else {
            emit_function_decl(stream, ns_or_call.call, name);
        }
    }

public:
    ExternCallPrototypes() {
        // Make sure we don't catch calls that are already in the global declarations
        const char *strs[] = {(const char *)halide_c_template_CodeGen_C_prologue,
                              (const char *)halide_internal_runtime_header_HalideRuntime_h,
                              (const char *)halide_internal_initmod_inlined_c};
        for (const char *str : strs) {
            size_t j = 0;
            for (size_t i = 0; str[i]; i++) {
                char c = str[i];
                if (c == '(' && i > j + 1) {
                    // Could be the end of a function_name.
                    string name(str + j + 1, i - j - 1);
                    processed.insert(name);
                }

                if (('A' <= c && c <= 'Z') ||
                    ('a' <= c && c <= 'z') ||
                    c == '_' ||
                    ('0' <= c && c <= '9')) {
                    // Could be part of a function name.
                } else {
                    j = i;
                }
            }
        }
    }

    void set_internal_linkage(const std::string &name) {
        internal_linkage.insert(name);
    }

    bool has_c_declarations() const {
        return !c_externs.empty() || !destructors.empty();
    }

    bool has_c_plus_plus_declarations() const {
        return !c_plus_plus_externs.empty();
    }

    void emit_c_declarations(ostream &stream) const {
        for (const auto &call : c_externs) {
            emit_function_decl(stream, call.second, call.first);
        }
        for (const auto &d : destructors) {
            stream << "void " << d << "(void *, void *);\n";
        }
        stream << "\n";
    }

    void emit_c_plus_plus_declarations(ostream &stream) const {
        for (const auto &ns_or_call : c_plus_plus_externs) {
            emit_namespace_or_call(stream, ns_or_call.second, ns_or_call.first);
        }
        stream << "\n";
    }
};
}  // namespace

void CodeGen_C::forward_declare_type_if_needed(const Type &t) {
    if (!t.handle_type ||
        forward_declared.count(t.handle_type) ||
        t.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Simple) {
        return;
    }
    for (const auto &ns : t.handle_type->namespaces) {
        stream << "namespace " << ns << " { ";
    }
    switch (t.handle_type->inner_name.cpp_type_type) {
    case halide_cplusplus_type_name::Simple:
        // nothing
        break;
    case halide_cplusplus_type_name::Struct:
        stream << "struct " << t.handle_type->inner_name.name << ";";
        break;
    case halide_cplusplus_type_name::Class:
        stream << "class " << t.handle_type->inner_name.name << ";";
        break;
    case halide_cplusplus_type_name::Union:
        stream << "union " << t.handle_type->inner_name.name << ";";
        break;
    case halide_cplusplus_type_name::Enum:
        internal_error << "Passing pointers to enums is unsupported\n";
        break;
    }
    for (const auto &ns : t.handle_type->namespaces) {
        (void)ns;
        stream << " }";
    }
    stream << "\n";
    forward_declared.insert(t.handle_type);
}

void CodeGen_C::emit_argv_wrapper(const std::string &function_name,
                                  const std::vector<LoweredArgument> &args) {
    if (is_header_or_extern_decl()) {
        stream << "\nHALIDE_FUNCTION_ATTRS\nint " << function_name << "_argv(void **args);\n";
        return;
    }

    stream << "\nHALIDE_FUNCTION_ATTRS\nint " << function_name << "_argv(void **args) {\n";
    indent += 1;

    stream << get_indent() << "return " << function_name << "(\n";
    indent += 1;

    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            stream << get_indent() << "(halide_buffer_t *)args[" << i << "]";
        } else {
            stream << get_indent() << "*(" << type_to_c_type(args[i].type, false) << " const *)args[" << i << "]";
        }
        if (i + 1 < args.size()) {
            stream << ",";
        }
        stream << "\n";
    }

    indent -= 1;
    stream << ");\n";

    indent -= 1;
    stream << "}";
}

void CodeGen_C::emit_metadata_getter(const std::string &function_name,
                                     const std::vector<LoweredArgument> &args,
                                     const MetadataNameMap &metadata_name_map) {
    if (is_header_or_extern_decl()) {
        stream << "\nHALIDE_FUNCTION_ATTRS\nconst struct halide_filter_metadata_t *" << function_name << "_metadata();\n";
        return;
    }

    auto map_name = [&metadata_name_map](const std::string &from) -> std::string {
        auto it = metadata_name_map.find(from);
        return it == metadata_name_map.end() ? from : it->second;
    };

    stream << "\nHALIDE_FUNCTION_ATTRS\nconst struct halide_filter_metadata_t *" << function_name << "_metadata() {\n";

    indent += 1;

    static const char *const kind_names[] = {
        "halide_argument_kind_input_scalar",
        "halide_argument_kind_input_buffer",
        "halide_argument_kind_output_buffer",
    };

    static const char *const type_code_names[] = {
        "halide_type_int",
        "halide_type_uint",
        "halide_type_float",
        "halide_type_handle",
        "halide_type_bfloat",
    };

    std::set<int64_t> constant_int64_in_use;
    const auto emit_constant_int64 = [this, &constant_int64_in_use](Expr e) -> std::string {
        if (!e.defined()) {
            return "nullptr";
        }

        internal_assert(!e.type().is_handle()) << "Should never see Handle types here.";
        if (!is_const(e)) {
            e = simplify(e);
            internal_assert(is_const(e)) << "Should only see constant values here.";
        }

        const IntImm *int_imm = e.as<IntImm>();
        internal_assert(int_imm && int_imm->type == Int(64));

        const std::string id = "const_" + std::to_string(int_imm->value);
        if (!constant_int64_in_use.count(int_imm->value)) {
            stream << get_indent() << "static const int64_t " << id << " = " << int_imm->value << "LL;\n";
            constant_int64_in_use.insert(int_imm->value);
        }
        return "&" + id;
    };

    int next_scalar_value_id = 0;
    const auto emit_constant_scalar_value = [this, &next_scalar_value_id](Expr e) -> std::string {
        if (!e.defined()) {
            return "nullptr";
        }

        internal_assert(!e.type().is_handle()) << "Should never see Handle types here.";
        if (!is_const(e)) {
            e = simplify(e);
            internal_assert(is_const(e)) << "Should only see constant values here.";
        }

        const IntImm *int_imm = e.as<IntImm>();
        const UIntImm *uint_imm = e.as<UIntImm>();
        const FloatImm *float_imm = e.as<FloatImm>();
        internal_assert(int_imm || uint_imm || float_imm);
        std::string value;
        if (int_imm) {
            value = std::to_string(int_imm->value);
        } else if (uint_imm) {
            value = std::to_string(uint_imm->value);
        } else if (float_imm) {
            value = std::to_string(float_imm->value);
        }

        std::string c_type = type_to_c_type(e.type(), false);
        std::string id = "halide_scalar_value_" + std::to_string(next_scalar_value_id++);

        // It's important that we allocate a full scalar_value_t_type here,
        // even if the type of the value is smaller; downstream consumers should
        // be able to correctly load an entire scalar_value_t_type regardless of its
        // type, and if we emit just (say) a uint8 value here, the pointer may be
        // misaligned and/or the storage after may be unmapped. We'll fake it by
        // making a constant array of the elements we need, setting the first to the
        // constant we want, and setting the rest to all-zeros. (This happens to work because
        // sizeof(halide_scalar_value_t) is evenly divisible by sizeof(any-union-field.)

        const size_t value_size = e.type().bytes();
        internal_assert(value_size > 0 && value_size <= sizeof(halide_scalar_value_t));

        const size_t array_size = sizeof(halide_scalar_value_t) / value_size;
        internal_assert(array_size * value_size == sizeof(halide_scalar_value_t));

        stream << get_indent() << "alignas(alignof(halide_scalar_value_t)) static const " << c_type << " " << id << "[" << array_size << "] = {" << value;
        for (size_t i = 1; i < array_size; i++) {
            stream << ", 0";
        }
        stream << "};\n";

        return "(const halide_scalar_value_t *)&" + id;
    };

    for (const auto &arg : args) {
        const auto legalized_name = c_print_name(map_name(arg.name));

        auto argument_estimates = arg.argument_estimates;
        if (arg.type.is_handle()) {
            // Handle values are always emitted into metadata as "undefined", regardless of
            // what sort of Expr is provided.
            argument_estimates = ArgumentEstimates{};
        }

        const auto defined_count = [](const Region &r) -> size_t {
            size_t c = 0;
            for (const auto &be : r) {
                c += be.min.defined() ? 1 : 0;
                c += be.extent.defined() ? 1 : 0;
            }
            return c;
        };

        std::string buffer_estimates_array_ptr = "nullptr";
        if (arg.is_buffer() && defined_count(argument_estimates.buffer_estimates) > 0) {
            internal_assert((int)argument_estimates.buffer_estimates.size() == arg.dimensions);
            std::vector<std::string> constants;
            for (const auto &be : argument_estimates.buffer_estimates) {
                Expr min = be.min;
                if (min.defined()) {
                    min = cast<int64_t>(min);
                }
                Expr extent = be.extent;
                if (extent.defined()) {
                    extent = cast<int64_t>(extent);
                }
                constants.push_back(emit_constant_int64(min));
                constants.push_back(emit_constant_int64(extent));
            }

            stream << get_indent() << "static const int64_t * const buffer_estimates_" << legalized_name << "[" << (int)arg.dimensions * 2 << "] = {\n";
            indent += 1;
            for (const auto &c : constants) {
                stream << get_indent() << c << ",\n";
            }
            indent -= 1;
            stream << get_indent() << "};\n";
        } else {
            stream << get_indent() << "int64_t const *const *buffer_estimates_" << legalized_name << " = nullptr;\n";
        }

        auto scalar_def = emit_constant_scalar_value(argument_estimates.scalar_def);
        auto scalar_min = emit_constant_scalar_value(argument_estimates.scalar_min);
        auto scalar_max = emit_constant_scalar_value(argument_estimates.scalar_max);
        auto scalar_estimate = emit_constant_scalar_value(argument_estimates.scalar_estimate);

        stream << get_indent() << "const halide_scalar_value_t *scalar_def_" << legalized_name << " = " << scalar_def << ";\n";
        stream << get_indent() << "const halide_scalar_value_t *scalar_min_" << legalized_name << " = " << scalar_min << ";\n";
        stream << get_indent() << "const halide_scalar_value_t *scalar_max_" << legalized_name << " = " << scalar_max << ";\n";
        stream << get_indent() << "const halide_scalar_value_t *scalar_estimate_" << legalized_name << " = " << scalar_estimate << ";\n";
    }

    stream << get_indent() << "static const halide_filter_argument_t args[" << args.size() << "] = {\n";
    indent += 1;
    for (const auto &arg : args) {
        const auto name = map_name(arg.name);
        const auto legalized_name = c_print_name(name);

        stream << get_indent() << "{\n";
        indent += 1;
        stream << get_indent() << "\"" << name << "\",\n";
        internal_assert(arg.kind < sizeof(kind_names) / sizeof(kind_names[0]));
        stream << get_indent() << kind_names[arg.kind] << ",\n";
        stream << get_indent() << (int)arg.dimensions << ",\n";
        internal_assert(arg.type.code() < sizeof(type_code_names) / sizeof(type_code_names[0]));
        stream << get_indent() << "{" << type_code_names[arg.type.code()] << ", " << (int)arg.type.bits() << ", " << (int)arg.type.lanes() << "},\n";
        stream << get_indent() << "scalar_def_" << legalized_name << ",\n";
        stream << get_indent() << "scalar_min_" << legalized_name << ",\n";
        stream << get_indent() << "scalar_max_" << legalized_name << ",\n";
        stream << get_indent() << "scalar_estimate_" << legalized_name << ",\n";
        stream << get_indent() << "buffer_estimates_" << legalized_name << ",\n";
        stream << get_indent() << "},\n";
        indent -= 1;
    }
    stream << get_indent() << "};\n";
    indent -= 1;

    stream << get_indent() << "static const halide_filter_metadata_t md = {\n";

    indent += 1;

    stream << get_indent() << "halide_filter_metadata_t::VERSION,\n";
    stream << get_indent() << args.size() << ",\n";
    stream << get_indent() << "args,\n";
    stream << get_indent() << "\"" << target.to_string() << "\",\n";
    stream << get_indent() << "\"" << function_name << "\",\n";
    stream << get_indent() << "};\n";
    indent -= 1;

    stream << get_indent() << "return &md;\n";

    indent -= 1;

    stream << "}\n";
}

void CodeGen_C::emit_constexpr_function_info(const std::string &function_name,
                                             const std::vector<LoweredArgument> &args,
                                             const MetadataNameMap &metadata_name_map) {
    internal_assert(!extern_c_open)
        << "emit_constexpr_function_info() must not be called from inside an extern \"C\" block";

    if (!is_header()) {
        return;
    }

    auto map_name = [&metadata_name_map](const std::string &from) -> std::string {
        auto it = metadata_name_map.find(from);
        return it == metadata_name_map.end() ? from : it->second;
    };

    static const std::array<const char *, 3> kind_names = {
        "::HalideFunctionInfo::InputScalar",
        "::HalideFunctionInfo::InputBuffer",
        "::HalideFunctionInfo::OutputBuffer",
    };

    static const std::array<const char *, 5> type_code_names = {
        "halide_type_int",
        "halide_type_uint",
        "halide_type_float",
        "halide_type_handle",
        "halide_type_bfloat",
    };

    stream << constexpr_argument_info_docs;

    stream << "inline constexpr std::array<::HalideFunctionInfo::ArgumentInfo, " << args.size() << "> "
           << function_name << "_argument_info() {\n";

    indent += 1;

    stream << get_indent() << "return {{\n";
    indent += 1;
    for (const auto &arg : args) {
        internal_assert(arg.kind < kind_names.size());
        internal_assert(arg.type.code() < type_code_names.size());

        const auto name = map_name(arg.name);

        stream << get_indent() << "{\"" << name << "\", " << kind_names[arg.kind] << ", " << (int)arg.dimensions
               << ", halide_type_t{" << type_code_names[arg.type.code()] << ", " << (int)arg.type.bits()
               << ", " << (int)arg.type.lanes() << "}},\n";
    }
    indent -= 1;
    stream << get_indent() << "}};\n";
    indent -= 1;
    internal_assert(indent == 0);

    stream << "}\n";
}

void CodeGen_C::emit_halide_free_helper(const std::string &alloc_name, const std::string &free_function) {
    stream << get_indent() << "HalideFreeHelper<" << free_function << "> "
           << alloc_name << "_free(_ucon, " << alloc_name << ");\n";
}

void CodeGen_C::compile(const Module &input) {
    add_platform_prologue();

    TypeInfoGatherer type_info;
    for (const auto &f : input.functions()) {
        if (f.body.defined()) {
            f.body.accept(&type_info);
        }
    }
    uses_gpu_for_loops = (type_info.for_types_used.count(ForType::GPUBlock) ||
                          type_info.for_types_used.count(ForType::GPUThread) ||
                          type_info.for_types_used.count(ForType::GPULane));

    // Forward-declare all the types we need; this needs to happen before
    // we emit function prototypes, since those may need the types.
    if (output_kind != CPlusPlusFunctionInfoHeader) {
        stream << "\n";
        for (const auto &f : input.functions()) {
            for (const auto &arg : f.args) {
                forward_declare_type_if_needed(arg.type);
            }
        }
        stream << "\n";
    }

    if (!is_header_or_extern_decl()) {
        add_vector_typedefs(type_info.vector_types_used);

        // Emit prototypes for all external and internal-only functions.
        // Gather them up and do them all up front, to reduce duplicates,
        // and to make it simpler to get internal-linkage functions correct.
        ExternCallPrototypes e;
        for (const auto &f : input.functions()) {
            f.body.accept(&e);
            if (f.linkage == LinkageType::Internal) {
                // We can't tell at the call site if a LoweredFunc is intended to be internal
                // or not, so mark them explicitly.
                e.set_internal_linkage(f.name);
            }
        }

        if (e.has_c_plus_plus_declarations()) {
            set_name_mangling_mode(NameMangling::CPlusPlus);
            e.emit_c_plus_plus_declarations(stream);
        }

        if (e.has_c_declarations()) {
            set_name_mangling_mode(NameMangling::C);
            e.emit_c_declarations(stream);
        }
    }

    for (const auto &b : input.buffers()) {
        compile(b);
    }
    const auto metadata_name_map = input.get_metadata_name_map();
    for (const auto &f : input.functions()) {
        compile(f, metadata_name_map);
    }
}

Stmt CodeGen_C::preprocess_function_body(const Stmt &stmt) {
    return stmt;
}

void CodeGen_C::compile(const LoweredFunc &f, const MetadataNameMap &metadata_name_map) {
    // Don't put non-external function declarations in headers.
    if (is_header_or_extern_decl() && f.linkage == LinkageType::Internal) {
        return;
    }

    const std::vector<LoweredArgument> &args = f.args;

    have_user_context = false;
    for (const auto &arg : args) {
        // TODO: check that its type is void *?
        have_user_context |= (arg.name == "__user_context");
    }

    NameMangling name_mangling = f.name_mangling;
    if (name_mangling == NameMangling::Default) {
        name_mangling = (target.has_feature(Target::CPlusPlusMangling) || output_kind == CPlusPlusFunctionInfoHeader ? NameMangling::CPlusPlus : NameMangling::C);
    }

    set_name_mangling_mode(name_mangling);

    std::vector<std::string> namespaces;
    std::string simple_name = c_print_name(extract_namespaces(f.name, namespaces), false);
    if (!is_c_plus_plus_interface()) {
        user_assert(namespaces.empty()) << "Namespace qualifiers not allowed on function name if not compiling with Target::CPlusPlusNameMangling.\n";
    }

    if (!namespaces.empty()) {
        for (const auto &ns : namespaces) {
            stream << "namespace " << ns << " {\n";
        }
        stream << "\n";
    }

    if (output_kind != CPlusPlusFunctionInfoHeader) {
        const auto emit_arg_decls = [&](const Type &ucon_type = Type()) {
            const char *comma = "";
            for (const auto &arg : args) {
                stream << comma;
                if (arg.is_buffer()) {
                    stream << "struct halide_buffer_t *"
                           << print_name(arg.name)
                           << "_buffer";
                } else {
                    // If this arg is the user_context value, *and* ucon_type is valid,
                    // use ucon_type instead of arg.type.
                    const Type &t = (arg.name == "__user_context" && ucon_type.bits() != 0) ? ucon_type : arg.type;
                    stream << print_type(t, AppendSpace) << print_name(arg.name);
                }
                comma = ", ";
            }
        };

        // Emit the function prototype
        if (f.linkage == LinkageType::Internal) {
            // If the function isn't public, mark it static.
            stream << "static ";
        }
        stream << "HALIDE_FUNCTION_ATTRS\n";
        stream << "int " << simple_name << "(";
        emit_arg_decls();

        if (is_header_or_extern_decl()) {
            stream << ");\n";
        } else {
            stream << ") ";
            open_scope();

            if (uses_gpu_for_loops) {
                stream << get_indent() << "halide_error("
                       << (have_user_context ? "const_cast<void *>(__user_context)" : "nullptr")
                       << ", \"C++ Backend does not support gpu_blocks() or gpu_threads() yet, "
                       << "this function will always fail at runtime\");\n";
                stream << get_indent() << "return halide_error_code_device_malloc_failed;\n";
            } else {
                // Emit a local user_context we can pass in all cases, either
                // aliasing __user_context or nullptr.
                stream << get_indent() << "void * const _ucon = "
                       << (have_user_context ? "const_cast<void *>(__user_context)" : "nullptr")
                       << ";\n";

                // Always declare it unused, since this could be a generated closure that doesn't
                // use _ucon at all, regardless of NoAsserts.
                stream << get_indent() << "halide_maybe_unused(_ucon);\n";

                // Emit the body
                Stmt body_to_print = preprocess_function_body(f.body);
                print(body_to_print);

                // Return success.
                stream << get_indent() << "return 0;\n";
                cache.clear();
            }

            // Ensure we use open/close_scope, so that the cache doesn't try to linger
            // across function boundaries for internal closures.
            close_scope("");
        }

        // Workaround for https://github.com/halide/Halide/issues/635:
        // For historical reasons, Halide-generated AOT code
        // defines user_context as `void const*`, but expects all
        // define_extern code with user_context usage to use `void *`. This
        // usually isn't an issue, but if both the caller and callee of the
        // pass a user_context, *and* c_plus_plus_name_mangling is enabled,
        // we get link errors because of this dichotomy. Fixing this
        // "correctly" (ie so that everything always uses identical types for
        // user_context in all cases) will require a *lot* of downstream
        // churn (see https://github.com/halide/Halide/issues/7298),
        // so this is a workaround: Add a wrapper with `void*`
        // ucon -> `void const*` ucon. In most cases this will be ignored
        // (and probably dead-stripped), but in these cases it's critical.
        //
        // (Note that we don't check to see if c_plus_plus_name_mangling is
        // enabled, since that would have to be done on the caller side, and
        // this is purely a callee-side fix.)
        if (f.linkage != LinkageType::Internal &&
            output_kind == CPlusPlusImplementation &&
            target.has_feature(Target::CPlusPlusMangling) &&
            get_target().has_feature(Target::UserContext)) {

            Type ucon_type = Type();
            for (const auto &arg : args) {
                if (arg.name == "__user_context") {
                    ucon_type = arg.type;
                    break;
                }
            }
            if (ucon_type == type_of<void const *>()) {
                stream << "\nHALIDE_FUNCTION_ATTRS\n";
                stream << "int " << simple_name << "(";
                emit_arg_decls(type_of<void *>());
                stream << ") ";
                open_scope();
                stream << get_indent() << "    return " << simple_name << "(";
                const char *comma = "";
                for (const auto &arg : args) {
                    if (arg.name == "__user_context") {
                        // Add an explicit cast here so we won't call ourselves into oblivion
                        stream << "(void const *)";
                    }
                    stream << comma << print_name(arg.name);
                    if (arg.is_buffer()) {
                        stream << "_buffer";
                    }
                    comma = ", ";
                }
                stream << ");\n";
                close_scope("");
            }
        }

        if (f.linkage == LinkageType::ExternalPlusArgv || f.linkage == LinkageType::ExternalPlusMetadata) {
            // Emit the argv version
            emit_argv_wrapper(simple_name, args);
        }

        if (f.linkage == LinkageType::ExternalPlusMetadata) {
            // Emit the metadata.
            emit_metadata_getter(simple_name, args, metadata_name_map);
        }
    } else {
        if (f.linkage != LinkageType::Internal) {
            emit_constexpr_function_info(simple_name, args, metadata_name_map);
        }
    }

    if (!namespaces.empty()) {
        stream << "\n";
        for (size_t i = namespaces.size(); i > 0; i--) {
            stream << "}  // namespace " << namespaces[i - 1] << "\n";
        }
        stream << "\n";
    }
}

void CodeGen_C::compile(const Buffer<> &buffer) {
    // Don't define buffers in headers or extern decls.
    if (is_header_or_extern_decl()) {
        return;
    }

    string name = print_name(buffer.name());
    halide_buffer_t b = *(buffer.raw_buffer());

    user_assert(b.host) << "Can't embed image: " << buffer.name() << " because it has a null host pointer\n";
    user_assert(!b.device_dirty()) << "Can't embed image: " << buffer.name() << "because it has a dirty device pointer\n";

    // Figure out the offset of the last pixel.
    size_t num_elems = 1;
    for (int d = 0; d < b.dimensions; d++) {
        num_elems += b.dim[d].stride * (size_t)(b.dim[d].extent - 1);
    }

    // For now, we assume buffers that aren't scalar are constant,
    // while scalars can be mutated. This accommodates all our existing
    // use cases, which is that all buffers are constant, except those
    // used to store stateful module information in offloading runtimes.
    bool is_constant = buffer.dimensions() != 0;

    // If it is an GPU source kernel, we would like to see the actual output, not the
    // uint8 representation. We use a string literal for this.
    if (ends_with(name, "gpu_source_kernels")) {
        stream << "static const char *" << name << "_string = R\"BUFCHARSOURCE(";
        stream.write((char *)b.host, num_elems);
        stream << ")BUFCHARSOURCE\";\n";

        stream << "static const HALIDE_ATTRIBUTE_ALIGN(32) uint8_t *" << name << "_data = (const uint8_t *) "
               << name << "_string;\n";
    } else {
        // Emit the data
        stream << "static " << (is_constant ? "const" : "") << " HALIDE_ATTRIBUTE_ALIGN(32) uint8_t " << name << "_data[] = {\n";
        stream << get_indent();
        for (size_t i = 0; i < num_elems * b.type.bytes(); i++) {
            if (i > 0) {
                stream << ",";
                if (i % 16 == 0) {
                    stream << "\n";
                    stream << get_indent();
                } else {
                    stream << " ";
                }
            }
            stream << (int)(b.host[i]);
        }
        stream << "\n};\n";
    }

    std::string buffer_shape = "nullptr";
    if (buffer.dimensions()) {
        // Emit the shape -- note that we can't use this for scalar buffers because
        // we'd emit a statement of the form "foo_buffer_shape[] = {}", and a zero-length
        // array will make some compilers unhappy.
        stream << "static const halide_dimension_t " << name << "_buffer_shape[] = {";
        for (int i = 0; i < buffer.dimensions(); i++) {
            stream << "halide_dimension_t(" << buffer.dim(i).min()
                   << ", " << buffer.dim(i).extent()
                   << ", " << buffer.dim(i).stride() << ")";
            if (i < buffer.dimensions() - 1) {
                stream << ", ";
            }
        }
        stream << "};\n";
        buffer_shape = "const_cast<halide_dimension_t*>(" + name + "_buffer_shape)";
    }

    Type t = buffer.type();

    // Emit the buffer struct. Note that although our shape and (usually) our host
    // data is const, the buffer itself isn't: embedded buffers in one pipeline
    // can be passed to another pipeline (e.g. for an extern stage), in which
    // case the buffer objects need to be non-const, because the constness
    // (from the POV of the extern stage) is a runtime property.
    stream << "static halide_buffer_t " << name << "_buffer_ = {"
           << "0, "                                              // device
           << "nullptr, "                                        // device_interface
           << "const_cast<uint8_t*>(&" << name << "_data[0]), "  // host
           << "0, "                                              // flags
           << "halide_type_t((halide_type_code_t)(" << (int)t.code() << "), " << t.bits() << ", " << t.lanes() << "), "
           << buffer.dimensions() << ", "
           << buffer_shape << "};\n";

    // Make a global pointer to it.
    stream << "static halide_buffer_t * const " << name << "_buffer = &" << name << "_buffer_;\n";
}

string CodeGen_C::print_expr(const Expr &e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

string CodeGen_C::print_cast_expr(const Type &t, const Expr &e) {
    string value = print_expr(e);
    if (e.type() == t) {
        // This is uncommon but does happen occasionally
        return value;
    }
    string type = print_type(t);
    if (t.is_vector() &&
        t.lanes() == e.type().lanes() &&
        t != e.type()) {
        return print_assignment(t, type + "_ops::convert_from<" + print_type(e.type()) + ">(" + value + ")");
    } else {
        return print_assignment(t, "(" + type + ")(" + value + ")");
    }
}

void CodeGen_C::print_stmt(const Stmt &s) {
    s.accept(this);
}

string CodeGen_C::print_assignment(Type t, const std::string &rhs) {
    auto cached = cache.find(rhs);
    if (cached == cache.end()) {
        id = unique_name('_');
        const char *const_flag = output_kind == CPlusPlusImplementation ? " const " : "";
        if (t.is_handle()) {
            // Don't print void *, which might lose useful type information. just use auto.
            stream << get_indent() << "auto *";
        } else {
            stream << get_indent() << print_type(t, AppendSpace);
        }
        stream << const_flag << id << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

void CodeGen_C::open_scope() {
    cache.clear();
    stream << get_indent();
    indent++;
    stream << "{\n";
}

void CodeGen_C::close_scope(const std::string &comment) {
    cache.clear();
    indent--;
    stream << get_indent();
    if (!comment.empty()) {
        stream << "} // " << comment << "\n";
    } else {
        stream << "}\n";
    }
}

void CodeGen_C::visit(const Variable *op) {
    if (starts_with(op->name, "::")) {
        // This is the name of a global, so we can't modify it.
        id = op->name;
    } else {
        // This substitution ensures const correctness for all calls
        if (op->name == "__user_context") {
            id = "_ucon";
        } else {
            id = print_name(op->name);
        }
    }
}

void CodeGen_C::visit(const Cast *op) {
    id = print_cast_expr(op->type, op->value);
}

void CodeGen_C::visit(const Reinterpret *op) {
    id = print_assignment(op->type, print_reinterpret(op->type, op->value));
}

void CodeGen_C::visit_binop(Type t, const Expr &a, const Expr &b, const char *op) {
    string sa = print_expr(a);
    string sb = print_expr(b);
    print_assignment(t, sa + " " + op + " " + sb);
}

void CodeGen_C::visit(const Add *op) {
    visit_binop(op->type, op->a, op->b, "+");
}

void CodeGen_C::visit(const Sub *op) {
    visit_binop(op->type, op->a, op->b, "-");
}

void CodeGen_C::visit(const Mul *op) {
    visit_binop(op->type, op->a, op->b, "*");
}

void CodeGen_C::visit(const Div *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        visit_binop(op->type, op->a, make_const(op->a.type(), bits), ">>");
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        visit_binop(op->type, op->a, make_const(op->a.type(), (1 << bits) - 1), "&");
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else if (op->type.is_float()) {
        string arg0 = print_expr(op->a);
        string arg1 = print_expr(op->b);
        ostringstream rhs;
        rhs << "fmod(" << arg0 << ", " << arg1 << ")";
        print_assignment(op->type, rhs.str());
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_C::visit(const Max *op) {
    // clang doesn't support the ternary operator on OpenCL style vectors.
    // See: https://bugs.llvm.org/show_bug.cgi?id=33103
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_max", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        rhs << print_type(op->type) << "_ops::max(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_C::visit(const Min *op) {
    // clang doesn't support the ternary operator on OpenCL style vectors.
    // See: https://bugs.llvm.org/show_bug.cgi?id=33103
    if (op->type.is_scalar()) {
        print_expr(Call::make(op->type, "::halide_cpp_min", {op->a, op->b}, Call::Extern));
    } else {
        ostringstream rhs;
        rhs << print_type(op->type) << "_ops::min(" << print_expr(op->a) << ", " << print_expr(op->b) << ")";
        print_assignment(op->type, rhs.str());
    }
}

void CodeGen_C::visit_relop(Type t, const Expr &a, const Expr &b, const char *scalar_op, const char *vector_op) {
    if (t.is_scalar() || !using_vector_typedefs) {
        visit_binop(t, a, b, scalar_op);
    } else {
        internal_assert(a.type() == b.type());
        string sa = print_expr(a);
        string sb = print_expr(b);
        print_assignment(t, print_type(a.type()) + "_ops::" + vector_op + "(" + sa + ", " + sb + ")");
    }
}

void CodeGen_C::visit(const EQ *op) {
    visit_relop(op->type, op->a, op->b, "==", "eq");
}

void CodeGen_C::visit(const NE *op) {
    visit_relop(op->type, op->a, op->b, "!=", "ne");
}

void CodeGen_C::visit(const LT *op) {
    visit_relop(op->type, op->a, op->b, "<", "lt");
}

void CodeGen_C::visit(const LE *op) {
    visit_relop(op->type, op->a, op->b, "<=", "le");
}

void CodeGen_C::visit(const GT *op) {
    visit_relop(op->type, op->a, op->b, ">", "gt");
}

void CodeGen_C::visit(const GE *op) {
    visit_relop(op->type, op->a, op->b, ">=", "ge");
}

void CodeGen_C::visit(const Or *op) {
    visit_relop(op->type, op->a, op->b, "||", "logical_or");
}

void CodeGen_C::visit(const And *op) {
    visit_relop(op->type, op->a, op->b, "&&", "logical_and");
}

void CodeGen_C::visit(const Not *op) {
    print_assignment(op->type, "!(" + print_expr(op->a) + ")");
}

void CodeGen_C::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        id = std::to_string(op->value);
    } else {
        static const char *const suffixes[3] = {
            "ll",  // PlainC
            "l",   // OpenCL
            "",    // HLSL
        };
        print_assignment(op->type, "(" + print_type(op->type) + ")(" + std::to_string(op->value) + suffixes[(int)integer_suffix_style] + ")");
    }
}

void CodeGen_C::visit(const UIntImm *op) {
    if (op->type == UInt(1)) {
        id = op->value ? "true" : "false";
    } else {
        static const char *const suffixes[3] = {
            "ull",  // PlainC
            "ul",   // OpenCL
            "",     // HLSL
        };
        print_assignment(op->type, "(" + print_type(op->type) + ")(" + std::to_string(op->value) + suffixes[(int)integer_suffix_style] + ")");
    }
}

void CodeGen_C::visit(const StringImm *op) {
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

void CodeGen_C::visit(const FloatImm *op) {
    if (std::isnan(op->value)) {
        id = "nan_f32()";
    } else if (std::isinf(op->value)) {
        if (op->value > 0) {
            id = "inf_f32()";
        } else {
            id = "neg_inf_f32()";
        }
    } else {
        // Write the constant as reinterpreted uint to avoid any bits lost in conversion.
        union {
            uint32_t as_uint;
            float as_float;
        } u;
        u.as_float = op->value;

        ostringstream oss;
        if (op->type.bits() == 64) {
            oss << "(double) ";
        }
        oss << "float_from_bits(" << u.as_uint << " /* " << u.as_float << " */)";
        print_assignment(op->type, oss.str());
    }
}

bool CodeGen_C::is_stack_private_to_thread() const {
    return false;
}

void CodeGen_C::visit(const Call *op) {

    internal_assert(op->is_extern() || op->is_intrinsic())
        << "Can only codegen extern calls and intrinsics\n";

    ostringstream rhs;

    // Handle intrinsics first
    if (op->is_intrinsic(Call::debug_to_file)) {
        internal_assert(op->args.size() == 3);
        const StringImm *string_imm = op->args[0].as<StringImm>();
        internal_assert(string_imm);
        string filename = string_imm->value;
        string typecode = print_expr(op->args[1]);
        string buffer = print_name(print_expr(op->args[2]));

        rhs << "halide_debug_to_file(_ucon, "
            << "\"" << filename << "\", "
            << typecode
            << ", (struct halide_buffer_t *)" << buffer << ")";
    } else if (op->is_intrinsic(Call::bitwise_and)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " & " << a1;
    } else if (op->is_intrinsic(Call::bitwise_xor)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " ^ " << a1;
    } else if (op->is_intrinsic(Call::bitwise_or)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " | " << a1;
    } else if (op->is_intrinsic(Call::bitwise_not)) {
        internal_assert(op->args.size() == 1);
        rhs << "~" << print_expr(op->args[0]);
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        if (op->args[1].type().is_uint()) {
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " << " << a1;
        } else {
            rhs << print_expr(lower_signed_shift_left(op->args[0], op->args[1]));
        }
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        if (op->args[1].type().is_uint()) {
            string a0 = print_expr(op->args[0]);
            string a1 = print_expr(op->args[1]);
            rhs << a0 << " >> " << a1;
        } else {
            rhs << print_expr(lower_signed_shift_right(op->args[0], op->args[1]));
        }
    } else if (op->is_intrinsic(Call::count_leading_zeros) ||
               op->is_intrinsic(Call::count_trailing_zeros) ||
               op->is_intrinsic(Call::popcount)) {
        internal_assert(op->args.size() == 1);
        if (op->args[0].type().is_vector()) {
            rhs << print_scalarized_expr(op);
        } else {
            string a0 = print_expr(op->args[0]);
            rhs << "halide_" << op->name << "(" << a0 << ")";
        }
    } else if (op->is_intrinsic(Call::lerp)) {
        internal_assert(op->args.size() == 3);
        Expr e = lower_lerp(op->type, op->args[0], op->args[1], op->args[2], target);
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Expr e = cast(op->type, select(a < b, b - a, a - b));
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        string arg0 = print_expr(op->args[0]);
        string arg1 = print_expr(op->args[1]);
        rhs << "return_second(" << arg0 << ", " << arg1 << ")";
    } else if (op->is_intrinsic(Call::if_then_else)) {
        internal_assert(op->args.size() == 2 || op->args.size() == 3);

        string result_id = unique_name('_');

        stream << get_indent() << print_type(op->args[1].type(), AppendSpace)
               << result_id << ";\n";

        string cond_id = print_expr(op->args[0]);

        stream << get_indent() << "if (" << cond_id << ")\n";
        open_scope();
        string true_case = print_expr(op->args[1]);
        stream << get_indent() << result_id << " = " << true_case << ";\n";
        close_scope("if " + cond_id);
        if (op->args.size() == 3) {
            stream << get_indent() << "else\n";
            open_scope();
            string false_case = print_expr(op->args[2]);
            stream << get_indent() << result_id << " = " << false_case << ";\n";
            close_scope("if " + cond_id + " else");
        }
        rhs << result_id;
    } else if (op->is_intrinsic(Call::require)) {
        internal_assert(op->args.size() == 3);
        if (op->args[0].type().is_vector()) {
            rhs << print_scalarized_expr(op);
        } else {
            create_assertion(op->args[0], op->args[2]);
            rhs << print_expr(op->args[1]);
        }
    } else if (op->is_intrinsic(Call::round)) {
        // There's no way to get rounding with ties to nearest even that works
        // in all contexts where someone might be compiling generated C++ code,
        // so we just lower it into primitive operations.
        rhs << print_expr(lower_round_to_nearest_ties_to_even(op->args[0]));
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        Expr a0 = op->args[0];
        rhs << print_expr(cast(op->type, select(a0 > 0, a0, -a0)));
    } else if (op->is_intrinsic(Call::memoize_expr)) {
        internal_assert(!op->args.empty());
        string arg = print_expr(op->args[0]);
        rhs << "(" << arg << ")";
    } else if (op->is_intrinsic(Call::alloca)) {
        internal_assert(op->args.size() == 1);
        internal_assert(op->type.is_handle());
        const int64_t *sz = as_const_int(op->args[0]);
        if (op->type == type_of<struct halide_buffer_t *>() &&
            Call::as_intrinsic(op->args[0], {Call::size_of_halide_buffer_t})) {
            stream << get_indent();
            string buf_name = unique_name('b');
            stream << "halide_buffer_t " << buf_name << ";\n";
            rhs << "&" << buf_name;
        } else if (op->type == type_of<struct halide_semaphore_t *>() &&
                   sz && *sz == 16) {
            stream << get_indent();
            string semaphore_name = unique_name('m');
            stream << "halide_semaphore_t " << semaphore_name << ";\n";
            rhs << "&" << semaphore_name;
        } else {
            // Make a stack of uint64_ts
            string size = print_expr(simplify((op->args[0] + 7) / 8));
            stream << get_indent();
            string array_name = unique_name('a');
            stream << "uint64_t " << array_name << "[" << size << "];\n";
            rhs << "(" << print_type(op->type) << ")(&" << array_name << ")";
        }
    } else if (op->is_intrinsic(Call::make_struct)) {
        if (op->args.empty()) {
            internal_assert(op->type.handle_type);
            // Add explicit cast so that different structs can't cache to the same value
            rhs << "(" << print_type(op->type) << ")(nullptr)";
        } else if (op->type == type_of<halide_dimension_t *>()) {
            // Emit a shape

            // Get the args
            vector<string> values;
            for (const auto &arg : op->args) {
                values.push_back(print_expr(arg));
            }

            static_assert(sizeof(halide_dimension_t) == 4 * sizeof(int32_t),
                          "CodeGen_C assumes a halide_dimension_t is four densely-packed int32_ts");

            internal_assert(values.size() % 4 == 0);
            int dimension = values.size() / 4;

            string shape_name = unique_name('s');
            stream
                << get_indent() << "struct halide_dimension_t " << shape_name
                << "[" << dimension << "] = {\n";
            indent++;
            for (int i = 0; i < dimension; i++) {
                stream
                    << get_indent() << "{"
                    << values[i * 4 + 0] << ", "
                    << values[i * 4 + 1] << ", "
                    << values[i * 4 + 2] << ", "
                    << values[i * 4 + 3] << "},\n";
            }
            indent--;
            stream << get_indent() << "};\n";

            rhs << shape_name;
        } else {
            // Emit a declaration like:
            // struct {int f_0, int f_1, char f_2} foo = {3, 4, 'c'};

            // Get the args
            vector<string> values;
            for (const auto &arg : op->args) {
                values.push_back(print_expr(arg));
            }
            stream << get_indent() << "struct {\n";
            // List the types.
            indent++;
            for (size_t i = 0; i < op->args.size(); i++) {
                stream << get_indent() << print_type(op->args[i].type()) << " f_" << i << ";\n";
            }
            indent--;
            string struct_name = unique_name('s');
            if (is_stack_private_to_thread()) {
                // Can't allocate the closure information on the stack; use malloc instead.
                stream << get_indent() << "} *" << struct_name << ";\n";
                stream << get_indent() << struct_name
                       << " = (decltype(" << struct_name << "))halide_malloc(_ucon, sizeof(*"
                       << struct_name << "));\n";

                create_assertion("(" + struct_name + ")", Call::make(Int(32), "halide_error_out_of_memory", {}, Call::Extern));

                // Assign the values.
                for (size_t i = 0; i < op->args.size(); i++) {
                    stream << get_indent() << struct_name << "->f_" << i << " = " << values[i] << ";\n";
                }

                // Insert destructor.
                emit_halide_free_helper(struct_name, "halide_free");

                // Return the pointer, casting to appropriate type if necessary.

                // TODO: This is dubious type-punning. We really need to
                // find a better way to do this. We dodge the problem for
                // the specific case of buffer shapes in the case above.
                if (op->type.handle_type) {
                    rhs << "(" << print_type(op->type) << ")";
                }
                rhs << struct_name;
            } else {
                stream << get_indent() << "} " << struct_name << " = {\n";
                // List the values.
                indent++;
                for (size_t i = 0; i < op->args.size(); i++) {
                    stream << get_indent() << values[i];
                    if (i < op->args.size() - 1) {
                        stream << ",";
                    }
                    stream << "\n";
                }
                indent--;
                stream << get_indent() << "};\n";

                // Return a pointer to it of the appropriate type

                // TODO: This is dubious type-punning. We really need to
                // find a better way to do this. We dodge the problem for
                // the specific case of buffer shapes in the case above.
                if (op->type.handle_type) {
                    rhs << "(" << print_type(op->type) << ")";
                }
                rhs << "(&" << struct_name << ")";
            }
        }
    } else if (op->is_intrinsic(Call::load_typed_struct_member)) {
        // Given a void * instance of a typed struct, an in-scope prototype
        // struct of the same type, and the index of a slot, load the value of
        // that slot.
        //
        // It is assumed that the slot index is valid for the given typed struct.
        //
        // TODO: this comment is replicated in CodeGen_LLVM and should be updated there too.
        // TODO: https://github.com/halide/Halide/issues/6468

        internal_assert(op->args.size() == 3);
        std::string struct_instance = print_expr(op->args[0]);
        std::string struct_prototype = print_expr(op->args[1]);
        const int64_t *index = as_const_int(op->args[2]);
        internal_assert(index != nullptr);
        rhs << "((decltype(" << struct_prototype << "))"
            << struct_instance << ")->f_" << *index;
    } else if (op->is_intrinsic(Call::get_user_context)) {
        internal_assert(op->args.empty());
        rhs << "_ucon";
    } else if (op->is_intrinsic(Call::stringify)) {
        // Rewrite to an snprintf
        vector<string> printf_args;
        string format_string = "";
        for (size_t i = 0; i < op->args.size(); i++) {
            Type t = op->args[i].type();
            if (t == type_of<halide_buffer_t *>()) {
                string buf_name = unique_name('b');
                printf_args.push_back(buf_name);
                // In Codegen_LLVM, we use 512 as a guesstimate for halide_buffer_t space:
                // Not a strict upper bound (there isn't one), but ought to be enough for most buffers.
                constexpr int buf_size = 512;
                stream << get_indent() << "char " << buf_name << "[" << buf_size << "];\n";
                stream << get_indent() << "halide_buffer_to_string(" << buf_name << ", " << buf_name << " + " << buf_size << ", " << print_expr(op->args[i]) << ");\n";
                format_string += "%s";
            } else {
                printf_args.push_back(print_expr(op->args[i]));
                if (t.is_int()) {
                    format_string += "%lld";
                    printf_args[i] = "(long long)(" + printf_args[i] + ")";
                } else if (t.is_uint()) {
                    format_string += "%llu";
                    printf_args[i] = "(long long unsigned)(" + printf_args[i] + ")";
                } else if (t.is_float()) {
                    if (t.bits() == 32) {
                        format_string += "%f";
                    } else {
                        format_string += "%e";
                    }
                } else if (op->args[i].as<StringImm>()) {
                    format_string += "%s";
                } else {
                    internal_assert(t.is_handle());
                    format_string += "%p";
                }
            }
        }
        string buf_name = unique_name('b');
        stream << get_indent() << "char " << buf_name << "[1024];\n";
        stream << get_indent() << "snprintf(" << buf_name << ", 1024, \"" << format_string << "\", " << with_commas(printf_args) << ");\n";
        rhs << buf_name;
    } else if (op->is_intrinsic(Call::register_destructor)) {
        internal_assert(op->args.size() == 2);
        const StringImm *free_fn = op->args[0].as<StringImm>();
        internal_assert(free_fn);
        string arg = print_expr(op->args[1]);
        emit_halide_free_helper(arg, free_fn->value);
        rhs << "(void *)nullptr";
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        rhs << print_expr(op->args[0]) << " / " << print_expr(op->args[1]);
    } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
        rhs << print_expr(op->args[0]) << " % " << print_expr(op->args[1]);
    } else if (op->is_intrinsic(Call::mux)) {
        rhs << print_expr(lower_mux(op));
    } else if (op->is_intrinsic(Call::signed_integer_overflow)) {
        user_error << "Signed integer overflow occurred during constant-folding. Signed"
                      " integer overflow for int32 and int64 is undefined behavior in"
                      " Halide.\n";
    } else if (op->is_intrinsic(Call::undef)) {
        user_error << "undef not eliminated before code generation. Please report this as a Halide bug.\n";
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_assert((op->args.size() == 4) && is_const_one(op->args[2]))
            << "Only prefetch of 1 cache line is supported in C backend.\n";

        const Expr &base_address = op->args[0];
        const Expr &base_offset = op->args[1];
        // const Expr &extent0 = op->args[2];  // unused
        // const Expr &stride0 = op->args[3];  // unused

        const Variable *base = base_address.as<Variable>();
        internal_assert(base && base->type.is_handle());

        // __builtin_prefetch() returns void, so use comma operator to satisfy assignment
        rhs << "(__builtin_prefetch("
            << "((" << print_type(op->type) << " *)" << print_name(base->name)
            << " + " << print_expr(base_offset) << "), /*rw*/0, /*locality*/0), 0)";
    } else if (op->is_intrinsic(Call::size_of_halide_buffer_t)) {
        rhs << "(sizeof(halide_buffer_t))";
    } else if (op->is_intrinsic(Call::strict_float)) {
        internal_assert(op->args.size() == 1);
        string arg0 = print_expr(op->args[0]);
        rhs << "(" << arg0 << ")";
    } else if (op->is_intrinsic()) {
        Expr lowered = lower_intrinsic(op);
        if (lowered.defined()) {
            rhs << print_expr(lowered);
        } else {
            // TODO: other intrinsics
            internal_error << "Unhandled intrinsic in C backend: " << op->name << "\n";
        }
    } else {
        // Generic extern calls
        rhs << print_extern_call(op);
    }

    // Special-case halide_print, which has IR that returns int, but really return void.
    // The clean thing to do would be to change the definition of halide_print() to return
    // an ignored int, but as halide_print() has many overrides downstream (and in third-party
    // consumers), this is arguably a simpler fix for allowing halide_print() to work in the C++ backend.
    if (op->name == "halide_print") {
        stream << get_indent() << rhs.str() << ";\n";
        // Make an innocuous assignment value for our caller (probably an Evaluate node) to ignore.
        print_assignment(op->type, "0");
    } else {
        print_assignment(op->type, rhs.str());
    }
}

string CodeGen_C::print_scalarized_expr(const Expr &e) {
    Type t = e.type();
    internal_assert(t.is_vector());
    string v = unique_name('_');
    // All of the lanes of this vector will get replaced, so in theory
    // we don't need to initialize it to anything, but if we don't,
    // we'll get "possible uninitialized var" warnings. Since this code
    // is already hopelessly inefficient at this point, let's just init
    // it with a broadcast(0) to avoid any possible weirdness.
    stream << get_indent() << print_type(t, AppendSpace) << v << " = " << print_type(t) + "_ops::broadcast(0);\n";
    for (int lane = 0; lane < t.lanes(); lane++) {
        Expr e2 = extract_lane(e, lane);
        string elem = print_expr(e2);
        ostringstream rhs;
        rhs << print_type(t) + "_ops::replace(" << v << ", " << lane << ", " << elem << ")";
        v = print_assignment(t, rhs.str());
    }
    return v;
}

string CodeGen_C::print_extern_call(const Call *op) {
    if (op->type.is_vector()) {
        // Need to split into multiple scalar calls.
        return print_scalarized_expr(op);
    }
    ostringstream rhs;
    vector<string> args(op->args.size());
    for (size_t i = 0; i < op->args.size(); i++) {
        args[i] = print_expr(op->args[i]);
        // This substitution ensures const correctness for all calls
        if (args[i] == "__user_context") {
            args[i] = "_ucon";
        }
    }
    if (function_takes_user_context(op->name)) {
        args.insert(args.begin(), "_ucon");
    }
    rhs << op->name << "(" << with_commas(args) << ")";
    return rhs.str();
}

void CodeGen_C::visit(const Load *op) {
    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.
    ostringstream rhs;

    Type t = op->type;
    string name = print_name(op->name);

    // If we're loading a contiguous ramp into a vector, just load the vector
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    if (dense_ramp_base.defined() && is_const_one(op->predicate)) {
        internal_assert(t.is_vector());
        string id_ramp_base = print_expr(dense_ramp_base);
        rhs << print_type(t) + "_ops::load(" << name << ", " << id_ramp_base << ")";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, gather vector elements.
        internal_assert(t.is_vector());
        string id_index = print_expr(op->index);
        if (is_const_one(op->predicate)) {
            rhs << print_type(t) + "_ops::load_gather(" << name << ", " << id_index << ")";
        } else {
            string id_predicate = print_expr(op->predicate);
            rhs << print_type(t) + "_ops::load_predicated(" << name << ", " << id_index << ", " << id_predicate << ")";
        }
    } else {
        user_assert(is_const_one(op->predicate)) << "Predicated scalar load is not supported by C backend.\n";

        string id_index = print_expr(op->index);
        bool type_cast_needed = !(allocations.contains(op->name) &&
                                  allocations.get(op->name).type.element_of() == t.element_of());
        if (type_cast_needed) {
            const char *const_flag = output_kind == CPlusPlusImplementation ? " const" : "";
            rhs << "((" << print_type(t.element_of()) << const_flag << " *)" << name << ")";
        } else {
            rhs << name;
        }
        rhs << "[" << id_index << "]";
    }
    print_assignment(t, rhs.str());
}

void CodeGen_C::visit(const Store *op) {
    Type t = op->value.type();

    if (inside_atomic_mutex_node) {
        user_assert(t.is_scalar())
            << "The vectorized atomic operation for the store" << op->name
            << " is lowered into a mutex lock, which does not support vectorization.\n";
    }

    // Issue atomic store if we are in the designated producer.
    if (emit_atomic_stores) {
        stream << "#if defined(_OPENMP)\n";
        stream << "#pragma omp atomic\n";
        stream << "#else\n";
        stream << "#error \"Atomic stores in the C backend are only supported in compilers that support OpenMP.\"\n";
        stream << "#endif\n";
    }

    string id_value = print_expr(op->value);
    string name = print_name(op->name);

    // TODO: We could replicate the logic in the llvm codegen which decides whether
    // the vector access can be aligned. Doing so would also require introducing
    // aligned type equivalents for all the vector types.

    // If we're writing a contiguous ramp, just store the vector.
    Expr dense_ramp_base = strided_ramp_base(op->index, 1);
    if (dense_ramp_base.defined() && is_const_one(op->predicate)) {
        internal_assert(op->value.type().is_vector());
        string id_ramp_base = print_expr(dense_ramp_base);
        stream << get_indent() << print_type(t) + "_ops::store(" << id_value << ", " << name << ", " << id_ramp_base << ");\n";
    } else if (op->index.type().is_vector()) {
        // If index is a vector, scatter vector elements.
        internal_assert(t.is_vector());
        string id_index = print_expr(op->index);
        if (is_const_one(op->predicate)) {
            stream << get_indent() << print_type(t) + "_ops::store_scatter(" << id_value << ", " << name << ", " << id_index << ");\n";
        } else {
            string id_predicate = print_expr(op->predicate);
            stream << get_indent() << print_type(t) + "_ops::store_predicated(" << id_value << ", " << name << ", " << id_index << ", " << id_predicate << ");\n";
        }
    } else {
        user_assert(is_const_one(op->predicate)) << "Predicated scalar store is not supported by C backend.\n";

        bool type_cast_needed =
            t.is_handle() ||
            !allocations.contains(op->name) ||
            allocations.get(op->name).type != t;

        string id_index = print_expr(op->index);
        stream << get_indent();
        if (type_cast_needed) {
            stream << "((" << print_type(t) << " *)" << name << ")";
        } else {
            stream << name;
        }
        stream << "[" << id_index << "] = " << id_value << ";\n";
    }
    cache.clear();
}

void CodeGen_C::visit(const Let *op) {
    string id_value = print_expr(op->value);
    Expr body = op->body;
    if (op->value.type().is_handle() && op->name != "__user_context") {
        // The body might contain a Load that references this directly
        // by name, so we can't rewrite the name.
        std::string name = print_name(op->name);
        stream << get_indent() << "auto "
               << name << " = " << id_value << ";\n";
        stream << get_indent() << "halide_maybe_unused(" << name << ");\n";
    } else {
        Expr new_var = Variable::make(op->value.type(), id_value);
        body = substitute(op->name, new_var, body);
    }
    print_expr(body);
}

void CodeGen_C::visit(const Select *op) {
    ostringstream rhs;
    string type = print_type(op->type);
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);

    // clang doesn't support the ternary operator on OpenCL style vectors.
    // See: https://bugs.llvm.org/show_bug.cgi?id=33103
    if (op->condition.type().is_scalar()) {
        rhs << "(" << type << ")"
            << "(" << cond
            << " ? " << true_val
            << " : " << false_val
            << ")";
    } else {
        rhs << type << "_ops::select(" << cond << ", " << true_val << ", " << false_val << ")";
    }
    print_assignment(op->type, rhs.str());
}

Expr CodeGen_C::scalarize_vector_reduce(const VectorReduce *op) {
    Expr (*binop)(Expr, Expr) = nullptr;
    switch (op->op) {
    case VectorReduce::Add:
        binop = Add::make;
        break;
    case VectorReduce::Mul:
        binop = Mul::make;
        break;
    case VectorReduce::Min:
        binop = Min::make;
        break;
    case VectorReduce::Max:
        binop = Max::make;
        break;
    case VectorReduce::And:
        binop = And::make;
        break;
    case VectorReduce::Or:
        binop = Or::make;
        break;
    case VectorReduce::SaturatingAdd:
        binop = Halide::saturating_add;
        break;
    }

    std::vector<Expr> lanes;
    int outer_lanes = op->type.lanes();
    int inner_lanes = op->value.type().lanes() / outer_lanes;
    for (int outer = 0; outer < outer_lanes; outer++) {
        Expr reduction = extract_lane(op->value, outer * inner_lanes);
        for (int inner = 1; inner < inner_lanes; inner++) {
            reduction = binop(reduction, extract_lane(op->value, outer * inner_lanes + inner));
        }
        lanes.push_back(reduction);
    }

    // No need to concat if there is only a single value.
    if (lanes.size() == 1) {
        return lanes[0];
    }

    return Shuffle::make_concat(lanes);
}

void CodeGen_C::visit(const VectorReduce *op) {
    stream << get_indent() << "// Vector reduce: " << op->op << "\n";

    Expr scalarized = scalarize_vector_reduce(op);
    if (scalarized.type().is_scalar()) {
        print_assignment(op->type, print_expr(scalarized));
    } else {
        print_assignment(op->type, print_scalarized_expr(scalarized));
    }
}

void CodeGen_C::visit(const LetStmt *op) {
    string id_value = print_expr(op->value);
    Stmt body = op->body;

    if (op->value.type().is_handle() && op->name != "__user_context") {
        // The body might contain a Load or Store that references this
        // directly by name, so we can't rewrite the name.
        std::string name = print_name(op->name);
        stream << get_indent() << "auto "
               << name << " = " << id_value << ";\n";
        stream << get_indent() << "halide_maybe_unused(" << name << ");\n";
    } else {
        Expr new_var = Variable::make(op->value.type(), id_value);
        body = substitute(op->name, new_var, body);
    }
    body.accept(this);
}

// Halide asserts have different semantics to C asserts.  They're
// supposed to clean up and make the containing function return
// -1, so we can't use the C version of assert. Instead we convert
// to an if statement.
void CodeGen_C::create_assertion(const string &id_cond, const Expr &message) {
    internal_assert(!message.defined() || message.type() == Int(32))
        << "Assertion result is not an int: " << message;

    if (target.has_feature(Target::NoAsserts)) {
        stream << get_indent() << "halide_maybe_unused(" << id_cond << ");\n";
        return;
    }

    stream << get_indent() << "if (!" << id_cond << ")\n";
    open_scope();
    string id_msg = print_expr(message);
    stream << get_indent() << "return " << id_msg << ";\n";
    close_scope("");
}

void CodeGen_C::create_assertion(const Expr &cond, const Expr &message) {
    create_assertion(print_expr(cond), message);
}

void CodeGen_C::visit(const AssertStmt *op) {
    create_assertion(op->condition, op->message);
}

void CodeGen_C::visit(const ProducerConsumer *op) {
    stream << get_indent();
    if (op->is_producer) {
        stream << "// produce " << op->name << "\n";
    } else {
        stream << "// consume " << op->name << "\n";
    }
    print_stmt(op->body);
}

void CodeGen_C::visit(const Fork *op) {
    // TODO: This doesn't actually work with nested tasks
    stream << get_indent() << "#pragma omp parallel\n";
    open_scope();
    stream << get_indent() << "#pragma omp single\n";
    open_scope();
    stream << get_indent() << "#pragma omp task\n";
    open_scope();
    print_stmt(op->first);
    close_scope("");
    stream << get_indent() << "#pragma omp task\n";
    open_scope();
    print_stmt(op->rest);
    close_scope("");
    stream << get_indent() << "#pragma omp taskwait\n";
    close_scope("");
    close_scope("");
}

void CodeGen_C::visit(const Acquire *op) {
    string id_sem = print_expr(op->semaphore);
    string id_count = print_expr(op->count);
    open_scope();
    stream << get_indent() << "while (!halide_semaphore_try_acquire(" << id_sem << ", " << id_count << "))\n";
    open_scope();
    stream << get_indent() << "#pragma omp taskyield\n";
    close_scope("");
    op->body.accept(this);
    close_scope("");
}

void CodeGen_C::visit(const Atomic *op) {
    if (!op->mutex_name.empty()) {
        internal_assert(!inside_atomic_mutex_node)
            << "Nested atomic mutex locks detected. This might causes a deadlock.\n";
        ScopedValue<bool> old_inside_atomic_mutex_node(inside_atomic_mutex_node, true);
        op->body.accept(this);
    } else {
        // Issue atomic stores.
        ScopedValue<bool> old_emit_atomic_stores(emit_atomic_stores, true);
        op->body.accept(this);
    }
}

void CodeGen_C::visit(const For *op) {
    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    if (op->for_type == ForType::Parallel) {
        stream << get_indent() << "#pragma omp parallel for\n";
    } else {
        internal_assert(op->for_type == ForType::Serial)
            << "Can only emit serial or parallel for loops to C\n";
    }

    stream << get_indent() << "for (int "
           << print_name(op->name)
           << " = " << id_min
           << "; "
           << print_name(op->name)
           << " < " << id_min
           << " + " << id_extent
           << "; "
           << print_name(op->name)
           << "++)\n";

    open_scope();
    op->body.accept(this);
    close_scope("for " + print_name(op->name));
}

void CodeGen_C::visit(const Ramp *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string id_base = print_expr(op->base);
    string id_stride = print_expr(op->stride);
    print_assignment(vector_type, print_type(vector_type) + "_ops::ramp(" + id_base + ", " + id_stride + ")");
}

void CodeGen_C::visit(const Broadcast *op) {
    Type vector_type = op->type.with_lanes(op->lanes);
    string id_value = print_expr(op->value);
    string rhs;
    if (op->lanes > 1) {
        rhs = print_type(vector_type) + "_ops::broadcast(" + id_value + ")";
    } else {
        rhs = id_value;
    }

    print_assignment(vector_type, rhs);
}

void CodeGen_C::visit(const Provide *op) {
    internal_error << "Cannot emit Provide statements as C\n";
}

void CodeGen_C::visit(const Allocate *op) {
    open_scope();

    string op_name = print_name(op->name);
    string op_type = print_type(op->type, AppendSpace);

    // For sizes less than 8k, do a stack allocation
    bool on_stack = false;
    int32_t constant_size;
    string size_id;
    Type size_id_type;

    if (op->new_expr.defined()) {
        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);
        heap_allocations.push(op->name);
        string new_e = print_expr(op->new_expr);
        stream << get_indent() << op_type << " *" << op_name << " = (" << op_type << "*)" << new_e << ";\n";
    } else {
        constant_size = op->constant_allocation_size();
        if (constant_size > 0) {
            int64_t stack_bytes = (int64_t)constant_size * op->type.bytes();

            if (stack_bytes > ((int64_t(1) << 31) - 1)) {
                user_error << "Total size for allocation "
                           << op->name << " is constant but exceeds 2^31 - 1.\n";
            } else {
                size_id_type = Int(32);
                size_id = print_expr(make_const(size_id_type, constant_size));

                if (op->memory_type == MemoryType::Stack ||
                    op->memory_type == MemoryType::Register ||
                    (op->memory_type == MemoryType::Auto &&
                     can_allocation_fit_on_stack(stack_bytes))) {
                    on_stack = true;
                }
            }
        } else {
            // Check that the allocation is not scalar (if it were scalar
            // it would have constant size).
            internal_assert(!op->extents.empty());

            size_id = print_assignment(Int(64), print_expr(op->extents[0]));
            size_id_type = Int(64);

            for (size_t i = 1; i < op->extents.size(); i++) {
                // Make the code a little less cluttered for two-dimensional case
                string new_size_id_rhs;
                string next_extent = print_expr(op->extents[i]);
                if (i > 1) {
                    new_size_id_rhs = "(" + size_id + " > ((int64_t(1) << 31) - 1)) ? " + size_id + " : (" + size_id + " * " + next_extent + ")";
                } else {
                    new_size_id_rhs = size_id + " * " + next_extent;
                }
                size_id = print_assignment(Int(64), new_size_id_rhs);
            }
            stream << get_indent() << "if (("
                   << size_id << " > ((int64_t(1) << 31) - 1)) || (("
                   << size_id << " * sizeof("
                   << op_type << ")) > ((int64_t(1) << 31) - 1)))\n";
            open_scope();
            stream << get_indent();
            // TODO: call halide_error_buffer_allocation_too_large() here instead
            // TODO: call create_assertion() so that NoAssertions works
            stream << "halide_error(_ucon, "
                   << "\"32-bit signed overflow computing size of allocation " << op->name << "\\n\");\n";
            stream << get_indent() << "return -1;\n";
            close_scope("overflow test " + op->name);
        }

        // Check the condition to see if this allocation should actually be created.
        // If the allocation is on the stack, the only condition we can respect is
        // unconditional false (otherwise a non-constant-sized array declaration
        // will be generated).
        if (!on_stack || is_const_zero(op->condition)) {
            Expr conditional_size = Select::make(op->condition,
                                                 Variable::make(size_id_type, size_id),
                                                 make_const(size_id_type, 0));
            conditional_size = simplify(conditional_size);
            size_id = print_assignment(Int(64), print_expr(conditional_size));
        }

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        stream << get_indent() << op_type;

        if (on_stack) {
            stream << op_name
                   << "[" << size_id << "];\n";
        } else {
            // Shouldn't ever currently be possible to have !on_stack && size_id.empty(),
            // but reality-check in case things change in the future.
            internal_assert(!size_id.empty());
            stream << "*"
                   << op_name
                   << " = ("
                   << op_type
                   << " *)halide_malloc(_ucon, sizeof("
                   << op_type
                   << ")*" << size_id << ");\n";
            heap_allocations.push(op->name);
        }
    }

    if (!on_stack) {
        ostringstream check;
        if (is_const_zero(op->condition)) {
            // Assertion always succeeds here, since allocation is never used
            check << print_expr(const_true());
        } else {
            // Assert that the allocation worked....
            // Note that size_id can be empty if the "allocation" is via a custom_new that
            // wraps _halide_buffer_get_host(), so don't emit malformed code in that case.
            check << "(" << op_name << " != nullptr)";
            if (!size_id.empty()) {
                check << " || (" << size_id << " == 0)";
            }
            if (!is_const_one(op->condition)) {
                // ...but if the condition is false, it's OK for the new_expr to be null.
                string op_condition = print_assignment(Bool(), print_expr(op->condition));
                check << " || (!" << op_condition << ")";
            }
        }
        create_assertion("(" + check.str() + ")", Call::make(Int(32), "halide_error_out_of_memory", {}, Call::Extern));

        string free_function = op->free_function.empty() ? "halide_free" : op->free_function;
        emit_halide_free_helper(op_name, free_function);
    }

    op->body.accept(this);

    // Free the memory if it was allocated on the heap and there is no matching
    // Free node.
    print_heap_free(op->name);
    if (allocations.contains(op->name)) {
        allocations.pop(op->name);
    }

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_C::print_heap_free(const std::string &alloc_name) {
    if (heap_allocations.contains(alloc_name)) {
        stream << get_indent() << print_name(alloc_name) << "_free.free();\n";
        heap_allocations.pop(alloc_name);
    }
}

void CodeGen_C::visit(const Free *op) {
    print_heap_free(op->name);
    allocations.pop(op->name);
}

void CodeGen_C::visit(const Realize *op) {
    internal_error << "Cannot emit realize statements to C\n";
}

void CodeGen_C::visit(const Prefetch *op) {
    internal_error << "Cannot emit prefetch statements to C\n";
}

void CodeGen_C::visit(const IfThenElse *op) {
    string cond_id = print_expr(op->condition);

    stream << get_indent() << "if (" << cond_id << ")\n";
    open_scope();
    op->then_case.accept(this);
    close_scope("if " + cond_id);

    if (op->else_case.defined()) {
        stream << get_indent() << "else\n";
        open_scope();
        op->else_case.accept(this);
        close_scope("if " + cond_id + " else");
    }
}

void CodeGen_C::visit(const Evaluate *op) {
    if (is_const(op->value)) {
        return;
    }
    string id = print_expr(op->value);
    stream << get_indent() << "halide_maybe_unused(" << id << ");\n";
}

void CodeGen_C::visit(const Shuffle *op) {
    internal_assert(!op->vectors.empty());
    for (size_t i = 1; i < op->vectors.size(); i++) {
        internal_assert(op->vectors[0].type() == op->vectors[i].type());
    }
    internal_assert(op->type.lanes() == (int)op->indices.size());
    const int max_index = (int)(op->vectors[0].type().lanes() * op->vectors.size());
    for (int i : op->indices) {
        internal_assert(i >= -1 && i < max_index);
    }

    std::vector<string> vecs;
    for (const Expr &v : op->vectors) {
        vecs.push_back(print_expr(v));
    }
    ostringstream rhs;
    if (op->type.is_scalar()) {
        // Deduce which vector we need. Apparently it's not required
        // that all vectors have identical lanes, so a loop is required.
        // Since idx of -1 means "don't care", we'll treat it as 0 to simplify.
        int idx = std::max(0, op->indices[0]);
        for (size_t vec_idx = 0; vec_idx < op->vectors.size(); vec_idx++) {
            const int vec_lanes = op->vectors[vec_idx].type().lanes();
            if (idx < vec_lanes) {
                rhs << vecs[vec_idx];
                if (op->vectors[vec_idx].type().is_vector()) {
                    rhs << "[" << idx << "]";
                }
                break;
            }
            idx -= vec_lanes;
        }
        internal_assert(!rhs.str().empty());
    } else {
        internal_assert(op->vectors[0].type().is_vector());
        string src = vecs[0];
        if (op->vectors.size() > 1) {
            // This code has always assumed/required that all the vectors
            // have identical types, so let's verify
            const Type t0 = op->vectors[0].type();
            for (const auto &v : op->vectors) {
                internal_assert(t0 == v.type());
            }
            ostringstream rhs;
            string storage_name = unique_name('_');
            // Combine them into one vector. Clang emits excellent code via this
            // union approach (typically without going thru memory) for both x64 and arm64.
            stream << get_indent() << "union { "
                   << print_type(t0) << " src[" << vecs.size() << "]; "
                   << print_type(op->type) << " dst; } "
                   << storage_name << " = {{ " << with_commas(vecs) << " }};\n";
            src = storage_name + ".dst";
        }
        rhs << print_type(op->type) << "_ops::shuffle<" << with_commas(op->indices) << ">(" << src << ")";
    }
    print_assignment(op->type, rhs.str());
}

void CodeGen_C::test() {
    LoweredArgument buffer_arg("buf", Argument::OutputBuffer, Int(32), 3, ArgumentEstimates{});
    LoweredArgument float_arg("alpha", Argument::InputScalar, Float(32), 0, ArgumentEstimates{});
    LoweredArgument int_arg("beta", Argument::InputScalar, Int(32), 0, ArgumentEstimates{});
    LoweredArgument user_context_arg("__user_context", Argument::InputScalar, type_of<const void *>(), 0, ArgumentEstimates{});
    vector<LoweredArgument> args = {buffer_arg, float_arg, int_arg, user_context_arg};
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Select::make(alpha > 4.0f, print_when(x < 1, 3), 2);
    Stmt s = Store::make("buf", e, x, Parameter(), const_true(), ModulusRemainder());
    s = LetStmt::make("x", beta + 1, s);
    s = Block::make(s, Free::make("tmp.stack"));
    s = Allocate::make("tmp.stack", Int(32), MemoryType::Stack, {127}, const_true(), s);
    s = Allocate::make("tmp.heap", Int(32), MemoryType::Heap, {43, beta}, const_true(), s);
    Expr buf = Variable::make(Handle(), "buf.buffer");
    s = LetStmt::make("buf", Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern), s);

    Module m("", get_host_target());
    m.append(LoweredFunc("test1", args, s, LinkageType::External));

    ostringstream source;
    {
        CodeGen_C cg(source, Target("host"), CodeGen_C::CImplementation);
        cg.compile(m);
    }

    string correct_source =
        string((const char *)halide_c_template_CodeGen_C_prologue) + '\n' +
        string((const char *)halide_internal_runtime_header_HalideRuntime_h) + '\n' +
        string((const char *)halide_internal_initmod_inlined_c) + '\n' +
        '\n' + kDefineMustUseResult + R"GOLDEN_CODE(
#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif



#ifdef __cplusplus
extern "C" {
#endif

HALIDE_FUNCTION_ATTRS
int test1(struct halide_buffer_t *_buf_buffer, float _alpha, int32_t _beta, void const *__user_context) {
 void * const _ucon = const_cast<void *>(__user_context);
 halide_maybe_unused(_ucon);
 auto *_0 = _halide_buffer_get_host(_buf_buffer);
 auto _buf = _0;
 halide_maybe_unused(_buf);
 {
  int64_t _1 = 43;
  int64_t _2 = _1 * _beta;
  if ((_2 > ((int64_t(1) << 31) - 1)) || ((_2 * sizeof(int32_t )) > ((int64_t(1) << 31) - 1)))
  {
   halide_error(_ucon, "32-bit signed overflow computing size of allocation tmp.heap\n");
   return -1;
  } // overflow test tmp.heap
  int64_t _3 = _2;
  int32_t *_tmp_heap = (int32_t  *)halide_malloc(_ucon, sizeof(int32_t )*_3);
  if (!((_tmp_heap != nullptr) || (_3 == 0)))
  {
   int32_t _4 = halide_error_out_of_memory(_ucon);
   return _4;
  }
  HalideFreeHelper<halide_free> _tmp_heap_free(_ucon, _tmp_heap);
  {
   int32_t _tmp_stack[127];
   int32_t _5 = _beta + 1;
   int32_t _6;
   bool _7 = _5 < 1;
   if (_7)
   {
    char b0[1024];
    snprintf(b0, 1024, "%lld%s", (long long)(3), "\n");
    auto *_8 = b0;
    halide_print(_ucon, _8);
    int32_t _9 = 0;
    int32_t _10 = return_second(_9, 3);
    _6 = _10;
   } // if _7
   else
   {
    _6 = 3;
   } // if _7 else
   int32_t _11 = _6;
   float _12 = float_from_bits(1082130432 /* 4 */);
   bool _13 = _alpha > _12;
   int32_t _14 = (int32_t)(_13 ? _11 : 2);
   ((int32_t *)_buf)[_5] = _14;
  } // alloc _tmp_stack
  _tmp_heap_free.free();
 } // alloc _tmp_heap
 return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif

)GOLDEN_CODE";

    const auto compare_srcs = [](const string &actual, const string &expected) {
        if (actual != expected) {
            int diff = 0;
            while (actual[diff] == expected[diff]) {
                diff++;
            }
            int diff_end = diff + 1;
            while (diff > 0 && actual[diff] != '\n') {
                diff--;
            }
            while (diff_end < (int)actual.size() && actual[diff_end] != '\n') {
                diff_end++;
            }

            internal_error
                << "Correct source code:\n"
                << expected
                << "Actual source code:\n"
                << actual
                << "Difference starts at:\n"
                << "Correct: " << expected.substr(diff, diff_end - diff) << "\n"
                << "Actual: " << actual.substr(diff, diff_end - diff) << "\n";
        }
    };

    compare_srcs(source.str(), correct_source);

    ostringstream function_info;
    {
        CodeGen_C cg(function_info, Target("host-no_runtime"), CodeGen_C::CPlusPlusFunctionInfoHeader, "Function/Info/Test");
        cg.compile(m);
    }

    string correct_function_info = R"GOLDEN_CODE(#ifndef HALIDE_FUNCTION_INFO__Function___Info___Test
#define HALIDE_FUNCTION_INFO__Function___Info___Test

/* MACHINE GENERATED By Halide. */

#if !(__cplusplus >= 201703L || _MSVC_LANG >= 201703L)
#error "This file requires C++17 or later; please upgrade your compiler."
#endif

#include "HalideRuntime.h"


/**
 * This function returns a constexpr array of information about a Halide-generated
 * function's argument signature (e.g., number of arguments, type of each, etc).
 * While this is a subset of the information provided by the existing _metadata
 * function, it has the distinct advantage of allowing one to use the information
 * it at compile time (rather than runtime). This can be quite useful for producing
 * e.g. automatic call wrappers, etc.
 *
 * For instance, to compute the number of Buffers in a Function, one could do something
 * like:
 *
 *      using namespace HalideFunctionInfo;
 *
 *      template<size_t arg_count>
 *      constexpr size_t count_buffers(const std::array<ArgumentInfo, arg_count> args) {
 *          size_t buffer_count = 0;
 *          for (const auto a : args) {
 *              if (a.kind == InputBuffer || a.kind == OutputBuffer) {
 *                  buffer_count++;
 *              }
 *          }
 *          return buffer_count;
 *      }
 *
 *      constexpr size_t count = count_buffers(metadata_tester_argument_info());
 *
 * The value of `count` will be computed entirely at compile-time, with no runtime
 * impact aside from the numerical value of the constant.
 */

inline constexpr std::array<::HalideFunctionInfo::ArgumentInfo, 4> test1_argument_info() {
 return {{
  {"buf", ::HalideFunctionInfo::OutputBuffer, 3, halide_type_t{halide_type_int, 32, 1}},
  {"alpha", ::HalideFunctionInfo::InputScalar, 0, halide_type_t{halide_type_float, 32, 1}},
  {"beta", ::HalideFunctionInfo::InputScalar, 0, halide_type_t{halide_type_int, 32, 1}},
  {"__user_context", ::HalideFunctionInfo::InputScalar, 0, halide_type_t{halide_type_handle, 64, 1}},
 }};
}
#endif
)GOLDEN_CODE";

    compare_srcs(function_info.str(), correct_function_info);

    std::cout << "CodeGen_C test passed\n";
}

}  // namespace Internal
}  // namespace Halide
