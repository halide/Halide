#include "Module.h"

#include <array>
#include <fstream>
#include <future>

#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "HexagonOffload.h"
#include "IROperator.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Outputs.h"
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
    const auto found = path.rfind(".");
    if (found == std::string::npos) {
        return path + suffix;
    }
    return path.substr(0, found) + suffix + path.substr(found);
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
    return out;
}

}  // namespace

struct ModuleContents {
    mutable RefCount ref_count;
    std::string name, auto_schedule;
    Target target;
    std::vector<Buffer<>> buffers;
    std::vector<Internal::LoweredFunc> functions;
    std::vector<Module> submodules;
    std::vector<ExternalCode> external_code;
    std::map<std::string, std::string> metadata_name_map;
    bool any_strict_float{false};
};

template<>
RefCount &ref_count<ModuleContents>(const ModuleContents *t) {
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
        this->args.push_back(i);
    }
}

}  // namespace Internal

using namespace Halide::Internal;

Module::Module(const std::string &name, const Target &target) :
    contents(new Internal::ModuleContents) {
    contents->name = name;
    contents->target = target;
}

void Module::set_auto_schedule(const std::string &auto_schedule) {
    internal_assert(contents->auto_schedule.empty());
    contents->auto_schedule = auto_schedule;
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

const std::string &Module::auto_schedule() const {
    return contents->auto_schedule;
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
        if (contents->auto_schedule.empty()) {
           file << "// auto_schedule_outputs() was not called for this Generator.\n";
        } else {
           file << contents->auto_schedule;
        }
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
        debug(1) << "compile_multitarget: compile_sub_target " << sub_out.object_name << "\n";
        sub_module.compile(sub_out);

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

    if (!output_files.static_library_name.empty()) {
        debug(1) << "compile_multitarget: static_library_name " << output_files.static_library_name << "\n";
        create_static_library(temp_dir.files(), base_target, output_files.static_library_name);
    }
}

}  // namespace Halide
