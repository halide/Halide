#include "Module.h"

#include <array>
#include <fstream>

#include "CodeGen_C.h"
#include "CodeGen_Internal.h"
#include "Debug.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "IROperator.h"
#include "Outputs.h"
#include "StmtToHtml.h"

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
        const char* ext = target.os == Target::Windows && !target.has_feature(Target::MinGW) ? ".obj" : ".o";
        std::string name = dir_path + "/" + split_string(base_path_name, "/").back() + suffix + ext;
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

Outputs add_suffixes(const Outputs &in, const std::string &suffix) {
    Outputs out;
    if (!in.object_name.empty()) out.object_name = add_suffix(in.object_name, suffix);
    if (!in.assembly_name.empty()) out.assembly_name = add_suffix(in.assembly_name, suffix);
    if (!in.bitcode_name.empty()) out.bitcode_name = add_suffix(in.bitcode_name, suffix);
    if (!in.llvm_assembly_name.empty()) out.llvm_assembly_name = add_suffix(in.llvm_assembly_name, suffix);
    if (!in.c_source_name.empty()) out.c_source_name = add_suffix(in.c_source_name, suffix);
    if (!in.stmt_name.empty()) out.stmt_name = add_suffix(in.stmt_name, suffix);
    if (!in.stmt_html_name.empty()) out.stmt_html_name = add_suffix(in.stmt_html_name, suffix);
    return out;
}

}  // namespace

struct ModuleContents {
    mutable RefCount ref_count;
    std::string name;
    Target target;
    std::vector<Internal::BufferPtr> buffers;
    std::vector<Internal::LoweredFunc> functions;
};

template<>
EXPORT RefCount &ref_count<ModuleContents>(const ModuleContents *f) {
    return f->ref_count;
}

template<>
EXPORT void destroy<ModuleContents>(const ModuleContents *f) {
    delete f;
}

LoweredFunc::LoweredFunc(const std::string &name, const std::vector<LoweredArgument> &args, Stmt body, LinkageType linkage)
    : name(name), args(args), body(body), linkage(linkage) {}

