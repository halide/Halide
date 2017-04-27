#include <iostream>
#include <limits>

#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Param.h"
#include "Var.h"
#include "Lerp.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

using std::ostream;
using std::endl;
using std::string;
using std::vector;
using std::ostringstream;
using std::map;

extern "C" unsigned char halide_internal_initmod_inlined_c[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntime_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeCuda_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeHexagonHost_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeMetal_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenCL_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenGLCompute_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeOpenGL_h[];
extern "C" unsigned char halide_internal_runtime_header_HalideRuntimeQurt_h[];

namespace {

const string headers =
    "#include <iostream>\n"
    "#include <math.h>\n"
    "#include <float.h>\n"
    "#include <assert.h>\n"
    "#include <string.h>\n"
    "#include <stdio.h>\n"
    "#include <stdint.h>\n";

const string globals =
    "extern \"C\" {\n"
    "int64_t halide_current_time_ns(void *ctx);\n"
    "void halide_profiler_pipeline_end(void *, void *);\n"
    "}\n"
    "\n"

    // We now add definitions of things in the runtime which are
    // intended to be inlined into every module but are only expressed
    // in .ll. The redundancy is regrettable (FIXME).
    "#ifdef _WIN32\n"
    "float roundf(float);\n"
    "double round(double);\n"
    "#else\n"
    "inline float asinh_f32(float x) {return asinhf(x);}\n"
    "inline float acosh_f32(float x) {return acoshf(x);}\n"
    "inline float atanh_f32(float x) {return atanhf(x);}\n"
    "inline double asinh_f64(double x) {return asinh(x);}\n"
    "inline double acosh_f64(double x) {return acosh(x);}\n"
    "inline double atanh_f64(double x) {return atanh(x);}\n"
    "#endif\n"
    "inline float sqrt_f32(float x) {return sqrtf(x);}\n"
    "inline float sin_f32(float x) {return sinf(x);}\n"
    "inline float asin_f32(float x) {return asinf(x);}\n"
    "inline float cos_f32(float x) {return cosf(x);}\n"
    "inline float acos_f32(float x) {return acosf(x);}\n"
    "inline float tan_f32(float x) {return tanf(x);}\n"
    "inline float atan_f32(float x) {return atanf(x);}\n"
    "inline float sinh_f32(float x) {return sinhf(x);}\n"
    "inline float cosh_f32(float x) {return coshf(x);}\n"
    "inline float tanh_f32(float x) {return tanhf(x);}\n"
    "inline float hypot_f32(float x, float y) {return hypotf(x, y);}\n"
    "inline float exp_f32(float x) {return expf(x);}\n"
    "inline float log_f32(float x) {return logf(x);}\n"
    "inline float pow_f32(float x, float y) {return powf(x, y);}\n"
    "inline float floor_f32(float x) {return floorf(x);}\n"
    "inline float ceil_f32(float x) {return ceilf(x);}\n"
    "inline float round_f32(float x) {return roundf(x);}\n"
    "\n"
    "inline double sqrt_f64(double x) {return sqrt(x);}\n"
    "inline double sin_f64(double x) {return sin(x);}\n"
    "inline double asin_f64(double x) {return asin(x);}\n"
    "inline double cos_f64(double x) {return cos(x);}\n"
    "inline double acos_f64(double x) {return acos(x);}\n"
    "inline double tan_f64(double x) {return tan(x);}\n"
    "inline double atan_f64(double x) {return atan(x);}\n"
    "inline double sinh_f64(double x) {return sinh(x);}\n"
    "inline double cosh_f64(double x) {return cosh(x);}\n"
    "inline double tanh_f64(double x) {return tanh(x);}\n"
    "inline double hypot_f64(double x, double y) {return hypot(x, y);}\n"
    "inline double exp_f64(double x) {return exp(x);}\n"
    "inline double log_f64(double x) {return log(x);}\n"
    "inline double pow_f64(double x, double y) {return pow(x, y);}\n"
    "inline double floor_f64(double x) {return floor(x);}\n"
    "inline double ceil_f64(double x) {return ceil(x);}\n"
    "inline double round_f64(double x) {return round(x);}\n"
    "\n"
    "inline float nan_f32() {return NAN;}\n"
    "inline float neg_inf_f32() {return -INFINITY;}\n"
    "inline float inf_f32() {return INFINITY;}\n"
    "inline bool is_nan_f32(float x) {return x != x;}\n"
    "inline bool is_nan_f64(double x) {return x != x;}\n"
    "template<typename A, typename B> A reinterpret(B b) {A a; memcpy(&a, &b, sizeof(a)); return a;}\n"
    "inline float float_from_bits(uint32_t bits) {return reinterpret<float, uint32_t>(bits);}\n"
    "\n"
    "template<typename T> T max(T a, T b) {if (a > b) return a; return b;}\n"
    "template<typename T> T min(T a, T b) {if (a < b) return a; return b;}\n"
    "\n";

}

CodeGen_C::CodeGen_C(ostream &s, Target t, OutputKind output_kind, const std::string &guard) :
    IRPrinter(s), id("$$ BAD ID $$"), target(t), output_kind(output_kind), extern_c_open(false) {

    if (is_header()) {
        // If it's a header, emit an include guard.
        stream << "#ifndef HALIDE_" << print_name(guard) << '\n'
               << "#define HALIDE_" << print_name(guard) << '\n'
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
               << "\n"
               << "// The legacy buffer type. Do not use in new code.\n"
               << "struct buffer_t;\n"
               << "\n";
        // We just forward declared the following types:
        forward_declared.insert(type_of<halide_buffer_t *>().handle_type);
        forward_declared.insert(type_of<halide_filter_metadata_t *>().handle_type);
        forward_declared.insert(type_of<buffer_t *>().handle_type);
    } else {
        // Include declarations of everything generated C source might want
        stream
            << headers
            << globals
            << halide_internal_runtime_header_HalideRuntime_h << '\n'
            << halide_internal_initmod_inlined_c << '\n';
    }

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
                   << halide_internal_runtime_header_HalideRuntime_h << '\n';
            if (target.has_feature(Target::CUDA)) {
                stream << halide_internal_runtime_header_HalideRuntimeCuda_h << '\n';
            }
            if (target.has_feature(Target::HVX_128) ||
                target.has_feature(Target::HVX_64)) {
                stream << halide_internal_runtime_header_HalideRuntimeHexagonHost_h << '\n';
            }
            if (target.has_feature(Target::Metal)) {
                stream << halide_internal_runtime_header_HalideRuntimeMetal_h << '\n';
            }
            if (target.has_feature(Target::OpenCL)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenCL_h << '\n';
            }
            if (target.has_feature(Target::OpenGLCompute)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenGLCompute_h << '\n';
            }
            if (target.has_feature(Target::OpenGL)) {
                stream << halide_internal_runtime_header_HalideRuntimeOpenGL_h << '\n';
            }
        }
        stream << "#endif\n";
    }
}

