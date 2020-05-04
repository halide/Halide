#include "Module.h"

#include <array>
#include <fstream>
#include <future>
#include <utility>

#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "CodeGen_PyTorch.h"
#include "CompilerLogger.h"
#include "Debug.h"
#include "HexagonOffload.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Pipeline.h"
#include "PythonExtensionGen.h"
#include "StmtToHtml.h"

using Halide::Internal::debug;

namespace Halide {
namespace Internal {

// This is the One True Source of the known output types for halide,
// and the appropriate file extension for each output type. If you are
// explicitly managing file extensions somewhere else, you are probably
// doing it wrong; please prefer to use this table as the source of truth.
//
// Note that we deliberately default to ".py.cpp" (rather than .py.c) here for python_extension;
// in theory, the Python extension file we generate can be compiled just
// fine as a plain-C file... but if we are building with cpp-name-mangling
// enabled in the target, we will include generated .h files that can't be compiled.
// We really don't want to vary the file extensions based on target flags,
// and in practice, it's extremely unlikely that anyone needs to rely on this
// being pure C output (vs possibly C++).
std::map<Output, OutputInfo> get_output_info(const Target &target) {
    const bool is_windows_coff = target.os == Target::Windows;
    std::map<Output, OutputInfo> ext = {
        {Output::assembly, {"assembly", ".s"}},
        {Output::bitcode, {"bitcode", ".bc"}},
        {Output::c_header, {"c_header", ".h"}},
        {Output::c_source, {"c_source", ".halide_generated.cpp"}},
        {Output::compiler_log, {"compiler_log", ".halide_compiler_log"}},
        {Output::cpp_stub, {"cpp_stub", ".stub.h"}},
        {Output::featurization, {"featurization", ".featurization"}},
        {Output::llvm_assembly, {"llvm_assembly", ".ll"}},
        {Output::object, {"object", is_windows_coff ? ".obj" : ".o"}},
        {Output::python_extension, {"python_extension", ".py.cpp"}},
        {Output::pytorch_wrapper, {"pytorch_wrapper", ".pytorch.h"}},
        {Output::registration, {"registration", ".registration.cpp"}},
        {Output::schedule, {"schedule", ".schedule.h"}},
        {Output::static_library, {"static_library", is_windows_coff ? ".lib" : ".a"}},
        {Output::stmt, {"stmt", ".stmt"}},
        {Output::stmt_html, {"stmt_html", ".stmt.html"}},
    };
    return ext;
}

namespace {

class TemporaryObjectFileDir final {
public:
    TemporaryObjectFileDir()
        : dir_path(dir_make_temp()) {
    }
    ~TemporaryObjectFileDir() {
        for (const auto &f : dir_files) {
            debug(1) << "file_unlink: " << f << "\n";
            file_unlink(f);
        }
        debug(1) << "dir_rmdir: " << dir_path << "\n";
        dir_rmdir(dir_path);
    }
    std::string add_temp_file(const std::string &base_path_name,
                              const std::string &suffix,
                              const Target &target,
                              bool in_front = false) {
        size_t slash_idx = base_path_name.rfind('/');
        size_t backslash_idx = base_path_name.rfind('\\');
        if (slash_idx == std::string::npos) {
            slash_idx = 0;
        } else {
            slash_idx++;
        }
        if (backslash_idx == std::string::npos) {
            backslash_idx = 0;
        } else {
            backslash_idx++;
        }
        std::string base_name = base_path_name.substr(std::max(slash_idx, backslash_idx));
        std::string name = dir_path + "/" + base_name + suffix;
        debug(1) << "add_temp_object_file: " << name << "\n";
        if (in_front) {
            dir_files.insert(dir_files.begin(), name);
        } else {
            dir_files.push_back(name);
        }
        return name;
    }

    std::string add_temp_object_file(const std::string &base_path_name,
                                     const std::string &suffix,
                                     const Target &target,
                                     bool in_front = false) {
        const char *ext = (target.os == Target::Windows) ? ".obj" : ".o";
        return add_temp_file(base_path_name, suffix + ext, target, in_front);
    }

