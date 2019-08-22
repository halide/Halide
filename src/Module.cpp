#include "Module.h"

#include <array>
#include <fstream>
#include <future>

#include "CodeGen_C.h"
#include "CodeGen_PyTorch.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "HexagonOffload.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Outputs.h"
#include "Pipeline.h"
#include "PythonExtensionGen.h"
#include "StmtToHtml.h"
#include "WrapExternStages.h"

using Halide::Internal::debug;

namespace Halide {
namespace Internal {

namespace {

class TemporaryObjectFileDir final {
public:
    TemporaryObjectFileDir() : dir_path(dir_make_temp()) {}
    ~TemporaryObjectFileDir() {
        for (const auto &f : dir_files) {
            debug(1) << "file_unlink: " << f << "\n";
            file_unlink(f);
        }
        debug(1) << "dir_rmdir: " << dir_path << "\n";
        dir_rmdir(dir_path);
    }
    std::string add_temp_object_file(const std::string &base_path_name,
                                     const std::string &suffix,
                                     const Target &target,
                                     bool in_front = false) {
        const char* ext = (target.os == Target::Windows && !target.has_feature(Target::MinGW)) ? ".obj" : ".o";
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
        std::string name = dir_path + "/" + base_name + suffix + ext;
        debug(1) << "add_temp_object_file: " << name << "\n";
        if (in_front) {
            dir_files.insert(dir_files.begin(), name);
        } else {
            dir_files.push_back(name);
        }
        return name;
    }

    const std::vector<std::string> &files() { return dir_files; }
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

// Given a pathname of the form /path/to/name.old, replace extension to produce /path/to/name.new.
std::string replace_extension(const std::string &path, const std::string &new_ext) {
    size_t last_path = std::min(path.rfind('/'), path.rfind('\\'));
    if (last_path == std::string::npos) {
        last_path = 0;
    }
    size_t dot = path.find('.', last_path);
    if (dot == std::string::npos) {
        return path + new_ext;
    } else {
        return path.substr(0, dot) + new_ext;
    }
}

Outputs add_suffixes(const Outputs &in, const std::string &suffix) {
    Outputs out;
    if (!in.object_name.empty()) out.object_name = add_suffix(in.object_name, suffix);
    if (!in.assembly_name.empty()) out.assembly_name = add_suffix(in.assembly_name, suffix);
    if (!in.bitcode_name.empty()) out.bitcode_name = add_suffix(in.bitcode_name, suffix);
    if (!in.llvm_assembly_name.empty()) out.llvm_assembly_name = add_suffix(in.llvm_assembly_name, suffix);
    if (!in.c_source_name.empty()) out.c_source_name = add_suffix(in.c_source_name, suffix);
    if (!in.stmt_name.empty()) out.stmt_name = add_suffix(in.stmt_name, suffix);
    if (!in.stmt_html_name.empty()) out.stmt_html_name = add_suffix(in.stmt_html_name, suffix);
    if (!in.schedule_name.empty()) out.schedule_name = add_suffix(in.schedule_name, suffix);
    if (!in.featurization_name.empty()) out.featurization_name = add_suffix(in.featurization_name, suffix);
    if (!in.registration_name.empty()) out.registration_name = add_suffix(in.registration_name, suffix);

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
        Target::LegacyBufferWrappers,
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
    : name(name), args(args), body(body), linkage(linkage), name_mangling(name_mangling) {}

LoweredFunc::LoweredFunc(const std::string &name,
                         const std::vector<Argument> &args,
                         Stmt body,
                         LinkageType linkage,
                         NameMangling name_mangling)
    : name(name), body(body), linkage(linkage), name_mangling(name_mangling) {
    for (const Argument &i : args) {
        this->args.push_back(LoweredArgument(i));
    }
}

}  // namespace Internal

using namespace Halide::Internal;

Module::Module(const std::string &name, const Target &target) :
    contents(new Internal::ModuleContents) {
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
        debug(2) << "Submodule assembly for " << name() << ": " << "\n";
        llvm::SmallString<4096> assembly;
        llvm::raw_svector_ostream assembly_stream(assembly);
        compile_llvm_module_to_assembly(*llvm_module, assembly_stream);
        debug(2) << assembly.c_str() << "\n";
    }