namespace {
string type_to_c_type(Type type, bool include_space, bool c_plus_plus = true) {
    bool needs_space = true;
    ostringstream oss;
    user_assert(type.lanes() == 1) << "Can't use vector types when compiling to C (yet)\n";
    if (type.is_float()) {
        if (type.bits() == 32) {
            oss << "float";
        } else if (type.bits() == 64) {
            oss << "double";
        } else {
            user_error << "Can't represent a float with this many bits in C: " << type << "\n";
        }

    } else if (type.is_handle()) {
        needs_space = false;

        // If there is no type info or is generating C (not C++) and
        // the type is a class or in an inner scope, just use void *.
        if (type.handle_type == NULL ||
            (!c_plus_plus &&
             (!type.handle_type->namespaces.empty() ||
              !type.handle_type->enclosing_types.empty() ||
              type.handle_type->inner_name.cpp_type_type == halide_cplusplus_type_name::Class))) {
            oss << "void *";
        } else {
            if (type.handle_type->inner_name.cpp_type_type ==
                halide_cplusplus_type_name::Struct) {
                oss << "struct ";
            }

            if (!type.handle_type->namespaces.empty() ||
                !type.handle_type->enclosing_types.empty()) {
                oss << "::";
                for (size_t i = 0; i < type.handle_type->namespaces.size(); i++) {
                    oss << type.handle_type->namespaces[i] << "::";
                }
                for (size_t i = 0; i < type.handle_type->enclosing_types.size(); i++) {
                    oss << type.handle_type->enclosing_types[i].name << "::";
                }
            }
            oss << type.handle_type->inner_name.name;
            if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                oss << " &";
            } else if (type.handle_type->reference_type == halide_handle_cplusplus_type::LValueReference) {
                oss << " &&";
            }
            for (auto modifier : type.handle_type->cpp_type_modifiers) {
                if (modifier & halide_handle_cplusplus_type::Const) {
                    oss << " const";
                }
                if (modifier & halide_handle_cplusplus_type::Volatile) {
                    oss << " volatile";
                }
                if (modifier & halide_handle_cplusplus_type::Restrict) {
                    oss << " restrict";
                }
                if (modifier & halide_handle_cplusplus_type::Pointer) {
                    oss << " *";
                }
            }
        }
    } else {
        switch (type.bits()) {
        case 1:
            oss << "bool";
            break;
        case 8: case 16: case 32: case 64:
            if (type.is_uint()) oss << 'u';
            oss << "int" << type.bits() << "_t";
            break;
        default:
            user_error << "Can't represent an integer with this many bits in C: " << type << "\n";
        }
    }
    if (include_space && needs_space)
        oss << " ";
    return oss.str();
}
}

void CodeGen_C::set_name_mangling_mode(NameMangling mode) {
    if (extern_c_open && mode != NameMangling::C) {
        stream << "\n#ifdef __cplusplus\n";
        stream << "}  // extern \"C\"\n";
        stream << "#endif\n";
        extern_c_open = false;
    } else if (!extern_c_open && mode == NameMangling::C) {
        stream << "#ifdef __cplusplus\n";
        stream << "extern \"C\" {\n";
        stream << "#endif\n";
        extern_c_open = true;
    }
}

string CodeGen_C::print_type(Type type, AppendSpaceIfNeeded space_option) {
    return type_to_c_type(type, space_option == AppendSpace);
}

string CodeGen_C::print_reinterpret(Type type, Expr e) {
    ostringstream oss;
    if (type.is_handle()) {
        // Use a c-style cast
        oss << "(" << print_type(type) << ")";
    } else {
        oss << "reinterpret<" << print_type(type) << ">";
    }
    oss << "(" << print_expr(e) << ")";
    return oss.str();
}

string CodeGen_C::print_name(const string &name) {
    ostringstream oss;

    // Prefix an underscore to avoid reserved words (e.g. a variable named "while")
    if (isalpha(name[0])) {
        oss << '_';
    }

    for (size_t i = 0; i < name.size(); i++) {
        if (name[i] == '.') {
            oss << '_';
        } else if (name[i] == '$') {
            oss << "__";
        } else if (name[i] != '_' && !isalnum(name[i])) {
            oss << "___";
        }
        else oss << name[i];
    }
    return oss.str();
}

namespace {
class ExternCallPrototypes : public IRGraphVisitor {
    struct NamespaceOrCall {
        const Call *call; // nullptr if this is a subnamespace
        std::map<string, NamespaceOrCall> names;
        NamespaceOrCall(const Call *call = nullptr) : call(call) { }
    };
    std::map<string, NamespaceOrCall> c_plus_plus_externs;
    std::map<string, const Call *> c_externs;
    std::set<std::string> &emitted;

    using IRGraphVisitor::visit;

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);