    const std::vector<std::string> &files() {
        return dir_files;
    }

private:
    const std::string dir_path;
    std::vector<std::string> dir_files;
    TemporaryObjectFileDir(const TemporaryObjectFileDir &) = delete;
    void operator=(const TemporaryObjectFileDir &) = delete;
};

// Given a pathname of the form /path/to/name.ext, append suffix before ext to produce /path/to/namesuffix.ext
std::string add_suffix(const std::string &path, const std::string &suffix) {
    size_t last_path = std::min(path.rfind('/'), path.rfind('\\'));
    if (last_path == std::string::npos) {
        last_path = 0;
    }
    size_t dot = path.find('.', last_path);
    if (dot == std::string::npos) {
        return path + suffix;
    } else {
        return path.substr(0, dot) + suffix + path.substr(dot);
    }
}

void validate_outputs(const std::map<Output, std::string> &in) {
    // We don't care about the extensions, so any Target will do
    auto known = get_output_info(Target());
    for (auto it : in) {
        internal_assert(!it.second.empty()) << "Empty value for output: " << known.at(it.first).name;
    }
}

bool contains(const std::map<Output, std::string> &in, const Output &key) {
    return in.find(key) != in.end();
}

std::map<Output, std::string> add_suffixes(const std::map<Output, std::string> &in, const std::string &suffix) {
    std::map<Output, std::string> out;
    for (auto it : in) {
        out[it.first] = add_suffix(it.second, suffix);
    }
    return out;
}

void emit_registration(const Module &m, std::ostream &stream) {
    /*
        This relies on the filter library being linked in a way that doesn't
        dead-strip "unused" initialization code; this may mean that you need to
        explicitly link with with --whole-archive (or the equivalent) to ensure
        that the registration code isn't omitted. Sadly, there's no portable way
        to do this, so you may need to take care in your make/build/etc files:

        Linux:      -Wl,--whole-archive "/path/to/library" -Wl,-no-whole-archive
        Darwin/OSX: -Wl,-force_load,/path/to/library
        VS2015 R2+: /WHOLEARCHIVE:/path/to/library.lib
        Bazel:      alwayslink=1

        Note also that registration files deliberately have no #includes, and
        are specifically designed to be legal to concatenate into a single
        source file; it should be equivalent to compile-and-link multiple
        registration files separately, or to concatenate multiple registration
        files into a single one which is then compiled.
    */

    const std::string registration_template = R"INLINE_CODE(
// MACHINE GENERATED -- DO NOT EDIT

extern "C" {
struct halide_filter_metadata_t;
void halide_register_argv_and_metadata(
    int (*filter_argv_call)(void **),
    const struct halide_filter_metadata_t *filter_metadata,
    const char * const *extra_key_value_pairs
);
}

$NAMESPACEOPEN$
extern int $SHORTNAME$_argv(void **args);
extern const struct halide_filter_metadata_t *$SHORTNAME$_metadata();
$NAMESPACECLOSE$

#ifdef HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC
extern "C" const char * const *HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC();
#endif  // HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC

namespace $NREGS$ {
namespace {
struct Registerer {
    Registerer() {
#ifdef HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC
        halide_register_argv_and_metadata(::$FULLNAME$_argv, ::$FULLNAME$_metadata(), HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC());
#else
        halide_register_argv_and_metadata(::$FULLNAME$_argv, ::$FULLNAME$_metadata(), nullptr);
#endif  // HALIDE_REGISTER_EXTRA_KEY_VALUE_PAIRS_FUNC
    }
};
static Registerer registerer;
}  // namespace
}  // $NREGS$

)INLINE_CODE";

    for (const auto &f : m.functions()) {
        if (f.linkage == LinkageType::ExternalPlusMetadata) {
            std::vector<std::string> namespaces;
            std::string simple_name = extract_namespaces(f.name, namespaces);
            std::string nsopen, nsclose;
            for (const auto &ns : namespaces) {
                nsopen += "namespace " + ns + " { ";
                nsclose += "}";
            }
            if (!m.target().has_feature(Target::CPlusPlusMangling)) {
                internal_assert(namespaces.empty());
                nsopen = "extern \"C\" {";
                nsclose = "}";
            }
            std::string nsreg = "halide_nsreg_" + replace_all(f.name, "::", "_");
            std::string s = replace_all(registration_template, "$NAMESPACEOPEN$", nsopen);
            s = replace_all(s, "$SHORTNAME$", simple_name);
            s = replace_all(s, "$NAMESPACECLOSE$", nsclose);
            s = replace_all(s, "$FULLNAME$", f.name);
            s = replace_all(s, "$NREGS$", nsreg);
            stream << s;
        }
    }
}

std::string indent_string(const std::string &src, const std::string &indent) {
    std::ostringstream o;
    bool prev_was_newline = true;
    for (size_t i = 0; i < src.size(); i++) {
        const char c = src[i];
        const bool is_newline = (c == '\n');
        if (prev_was_newline && !is_newline) {
            o << indent;
        }
        o << c;
        prev_was_newline = is_newline;
    }
    return o.str();
}

void emit_schedule_file(const std::string &name,
                        const std::vector<Target> &targets,
                        const std::string &scheduler_name,
                        const std::string &machine_params_string,
                        const std::string &body,
                        std::ostream &stream) {
    std::string s = R"INLINE_CODE(#ifndef $CLEANNAME$_SCHEDULE_H
#define $CLEANNAME$_SCHEDULE_H

// MACHINE GENERATED -- DO NOT EDIT
// This schedule was automatically generated by $SCHEDULER$
// for target=$TARGET$  // NOLINT
// with machine_params=$MACHINEPARAMS$

#include "Halide.h"

$NAMESPACEOPEN$
inline void apply_schedule_$SHORTNAME$(
    ::Halide::Pipeline pipeline,
    ::Halide::Target target
) {
    using ::Halide::Func;
    using ::Halide::MemoryType;
    using ::Halide::RVar;
    using ::Halide::TailStrategy;
    using ::Halide::Var;
$BODY$
}
$NAMESPACECLOSE$
#endif  // $CLEANNAME$_SCHEDULE_H
)INLINE_CODE";