    Buffer<uint8_t> result(object.size(), name());
    memcpy(result.data(), reinterpret_cast<uint8_t*>(&object[0]), object.size());
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

void Module::compile(const Outputs &output_files_arg) const {
    Outputs output_files = output_files_arg;

    // output stmt and html prior to resolving submodules. We need to
    // clear the output after writing it, otherwise the output will
    // be overwritten by recursive calls after submodules are resolved.
    if (!output_files.stmt_name.empty()) {
        debug(1) << "Module.compile(): stmt_name " << output_files.stmt_name << "\n";
        std::ofstream file(output_files.stmt_name);
        file << *this;
        output_files.stmt_name.clear();
    }
    if (!output_files.stmt_html_name.empty()) {
        debug(1) << "Module.compile(): stmt_html_name " << output_files.stmt_html_name << "\n";
        Internal::print_to_html(output_files.stmt_html_name, *this);
        output_files.stmt_html_name.clear();
    }

    // If there are submodules, recursively lower submodules to
    // buffers on a copy of the module being compiled, then compile
    // the copied module.
    if (!submodules().empty()) {
        resolve_submodules().compile(output_files);
        return;
    }

    if (!output_files.object_name.empty() || !output_files.assembly_name.empty() ||
        !output_files.bitcode_name.empty() || !output_files.llvm_assembly_name.empty() ||
        !output_files.static_library_name.empty()) {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(*this, context));

        if (!output_files.object_name.empty()) {
            debug(1) << "Module.compile(): object_name " << output_files.object_name << "\n";
            auto out = make_raw_fd_ostream(output_files.object_name);
            compile_llvm_module_to_object(*llvm_module, *out);
        }
        if (!output_files.static_library_name.empty()) {
            // To simplify the code, we always create a temporary object output
            // here, even if output_files.object_name was also set: in practice,
            // no real-world code ever sets both object_name and static_library_name
            // at the same time, so there is no meaningful performance advantage
            // to be had.
            TemporaryObjectFileDir temp_dir;
            {
                std::string object_name = temp_dir.add_temp_object_file(output_files.static_library_name, "", target());
                debug(1) << "Module.compile(): temporary object_name " << object_name << "\n";
                auto out = make_raw_fd_ostream(object_name);
                compile_llvm_module_to_object(*llvm_module, *out);
                out->flush();  // create_static_library() is happier if we do this
            }
            debug(1) << "Module.compile(): static_library_name " << output_files.static_library_name << "\n";
            Target base_target(target().os, target().arch, target().bits);
            create_static_library(temp_dir.files(), base_target, output_files.static_library_name);
        }
        if (!output_files.assembly_name.empty()) {
            debug(1) << "Module.compile(): assembly_name " << output_files.assembly_name << "\n";
            auto out = make_raw_fd_ostream(output_files.assembly_name);
            compile_llvm_module_to_assembly(*llvm_module, *out);
        }
        if (!output_files.bitcode_name.empty()) {
            debug(1) << "Module.compile(): bitcode_name " << output_files.bitcode_name << "\n";
            auto out = make_raw_fd_ostream(output_files.bitcode_name);
            compile_llvm_module_to_llvm_bitcode(*llvm_module, *out);
        }
        if (!output_files.llvm_assembly_name.empty()) {
            debug(1) << "Module.compile(): llvm_assembly_name " << output_files.llvm_assembly_name << "\n";
            auto out = make_raw_fd_ostream(output_files.llvm_assembly_name);
            compile_llvm_module_to_llvm_assembly(*llvm_module, *out);
        }
    }
    if (!output_files.c_header_name.empty()) {
        debug(1) << "Module.compile(): c_header_name " << output_files.c_header_name << "\n";
        std::ofstream file(output_files.c_header_name);
        Internal::CodeGen_C cg(file,
                               target(),
                               target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusHeader : Internal::CodeGen_C::CHeader,
                               output_files.c_header_name);
        cg.compile(*this);
    }
    if (!output_files.c_source_name.empty()) {
        debug(1) << "Module.compile(): c_source_name " << output_files.c_source_name << "\n";
        std::ofstream file(output_files.c_source_name);
        Internal::CodeGen_C cg(file,
                               target(),
                               target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusImplementation : Internal::CodeGen_C::CImplementation);
        cg.compile(*this);
    }
    if (!output_files.python_extension_name.empty()) {
        debug(1) << "Module.compile(): python_extension_name " << output_files.python_extension_name << "\n";
        std::string c_header_name = output_files.c_header_name;
        if (c_header_name.empty()) {
          // If we we're not generating a header right now, guess the filename.
          c_header_name = replace_extension(output_files.python_extension_name, ".h");
        }
        std::ofstream file(output_files.python_extension_name);
        Internal::PythonExtensionGen python_extension_gen(file,
                                                          c_header_name,
                                                          target());
        python_extension_gen.compile(*this);
    }
    if (!output_files.schedule_name.empty()) {
        debug(1) << "Module.compile(): schedule_name " << output_files.schedule_name << "\n";
        std::ofstream file(output_files.schedule_name);
        auto *r = contents->auto_scheduler_results.get();
        std::string scheduler = r  ? r->scheduler_name : "(None)";
        std::string machine_params = r  ? r->machine_params_string : "(None)";
        std::string body = r && !r->schedule_source.empty()
            ? r->schedule_source
            : "// No autoscheduler has been run for this Generator.\n";
        emit_schedule_file(name(), {target()}, scheduler, machine_params, body, file);
    }
    if (!output_files.featurization_name.empty()) {
        debug(1) << "Module.compile(): featurization_name " << output_files.featurization_name << "\n";
        // If the featurization data is empty, just write an empty file
        std::ofstream binfile(output_files.featurization_name, std::ios::binary | std::ios_base::trunc);
        auto *r = contents->auto_scheduler_results.get();
        if (r) {
            binfile.write((const char *) r->featurization.data(), r->featurization.size());
        }
        binfile.close();
    }
    if (!output_files.registration_name.empty()) {
        debug(1) << "Module.compile(): registration_name " << output_files.registration_name << "\n";
        std::ofstream file(output_files.registration_name);
        emit_registration(*this, file);
        file.close();
        internal_assert(!file.fail());
    }
    if (!output_files.pytorch_wrapper_name.empty()) {
      debug(1) << "Module.compile(): pytorch_wrapper_name " << output_files.pytorch_wrapper_name << "\n" ;
      std::ofstream file(output_files.pytorch_wrapper_name+".h");
      Internal::CodeGen_PyTorch cg(file, target(), output_files.c_header_name);
      cg.compile(*this);
    }
}

Outputs compile_standalone_runtime(const Outputs &output_files, Target t) {
    Module empty("standalone_runtime", t.without_feature(Target::NoRuntime).without_feature(Target::JIT));
    // For runtime, it only makes sense to output object files or static_library, so ignore
    // everything else.
    Outputs actual_outputs = Outputs().object(output_files.object_name).static_library(output_files.static_library_name);
    empty.compile(actual_outputs);
    return actual_outputs;
}

void compile_standalone_runtime(const std::string &object_filename, Target t) {
    compile_standalone_runtime(Outputs().object(object_filename), t);
}

void compile_multitarget(const std::string &fn_name,
                         const Outputs &output_files,
                         const std::vector<Target> &targets,
                         ModuleProducer module_producer,
                         const std::map<std::string, std::string> &suffixes) {
    user_assert(!fn_name.empty()) << "Function name must be specified.\n";
    user_assert(!targets.empty()) << "Must specify at least one target.\n";

    // You can't ask for .o files when doing this; it's not really useful,
    // and would complicate output (we might have to do multiple passes
    // if different values for NoRuntime are specified)... so just forbid
    // it up front.
    user_assert(output_files.object_name.empty()) << "Cannot request object_name for compile_multitarget.\n";

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
        module_producer(fn_name, base_target).compile(output_files);
        return;
    }

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

