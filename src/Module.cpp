#include "Module.h"

#include <array>
#include <fstream>
#include <future>
#include <memory>
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
#include "StmtToViz.h"

namespace Halide {
namespace Internal {

// This is the One True Source of the known output types for halide,
// and the appropriate file extension for each output type. If you are
// explicitly managing file extensions somewhere else, you are probably
// doing it wrong; please prefer to use this table as the source of truth.
std::map<OutputFileType, const OutputInfo> get_output_info(const Target &target) {
    constexpr bool IsMulti = true;
    constexpr bool IsSingle = false;
    const bool is_windows_coff = target.os == Target::Windows;
    std::map<OutputFileType, const OutputInfo> ext = {
        {OutputFileType::assembly, {"assembly", ".s", IsMulti}},
        {OutputFileType::bitcode, {"bitcode", ".bc", IsMulti}},
        {OutputFileType::c_header, {"c_header", ".h", IsSingle}},
        {OutputFileType::c_source, {"c_source", ".halide_generated.cpp", IsSingle}},
        {OutputFileType::compiler_log, {"compiler_log", ".halide_compiler_log", IsSingle}},
        {OutputFileType::cpp_stub, {"cpp_stub", ".stub.h", IsSingle}},
        {OutputFileType::featurization, {"featurization", ".featurization", IsMulti}},
        {OutputFileType::function_info_header, {"function_info_header", ".function_info.h", IsSingle}},
        {OutputFileType::hlpipe, {"hlpipe", ".hlpipe", IsSingle}},
        {OutputFileType::llvm_assembly, {"llvm_assembly", ".ll", IsMulti}},
        {OutputFileType::object, {"object", is_windows_coff ? ".obj" : ".o", IsMulti}},
        {OutputFileType::python_extension, {"python_extension", ".py.cpp", IsSingle}},
        {OutputFileType::pytorch_wrapper, {"pytorch_wrapper", ".pytorch.h", IsSingle}},
        {OutputFileType::registration, {"registration", ".registration.cpp", IsSingle}},
        {OutputFileType::schedule, {"schedule", ".schedule.h", IsSingle}},
        {OutputFileType::static_library, {"static_library", is_windows_coff ? ".lib" : ".a", IsSingle}},
        {OutputFileType::stmt, {"stmt", ".stmt", IsMulti}},
        {OutputFileType::stmt_html, {"stmt_html", ".stmt.html", IsMulti}},
    };
    return ext;
}

namespace {

class TemporaryFileDir final {
public:
    TemporaryFileDir()
        : dir_path(dir_make_temp()) {
    }
    ~TemporaryFileDir() {
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

public:
    TemporaryFileDir(const TemporaryFileDir &) = delete;
    TemporaryFileDir &operator=(const TemporaryFileDir &) = delete;
    TemporaryFileDir(TemporaryFileDir &&) = delete;
    TemporaryFileDir &operator=(TemporaryFileDir &&) = delete;
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

void validate_outputs(const std::map<OutputFileType, std::string> &in) {
    // We don't care about the extensions, so any Target will do
    auto known = get_output_info(Target());
    for (const auto &it : in) {
        internal_assert(!it.second.empty()) << "Empty value for output: " << known.at(it.first).name;
    }
}

bool contains(const std::map<OutputFileType, std::string> &in, const OutputFileType &key) {
    return in.find(key) != in.end();
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
    for (char c : src) {
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
                        const std::string &autoscheduler_params_string,
                        const std::string &body,
                        std::ostream &stream) {
    std::string s = R"INLINE_CODE(#ifndef $CLEANNAME$_SCHEDULE_H
#define $CLEANNAME$_SCHEDULE_H

// MACHINE GENERATED -- DO NOT EDIT
// This schedule was automatically generated by $SCHEDULER$
// for target=$TARGET$  // NOLINT
// with $MPNAME$=$MACHINEPARAMS$

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
        if (!target_string.empty()) {
            target_string += ",";
        }
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
    s = replace_all(s, "$MPNAME$", "autoscheduler_params");
    s = replace_all(s, "$MACHINEPARAMS$", autoscheduler_params_string);
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
    MetadataNameMap metadata_name_map;
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

Module::Module(const std::string &name, const Target &target, const MetadataNameMap &metadata_name_map)
    : contents(new Internal::ModuleContents) {
    contents->name = name;
    contents->target = target;
    contents->metadata_name_map = metadata_name_map;
}

void Module::set_auto_scheduler_results(const AutoSchedulerResults &auto_scheduler_results) {
    internal_assert(contents->auto_scheduler_results.get() == nullptr);
    contents->auto_scheduler_results = std::make_unique<AutoSchedulerResults>(auto_scheduler_results);
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

Module link_modules(const std::string &name, const std::vector<Module> &modules) {
    Module output(name, modules.front().target());

    for (const auto &input : modules) {
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
    for (const auto &m : submodules()) {
        Module copy(m.resolve_submodules());

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

MetadataNameMap Module::get_metadata_name_map() const {
    return contents->metadata_name_map;
}

void Module::compile(const std::map<OutputFileType, std::string> &output_files) const {
    validate_outputs(output_files);

    if (target().has_feature(Target::OpenGLCompute)) {
        user_warning << "WARNING: OpenGLCompute is deprecated in Halide 16 and will be removed in Halide 17.\n";
    }

    // Minor but worthwhile optimization: if all of the output files are of types that won't
    // ever rely on submodules (e.g.: toplevel declarations in C/C++), don't bother resolving
    // the submodules, which can call compile_to_buffer().
    const auto should_ignore_submodules = [](const std::map<OutputFileType, std::string> &output_files) {
        const size_t uninteresting_count = output_files.count(OutputFileType::c_header) +
                                           output_files.count(OutputFileType::function_info_header) +
                                           output_files.count(OutputFileType::registration);
        return output_files.size() == uninteresting_count;
    };

    // If there are submodules, recursively lower submodules to
    // buffers on a copy of the module being compiled, then compile
    // the copied module.
    if (!submodules().empty() && !should_ignore_submodules(output_files)) {
        debug(1) << "Module.compile(): begin submodules\n";
        resolve_submodules().compile(output_files);
        debug(1) << "Module.compile(): end submodules\n";
        return;
    }

    TemporaryFileDir temp_assembly_dir;

    std::string assembly_path;
    if (contains(output_files, OutputFileType::assembly)) {
        assembly_path = output_files.at(OutputFileType::assembly);
    } else if (contains(output_files, OutputFileType::stmt_html)) {
        // We need assembly in order to generate stmt_html, but the user doesn't
        // want it on its own, so we will generate it to a temp directory, since some
        // build systems (e.g. Bazel) are strict about what you can generate to the 'expected'
        // build-products directory (but grant exemptions for /tmp).
        assembly_path = temp_assembly_dir.add_temp_file(output_files.at(OutputFileType::stmt_html),
                                                        get_output_info(target()).at(OutputFileType::assembly).extension,
                                                        target());
        debug(1) << "Module.compile(): creating temp file for assembly output at " << assembly_path << "\n";
    }

    auto *logger = get_compiler_logger();
    if (contains(output_files, OutputFileType::object) || contains(output_files, OutputFileType::assembly) ||
        contains(output_files, OutputFileType::bitcode) || contains(output_files, OutputFileType::llvm_assembly) ||
        contains(output_files, OutputFileType::static_library) || !assembly_path.empty()) {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(*this, context));

        if (contains(output_files, OutputFileType::object)) {
            const auto &f = output_files.at(OutputFileType::object);
            debug(1) << "Module.compile(): object " << f << "\n";
            auto out = make_raw_fd_ostream(f);
            compile_llvm_module_to_object(*llvm_module, *out);
            if (logger) {
                out->flush();
                logger->record_object_code_size(file_stat(f).file_size);
            }
        }
        if (contains(output_files, OutputFileType::static_library)) {
            // To simplify the code, we always emit to a temporary file
            // here, even if output_files.at(OutputFileType::object) was also set:
            // in practice, no real-world code ever sets both object and static_library
            // at the same time, so there is no meaningful performance advantage
            // to be had.
            //
            // (Use a separate TemporaryFileDir here so we don't try to embed assembly files from
            // `temp_assembly_dir` into a static library...)
            TemporaryFileDir temp_object_dir;
            {
                std::string object = temp_object_dir.add_temp_object_file(output_files.at(OutputFileType::static_library), "", target());
                debug(1) << "Module.compile(): temporary object " << object << "\n";
                auto out = make_raw_fd_ostream(object);
                compile_llvm_module_to_object(*llvm_module, *out);
                out->flush();  // create_static_library() is happier if we do this
                if (logger && !contains(output_files, OutputFileType::object)) {
                    // Don't double-record object-code size if we already recorded it for object
                    logger->record_object_code_size(file_stat(object).file_size);
                }
            }
            debug(1) << "Module.compile(): static_library " << output_files.at(OutputFileType::static_library) << "\n";
            Target base_target(target().os, target().arch, target().bits, target().processor_tune);
            create_static_library(temp_object_dir.files(), base_target, output_files.at(OutputFileType::static_library));
        }
        // Don't use contains() here, we might need assembly output for stmt_html
        if (!assembly_path.empty()) {
            debug(1) << "Module.compile(): assembly " << assembly_path << "\n";
            auto out = make_raw_fd_ostream(assembly_path);
            compile_llvm_module_to_assembly(*llvm_module, *out);
        }
        if (contains(output_files, OutputFileType::bitcode)) {
            debug(1) << "Module.compile(): bitcode " << output_files.at(OutputFileType::bitcode) << "\n";
            auto out = make_raw_fd_ostream(output_files.at(OutputFileType::bitcode));
            compile_llvm_module_to_llvm_bitcode(*llvm_module, *out);
        }
        if (contains(output_files, OutputFileType::llvm_assembly)) {
            debug(1) << "Module.compile(): llvm_assembly " << output_files.at(OutputFileType::llvm_assembly) << "\n";
            auto out = make_raw_fd_ostream(output_files.at(OutputFileType::llvm_assembly));
            compile_llvm_module_to_llvm_assembly(*llvm_module, *out);
        }
    }

    if (contains(output_files, OutputFileType::stmt)) {
        debug(1) << "Module.compile(): stmt " << output_files.at(OutputFileType::stmt) << "\n";
        std::ofstream file(output_files.at(OutputFileType::stmt));
        file << *this;
    }
    if (contains(output_files, OutputFileType::stmt_html)) {
        internal_assert(!assembly_path.empty());
        debug(1) << "Module.compile(): stmt_html " << output_files.at(OutputFileType::stmt_html) << "\n";
        Internal::print_to_viz(output_files.at(OutputFileType::stmt_html), *this, assembly_path);
    }
    if (contains(output_files, OutputFileType::function_info_header)) {
        debug(1) << "Module.compile(): function_info_header " << output_files.at(OutputFileType::function_info_header) << "\n";
        std::ofstream file(output_files.at(OutputFileType::function_info_header));
        Internal::CodeGen_C cg(file,
                               target(),
                               Internal::CodeGen_C::CPlusPlusFunctionInfoHeader,
                               output_files.at(OutputFileType::function_info_header));
        cg.compile(*this);
    }
    if (contains(output_files, OutputFileType::c_header)) {
        debug(1) << "Module.compile(): c_header " << output_files.at(OutputFileType::c_header) << "\n";
        std::ofstream file(output_files.at(OutputFileType::c_header));
        Internal::CodeGen_C cg(file,
                               target(),
                               target().has_feature(Target::CPlusPlusMangling) ? Internal::CodeGen_C::CPlusPlusHeader : Internal::CodeGen_C::CHeader,
                               output_files.at(OutputFileType::c_header));
        cg.compile(*this);
    }
    if (contains(output_files, OutputFileType::c_source)) {
        debug(1) << "Module.compile(): c_source " << output_files.at(OutputFileType::c_source) << "\n";
        std::ofstream file(output_files.at(OutputFileType::c_source));
        Internal::CodeGen_C cg(file,
                               target(),
                               target().has_feature(Target::CPlusPlusMangling) ? Internal::CodeGen_C::CPlusPlusImplementation : Internal::CodeGen_C::CImplementation);
        cg.compile(*this);
    }
    if (contains(output_files, OutputFileType::python_extension)) {
        debug(1) << "Module.compile(): python_extension " << output_files.at(OutputFileType::python_extension) << "\n";
        std::ofstream file(output_files.at(OutputFileType::python_extension));
        Internal::PythonExtensionGen python_extension_gen(file);
        python_extension_gen.compile(*this);
    }
    if (contains(output_files, OutputFileType::schedule)) {
        debug(1) << "Module.compile(): schedule " << output_files.at(OutputFileType::schedule) << "\n";
        std::ofstream file(output_files.at(OutputFileType::schedule));
        auto *r = contents->auto_scheduler_results.get();
        std::string body = r && !r->schedule_source.empty() ? r->schedule_source : "// No autoscheduler has been run for this Generator.\n";
        std::string scheduler = r ? r->autoscheduler_params.name : "(None)";
        std::string autoscheduler_params_string = r ? r->autoscheduler_params.to_string() : "(None)";
        emit_schedule_file(name(), {target()}, scheduler, autoscheduler_params_string, body, file);
    }
    if (contains(output_files, OutputFileType::featurization)) {
        debug(1) << "Module.compile(): featurization " << output_files.at(OutputFileType::featurization) << "\n";
        // If the featurization data is empty, just write an empty file
        std::ofstream binfile(output_files.at(OutputFileType::featurization), std::ios::binary | std::ios_base::trunc);
        auto *r = contents->auto_scheduler_results.get();
        if (r) {
            binfile.write((const char *)r->featurization.data(), r->featurization.size());
        }
        binfile.close();
    }
    if (contains(output_files, OutputFileType::registration)) {
        debug(1) << "Module.compile(): registration " << output_files.at(OutputFileType::registration) << "\n";
        std::ofstream file(output_files.at(OutputFileType::registration));
        emit_registration(*this, file);
        file.close();
        internal_assert(!file.fail());
    }
    if (contains(output_files, OutputFileType::pytorch_wrapper)) {
        debug(1) << "Module.compile(): pytorch_wrapper " << output_files.at(OutputFileType::pytorch_wrapper) << "\n";

        std::ofstream file(output_files.at(OutputFileType::pytorch_wrapper));
        Internal::CodeGen_PyTorch cg(file);
        cg.compile(*this);
        file.close();
        internal_assert(!file.fail());
    }
    if (contains(output_files, OutputFileType::compiler_log)) {
        debug(1) << "Module.compile(): compiler_log " << output_files.at(OutputFileType::compiler_log) << "\n";
        std::ofstream file(output_files.at(OutputFileType::compiler_log));
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

std::map<OutputFileType, std::string> compile_standalone_runtime(const std::map<OutputFileType, std::string> &output_files, const Target &t) {
    validate_outputs(output_files);

    Module empty("standalone_runtime", t.without_feature(Target::NoRuntime).without_feature(Target::JIT));
    // For runtime, it only makes sense to output object files or static_library, so ignore
    // everything else.
    std::map<OutputFileType, std::string> actual_outputs;
    // If the python_extension output is specified, we'll generate just the module-registration code,
    // with no functions at all. This is useful when gluing together multiple Halide functions
    // into the same Python extension.
    for (auto key : {OutputFileType::object, OutputFileType::static_library, OutputFileType::python_extension}) {
        auto it = output_files.find(key);
        if (it != output_files.end()) {
            actual_outputs[key] = it->second;
        }
    }
    empty.compile(actual_outputs);
    return actual_outputs;
}

void compile_standalone_runtime(const std::string &object_filename, const Target &t) {
    compile_standalone_runtime({{OutputFileType::object, object_filename}}, t);
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
                         const std::map<OutputFileType, std::string> &output_files,
                         const std::vector<Target> &targets,
                         const std::vector<std::string> &suffixes,
                         const ModuleFactory &module_factory,
                         const CompilerLoggerFactory &compiler_logger_factory) {
    validate_outputs(output_files);

    user_assert(!fn_name.empty()) << "Function name must be specified.\n";
    user_assert(!targets.empty()) << "Must specify at least one target.\n";
    user_assert(suffixes.empty() || suffixes.size() == targets.size())
        << "The suffixes list must be empty or the same length as the targets list.\n";

    // Some tests were mistakenly passing filenames/pathnames here, which is not kosher
    for (char c : "/\\") {
        user_assert(fn_name.find(c) == std::string::npos) << "compile_multitarget: fn_name must not contain '" << c << "', but saw '" << fn_name << "'\n";
    }

    // The final target in the list is considered "baseline", and is used
    // for (e.g.) the runtime and shared code. It is often just arch-bits-os
    // with no other features (though this is *not* a requirement).
    const Target &base_target = targets.back();

    // JIT makes no sense.
    user_assert(!base_target.has_feature(Target::JIT)) << "JIT not allowed for compile_multitarget.\n";

    const auto suffix_for_entry = [&](int i) -> std::string {
        return "-" + (suffixes.empty() ? targets[i].to_string() : suffixes[i]);
    };

    const auto add_suffixes = [&](const std::map<OutputFileType, std::string> &in, const std::string &suffix) -> std::map<OutputFileType, std::string> {
        // is_multi doesn't vary by Target, so we can pass an empty target here safely
        auto output_info = get_output_info(Target());
        std::map<OutputFileType, std::string> out = in;
        for (auto &it : out) {
            if (output_info[it.first].is_multi) {
                out[it.first] = add_suffix(it.second, suffix);
            }
        }
        return out;
    };

    // If only one target, don't bother with the runtime feature detection wrapping.
    const bool needs_wrapper = (targets.size() > 1);
    if (targets.size() == 1) {
        debug(1) << "compile_multitarget: single target is " << base_target.to_string() << "\n";
        ScopedCompilerLogger activate(compiler_logger_factory, fn_name, base_target);

        // If we want to have single-output object files use the target suffix, we'd
        // want to do this instead:
        //
        //     auto sub_out = add_suffixes(output_files, suffix_for_entry(0));
        //     module_factory(fn_name, base_target).compile(sub_out);
        //
        // This would make the filename outputs more symmetrical (ie the same for n=1 as for n>1)
        // but at the expense of breaking existing users. So for now, we're going to continue
        // with the legacy treatment below:
        reset_random_counters();
        module_factory(fn_name, base_target).compile(output_files);
        return;
    }

    user_assert(((int)contains(output_files, OutputFileType::object) + (int)contains(output_files, OutputFileType::static_library)) == 1)
        << "compile_multitarget() expects exactly one of 'object' and 'static_library' to be specified when multiple targets are specified.\n";

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

    TemporaryFileDir temp_obj_dir, temp_compiler_log_dir;
    std::vector<Expr> wrapper_args;
    std::vector<LoweredArgument> base_target_args;
    std::vector<AutoSchedulerResults> auto_scheduler_results;
    MetadataNameMap metadata_name_map;

    for (size_t i = 0; i < targets.size(); ++i) {
        const Target &target = targets[i];

        // arch-bits-os must be identical across all targets.
        if (target.os != base_target.os ||
            target.arch != base_target.arch ||
            target.bits != base_target.bits) {
            user_error << "All Targets must have matching arch-bits-os for compile_multitarget.\n";
        }
        // Some features must match across all targets.
        static const std::array<Target::Feature, 10> must_match_features = {{
            Target::ASAN,
            Target::CPlusPlusMangling,
            Target::Debug,
            Target::JIT,
            Target::MSAN,
            Target::NoRuntime,
            Target::TSAN,
            Target::SanitizerCoverage,
            Target::UserContext,
        }};
        for (auto f : must_match_features) {
            if (target.has_feature(f) != base_target.has_feature(f)) {
                user_error << "All Targets must have feature '" << Target::feature_to_name(f) << "'' set identically for compile_multitarget.\n";
                break;
            }
        }

        // Each sub-target has a function name that is the 'real' name plus a suffix
        std::string suffix = suffix_for_entry(i);
        std::string sub_fn_name = needs_wrapper ? (fn_name + suffix) : fn_name;

        // We always produce the runtime separately, so add NoRuntime explicitly.
        Target sub_fn_target = target.with_feature(Target::NoRuntime);

        // Ensure that each subtarget sees the same sequence of random numbers
        reset_random_counters();
        {
            ScopedCompilerLogger activate(compiler_logger_factory, sub_fn_name, sub_fn_target);
            Module sub_module = module_factory(sub_fn_name, sub_fn_target);
            // Re-assign every time -- should be the same across all targets anyway,
            // but base_target is always the last one we encounter.
            base_target_args = sub_module.get_function_by_name(sub_fn_name).args;

            auto sub_out = add_suffixes(output_files, suffix);
            if (contains(output_files, OutputFileType::static_library)) {
                sub_out[OutputFileType::object] = temp_obj_dir.add_temp_object_file(output_files.at(OutputFileType::static_library), suffix, target);
                sub_out.erase(OutputFileType::static_library);
            }
            sub_out.erase(OutputFileType::registration);
            sub_out.erase(OutputFileType::schedule);
            sub_out.erase(OutputFileType::c_header);
            sub_out.erase(OutputFileType::function_info_header);
            if (contains(sub_out, OutputFileType::compiler_log)) {
                sub_out[OutputFileType::compiler_log] = temp_compiler_log_dir.add_temp_file(output_files.at(OutputFileType::compiler_log), suffix, target);
            }
            debug(1) << "compile_multitarget: compile_sub_target " << sub_out[OutputFileType::object] << "\n";
            sub_module.compile(sub_out);
            const auto *r = sub_module.get_auto_scheduler_results();
            auto_scheduler_results.push_back(r ? *r : AutoSchedulerResults());
            if (target == base_target) {
                metadata_name_map = sub_module.get_metadata_name_map();
            }
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
            for (uint64_t feature : cur_target_features) {
                features_struct_args.emplace_back(UIntImm::make(UInt(64), feature));
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
        Target runtime_target(base_target.os, base_target.arch, base_target.bits, base_target.processor_tune);
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
        std::string runtime_path = contains(output_files, OutputFileType::static_library) ?
                                       temp_obj_dir.add_temp_object_file(output_files.at(OutputFileType::static_library), "_runtime", runtime_target) :
                                       add_suffix(output_files.at(OutputFileType::object), "_runtime");

        std::map<OutputFileType, std::string> runtime_out =
            {{OutputFileType::object, runtime_path}};
        debug(1) << "compile_multitarget: compile_standalone_runtime " << runtime_out.at(OutputFileType::object) << "\n";
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

        Module wrapper_module(fn_name, wrapper_target, metadata_name_map);
        wrapper_module.append(LoweredFunc(fn_name, base_target_args, wrapper_body, LinkageType::ExternalPlusMetadata));

        std::string wrapper_path = contains(output_files, OutputFileType::static_library) ?
                                       temp_obj_dir.add_temp_object_file(output_files.at(OutputFileType::static_library), "_wrapper", base_target, /* in_front*/ true) :
                                       add_suffix(output_files.at(OutputFileType::object), "_wrapper");

        std::map<OutputFileType, std::string> wrapper_out = {{OutputFileType::object, wrapper_path}};
        debug(1) << "compile_multitarget: wrapper " << wrapper_out.at(OutputFileType::object) << "\n";
        wrapper_module.compile(wrapper_out);
    }

    if (contains(output_files, OutputFileType::c_header)) {
        Module header_module(fn_name, base_target);
        header_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        std::map<OutputFileType, std::string> header_out = {{OutputFileType::c_header, output_files.at(OutputFileType::c_header)}};
        debug(1) << "compile_multitarget: c_header " << header_out.at(OutputFileType::c_header) << "\n";
        header_module.compile(header_out);
    }

    if (contains(output_files, OutputFileType::function_info_header)) {
        Module header_module(fn_name, base_target);
        header_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        std::map<OutputFileType, std::string> header_out = {{OutputFileType::function_info_header, output_files.at(OutputFileType::function_info_header)}};
        debug(1) << "compile_multitarget: function_info_header " << header_out.at(OutputFileType::function_info_header) << "\n";
        header_module.compile(header_out);
    }

    if (contains(output_files, OutputFileType::registration)) {
        debug(1) << "compile_multitarget: registration " << output_files.at(OutputFileType::registration) << "\n";
        Module registration_module(fn_name, base_target);
        registration_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        std::map<OutputFileType, std::string> registration_out = {{OutputFileType::registration, output_files.at(OutputFileType::registration)}};
        debug(1) << "compile_multitarget: registration " << registration_out.at(OutputFileType::registration) << "\n";
        registration_module.compile(registration_out);
    }

    if (contains(output_files, OutputFileType::schedule)) {
        debug(1) << "compile_multitarget: schedule " << output_files.at(OutputFileType::schedule) << "\n";
        const auto &autoscheduler_params = auto_scheduler_results.front().autoscheduler_params;
        std::string scheduler = autoscheduler_params.name.empty() ? "(None)" : autoscheduler_params.name;
        std::string autoscheduler_params_string = autoscheduler_params.name.empty() ? "(None)" : autoscheduler_params.to_string();

        // TODO(https://github.com/halide/Halide/issues/7539): this is a horrible hack;
        // the Anderson2021 autoscheduler is GPU-only, and emits the same schedule for each subtarget.
        // Avoid confusing noise in the output by just lopping off all results aftet the first one.
        // This isn't a good fix; aside from the hack here, we also are wasting time recomputing the
        // same schedule multiple times above.
        if (scheduler == "Anderson2021") {
            while (auto_scheduler_results.size() > 1) {
                auto_scheduler_results.pop_back();
            }
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
                        if (!cur_features[i]) {
                            continue;
                        }
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

        std::ofstream file(output_files.at(OutputFileType::schedule));
        emit_schedule_file(fn_name, targets, scheduler, autoscheduler_params_string, body.str(), file);
    }

    if (contains(output_files, OutputFileType::static_library)) {
        debug(1) << "compile_multitarget: static_library "
                 << output_files.at(OutputFileType::static_library) << "\n";
        create_static_library(temp_obj_dir.files(), base_target, output_files.at(OutputFileType::static_library));
    }

    if (contains(output_files, OutputFileType::compiler_log)) {
        debug(1) << "compile_multitarget: compiler_log "
                 << output_files.at(OutputFileType::compiler_log) << "\n";

        std::ofstream compiler_log_file(output_files.at(OutputFileType::compiler_log));
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