    // For logging in the comment, strip out features that are almost
    // certainly irrelevant to scheduling issues, to make for easier reading
    const Target::Feature irrelevant_features[] = {
        Target::CPlusPlusMangling,
        Target::NoRuntime,
        Target::UserContext,
    };

    std::vector<std::string> namespaces;
    std::string simple_name = extract_namespaces(name, namespaces);
    std::string nsopen, nsclose;
    for (const auto &ns : namespaces) {
        nsopen += "namespace " + ns + " {\n";
        nsclose += "}  // namespace " + ns + "\n";
    }
    std::string clean_name = replace_all(name, "::", "_");
    std::string target_string;
    for (Target t : targets) {
        if (!target_string.empty()) target_string += ",";
        for (auto f : irrelevant_features) {
            t = t.without_feature(f);
        }
        target_string += t.to_string();
    }
    std::string body_text = indent_string(body, "    ");
    s = replace_all(s, "$SCHEDULER$", scheduler_name);
    s = replace_all(s, "$NAMESPACEOPEN$", nsopen);
    s = replace_all(s, "$SHORTNAME$", simple_name);
    s = replace_all(s, "$CLEANNAME$", clean_name);
    s = replace_all(s, "$NAMESPACECLOSE$", nsclose);
    s = replace_all(s, "$TARGET$", target_string);
    s = replace_all(s, "$BODY$", body_text);
    s = replace_all(s, "$MACHINEPARAMS$", machine_params_string);
    stream << s;
}

}  // namespace

struct ModuleContents {
    mutable RefCount ref_count;
    std::string name;
    Target target;
    std::vector<Buffer<>> buffers;
    std::vector<Internal::LoweredFunc> functions;
    std::vector<Module> submodules;
    std::vector<ExternalCode> external_code;
    std::map<std::string, std::string> metadata_name_map;
    bool any_strict_float{false};
    std::unique_ptr<AutoSchedulerResults> auto_scheduler_results;
};

template<>
RefCount &ref_count<ModuleContents>(const ModuleContents *t) noexcept {
    return t->ref_count;
}

template<>
void destroy<ModuleContents>(const ModuleContents *t) {
    delete t;
}

LoweredFunc::LoweredFunc(const std::string &name,
                         const std::vector<LoweredArgument> &args,
                         Stmt body,
                         LinkageType linkage,
                         NameMangling name_mangling)
    : name(name), args(args), body(std::move(body)), linkage(linkage), name_mangling(name_mangling) {
}

LoweredFunc::LoweredFunc(const std::string &name,
                         const std::vector<Argument> &args,
                         Stmt body,
                         LinkageType linkage,
                         NameMangling name_mangling)
    : name(name), body(std::move(body)), linkage(linkage), name_mangling(name_mangling) {
    for (const Argument &i : args) {
        this->args.emplace_back(i);
    }
}

}  // namespace Internal

using namespace Halide::Internal;

Module::Module(const std::string &name, const Target &target)
    : contents(new Internal::ModuleContents) {
    contents->name = name;
    contents->target = target;
}

void Module::set_auto_scheduler_results(const AutoSchedulerResults &auto_scheduler_results) {
    internal_assert(contents->auto_scheduler_results.get() == nullptr);
    contents->auto_scheduler_results.reset(new AutoSchedulerResults(auto_scheduler_results));
}

void Module::set_any_strict_float(bool any_strict_float) {
    contents->any_strict_float = any_strict_float;
}

const Target &Module::target() const {
    return contents->target;
}

const std::string &Module::name() const {
    return contents->name;
}

const AutoSchedulerResults *Module::get_auto_scheduler_results() const {
    return contents->auto_scheduler_results.get();
}

bool Module::any_strict_float() const {
    return contents->any_strict_float;
}

const std::vector<Buffer<>> &Module::buffers() const {
    return contents->buffers;
}

const std::vector<Internal::LoweredFunc> &Module::functions() const {
    return contents->functions;
}

std::vector<Internal::LoweredFunc> &Module::functions() {
    return contents->functions;
}

const std::vector<Module> &Module::submodules() const {
    return contents->submodules;
}

const std::vector<ExternalCode> &Module::external_code() const {
    return contents->external_code;
}

Internal::LoweredFunc Module::get_function_by_name(const std::string &name) const {
    for (const auto &f : functions()) {
        if (f.name == name) {
            return f;
        }
    }
    user_error << "get_function_by_name: function " << name << " not found.\n";
    return Internal::LoweredFunc("", std::vector<Argument>{}, {}, LinkageType::External);
}

void Module::append(const Buffer<> &buffer) {
    contents->buffers.push_back(buffer);
}

void Module::append(const Internal::LoweredFunc &function) {
    contents->functions.push_back(function);
}

void Module::append(const Module &module) {
    contents->submodules.push_back(module);
}

void Module::append(const ExternalCode &external_code) {
    contents->external_code.push_back(external_code);
}