        if (!emitted.count(op->name)) {
            if (op->call_type == Call::Extern) {
                c_externs.insert({op->name, op});
            } else if (op->call_type == Call::ExternCPlusPlus) {
                std::vector<std::string> namespaces;
                std::string name = extract_namespaces(op->name, namespaces);
                std::map<string, NamespaceOrCall> *namespace_map(&c_plus_plus_externs);
                for (const auto &ns : namespaces) {
                    auto insertion = namespace_map->insert({ns, NamespaceOrCall()});
                    namespace_map = &insertion.first->second.names;
                }
                namespace_map->insert({name, NamespaceOrCall(op)});
            }
            emitted.insert(op->name);
        }
    }

    void emit_function_decl(ostream &stream, const Call *op, const std::string &name) {
        stream << type_to_c_type(op->type, true) << " " << name << "(";
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

    void emit_namespace_or_call(ostream &stream, const NamespaceOrCall &ns_or_call, const std::string &name) {
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
    ExternCallPrototypes(std::set<string> &emitted, bool in_c_plus_plus)
        : emitted(emitted) {
        // Make sure we don't catch calls that are already in the global declarations
        const char *strs[] = {globals.c_str(),
                              (const char *)halide_internal_runtime_header_HalideRuntime_h,
                              (const char *)halide_internal_initmod_inlined_c};
        for (const char *str : strs) {
            size_t j = 0;
            for (size_t i = 0; str[i]; i++) {
                char c = str[i];
                if (c == '(' && i > j+1) {
                    // Could be the end of a function_name.
                    string name(str + j + 1, i-j-1);
                    emitted.insert(name);
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

    bool has_c_declarations() {
        return !c_externs.empty();
    }

    bool has_c_plus_plus_declarations() {
        return !c_plus_plus_externs.empty();
    }

    void emit_c_declarations(ostream &stream) {
        for (const auto &call : c_externs) {
            emit_function_decl(stream, call.second, call.first);
        }
        stream << "\n";
    }

    void emit_c_plus_plus_declarations(ostream &stream) {
        for (const auto &ns_or_call : c_plus_plus_externs) {
            emit_namespace_or_call(stream, ns_or_call.second, ns_or_call.first);
        }
        stream << "\n";
    }
};
}

void CodeGen_C::compile(const Module &input) {
    for (const auto &b : input.buffers()) {
        compile(b);
    }
    for (const auto &f : input.functions()) {
        compile(f);
    }
}

void CodeGen_C::compile(const LoweredFunc &f) {
    // Don't put non-external function declarations in headers.
    if (is_header() && f.linkage == LoweredFunc::Internal) {
        return;
    }

    emitted.insert(f.name);

    const std::vector<LoweredArgument> &args = f.args;

    for (size_t i = 0; i < args.size(); i++) {
        auto handle_type = args[i].type.handle_type;
        if (!handle_type) continue;
        if (forward_declared.count(handle_type)) continue;
        auto type_type = handle_type->inner_name.cpp_type_type;
        for (size_t ns = 0; ns < handle_type->namespaces.size(); ns++ ) {
            stream << "namespace " << handle_type->namespaces[ns] << " {\n";
        }
        if (type_type == halide_cplusplus_type_name::Struct) {
            stream << "struct " << handle_type->inner_name.name << ";\n";
        } else if (type_type == halide_cplusplus_type_name::Class) {
            stream << "class " << handle_type->inner_name.name << ";\n";
        } else if (type_type == halide_cplusplus_type_name::Union) {
            stream << "union " << handle_type->inner_name.name << ";\n";
        } else if (type_type == halide_cplusplus_type_name::Enum) {
            internal_error << "Passing pointers to enums is unsupported\n";
        }
        for (size_t ns = 0; ns < handle_type->namespaces.size(); ns++ ) {
            stream << "}\n";
        }
        forward_declared.insert(handle_type);
    }

    have_user_context = false;
    for (size_t i = 0; i < args.size(); i++) {
        // TODO: check that its type is void *?
        have_user_context |= (args[i].name == "__user_context");
    }

    // Emit prototypes for any extern calls used.
    if (!is_header()) {
        stream << "\n";
        ExternCallPrototypes e(emitted, is_c_plus_plus_interface());
        f.body.accept(&e);

        if (e.has_c_plus_plus_declarations()) {
            set_name_mangling_mode(NameMangling::CPlusPlus);
            e.emit_c_plus_plus_declarations(stream);
        }

        if (e.has_c_declarations()) {
            set_name_mangling_mode(NameMangling::C);
            e.emit_c_declarations(stream);
        }
    }

    NameMangling name_mangling = f.name_mangling;
    if (name_mangling == NameMangling::Default) {
        name_mangling = (target.has_feature(Target::CPlusPlusMangling) ?
                         NameMangling::CPlusPlus : NameMangling::C);
    }

    set_name_mangling_mode(name_mangling);
    stream << "\n";

    std::vector<std::string> namespaces;
    std::string simple_name = extract_namespaces(f.name, namespaces);
    if (!is_c_plus_plus_interface()) {
        user_assert(namespaces.empty()) <<
            "Namespace qualifiers not allowed on function name if not compiling with Target::CPlusPlusNameMangling.\n";
    }

    if (!namespaces.empty()) {
        const char *separator = "";
        for (const auto &ns : namespaces) {
            stream << separator << "namespace " << ns << " {";
            separator = " ";
        }
        stream << "\n\n";
    }

    // Emit the function prototype
    if (f.linkage == LoweredFunc::Internal) {
        // If the function isn't public, mark it static.
        stream << "static ";
    }
    stream << "int " << simple_name << "(";
    for (size_t i = 0; i < args.size(); i++) {
        if (args[i].is_buffer()) {
            stream << "struct halide_buffer_t *"
                   << print_name(args[i].name)
                   << "_buffer";
        } else {
            stream << print_type(args[i].type, AppendSpace)
                   << print_name(args[i].name);
        }

        if (i < args.size()-1) stream << ", ";
    }

    if (is_header()) {
        stream << ") HALIDE_FUNCTION_ATTRS;\n";
    } else {
        stream << ") HALIDE_FUNCTION_ATTRS {\n";
        indent += 1;

        // Emit the body
        print(f.body);

        // Return success.
        do_indent();
        stream << "return 0;\n";

        indent -= 1;
        stream << "}\n";
    }

    if (is_header() && f.linkage == LoweredFunc::ExternalPlusMetadata) {
        // Emit the argv version
        stream << "int " << simple_name << "_argv(void **args) HALIDE_FUNCTION_ATTRS;\n";

        // And also the metadata.
        stream << "const struct halide_filter_metadata_t *" << simple_name << "_metadata() HALIDE_FUNCTION_ATTRS;\n";
    }

    if (!namespaces.empty()) {
        stream << "\n";
        for (size_t i = 0; i < namespaces.size(); i++) {
            stream << "}";
        }
        stream << " // Close namespaces ";
        const char *separator = "";
        for (const auto &ns : namespaces) {
            stream << separator << ns;
            separator = "::";
        }

        stream << "\n\n";
    }
}

void CodeGen_C::compile(const Buffer<> &buffer) {
    // Don't define buffers in headers.
    if (is_header()) {
        return;
    }

    string name = print_name(buffer.name());
    halide_buffer_t b = *(buffer.raw_buffer());

    user_assert(b.host) << "Can't embed image: " << buffer.name() << " because it has a null host pointer\n";
    user_assert(!b.device_dirty()) << "Can't embed image: " << buffer.name() << "because it has a dirty device pointer\n";

    // Figure out the offset of the last pixel.
    size_t num_elems = 1;
    for (int d = 0; b.dim[d].extent; d++) {
        num_elems += b.dim[d].stride * (b.dim[d].extent - 1);
    }

    // Emit the data
    stream << "static uint8_t " << name << "_data[] __attribute__ ((aligned (32))) = {";
    for (size_t i = 0; i < num_elems * b.type.bytes(); i++) {
        if (i > 0) stream << ", ";
        stream << (int)(b.host[i]);
    }
    stream << "};\n";

    // Emit the shape
    stream << "static halide_dimension_t " << name << "_buffer_shape[] = {";
    for (int i = 0; i < buffer.dimensions(); i++) {
        stream << "{" << buffer.dim(i).min()
               << ", " << buffer.dim(i).extent()
               << ", " << buffer.dim(i).stride() << "}";
        if (i < buffer.dimensions() - 1) {
            stream << ", ";
        }
    }
    stream << "};\n";

    Type t = buffer.type();

    // Emit the buffer struct
    stream << "static halide_buffer_t " << name << "_buffer = {"
           << "0, "             // device
           << "NULL, "          // device_interface
           << "&" << name << "_data[0], " // host
           << "0, "             // flags
           << "{(halide_type_code_t)(" << (int)t.code() << "), " << t.bits() << ", " << t.lanes() << "}, "
           << buffer.dimensions() << ", "
           << name << "_buffer_shape};\n";

    // Make a global pointer to it
    stream << "static halide_buffer_t *" << name << " = &" << name << "_buffer;\n";
}

string CodeGen_C::print_expr(Expr e) {
    id = "$$ BAD ID $$";
    e.accept(this);
    return id;
}

void CodeGen_C::print_stmt(Stmt s) {
    s.accept(this);
}

string CodeGen_C::print_assignment(Type t, const std::string &rhs) {

    map<string, string>::iterator cached = cache.find(rhs);

    if (cached == cache.end()) {
        id = unique_name('_');
        do_indent();
        stream << print_type(t, AppendSpace) << id << " = " << rhs << ";\n";
        cache[rhs] = id;
    } else {
        id = cached->second;
    }
    return id;
}

void CodeGen_C::open_scope() {
    cache.clear();
    do_indent();
    indent++;
    stream << "{\n";
}

void CodeGen_C::close_scope(const std::string &comment) {
    cache.clear();
    indent--;
    do_indent();
    if (!comment.empty()) {
        stream << "} // " << comment << "\n";
    } else {
        stream << "}\n";
    }
}

void CodeGen_C::visit(const Variable *op) {
    id = print_name(op->name);
}

void CodeGen_C::visit(const Cast *op) {
    print_assignment(op->type, "(" + print_type(op->type) + ")(" + print_expr(op->value) + ")");
}

void CodeGen_C::visit_binop(Type t, Expr a, Expr b, const char * op) {
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
        ostringstream oss;
        oss << print_expr(op->a) << " >> " << bits;
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_div(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "/");
    }
}

void CodeGen_C::visit(const Mod *op) {
    int bits;
    if (is_const_power_of_two_integer(op->b, &bits)) {
        ostringstream oss;
        oss << print_expr(op->a) << " & " << ((1 << bits)-1);
        print_assignment(op->type, oss.str());
    } else if (op->type.is_int()) {
        print_expr(lower_euclidean_mod(op->a, op->b));
    } else {
        visit_binop(op->type, op->a, op->b, "%");
    }
}

void CodeGen_C::visit(const Max *op) {
    print_expr(Call::make(op->type, "max", {op->a, op->b}, Call::Extern));
}

void CodeGen_C::visit(const Min *op) {
    print_expr(Call::make(op->type, "min", {op->a, op->b}, Call::Extern));
}

void CodeGen_C::visit(const EQ *op) {
    visit_binop(op->type, op->a, op->b, "==");
}

void CodeGen_C::visit(const NE *op) {
    visit_binop(op->type, op->a, op->b, "!=");
}

void CodeGen_C::visit(const LT *op) {
    visit_binop(op->type, op->a, op->b, "<");
}

void CodeGen_C::visit(const LE *op) {
    visit_binop(op->type, op->a, op->b, "<=");
}

void CodeGen_C::visit(const GT *op) {
    visit_binop(op->type, op->a, op->b, ">");
}

void CodeGen_C::visit(const GE *op) {
    visit_binop(op->type, op->a, op->b, ">=");
}

void CodeGen_C::visit(const Or *op) {
    visit_binop(op->type, op->a, op->b, "||");
}

void CodeGen_C::visit(const And *op) {
    visit_binop(op->type, op->a, op->b, "&&");
}

void CodeGen_C::visit(const Not *op) {
    print_assignment(op->type, "!(" + print_expr(op->a) + ")");
}

void CodeGen_C::visit(const IntImm *op) {
    if (op->type == Int(32)) {
        id = std::to_string(op->value);
    } else {
        print_assignment(op->type, "(" + print_type(op->type) + ")(" + std::to_string(op->value) + ")");
    }
}

void CodeGen_C::visit(const UIntImm *op) {
    print_assignment(op->type, "(" + print_type(op->type) + ")(" + std::to_string(op->value) + ")");
}

void CodeGen_C::visit(const StringImm *op) {
    ostringstream oss;
    oss << Expr(op);
    id = oss.str();
}

// NaN is the only float/double for which this is true... and
// surprisingly, there doesn't seem to be a portable isnan function
// (dsharlet).
template <typename T>
static bool isnan(T x) { return x != x; }

template <typename T>
static bool isinf(T x)
{
    return std::numeric_limits<T>::has_infinity && (
        x == std::numeric_limits<T>::infinity() ||
        x == -std::numeric_limits<T>::infinity());
}

void CodeGen_C::visit(const FloatImm *op) {
    if (isnan(op->value)) {
        id = "nan_f32()";
    } else if (isinf(op->value)) {
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
        oss << "float_from_bits(" << u.as_uint << " /* " << u.as_float << " */)";
        id = oss.str();
    }
}

void CodeGen_C::visit(const Call *op) {

    internal_assert(op->call_type == Call::Extern ||
                    op->call_type == Call::ExternCPlusPlus ||
                    op->call_type == Call::PureExtern ||
                    op->call_type == Call::Intrinsic ||
                    op->call_type == Call::PureIntrinsic)
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

        rhs << "halide_debug_to_file(";
        rhs << (have_user_context ? "__user_context_" : "nullptr");
        rhs << ", \"" + filename + "\", " + typecode;
        rhs << ", (struct halide_buffer_t *)" << buffer;
        rhs << ")";
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
    } else if (op->is_intrinsic(Call::reinterpret)) {
        internal_assert(op->args.size() == 1);
        rhs << print_reinterpret(op->type, op->args[0]);
    } else if (op->is_intrinsic(Call::shift_left)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " << " << a1;
    } else if (op->is_intrinsic(Call::shift_right)) {
        internal_assert(op->args.size() == 2);
        string a0 = print_expr(op->args[0]);
        string a1 = print_expr(op->args[1]);
        rhs << a0 << " >> " << a1;
    } else if (op->is_intrinsic(Call::lerp)) {
        internal_assert(op->args.size() == 3);
        Expr e = lower_lerp(op->args[0], op->args[1], op->args[2]);
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::absd)) {
        internal_assert(op->args.size() == 2);
        Expr a = op->args[0];
        Expr b = op->args[1];
        Expr e = select(a < b, b - a, a - b);
        rhs << print_expr(e);
    } else if (op->is_intrinsic(Call::return_second)) {
        internal_assert(op->args.size() == 2);
        string arg0 = print_expr(op->args[0]);
        string arg1 = print_expr(op->args[1]);
        rhs << "(" << arg0 << ", " << arg1 << ")";
    } else if (op->is_intrinsic(Call::if_then_else)) {
        internal_assert(op->args.size() == 3);

        string result_id = unique_name('_');

        do_indent();
        stream << print_type(op->args[1].type(), AppendSpace)
               << result_id << ";\n";

        string cond_id = print_expr(op->args[0]);

        do_indent();
        stream << "if (" << cond_id << ")\n";
        open_scope();
        string true_case = print_expr(op->args[1]);
        do_indent();
        stream << result_id << " = " << true_case << ";\n";
        close_scope("if " + cond_id);
        do_indent();
        stream << "else\n";
        open_scope();
        string false_case = print_expr(op->args[2]);
        do_indent();
        stream << result_id << " = " << false_case << ";\n";
        close_scope("if " + cond_id + " else");

        rhs << result_id;
    } else if (op->is_intrinsic(Call::abs)) {
        internal_assert(op->args.size() == 1);
        Expr a0 = op->args[0];
        rhs << print_expr(cast(op->type, select(a0 > 0, a0, -a0)));
    } else if (op->is_intrinsic(Call::memoize_expr)) {
        internal_assert(op->args.size() >= 1);
        string arg = print_expr(op->args[0]);
        rhs << "(" << arg << ")";
    } else if (op->is_intrinsic(Call::alloca)) {
        internal_assert(op->args.size() == 1);
        internal_assert(op->type.is_handle());
        const Call *call = op->args[0].as<Call>();
        if (op->type == type_of<struct halide_buffer_t *>() &&
            call && call->is_intrinsic(Call::size_of_halide_buffer_t)) {
            do_indent();
            string buf_name = unique_name('b');
            stream << "halide_buffer_t " << buf_name << ";\n";
            rhs << "&" << buf_name;
        } else {
            // Make a stack of uint64_ts
            string size = print_expr(simplify((op->args[0] + 7)/8));
            do_indent();
            string array_name = unique_name('a');
            stream << "uint64_t " << array_name << "[" << size << "];";
            rhs << "(" << print_type(op->type) << ")(&" << array_name << ")";
        }
    } else if (op->is_intrinsic(Call::make_struct)) {
        if (op->args.empty()) {
            rhs << "NULL";
        } else {
            // Emit a line something like:
            // struct {const int f_0, const char f_1, const int f_2} foo = {3, 'c', 4};

            // Get the args
            vector<string> values;
            for (size_t i = 0; i < op->args.size(); i++) {
                values.push_back(print_expr(op->args[i]));
            }
            do_indent();
            stream << "struct {";
            // List the types.
            for (size_t i = 0; i < op->args.size(); i++) {
                stream << "const " << print_type(op->args[i].type()) << " f_" << i << "; ";
            }
            string struct_name = unique_name('s');
            stream << "}  " << struct_name << " = {";
            // List the values.
            for (size_t i = 0; i < op->args.size(); i++) {
                if (i > 0) stream << ", ";
                stream << values[i];
            }
            stream << "};\n";
            // Return a pointer to it of the appropriate type
            if (op->type.handle_type) {
                rhs << "(" << print_type(op->type) << ")";
            }
            rhs << "(&" << struct_name << ")";
        }
    } else if (op->is_intrinsic(Call::stringify)) {
        // Rewrite to an snprintf
        vector<string> printf_args;
        string format_string = "";
        for (size_t i = 0; i < op->args.size(); i++) {
            Type t = op->args[i].type();
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
        string buf_name = unique_name('b');
        do_indent();
        stream << "char " << buf_name << "[1024];\n";
        do_indent();
        stream << "snprintf(" << buf_name << ", 1024, \"" << format_string << "\"";
        for (size_t i = 0; i < printf_args.size(); i++) {
            stream << ", " << printf_args[i];
        }
        stream << ");\n";
        rhs << buf_name;

    } else if (op->is_intrinsic(Call::register_destructor)) {
        internal_assert(op->args.size() == 2);
        const StringImm *fn = op->args[0].as<StringImm>();
        internal_assert(fn);
        string arg = print_expr(op->args[1]);

        string call =
            fn->value + "(" +
            (have_user_context ? "__user_context_, " : "nullptr, ")
            + "arg);";

        do_indent();
        // Make a struct on the stack that calls the given function as a destructor
        string struct_name = unique_name('s');
        string instance_name = unique_name('d');
        stream << "struct " << struct_name << "{ "
               << "void *arg; "
               << struct_name << "(void *a) : arg((void *)a) {} "
               << "~" << struct_name << "() {" << call << "}"
               << "} " << instance_name << "(" << arg << ");\n";
        rhs << print_expr(0);
    } else if (op->is_intrinsic(Call::div_round_to_zero)) {
        rhs << print_expr(op->args[0]) << " / " << print_expr(op->args[1]);
    } else if (op->is_intrinsic(Call::mod_round_to_zero)) {
        rhs << print_expr(op->args[0]) << " % " << print_expr(op->args[1]);
    } else if (op->is_intrinsic(Call::signed_integer_overflow)) {
        user_error << "Signed integer overflow occurred during constant-folding. Signed"
            " integer overflow for int32 and int64 is undefined behavior in"
            " Halide.\n";
    } else if (op->is_intrinsic(Call::prefetch)) {
        user_assert((op->args.size() == 4) && is_one(op->args[2]))
            << "Only prefetch of 1 cache line is supported in C backend.\n";
        const Variable *base = op->args[0].as<Variable>();
        internal_assert(base && base->type.is_handle());
        rhs << "__builtin_prefetch("
            << "((" << print_type(op->type) << " *)" << print_name(base->name)
            << " + " << print_expr(op->args[1]) << "), 1)";
    } else if (op->is_intrinsic(Call::indeterminate_expression)) {
        user_error << "Indeterminate expression occurred during constant-folding.\n";
    } else if (op->is_intrinsic(Call::size_of_halide_buffer_t)) {
        rhs << "(sizeof(halide_buffer_t))";
    } else if (op->call_type == Call::Intrinsic ||
               op->call_type == Call::PureIntrinsic) {
        // TODO: other intrinsics
        internal_error << "Unhandled intrinsic in C backend: " << op->name << '\n';
    } else {
        // Generic calls
        vector<string> args(op->args.size());
        for (size_t i = 0; i < op->args.size(); i++) {
            args[i] = print_expr(op->args[i]);
        }
        rhs << op->name << "(";

        if (function_takes_user_context(op->name)) {
            rhs << (have_user_context ? "__user_context_, " : "nullptr, ");
        }

        for (size_t i = 0; i < op->args.size(); i++) {
            if (i > 0) rhs << ", ";
            rhs << args[i];
        }
        rhs << ")";
    }

    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const Load *op) {

    Type t = op->type;
    bool type_cast_needed =
        !allocations.contains(op->name) ||
        allocations.get(op->name).type != t;

    ostringstream rhs;
    if (type_cast_needed) {
        rhs << "((const "
            << print_type(op->type)
            << " *)"
            << print_name(op->name)
            << ")";
    } else {
        rhs << print_name(op->name);
    }
    rhs << "["
        << print_expr(op->index)
        << "]";

    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const Store *op) {

    Type t = op->value.type();

    bool type_cast_needed =
        t.is_handle() ||
        !allocations.contains(op->name) ||
        allocations.get(op->name).type != t;

    string id_index = print_expr(op->index);
    string id_value = print_expr(op->value);
    do_indent();

    if (type_cast_needed) {
        stream << "(("
               << print_type(t)
               << " *)"
               << print_name(op->name)
               << ")";
    } else {
        stream << print_name(op->name);
    }
    stream << "["
           << id_index
           << "] = "
           << id_value
           << ";\n";

    cache.clear();
}

void CodeGen_C::visit(const Let *op) {
    string id_value = print_expr(op->value);
    Expr body = op->body;
    if (op->value.type().is_handle()) {
        // The body might contain a Load that references this directly
        // by name, so we can't rewrite the name.
        do_indent();
        stream << print_type(op->value.type())
               << " " << print_name(op->name)
               << " = " << id_value << ";\n";
    } else {
        Expr new_var = Variable::make(op->value.type(), id_value);
        body = substitute(op->name, new_var, body);
    }
    print_expr(body);
}

void CodeGen_C::visit(const Select *op) {
    ostringstream rhs;
    string true_val = print_expr(op->true_value);
    string false_val = print_expr(op->false_value);
    string cond = print_expr(op->condition);
    rhs << "(" << print_type(op->type) << ")"
        << "(" << cond
        << " ? " << true_val
        << " : " << false_val
        << ")";
    print_assignment(op->type, rhs.str());
}

void CodeGen_C::visit(const LetStmt *op) {
    string id_value = print_expr(op->value);
    Stmt body = op->body;
    if (op->value.type().is_handle()) {
        // The body might contain a Load or Store that references this
        // directly by name, so we can't rewrite the name.
        do_indent();
        stream << print_type(op->value.type())
               << " " << print_name(op->name)
               << " = " << id_value << ";\n";
    } else {
        Expr new_var = Variable::make(op->value.type(), id_value);
        body = substitute(op->name, new_var, body);
    }
    body.accept(this);
}

void CodeGen_C::visit(const AssertStmt *op) {
    string id_cond = print_expr(op->condition);

    do_indent();
    // Halide asserts have different semantics to C asserts.  They're
    // supposed to clean up and make the containing function return
    // -1, so we can't use the C version of assert. Instead we convert
    // to an if statement.

    stream << "if (!" << id_cond << ") ";
    open_scope();
    string id_msg = print_expr(op->message);
    do_indent();
    stream << "return " << id_msg << ";\n";
    close_scope("");
}

void CodeGen_C::visit(const ProducerConsumer *op) {
    do_indent();
    if (op->is_producer) {
        stream << "// produce " << op->name << '\n';
    } else {
        stream << "// consume " << op->name << '\n';
    }
    print_stmt(op->body);
}

void CodeGen_C::visit(const For *op) {
    if (op->for_type == ForType::Parallel) {
        do_indent();
        stream << "#pragma omp parallel for\n";
    } else {
        internal_assert(op->for_type == ForType::Serial)
            << "Can only emit serial or parallel for loops to C\n";
    }

    string id_min = print_expr(op->min);
    string id_extent = print_expr(op->extent);

    do_indent();
    stream << "for (int "
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

void CodeGen_C::visit(const Provide *op) {
    internal_error << "Cannot emit Provide statements as C\n";
}

void CodeGen_C::visit(const Allocate *op) {
    open_scope();

    // For sizes less than 8k, do a stack allocation
    bool on_stack = false;
    int32_t constant_size;
    string size_id;
    if (op->new_expr.defined()) {
        Allocation alloc;
        alloc.type = op->type;
        alloc.free_function = op->free_function;
        allocations.push(op->name, alloc);
        heap_allocations.push(op->name, 0);
        stream << print_type(op->type) << "*" << print_name(op->name) << " = (" << print_expr(op->new_expr) << ");\n";
    } else {
        constant_size = op->constant_allocation_size();
        if (constant_size > 0) {
            int64_t stack_bytes = constant_size * op->type.bytes();

            if (stack_bytes > ((int64_t(1) << 31) - 1)) {
                user_error << "Total size for allocation "
                           << op->name << " is constant but exceeds 2^31 - 1.\n";
            } else {
                size_id = print_expr(Expr(static_cast<int32_t>(constant_size)));
                if (can_allocation_fit_on_stack(stack_bytes)) {
                    on_stack = true;
                }
            }
        } else {
            // Check that the allocation is not scalar (if it were scalar
            // it would have constant size).
            internal_assert(op->extents.size() > 0);

            size_id = print_assignment(Int(64), print_expr(op->extents[0]));

            for (size_t i = 1; i < op->extents.size(); i++) {
                // Make the code a little less cluttered for two-dimensional case
                string new_size_id_rhs;
                string next_extent = print_expr(op->extents[i]);
                if (i > 1) {
                    new_size_id_rhs =  "(" + size_id + " > ((int64_t(1) << 31) - 1)) ? " + size_id + " : (" + size_id + " * " + next_extent + ")";
                } else {
                    new_size_id_rhs = size_id + " * " + next_extent;
                }
                size_id = print_assignment(Int(64), new_size_id_rhs);
            }
            do_indent();
            stream << "if ((" << size_id << " > ((int64_t(1) << 31) - 1)) || ((" << size_id <<
              " * sizeof(" << print_type(op->type) << ")) > ((int64_t(1) << 31) - 1)))\n";
            open_scope();
            do_indent();
            stream << "halide_error("
                   << (have_user_context ? "__user_context_" : "nullptr")
                   << ", \"32-bit signed overflow computing size of allocation "
                   << op->name << "\\n\");\n";
            do_indent();
            stream << "return -1;\n";
            close_scope("overflow test " + op->name);
        }

        // Check the condition to see if this allocation should actually be created.
        // If the allocation is on the stack, the only condition we can respect is
        // unconditional false (otherwise a non-constant-sized array declaration
        // will be generated).
        if (!on_stack || is_zero(op->condition)) {
            Expr conditional_size = Select::make(op->condition,
                                                 Var(size_id),
                                                 Expr(static_cast<int32_t>(0)));
            conditional_size = simplify(conditional_size);
            size_id = print_assignment(Int(64), print_expr(conditional_size));
        }

        Allocation alloc;
        alloc.type = op->type;
        allocations.push(op->name, alloc);

        do_indent();
        stream << print_type(op->type) << ' ';

        if (on_stack) {
            stream << print_name(op->name)
                   << "[" << size_id << "];\n";
        } else {
            stream << "*"
                   << print_name(op->name)
                   << " = ("
                   << print_type(op->type)
                   << " *)halide_malloc("
                   << (have_user_context ? "__user_context_" : "nullptr")
                   << ", sizeof("
                   << print_type(op->type)
                   << ")*" << size_id << ");\n";
            heap_allocations.push(op->name, 0);
        }
    }

    op->body.accept(this);

    // Should have been freed internally
    internal_assert(!allocations.contains(op->name));

    close_scope("alloc " + print_name(op->name));
}

void CodeGen_C::visit(const Free *op) {
    if (heap_allocations.contains(op->name)) {
        string free_function = allocations.get(op->name).free_function;
        if (free_function.empty()) {
            free_function = "halide_free";
        }

        do_indent();
        stream << free_function << "("
               << (have_user_context ? "__user_context_, " : "nullptr, ")
               << print_name(op->name)
               << ");\n";
        heap_allocations.pop(op->name);
    }
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

    do_indent();
    stream << "if (" << cond_id << ")\n";
    open_scope();
    op->then_case.accept(this);
    close_scope("if " + cond_id);

    if (op->else_case.defined()) {
        do_indent();
        stream << "else\n";
        open_scope();
        op->else_case.accept(this);
        close_scope("if " + cond_id + " else");
    }
}

void CodeGen_C::visit(const Evaluate *op) {
    if (is_const(op->value)) return;
    string id = print_expr(op->value);
    do_indent();
    stream << "(void)" << id << ";\n";
}

void CodeGen_C::visit(const Shuffle *op) {
    internal_error << "Cannot emit vector code to C\n";
}

void CodeGen_C::test() {
    LoweredArgument buffer_arg("buf", Argument::OutputBuffer, Int(32), 3);
    LoweredArgument float_arg("alpha", Argument::InputScalar, Float(32), 0);
    LoweredArgument int_arg("beta", Argument::InputScalar, Int(32), 0);
    LoweredArgument user_context_arg("__user_context", Argument::InputScalar, type_of<const void*>(), 0);
    vector<LoweredArgument> args = { buffer_arg, float_arg, int_arg, user_context_arg };
    Var x("x");
    Param<float> alpha("alpha");
    Param<int> beta("beta");
    Expr e = Select::make(alpha > 4.0f, print_when(x < 1, 3), 2);
    Stmt s = Store::make("buf", e, x, Parameter(), const_true());
    s = LetStmt::make("x", beta+1, s);
    s = Block::make(s, Free::make("tmp.stack"));
    s = Allocate::make("tmp.stack", Int(32), {127}, const_true(), s);
    s = Block::make(s, Free::make("tmp.heap"));
    s = Allocate::make("tmp.heap", Int(32), {43, beta}, const_true(), s);
    Expr buf = Variable::make(Handle(), "buf.buffer");
    s = LetStmt::make("buf", Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern), s);

    Module m("", get_host_target());
    m.append(LoweredFunc("test1", args, s, LoweredFunc::External));

    ostringstream source;
    {
        CodeGen_C cg(source, Target("host"), CodeGen_C::CImplementation);
        cg.compile(m);
    }

    string src = source.str();
    string correct_source =
        headers +
        globals +
        string((const char *)halide_internal_runtime_header_HalideRuntime_h) + '\n' +
        string((const char *)halide_internal_initmod_inlined_c) + R"GOLDEN_CODE(
#ifndef HALIDE_FUNCTION_ATTRS
#define HALIDE_FUNCTION_ATTRS
#endif

#ifdef __cplusplus
extern "C" {
#endif

int test1(struct halide_buffer_t *_buf_buffer, float _alpha, int32_t _beta, void const *__user_context) HALIDE_FUNCTION_ATTRS {
 void *_0 = _halide_buffer_get_host(_buf_buffer);
 void * _buf = _0;
 {
  int64_t _1 = 43;
  int64_t _2 = _1 * _beta;
  if ((_2 > ((int64_t(1) << 31) - 1)) || ((_2 * sizeof(int32_t)) > ((int64_t(1) << 31) - 1)))
  {
   halide_error(__user_context_, "32-bit signed overflow computing size of allocation tmp.heap\n");
   return -1;
  } // overflow test tmp.heap
  int64_t _3 = _2;
  int32_t *_tmp_heap = (int32_t *)halide_malloc(__user_context_, sizeof(int32_t)*_3);
  {
   int32_t _tmp_stack[127];
   int32_t _4 = _beta + 1;
   int32_t _5;
   bool _6 = _4 < 1;
   if (_6)
   {
    char b0[1024];
    snprintf(b0, 1024, "%lld%s", (long long)(3), "\n");
    char const *_7 = b0;
    int32_t _8 = halide_print(__user_context_, _7);
    int32_t _9 = (_8, 3);
    _5 = _9;
   } // if _6
   else
   {
    _5 = 3;
   } // if _6 else
   int32_t _10 = _5;
   bool _11 = _alpha > float_from_bits(1082130432 /* 4 */);
   int32_t _12 = (int32_t)(_11 ? _10 : 2);
   ((int32_t *)_buf)[_4] = _12;
  } // alloc _tmp_stack
  halide_free(__user_context_, _tmp_heap);
 } // alloc _tmp_heap
 return 0;
}

#ifdef __cplusplus
}  // extern "C"
#endif
)GOLDEN_CODE";

    if (src != correct_source) {
        int diff = 0;
        while (src[diff] == correct_source[diff]) diff++;
        int diff_end = diff + 1;
        while (diff > 0 && src[diff] != '\n') diff--;
        while (diff_end < (int)src.size() && src[diff_end] != '\n') diff_end++;

        internal_error
            << "Correct source code:\n" << correct_source
            << "Actual source code:\n" << src
            << "Difference starts at:\n"
            << "Correct: " << correct_source.substr(diff, diff_end - diff) << "\n"
            << "Actual: " << src.substr(diff, diff_end - diff) << "\n";
    }

    std::cout << "CodeGen_C test passed\n";
}

}
}