    TemporaryObjectFileDir temp_dir;
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
        static const std::array<Target::Feature, 8> must_match_features = {{
            Target::ASAN,
            Target::CPlusPlusMangling,
            Target::JIT,
            Target::Matlab,
            Target::MSAN,
            Target::NoRuntime,
            Target::TSAN,
            Target::UserContext,
        }};
        for (auto f : must_match_features) {
            if (target.has_feature(f) != base_target.has_feature(f)) {
                user_error << "All Targets must have feature " << f << " set identically for compile_multitarget.\n";
                break;
            }
        }

        // Each sub-target has a function name that is the 'real' name plus a suffix
        // (which defaults to the target string but can be customized via the suffixes map)
        std::string suffix = replace_all(target.to_string(), "-", "_");
        auto it = suffixes.find(suffix);
        if (it != suffixes.end()) {
          suffix = it->second;
        }
        suffix = "_" + suffix;
        std::string sub_fn_name = needs_wrapper ? (fn_name + suffix) : fn_name;

        // We always produce the runtime separately, so add NoRuntime explicitly.
        // Matlab should be added to the wrapper pipeline below, instead of each sub-pipeline.
        Target sub_fn_target = target.with_feature(Target::NoRuntime);
        if (needs_wrapper) {
            sub_fn_target = sub_fn_target.without_feature(Target::Matlab);
        }