Module link_modules(const std::string &name, const std::vector<Module> &modules) {
    Module output(name, modules.front().target());

    for (size_t i = 0; i < modules.size(); i++) {
        const Module &input = modules[i];

        if (output.target() != input.target()) {
            user_error << "Mismatched targets in modules to link ("
                       << output.name() << ", " << output.target().to_string()
                       << "), ("
                       << input.name() << ", " << input.target().to_string() << ")\n";
        }

        // TODO(dsharlet): Check for naming collisions, maybe rename
        // internal linkage declarations in the case of collision.
        for (const auto &b : input.buffers()) {
            output.append(b);
        }
        for (const auto &f : input.functions()) {
            output.append(f);
        }
    }

    return output;
}

Buffer<uint8_t> Module::compile_to_buffer() const {
    // TODO: This Hexagon specific code should be removed as soon as possible.
    // This may involve adding more general support for post-processing and
    // a way of specifying to use it.
    if (target().arch == Target::Hexagon) {
        return compile_module_to_hexagon_shared_object(*this);
    }

    llvm::LLVMContext context;
    std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(*this, context));

    llvm::SmallVector<char, 4096> object;
    llvm::raw_svector_ostream object_stream(object);
    compile_llvm_module_to_object(*llvm_module, object_stream);

    if (debug::debug_level() >= 2) {
        debug(2) << "Submodule assembly for " << name() << ": "
                 << "\n";
        llvm::SmallString<4096> assembly;
        llvm::raw_svector_ostream assembly_stream(assembly);
        compile_llvm_module_to_assembly(*llvm_module, assembly_stream);
        debug(2) << assembly.c_str() << "\n";
    }

    Buffer<uint8_t> result(object.size(), name());
    memcpy(result.data(), reinterpret_cast<uint8_t *>(&object[0]), object.size());
    return result;
}

Module Module::resolve_submodules() const {
    if (submodules().empty()) {
        return *this;
    }

    Module lowered_module(name(), target());

    for (const auto &f : functions()) {
        lowered_module.append(f);
    }
    for (const auto &buf : buffers()) {
        lowered_module.append(buf);
    }
    for (const auto &ec : external_code()) {
        lowered_module.append(ec);
    }
    for (const auto &m : submodules()) {
        Module copy(m.resolve_submodules());

        // Propagate external code blocks.
        for (const auto &ec : external_code()) {
            // TODO(zalman): Is this the right thing to do?
            bool already_in_list = false;
            for (const auto &ec_sub : copy.external_code()) {
                if (ec_sub.name() == ec.name()) {
                    already_in_list = true;
                    break;
                }
            }
            if (!already_in_list) {
                copy.append(ec);
            }
        }

        auto buf = copy.compile_to_buffer();
        lowered_module.append(buf);
    }
    // Copy the autoscheduler results back into the lowered module after resolving the submodules.
    if (auto *r = contents->auto_scheduler_results.get()) {
        lowered_module.set_auto_scheduler_results(*r);
    }
    return lowered_module;
}

void Module::remap_metadata_name(const std::string &from, const std::string &to) const {
    internal_assert(contents->metadata_name_map.find(from) == contents->metadata_name_map.end());
    internal_assert(contents->metadata_name_map.find(to) == contents->metadata_name_map.end());
    contents->metadata_name_map[from] = to;
}

std::map<std::string, std::string> Module::get_metadata_name_map() const {
    return contents->metadata_name_map;
}