LoweredFunc::LoweredFunc(const std::string &name, const std::vector<Argument> &args, Stmt body, LinkageType linkage)
    : name(name), body(body), linkage(linkage) {
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

const Target &Module::target() const {
    return contents->target;
}

const std::string &Module::name() const {
    return contents->name;
}

const std::vector<Internal::BufferPtr> &Module::buffers() const {
    return contents->buffers;
}

const std::vector<Internal::LoweredFunc> &Module::functions() const {
    return contents->functions;
}

void Module::append(const Internal::BufferPtr &buffer) {
    contents->buffers.push_back(buffer);
}

void Module::append(const Internal::LoweredFunc &function) {
    contents->functions.push_back(function);
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

void Module::compile(const Outputs &output_files) const {
    if (!output_files.object_name.empty() || !output_files.assembly_name.empty() ||
        !output_files.bitcode_name.empty() || !output_files.llvm_assembly_name.empty() ||
        !output_files.static_library_name.empty()) {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(*this, context));

        if (!output_files.object_name.empty() || !output_files.static_library_name.empty()) {
            // We must always generate the object files here, either because they are
            // needed directly, or as temporary inputs to create a static library.
            // If they are just temporary inputs, we delete them when we're done,
            // to minimize the cruft left laying around in build products directory.
            std::unique_ptr<TemporaryObjectFileDir> temp_dir;

            std::string object_name = output_files.object_name;
            if (object_name.empty()) {
                temp_dir = std::unique_ptr<TemporaryObjectFileDir>(new TemporaryObjectFileDir());
                object_name = temp_dir->add_temp_object_file(output_files.static_library_name, "", target());
            }

            {
                debug(1) << "Module.compile(): object_name " << object_name << "\n";
                auto out = make_raw_fd_ostream(object_name);
                if (target().arch == Target::PNaCl) {
                    compile_llvm_module_to_llvm_bitcode(*llvm_module, *out);
                } else {
                    compile_llvm_module_to_object(*llvm_module, *out);
                }
                out->flush();
            }

            if (!output_files.static_library_name.empty()) {
                debug(1) << "Module.compile(): static_library_name " << output_files.static_library_name << "\n";
                Target base_target(target().os, target().arch, target().bits);
                create_static_library({object_name}, base_target, output_files.static_library_name);
            }
        }
        if (!output_files.assembly_name.empty()) {
            debug(1) << "Module.compile(): assembly_name " << output_files.assembly_name << "\n";
            auto out = make_raw_fd_ostream(output_files.assembly_name);
            if (target().arch == Target::PNaCl) {
                compile_llvm_module_to_llvm_assembly(*llvm_module, *out);
            } else {
                compile_llvm_module_to_assembly(*llvm_module, *out);
            }
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
                               target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusHeader : Internal::CodeGen_C::CHeader,
                               output_files.c_header_name);
        cg.compile(*this);
    }
    if (!output_files.c_source_name.empty()) {
        debug(1) << "Module.compile(): c_source_name " << output_files.c_source_name << "\n";
        std::ofstream file(output_files.c_source_name);
        Internal::CodeGen_C cg(file,
                               target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusImplementation : Internal::CodeGen_C::CImplementation);
        cg.compile(*this);
    }
    if (!output_files.stmt_name.empty()) {
        debug(1) << "Module.compile(): stmt_name " << output_files.stmt_name << "\n";
        std::ofstream file(output_files.stmt_name);
        file << *this;
    }
    if (!output_files.stmt_html_name.empty()) {
        debug(1) << "Module.compile(): stmt_html_name " << output_files.stmt_html_name << "\n";
        Internal::print_to_html(output_files.stmt_html_name, *this);
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
                         ModuleProducer module_producer) {
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

    // PNaCl might work, but is untested in this path.
    user_assert(base_target.arch != Target::PNaCl) << "PNaCl not allowed for compile_multitarget.\n";

    // If only one target, don't bother with the runtime feature detection wrapping.
    if (targets.size() == 1) {
        debug(1) << "compile_multitarget: single target is " << base_target.to_string() << "\n";
        module_producer(fn_name, base_target).compile(output_files);
        return;
    }

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
        static const std::array<Target::Feature, 6> must_match_features = {{
            Target::CPlusPlusMangling,
            Target::JIT,
            Target::Matlab,
            Target::NoRuntime,
            Target::UserContext,
        }};
        for (auto f : must_match_features) {
            if (target.has_feature(f) != base_target.has_feature(f)) {
                user_error << "All Targets must have feature " << f << " set identically for compile_multitarget.\n";
                break;
            }
        }

        // Each sub-target has a function name that is the 'real' name plus the target string.
        auto suffix = "_" + replace_all(target.to_string(), "-", "_");
        std::string sub_fn_name = fn_name + suffix;

        // We always produce the runtime separately, so add NoRuntime explicitly.
        // Matlab should be added to the wrapper pipeline below, instead of each sub-pipeline.
        Target sub_fn_target = target
            .with_feature(Target::NoRuntime)
            .without_feature(Target::Matlab);

        Module module = module_producer(sub_fn_name, sub_fn_target);
        Outputs sub_out = add_suffixes(output_files, suffix);
        if (sub_out.object_name.empty()) {
            sub_out.object_name = temp_dir.add_temp_object_file(output_files.static_library_name, suffix, target);
        }
        module.compile(sub_out);

        static_assert(sizeof(uint64_t)*8 >= Target::FeatureEnd, "Features will not fit in uint64_t");
        uint64_t feature_bits = 0;
        for (int i = 0; i < Target::FeatureEnd; ++i) {
            if (target.has_feature(static_cast<Target::Feature>(i))) {
                feature_bits |= static_cast<uint64_t>(1) << i;
            }
        }

        Expr can_use = Call::make(Int(32), "halide_can_use_target_features", {UIntImm::make(UInt(64), feature_bits)}, Call::Extern);

        if (target == base_target) {
            can_use = IntImm::make(Int(32), 1);
            base_target_args = module.functions().back().args;
        }

        wrapper_args.push_back(can_use != 0);
        wrapper_args.push_back(sub_fn_name);
    }

    // If we haven't specified "no runtime", build a runtime with the base target
    // and add that to the result.
    if (!base_target.has_feature(Target::NoRuntime)) {
        const Target runtime_target = base_target.without_feature(Target::NoRuntime);
        compile_standalone_runtime(Outputs().object(temp_dir.add_temp_object_file(output_files.static_library_name, "_runtime", runtime_target)),
            runtime_target);
    }

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
    wrapper_module.append(LoweredFunc(fn_name, base_target_args, wrapper_body, LoweredFunc::External));
    wrapper_module.compile(Outputs().object(temp_dir.add_temp_object_file(output_files.static_library_name, "_wrapper", base_target, /* in_front*/ true)));

    if (!output_files.c_header_name.empty()) {
        debug(1) << "compile_multitarget: c_header_name " << output_files.c_header_name << "\n";
        wrapper_module.compile(Outputs().c_header(output_files.c_header_name));
    }
    if (!output_files.static_library_name.empty()) {
        debug(1) << "compile_multitarget: static_library_name " << output_files.static_library_name << "\n";
        create_static_library(temp_dir.files(), base_target, output_files.static_library_name);
    }
}

}  // namespace Halide