        Module sub_module = module_producer(sub_fn_name, sub_fn_target);
        // Re-assign every time -- should be the same across all targets anyway,
        // but base_target is always the last one we encounter.
        base_target_args = sub_module.get_function_by_name(sub_fn_name).args;

        Outputs sub_out = add_suffixes(output_files, suffix);
        internal_assert(sub_out.object_name.empty());
        sub_out.object_name = temp_dir.add_temp_object_file(output_files.static_library_name, suffix, target);
        sub_out.registration_name.clear();
        sub_out.schedule_name.clear();
        debug(1) << "compile_multitarget: compile_sub_target " << sub_out.object_name << "\n";
        sub_module.compile(sub_out);
        auto *r = sub_module.get_auto_scheduler_results();
        auto_scheduler_results.push_back(r ? *r : AutoSchedulerResults());


        uint64_t cur_target_features[kFeaturesWordCount] = {0};
        for (int i = 0; i < Target::FeatureEnd; ++i) {
            if (target.has_feature((Target::Feature) i)) {
                cur_target_features[i >> 6] |= ((uint64_t) 1) << (i & 63);
            }
        }

        Expr can_use;
        if (target != base_target) {
            std::vector<Expr> features_struct_args;
            for (int i = 0; i < kFeaturesWordCount; ++i) {
                features_struct_args.push_back(UIntImm::make(UInt(64), cur_target_features[i]));
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
        wrapper_args.push_back(sub_fn_name);
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
            if (runtime_features[word] & (((uint64_t) 1) << bit)) {
                runtime_target.set_feature((Target::Feature) i);
            }
        }
        Outputs runtime_out = Outputs().object(
            temp_dir.add_temp_object_file(output_files.static_library_name, "_runtime", runtime_target));
        debug(1) << "compile_multitarget: compile_standalone_runtime " << runtime_out.static_library_name << "\n";
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
        // only way to propagate a nonzero result code to our caller. (Note that this
        // does mean we get redundant check-for-null tests in the wrapper code for buffer_t*
        // arguments; this is regrettable but fairly minor in terms of both code size and speed,
        // at least for real-world code.)
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

        // Add a wrapper to accept old buffer_ts
        add_legacy_wrapper(wrapper_module, wrapper_module.functions().back());

        Outputs wrapper_out = Outputs().object(
            temp_dir.add_temp_object_file(output_files.static_library_name, "_wrapper", base_target, /* in_front*/ true));
        debug(1) << "compile_multitarget: wrapper " << wrapper_out.object_name << "\n";
        wrapper_module.compile(wrapper_out);
    }

    if (!output_files.c_header_name.empty()) {
        Module header_module(fn_name, base_target);
        header_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        // Add a wrapper to accept old buffer_ts
        add_legacy_wrapper(header_module, header_module.functions().back());
        Outputs header_out = Outputs().c_header(output_files.c_header_name);
        debug(1) << "compile_multitarget: c_header_name " << header_out.c_header_name << "\n";
        header_module.compile(header_out);
    }

    if (!output_files.registration_name.empty()) {
        debug(1) << "compile_multitarget: registration_name " << output_files.registration_name << "\n";
        Module registration_module(fn_name, base_target);
        registration_module.append(LoweredFunc(fn_name, base_target_args, {}, LinkageType::ExternalPlusMetadata));
        Outputs registration_out = Outputs().registration(output_files.registration_name);
        registration_module.compile(registration_out);
    }

    if (!output_files.schedule_name.empty()) {
        debug(1) << "compile_multitarget: schedule_name " << output_files.schedule_name << "\n";
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
                                << Target::feature_to_name((Target::Feature) i) << ")";
                  }
                  body << "if (" << condition.str() << ") {\n";
              }
              body << indent_string(a.schedule_source, "    ");
              body << "    return;\n";
              body << "}";
            }
        }

        std::ofstream file(output_files.schedule_name);
        emit_schedule_file(fn_name, targets, scheduler, machine_params, body.str(), file);
    }

    if (!output_files.static_library_name.empty()) {
        debug(1) << "compile_multitarget: static_library_name " << output_files.static_library_name << "\n";
        create_static_library(temp_dir.files(), base_target, output_files.static_library_name);
    }
}

}  // namespace Halide