void Module::compile(const std::map<Output, std::string> &output_files) const {
    validate_outputs(output_files);

    // output stmt and html prior to resolving submodules. We need to
    // clear the output after writing it, otherwise the output will
    // be overwritten by recursive calls after submodules are resolved.
    if (contains(output_files, Output::stmt)) {
        debug(1) << "Module.compile(): stmt " << output_files.at(Output::stmt) << "\n";
        std::ofstream file(output_files.at(Output::stmt));
        file << *this;
    }
    if (contains(output_files, Output::stmt_html)) {
        debug(1) << "Module.compile(): stmt_html " << output_files.at(Output::stmt_html) << "\n";
        Internal::print_to_html(output_files.at(Output::stmt_html), *this);
    }

    // If there are submodules, recursively lower submodules to
    // buffers on a copy of the module being compiled, then compile
    // the copied module.
    if (!submodules().empty()) {
        std::map<Output, std::string> output_files_copy = output_files;
        output_files_copy.erase(Output::stmt);
        output_files_copy.erase(Output::stmt_html);
        resolve_submodules().compile(output_files_copy);
        return;
    }

    auto *logger = get_compiler_logger();
    if (contains(output_files, Output::object) || contains(output_files, Output::assembly) ||
        contains(output_files, Output::bitcode) || contains(output_files, Output::llvm_assembly) ||
        contains(output_files, Output::static_library)) {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(*this, context));

        if (contains(output_files, Output::object)) {
            const auto &f = output_files.at(Output::object);
            debug(1) << "Module.compile(): object " << f << "\n";
            auto out = make_raw_fd_ostream(f);
            compile_llvm_module_to_object(*llvm_module, *out);
            if (logger) {
                out->flush();
                logger->record_object_code_size(file_stat(f).file_size);
            }
        }
        if (contains(output_files, Output::static_library)) {
            // To simplify the code, we always create a temporary object output
            // here, even if output_files.at(Output::object) was also set: in practice,
            // no real-world code ever sets both object and static_library
            // at the same time, so there is no meaningful performance advantage
            // to be had.
            TemporaryObjectFileDir temp_dir;
            {
                std::string object = temp_dir.add_temp_object_file(output_files.at(Output::static_library), "", target());
                debug(1) << "Module.compile(): temporary object " << object << "\n";
                auto out = make_raw_fd_ostream(object);
                compile_llvm_module_to_object(*llvm_module, *out);
                out->flush();  // create_static_library() is happier if we do this
                if (logger && !contains(output_files, Output::object)) {
                    // Don't double-record object-code size if we already recorded it for object
                    logger->record_object_code_size(file_stat(object).file_size);
                }
            }
            debug(1) << "Module.compile(): static_library " << output_files.at(Output::static_library) << "\n";
            Target base_target(target().os, target().arch, target().bits);
            create_static_library(temp_dir.files(), base_target, output_files.at(Output::static_library));
        }
        if (contains(output_files, Output::assembly)) {
            debug(1) << "Module.compile(): assembly " << output_files.at(Output::assembly) << "\n";
            auto out = make_raw_fd_ostream(output_files.at(Output::assembly));
            compile_llvm_module_to_assembly(*llvm_module, *out);
        }
        if (contains(output_files, Output::bitcode)) {
            debug(1) << "Module.compile(): bitcode " << output_files.at(Output::bitcode) << "\n";
            auto out = make_raw_fd_ostream(output_files.at(Output::bitcode));
            compile_llvm_module_to_llvm_bitcode(*llvm_module, *out);
        }
        if (contains(output_files, Output::llvm_assembly)) {
            debug(1) << "Module.compile(): llvm_assembly " << output_files.at(Output::llvm_assembly) << "\n";
            auto out = make_raw_fd_ostream(output_files.at(Output::llvm_assembly));
            compile_llvm_module_to_llvm_assembly(*llvm_module, *out);
        }
    }
    if (contains(output_files, Output::c_header)) {
        debug(1) << "Module.compile(): c_header " << output_files.at(Output::c_header) << "\n";
        std::ofstream file(output_files.at(Output::c_header));
        Internal::CodeGen_C cg(file,
                               target(),
                               target().has_feature(Target::CPlusPlusMangling) ? Internal::CodeGen_C::CPlusPlusHeader : Internal::CodeGen_C::CHeader,
                               output_files.at(Output::c_header));
        cg.compile(*this);
    }
    if (contains(output_files, Output::c_source)) {
        debug(1) << "Module.compile(): c_source " << output_files.at(Output::c_source) << "\n";
        std::ofstream file(output_files.at(Output::c_source));
        Internal::CodeGen_C cg(file,
                               target(),
                               target().has_feature(Target::CPlusPlusMangling) ? Internal::CodeGen_C::CPlusPlusImplementation : Internal::CodeGen_C::CImplementation);
        cg.compile(*this);
    }
    if (contains(output_files, Output::python_extension)) {
        debug(1) << "Module.compile(): python_extension " << output_files.at(Output::python_extension) << "\n";
        std::ofstream file(output_files.at(Output::python_extension));
        Internal::PythonExtensionGen python_extension_gen(file);
        python_extension_gen.compile(*this);
    }
    if (contains(output_files, Output::schedule)) {
        debug(1) << "Module.compile(): schedule " << output_files.at(Output::schedule) << "\n";
        std::ofstream file(output_files.at(Output::schedule));
        auto *r = contents->auto_scheduler_results.get();
        std::string scheduler = r ? r->scheduler_name : "(None)";
        std::string machine_params = r ? r->machine_params_string : "(None)";
        std::string body = r && !r->schedule_source.empty() ? r->schedule_source : "// No autoscheduler has been run for this Generator.\n";
        emit_schedule_file(name(), {target()}, scheduler, machine_params, body, file);
    }
    if (contains(output_files, Output::featurization)) {
        debug(1) << "Module.compile(): featurization " << output_files.at(Output::featurization) << "\n";
        // If the featurization data is empty, just write an empty file
        std::ofstream binfile(output_files.at(Output::featurization), std::ios::binary | std::ios_base::trunc);
        auto *r = contents->auto_scheduler_results.get();
        if (r) {
            binfile.write((const char *)r->featurization.data(), r->featurization.size());
        }
        binfile.close();
    }
    if (contains(output_files, Output::registration)) {
        debug(1) << "Module.compile(): registration " << output_files.at(Output::registration) << "\n";
        std::ofstream file(output_files.at(Output::registration));
        emit_registration(*this, file);
        file.close();
        internal_assert(!file.fail());
    }
    if (contains(output_files, Output::pytorch_wrapper)) {
        debug(1) << "Module.compile(): pytorch_wrapper " << output_files.at(Output::pytorch_wrapper) << "\n";

        std::ofstream file(output_files.at(Output::pytorch_wrapper));
        Internal::CodeGen_PyTorch cg(file);
        cg.compile(*this);
        file.close();
        internal_assert(!file.fail());
    }
    if (contains(output_files, Output::compiler_log)) {
        debug(1) << "Module.compile(): compiler_log " << output_files.at(Output::compiler_log) << "\n";
        std::ofstream file(output_files.at(Output::compiler_log));
        internal_assert(get_compiler_logger() != nullptr);
        get_compiler_logger()->emit_to_stream(file);
        file.close();
        internal_assert(!file.fail());
    }
    // If HL_DEBUG_COMPILER_LOGGER is set, dump the log (if any) to stderr now, whether or it is required
    if (get_env_variable("HL_DEBUG_COMPILER_LOGGER") == "1" && get_compiler_logger() != nullptr) {
        get_compiler_logger()->emit_to_stream(std::cerr);
    }
}

std::map<Output, std::string> compile_standalone_runtime(const std::map<Output, std::string> &output_files, Target t) {
    validate_outputs(output_files);

    Module empty("standalone_runtime", t.without_feature(Target::NoRuntime).without_feature(Target::JIT));
    // For runtime, it only makes sense to output object files or static_library, so ignore
    // everything else.
    std::map<Output, std::string> actual_outputs;
    for (auto key : {Output::object, Output::static_library}) {
        auto it = output_files.find(key);
        if (it != output_files.end()) {
            actual_outputs[key] = it->second;
        }
    }
    empty.compile(actual_outputs);
    return actual_outputs;
}

void compile_standalone_runtime(const std::string &object_filename, Target t) {
    compile_standalone_runtime({{Output::object, object_filename}}, t);
}

namespace {

class ScopedCompilerLogger {
public:
    ScopedCompilerLogger(const CompilerLoggerFactory &compiler_logger_factory, const std::string &fn_name, const Target &target) {
        internal_assert(!get_compiler_logger());
        if (compiler_logger_factory) {
            set_compiler_logger(compiler_logger_factory(fn_name, target));
        } else {
            set_compiler_logger(nullptr);
        }
    }

    ~ScopedCompilerLogger() {
        set_compiler_logger(nullptr);
    }
};

}  // namespace

void compile_multitarget(const std::string &fn_name,
                         const std::map<Output, std::string> &output_files,
                         const std::vector<Target> &targets,
                         const ModuleFactory &module_factory,
                         const CompilerLoggerFactory &compiler_logger_factory) {
    validate_outputs(output_files);

    user_assert(!fn_name.empty()) << "Function name must be specified.\n";
    user_assert(!targets.empty()) << "Must specify at least one target.\n";

    // The final target in the list is considered "baseline", and is used
    // for (e.g.) the runtime and shared code. It is often just os-arch-bits
    // with no other features (though this is *not* a requirement).
    const Target &base_target = targets.back();

    // JIT makes no sense.
    user_assert(!base_target.has_feature(Target::JIT)) << "JIT not allowed for compile_multitarget.\n";

    // If only one target, don't bother with the runtime feature detection wrapping.
    const bool needs_wrapper = (targets.size() > 1);
    if (targets.size() == 1) {
        debug(1) << "compile_multitarget: single target is " << base_target.to_string() << "\n";
        ScopedCompilerLogger activate(compiler_logger_factory, fn_name, base_target);
        module_factory(fn_name, base_target).compile(output_files);
        return;
    }

    // You can't ask for .o files when doing this; it's not really useful,
    // and would complicate output (we might have to do multiple passes
    // if different values for NoRuntime are specified)... so just forbid
    // it up front.
    user_assert(!contains(output_files, Output::object)) << "Cannot request object for compile_multitarget.\n";

    // For safety, the runtime must be built only with features common to all
    // of the targets; given an unusual ordering like
    //
    //     x86-64-linux,x86-64-sse41
    //
    // we should still always be *correct*: this ordering would never select sse41
    // (since x86-64-linux would be selected first due to ordering), but could
    // crash on non-sse41 machines (if we generated a runtime with sse41 instructions
    // included). So we'll keep track of the common features as we walk thru the targets.

    // Using something like std::bitset would be arguably cleaner here, but we need an
    // array-of-uint64 for calls to halide_can_use_target_features() anyway,
    // so we'll just build and maintain in that form to avoid extra conversion.
    constexpr int kFeaturesWordCount = (Target::FeatureEnd + 63) / (sizeof(uint64_t) * 8);
    uint64_t runtime_features[kFeaturesWordCount] = {(uint64_t)-1LL};

    TemporaryObjectFileDir temp_obj_dir, temp_compiler_log_dir;
    std::vector<Expr> wrapper_args;
    std::vector<LoweredArgument> base_target_args;
    std::vector<AutoSchedulerResults> auto_scheduler_results;
    for (const Target &target : targets) {
        // arch-bits-os must be identical across all targets.
        if (target.os != base_target.os ||
            target.arch != base_target.arch ||
            target.bits != base_target.bits) {
            user_error << "All Targets must have matching arch-bits-os for compile_multitarget.\n";
        }
        // Some features must match across all targets.
        static const std::array<Target::Feature, 9> must_match_features = {{
            Target::ASAN,
            Target::CPlusPlusMangling,
            Target::Debug,
            Target::JIT,
            Target::Matlab,
            Target::MSAN,
            Target::NoRuntime,
            Target::TSAN,
            Target::UserContext,
        }};
        for (auto f : must_match_features) {
            if (target.has_feature(f) != base_target.has_feature(f)) {
                user_error << "All Targets must have feature '" << Target::feature_to_name(f) << "'' set identically for compile_multitarget.\n";
                break;
            }
        }

        // Each sub-target has a function name that is the 'real' name plus a suffix
        std::string suffix = "_" + replace_all(target.to_string(), "-", "_");
        std::string sub_fn_name = needs_wrapper ? (fn_name + suffix) : fn_name;

        // We always produce the runtime separately, so add NoRuntime explicitly.
        // Matlab should be added to the wrapper pipeline below, instead of each sub-pipeline.
        Target sub_fn_target = target.with_feature(Target::NoRuntime);
        if (needs_wrapper) {
            sub_fn_target = sub_fn_target.without_feature(Target::Matlab);
        }

        {
            ScopedCompilerLogger activate(compiler_logger_factory, sub_fn_name, sub_fn_target);
            Module sub_module = module_factory(sub_fn_name, sub_fn_target);
            // Re-assign every time -- should be the same across all targets anyway,
            // but base_target is always the last one we encounter.
            base_target_args = sub_module.get_function_by_name(sub_fn_name).args;

            auto sub_out = add_suffixes(output_files, suffix);
            internal_assert(contains(output_files, Output::static_library));
            sub_out[Output::object] = temp_obj_dir.add_temp_object_file(output_files.at(Output::static_library), suffix, target);
            sub_out.erase(Output::registration);
            sub_out.erase(Output::schedule);
            if (contains(sub_out, Output::compiler_log)) {
                sub_out[Output::compiler_log] = temp_compiler_log_dir.add_temp_file(output_files.at(Output::static_library), suffix, target);
            }
            debug(1) << "compile_multitarget: compile_sub_target " << sub_out[Output::object] << "\n";
            sub_module.compile(sub_out);
            auto *r = sub_module.get_auto_scheduler_results();
            auto_scheduler_results.push_back(r ? *r : AutoSchedulerResults());
        }

        uint64_t cur_target_features[kFeaturesWordCount] = {0};
        for (int i = 0; i < Target::FeatureEnd; ++i) {
            if (target.has_feature((Target::Feature)i)) {
                cur_target_features[i >> 6] |= ((uint64_t)1) << (i & 63);
            }
        }

        Expr can_use;
        if (target != base_target) {
            std::vector<Expr> features_struct_args;
            for (int i = 0; i < kFeaturesWordCount; ++i) {
                features_struct_args.emplace_back(UIntImm::make(UInt(64), cur_target_features[i]));
            }
            can_use = Call::make(Int(32), "halide_can_use_target_features",
                                 {kFeaturesWordCount, Call::make(type_of<uint64_t *>(), Call::make_struct, features_struct_args, Call::Intrinsic)},
                                 Call::Extern);
        } else {
            can_use = IntImm::make(Int(32), 1);
        }

        for (int i = 0; i < kFeaturesWordCount; ++i) {
            runtime_features[i] &= cur_target_features[i];
        }

        wrapper_args.push_back(can_use != 0);
        wrapper_args.emplace_back(sub_fn_name);
    }

    // If we haven't specified "no runtime", build a runtime with the base target
    // and add that to the result.
    if (!base_target.has_feature(Target::NoRuntime)) {
        // Start with a bare Target, set only the features we know are common to all.
        Target runtime_target(base_target.os, base_target.arch, base_target.bits);
        for (int i = 0; i < Target::FeatureEnd; ++i) {
            // We never want NoRuntime set here.
            if (i == Target::NoRuntime) {
                continue;
            }
            const int word = i >> 6;
            const int bit = i & 63;
            if (runtime_features[word] & (((uint64_t)1) << bit)) {
                runtime_target.set_feature((Target::Feature)i);
            }
        }
        std::map<Output, std::string> runtime_out =
            {{Output::object,
              temp_obj_dir.add_temp_object_file(output_files.at(Output::static_library), "_runtime", runtime_target)}};
        debug(1) << "compile_multitarget: compile_standalone_runtime " << runtime_out.at(Output::object) << "\n";
        compile_standalone_runtime(runtime_out, runtime_target);
    }

    if (needs_wrapper) {
        Expr indirect_result = Call::make(Int(32), Call::call_cached_indirect_function, wrapper_args, Call::Intrinsic);
        std::string private_result_name = unique_name(fn_name + "_result");
        Expr private_result_var = Variable::make(Int(32), private_result_name);
        Stmt wrapper_body = AssertStmt::make(private_result_var == 0, private_result_var);
        wrapper_body = LetStmt::make(private_result_name, indirect_result, wrapper_body);

        // Always build with NoRuntime: that's handled as a separate module.
        //
        // Always build with NoBoundsQuery: underlying code will implement that (or not).
        //
        // Always build *without* NoAsserts (ie, with Asserts enabled): that's the
        // only way to propagate a nonzero result code to our caller.
        Target wrapper_target = base_target
                                    .with_feature(Target::NoRuntime)
                                    .with_feature(Target::NoBoundsQuery)
                                    .without_feature(Target::NoAsserts);

        // If the base target specified the Matlab target, we want the Matlab target
        // on the wrapper instead.
        if (base_target.has_feature(Target::Matlab)) {
            wrapper_target = wrapper_target.with_feature(Target::Matlab);
        }

        Module wrapper_module(fn_name, wrapper_target);
        wrapper_module.append(LoweredFunc(fn_name, base_target_args, wrapper_body, LinkageType::ExternalPlusMetadata));

        std::map<Output, std::string> wrapper_out = {{Output::object,
                                                      temp_obj_dir.add_temp_object_file(output_files.at(Output::static_library), "_wrapper", base_target, /* in_front*/ true)}};
        debug(1) << "compile_multitarget: wrapper " << wrapper_out.at(Output::object) << "\n";
        wrapper_module.compile(wrapper_out);
    }

    if (contains(output_files, Output::c_header)) {
        Module header_module(fn_name, base_target);
        header_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        std::map<Output, std::string> header_out = {{Output::c_header, output_files.at(Output::c_header)}};
        debug(1) << "compile_multitarget: c_header " << header_out.at(Output::c_header) << "\n";
        header_module.compile(header_out);
    }

    if (contains(output_files, Output::registration)) {
        debug(1) << "compile_multitarget: registration " << output_files.at(Output::registration) << "\n";
        Module registration_module(fn_name, base_target);
        registration_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        std::map<Output, std::string> registration_out = {{Output::registration, output_files.at(Output::registration)}};
        debug(1) << "compile_multitarget: registration " << registration_out.at(Output::registration) << "\n";
        registration_module.compile(registration_out);
    }

    if (contains(output_files, Output::schedule)) {
        debug(1) << "compile_multitarget: schedule " << output_files.at(Output::schedule) << "\n";
        std::string scheduler = auto_scheduler_results.front().scheduler_name;
        if (scheduler.empty()) {
            scheduler = "(None)";
        }
        std::string machine_params = auto_scheduler_results.front().machine_params_string;
        if (machine_params.empty()) {
            machine_params = "(None)";
        }

        // Find the features that are unique to each stage (vs the baseline case).
        const auto &baseline_target = auto_scheduler_results.back().target;
        const auto &baseline_features = baseline_target.get_features_bitset();

        // Autoscheduling should be all-or-none across the subtargets;
        // if code tries to somehow only autoschedule some subtargets,
        // this code may break, and that's ok.
        std::ostringstream body;
        if (baseline_target.os == Target::OSUnknown && baseline_target.arch == Target::ArchUnknown) {
            body << "// No autoscheduler has been run for this Generator.";
        } else {
            for (size_t i = 0; i < auto_scheduler_results.size(); i++) {
                const auto &a = auto_scheduler_results[i];
                body << "\n\n";
                if (i == auto_scheduler_results.size() - 1) {
                    body << "// default schedule\n";
                    body << "{\n";
                } else {
                    auto cur_features = a.target.get_features_bitset() & ~baseline_features;
                    user_assert(cur_features.count() > 0) << "Multitarget subtargets must be distinct";
                    std::ostringstream condition;
                    for (int i = 0; i < Target::FeatureEnd; ++i) {
                        if (!cur_features[i]) continue;
                        if (!condition.str().empty()) {
                            condition << " &&\n    ";
                        }
                        condition << "target.has_feature(halide_target_feature_"
                                  << Target::feature_to_name((Target::Feature)i) << ")";
                    }
                    body << "if (" << condition.str() << ") {\n";
                }
                body << indent_string(a.schedule_source, "    ");
                body << "    return;\n";
                body << "}";
            }
        }

        std::ofstream file(output_files.at(Output::schedule));
        emit_schedule_file(fn_name, targets, scheduler, machine_params, body.str(), file);
    }

    if (contains(output_files, Output::static_library)) {
        debug(1) << "compile_multitarget: static_library " << output_files.at(Output::static_library) << "\n";
        create_static_library(temp_obj_dir.files(), base_target, output_files.at(Output::static_library));
    }

    if (contains(output_files, Output::compiler_log)) {
        debug(1) << "compile_multitarget: compiler_log " << output_files.at(Output::compiler_log) << "\n";

        std::ofstream compiler_log_file(output_files.at(Output::compiler_log));
        compiler_log_file << "[\n";
        const auto &f = temp_compiler_log_dir.files();
        for (size_t i = 0; i < f.size(); i++) {
            auto d = read_entire_file(f[i]);
            compiler_log_file.write(d.data(), d.size());
            if (i < f.size() - 1) {
                compiler_log_file << ",\n";
            }
        }
        compiler_log_file << "]\n";
        compiler_log_file.close();
        internal_assert(!compiler_log_file.fail());
    }
}

}  // namespace Halide
