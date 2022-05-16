#include <algorithm>
#include <atomic>
#include <utility>

#include "Argument.h"
#include "CodeGen_Internal.h"
#include "FindCalls.h"
#include "Func.h"
#include "IRVisitor.h"
#include "InferArguments.h"
#include "LLVM_Output.h"
#include "Lower.h"
#include "Module.h"
#include "ParamMap.h"
#include "Pipeline.h"
#include "PrintLoopNest.h"
#include "RealizationOrder.h"
#include "WasmExecutor.h"

using namespace Halide::Internal;

namespace Halide {

using std::string;
using std::vector;

namespace {

std::string output_name(const string &filename, const string &fn_name, const string &ext) {
    return !filename.empty() ? filename : (fn_name + ext);
}

std::string output_name(const string &filename, const Module &m, const string &ext) {
    return output_name(filename, m.name(), ext);
}

std::map<OutputFileType, std::string> single_output(const string &filename, const Module &m, OutputFileType output_type) {
    auto ext = get_output_info(m.target());
    std::map<OutputFileType, std::string> outputs = {
        {output_type, output_name(filename, m, ext.at(output_type).extension)}};
    return outputs;
}

std::map<OutputFileType, std::string> static_library_outputs(const string &filename_prefix, const Target &target) {
    auto ext = get_output_info(target);
    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::c_header, filename_prefix + ext.at(OutputFileType::c_header).extension},
        {OutputFileType::static_library, filename_prefix + ext.at(OutputFileType::static_library).extension},
    };
    return outputs;
}

std::map<OutputFileType, std::string> object_file_outputs(const string &filename_prefix, const Target &target) {
    auto ext = get_output_info(target);
    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::c_header, filename_prefix + ext.at(OutputFileType::c_header).extension},
        {OutputFileType::object, filename_prefix + ext.at(OutputFileType::object).extension},
    };
    return outputs;
}

}  // namespace

struct PipelineContents {
    mutable RefCount ref_count;

    // Cached lowered stmt
    Module module;

    // Name of the generated function
    string name;

    // Cached jit-compiled code
    JITModule jit_module;
    Target jit_target;

    // Cached compiled JavaScript and/or wasm if defined */
    WasmModule wasm_module;

    /** Clear all cached state */
    void invalidate_cache() {
        module = Module("", Target());
        jit_module = JITModule();
        jit_target = Target();
        inferred_args.clear();
        wasm_module = WasmModule();
    }

    // The outputs
    vector<Function> outputs;

    // JIT custom overrides
    JITHandlers jit_handlers;

    /** The user context that's used when jitting. This is not
     * settable by user code, but is reserved for internal use.  Note
     * that this is an Argument + Parameter (rather than a
     * Param<void*>) so that we can exclude it from the
     * ObjectInstanceRegistry. */
    InferredArgument user_context_arg;

    /** A set of custom passes to use when lowering this Func. */
    vector<CustomLoweringPass> custom_lowering_passes;

    /** The inferred arguments. Also the arguments to the main
     * function in the jit_module above. The two must be updated
     * together. */
    vector<InferredArgument> inferred_args;

    /** List of C funtions and Funcs to satisfy HalideExtern* and
     * define_extern calls. */
    std::map<std::string, JITExtern> jit_externs;

    std::vector<Stmt> requirements;

    bool trace_pipeline = false;

    PipelineContents()
        : module("", Target()) {
        user_context_arg.arg = Argument("__user_context", Argument::InputScalar, type_of<const void *>(), 0, ArgumentEstimates{});
        user_context_arg.param = Parameter(Handle(), false, 0, "__user_context");
    }

    ~PipelineContents() {
        clear_custom_lowering_passes();
    }

    void clear_custom_lowering_passes() {
        invalidate_cache();
        for (auto &custom_lowering_pass : custom_lowering_passes) {
            if (custom_lowering_pass.deleter) {
                custom_lowering_pass.deleter();
            }
        }
        custom_lowering_passes.clear();
    }
};

namespace Internal {
template<>
RefCount &ref_count<PipelineContents>(const PipelineContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<PipelineContents>(const PipelineContents *p) {
    delete p;
}
}  // namespace Internal

Pipeline::Pipeline()
    : contents(nullptr) {
}

bool Pipeline::defined() const {
    return contents.defined();
}

Pipeline::Pipeline(const Func &output)
    : contents(new PipelineContents) {
    output.function().freeze();
    contents->outputs.push_back(output.function());
}

Pipeline::Pipeline(const vector<Func> &outputs)
    : contents(new PipelineContents) {
    for (const Func &f : outputs) {
        f.function().freeze();
        contents->outputs.push_back(f.function());
    }
}

vector<Func> Pipeline::outputs() const {
    vector<Func> funcs;
    for (const Function &f : contents->outputs) {
        funcs.emplace_back(f);
    }
    return funcs;
}

/* static */
std::map<std::string, AutoSchedulerFn> &Pipeline::get_autoscheduler_map() {
    static std::map<std::string, AutoSchedulerFn> autoschedulers = {};
    return autoschedulers;
}

/* static */
std::string &Pipeline::get_default_autoscheduler_name() {
    static std::string autoscheduler_name = "";
    if (autoscheduler_name.empty() && !get_autoscheduler_map().empty()) {
        autoscheduler_name = get_autoscheduler_map().begin()->first;
    }
    return autoscheduler_name;
}

/* static */
AutoSchedulerFn Pipeline::find_autoscheduler(const std::string &autoscheduler_name) {
    const auto &m = get_autoscheduler_map();
    auto it = m.find(autoscheduler_name);
    if (it == m.end()) {
        std::ostringstream o;
        o << "Unknown autoscheduler name '" << autoscheduler_name << "'; known names are:\n";
        for (const auto &a : m) {
            o << "    " << a.first << "\n";
        }
        user_error << o.str();
    }
    return it->second;
}

AutoSchedulerResults Pipeline::auto_schedule(const std::string &autoscheduler_name, const Target &target, const MachineParams &arch_params) {
    auto autoscheduler_fn = find_autoscheduler(autoscheduler_name);
    user_assert(autoscheduler_fn)
        << "Could not find autoscheduler named '" << autoscheduler_name << "'.\n"
        << "Did you remember to load the plugin?";

    AutoSchedulerResults results;
    results.target = target;
    results.machine_params_string = arch_params.to_string();

    autoscheduler_fn(*this, target, arch_params, &results);
    return results;
}

AutoSchedulerResults Pipeline::auto_schedule(const Target &target, const MachineParams &arch_params) {
    return auto_schedule(get_default_autoscheduler_name(), target, arch_params);
}

/* static */
void Pipeline::add_autoscheduler(const std::string &autoscheduler_name, const AutoSchedulerFn &autoscheduler) {
    auto &m = get_autoscheduler_map();
    user_assert(m.find(autoscheduler_name) == m.end()) << "'" << autoscheduler_name << "' is already registered as an autoscheduler.\n";
    m[autoscheduler_name] = autoscheduler;
}

/* static */
void Pipeline::set_default_autoscheduler_name(const std::string &autoscheduler_name) {
    (void)find_autoscheduler(autoscheduler_name);  // ensure it's valid
    get_default_autoscheduler_name() = autoscheduler_name;
}

Func Pipeline::get_func(size_t index) {
    // Compute an environment
    std::map<string, Function> env;
    for (const Function &f : contents->outputs) {
        std::map<string, Function> more_funcs = find_transitive_calls(f);
        env.insert(more_funcs.begin(), more_funcs.end());
    }
    // Compute a topological order
    vector<string> order = topological_order(contents->outputs, env);

    user_assert(index < order.size())
        << "Index value passed is " << index << "; however, there are only "
        << order.size() << " functions in the pipeline.\n";
    return Func(env.find(order[index])->second);
}

void Pipeline::compile_to(const std::map<OutputFileType, std::string> &output_files,
                          const vector<Argument> &args,
                          const string &fn_name,
                          const Target &target) {
    compile_to_module(args, fn_name, target).compile(output_files);
}

void Pipeline::compile_to_bitcode(const string &filename,
                                  const vector<Argument> &args,
                                  const string &fn_name,
                                  const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(single_output(filename, m, OutputFileType::bitcode));
}

void Pipeline::compile_to_llvm_assembly(const string &filename,
                                        const vector<Argument> &args,
                                        const string &fn_name,
                                        const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(single_output(filename, m, OutputFileType::llvm_assembly));
}

void Pipeline::compile_to_object(const string &filename,
                                 const vector<Argument> &args,
                                 const string &fn_name,
                                 const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    auto ext = get_output_info(target);
    m.compile({{OutputFileType::object, output_name(filename, m, ext.at(OutputFileType::object).extension)}});
}

void Pipeline::compile_to_header(const string &filename,
                                 const vector<Argument> &args,
                                 const string &fn_name,
                                 const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(single_output(filename, m, OutputFileType::c_header));
}

void Pipeline::compile_to_assembly(const string &filename,
                                   const vector<Argument> &args,
                                   const string &fn_name,
                                   const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(single_output(filename, m, OutputFileType::assembly));
}

void Pipeline::compile_to_c(const string &filename,
                            const vector<Argument> &args,
                            const string &fn_name,
                            const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(single_output(filename, m, OutputFileType::c_source));
}

void Pipeline::print_loop_nest() {
    user_assert(defined()) << "Can't print loop nest of undefined Pipeline.\n";
    debug(0) << Halide::Internal::print_loop_nest(contents->outputs);
}

void Pipeline::compile_to_lowered_stmt(const string &filename,
                                       const vector<Argument> &args,
                                       StmtOutputFormat fmt,
                                       const Target &target) {
    Module m = compile_to_module(args, "", target);
    m.compile(single_output(filename, m, fmt == HTML ? OutputFileType::stmt_html : OutputFileType::stmt));
}

void Pipeline::compile_to_static_library(const string &filename_prefix,
                                         const vector<Argument> &args,
                                         const std::string &fn_name,
                                         const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(static_library_outputs(filename_prefix, target));
}

void Pipeline::compile_to_multitarget_static_library(const std::string &filename_prefix,
                                                     const std::vector<Argument> &args,
                                                     const std::vector<Target> &targets) {
    auto module_producer = [this, &args](const std::string &name, const Target &target) -> Module {
        return compile_to_module(args, name, target);
    };
    auto outputs = static_library_outputs(filename_prefix, targets.back());
    compile_multitarget(generate_function_name(), outputs, targets, {}, module_producer);
}

void Pipeline::compile_to_multitarget_object_files(const std::string &filename_prefix,
                                                   const std::vector<Argument> &args,
                                                   const std::vector<Target> &targets,
                                                   const std::vector<std::string> &suffixes) {
    auto module_producer = [this, &args](const std::string &name, const Target &target) -> Module {
        return compile_to_module(args, name, target);
    };
    auto outputs = object_file_outputs(filename_prefix, targets.back());
    compile_multitarget(generate_function_name(), outputs, targets, suffixes, module_producer);
}

void Pipeline::compile_to_file(const string &filename_prefix,
                               const vector<Argument> &args,
                               const std::string &fn_name,
                               const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    auto ext = get_output_info(target);
    std::map<OutputFileType, std::string> outputs = {
        {OutputFileType::c_header, filename_prefix + ext.at(OutputFileType::c_header).extension},
        {OutputFileType::object, filename_prefix + ext.at(OutputFileType::object).extension},
    };
    m.compile(outputs);
}

vector<Argument> Pipeline::infer_arguments(const Stmt &body) {
    Stmt s = body;
    if (!contents->requirements.empty()) {
        s = Block::make(contents->requirements);
        if (body.defined()) {
            s = Block::make(s, body);
        }
    }
    contents->inferred_args = ::infer_arguments(s, contents->outputs);

    // Add the user context argument if it's not already there, or hook up our user context
    // Parameter to any existing one.
    bool has_user_context = false;
    for (auto &arg : contents->inferred_args) {
        if (arg.arg.name == contents->user_context_arg.arg.name) {
            arg = contents->user_context_arg;
            has_user_context = true;
        }
    }
    if (!has_user_context) {
        contents->inferred_args.push_back(contents->user_context_arg);
    }

    // Return the inferred argument types, minus any constant images
    // (we'll embed those in the binary by default), and minus the user_context arg.
    vector<Argument> result;
    for (const InferredArgument &arg : contents->inferred_args) {
        debug(2) << "Inferred argument: " << arg.arg.type << " " << arg.arg.name << "\n";
        if (!arg.buffer.defined() &&
            arg.arg.name != contents->user_context_arg.arg.name) {
            result.push_back(arg.arg);
        }
    }

    return result;
}

class FindExterns : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);

        if ((op->call_type == Call::Extern || op->call_type == Call::PureExtern) && externs.count(op->name) == 0) {
            void *address = get_symbol_address(op->name.c_str());
            if (address == nullptr && !starts_with(op->name, "_")) {
                std::string underscored_name = "_" + op->name;
                address = get_symbol_address(underscored_name.c_str());
            }
            if (address != nullptr) {
                // TODO: here and below for arguments, we force types to scalar,
                // which means this code cannot support functions which actually do
                // take vectors. But generally the function is actually scalar and
                // call sites which use vectors will have to be scalarized into a
                // separate call per lane. Not sure there is anywhere to get
                // information to make a distinction in the current design.
                std::vector<Type> arg_types;
                if (function_takes_user_context(op->name)) {
                    arg_types.push_back(type_of<void *>());
                }
                for (const Expr &e : op->args) {
                    arg_types.push_back(e.type().element_of());
                }
                bool is_void_return = op->type.bits() == 0 || op->name == "halide_print";
                ExternSignature sig(is_void_return ? Type() : op->type.element_of(), is_void_return, arg_types);
                ExternCFunction f(address, sig);
                JITExtern jit_extern(f);
                debug(2) << "FindExterns adds: " << op->name << "\n";
                externs.emplace(op->name, jit_extern);
            }
        }
    }

public:
    FindExterns(std::map<std::string, JITExtern> &externs)
        : externs(externs) {
    }

    std::map<std::string, JITExtern> &externs;
};

vector<Argument> Pipeline::infer_arguments() {
    return infer_arguments(Stmt());
}

Module Pipeline::compile_to_module(const vector<Argument> &args,
                                   const string &fn_name,
                                   const Target &target,
                                   const LinkageType linkage_type) {
    user_assert(defined()) << "Can't compile undefined Pipeline.\n";

    for (const Function &f : contents->outputs) {
        user_assert(f.has_pure_definition() || f.has_extern_definition())
            << "Can't compile Pipeline with undefined output Func: " << f.name() << ".\n";
    }

    string new_fn_name(fn_name);
    if (new_fn_name.empty()) {
        new_fn_name = generate_function_name();
    }
    internal_assert(!new_fn_name.empty()) << "new_fn_name cannot be empty\n";
    // TODO: Assert that the function name is legal

    vector<Argument> lowering_args(args);

    // If the target specifies user context but it's not in the args
    // vector, add it at the start (the jit path puts it in there
    // explicitly).
    const bool requires_user_context = target.has_feature(Target::UserContext);
    bool has_user_context = false;
    for (const Argument &arg : lowering_args) {
        if (arg.name == contents->user_context_arg.arg.name) {
            has_user_context = true;
        }
    }
    if (requires_user_context && !has_user_context) {
        lowering_args.insert(lowering_args.begin(), contents->user_context_arg.arg);
    }

    const Module &old_module = contents->module;

    bool same_compile = !old_module.functions().empty() && old_module.target() == target;
    // Either generated name or one of the LoweredFuncs in the existing module has the same name.
    same_compile = same_compile && fn_name.empty();
    bool found_name = false;
    for (const auto &lf : old_module.functions()) {
        if (lf.name == fn_name) {
            found_name = true;
            break;
        }
    }
    same_compile = same_compile && found_name;
    // Number of args + number of outputs is the same as total args in existing LoweredFunc
    same_compile = same_compile && (lowering_args.size() + outputs().size()) == old_module.functions().front().args.size();
    // The initial args are the same.
    same_compile = same_compile && std::equal(lowering_args.begin(), lowering_args.end(), old_module.functions().front().args.begin());
    // Linkage is the same.
    same_compile = same_compile && old_module.functions().front().linkage == linkage_type;
    // The outputs of a Pipeline cannot change, so no need to test them.

    if (same_compile) {
        // We can avoid relowering and just reuse the existing module.
        debug(2) << "Reusing old module\n";
    } else {
        vector<IRMutator *> custom_passes;
        for (const CustomLoweringPass &p : contents->custom_lowering_passes) {
            custom_passes.push_back(p.pass);
        }

        contents->module = lower(contents->outputs, new_fn_name, target, lowering_args,
                                 linkage_type, contents->requirements, contents->trace_pipeline,
                                 custom_passes);
    }

    return contents->module;
}

std::string Pipeline::generate_function_name() const {
    user_assert(defined()) << "Pipeline is undefined\n";
    // Come up with a name for a generated function
    string name = contents->outputs[0].name();
    for (char &c : name) {
        if (!isalnum(c)) {
            c = '_';
        }
    }
    return name;
}

// This essentially is just a getter for contents->jit_target,
// but also reality-checks that the status of the jit_module and/or wasm_module
// match what we expect.
Target Pipeline::get_compiled_jit_target() const {
    const bool has_wasm = contents->wasm_module.contents.defined();
    const bool has_native = contents->jit_module.compiled();
    if (contents->jit_target.arch == Target::WebAssembly) {
        internal_assert(has_wasm && !has_native);
    } else if (!contents->jit_target.has_unknowns()) {
        internal_assert(!has_wasm && has_native);
    } else {
        internal_assert(!has_wasm && !has_native);
    }
    return contents->jit_target;
}

void Pipeline::compile_jit(const Target &target_arg) {
    user_assert(defined()) << "Pipeline is undefined\n";
    user_assert(!target_arg.has_unknowns()) << "Cannot compile_jit() for target '" << target_arg << "'\n";

    Target target(target_arg);
    target.set_feature(Target::JIT);
    target.set_feature(Target::UserContext);

    // If we're re-jitting for the same target, we can just keep the old jit module.
    if (get_compiled_jit_target() == target) {
        debug(2) << "Reusing old jit module compiled for :\n"
                 << target << "\n";
        return;
    }

    debug(2) << "jit-compiling for: " << target_arg << "\n";

    // Clear all cached info in case there is an error.
    contents->invalidate_cache();

    contents->jit_target = target;

    // Infer an arguments vector
    infer_arguments();

    // Don't actually use the return value - it embeds all constant
    // images and we don't want to do that when jitting. Instead
    // use the vector of parameters found to make a more complete
    // arguments list.
    vector<Argument> args;
    for (const InferredArgument &arg : contents->inferred_args) {
        args.push_back(arg.arg);
    }

    // Come up with a name for the generated function
    string name = generate_function_name();

    // Compile to a module and also compile any submodules.
    Module module = compile_to_module(args, name, target).resolve_submodules();

    std::map<std::string, JITExtern> lowered_externs = contents->jit_externs;

    if (target.arch == Target::WebAssembly) {
        FindExterns find_externs(lowered_externs);
        for (const LoweredFunc &f : contents->module.functions()) {
            f.body.accept(&find_externs);
        }
        if (debug::debug_level() >= 1) {
            for (const auto &p : lowered_externs) {
                debug(1) << "Found extern: " << p.first << "\n";
            }
        }

        vector<Argument> args_and_outputs = args;
        for (auto &out : contents->outputs) {
            for (Type t : out.output_types()) {
                args_and_outputs.emplace_back(out.name(), Argument::OutputBuffer, t, out.dimensions(), ArgumentEstimates{});
            }
        }

        contents->wasm_module = WasmModule::compile(
            module,
            args_and_outputs,
            contents->module.name(),
            lowered_externs,
            make_externs_jit_module(target, lowered_externs));
        return;
    }

    auto f = module.get_function_by_name(name);

    // Compile to jit module
    JITModule jit_module(module, f, make_externs_jit_module(target_arg, lowered_externs));

    // Dump bitcode to a file if the environment variable
    // HL_GENBITCODE is defined to a nonzero value.
    if (atoi(get_env_variable("HL_GENBITCODE").c_str())) {
        string program_name = running_program_name();
        if (program_name.empty()) {
            program_name = "unknown" + unique_name('_').substr(1);
        }
        string file_name = program_name + "_" + name + "_" + unique_name('g').substr(1) + ".bc";
        debug(4) << "Saving bitcode to: " << file_name << "\n";
        module.compile({{OutputFileType::bitcode, file_name}});
    }

    contents->jit_module = jit_module;
}

void Pipeline::set_jit_externs(const std::map<std::string, JITExtern> &externs) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_externs = externs;
    invalidate_cache();
}

const std::map<std::string, JITExtern> &Pipeline::get_jit_externs() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents->jit_externs;
}

void Pipeline::add_custom_lowering_pass(IRMutator *pass, std::function<void()> deleter) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->invalidate_cache();
    CustomLoweringPass p = {pass, std::move(deleter)};
    contents->custom_lowering_passes.push_back(p);
}

void Pipeline::clear_custom_lowering_passes() {
    if (!defined()) {
        return;
    }
    contents->clear_custom_lowering_passes();
}

const vector<CustomLoweringPass> &Pipeline::custom_lowering_passes() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents->custom_lowering_passes;
}

JITHandlers &Pipeline::jit_handlers() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents->jit_handlers;
}

Realization Pipeline::realize(vector<int32_t> sizes, const Target &target,
                              const ParamMap &param_map) {
    return realize(nullptr, std::move(sizes), target, param_map);
}

Realization Pipeline::realize(JITUserContext *context,
                              vector<int32_t> sizes,
                              const Target &target,
                              const ParamMap &param_map) {
    user_assert(defined()) << "Pipeline is undefined\n";
    vector<Buffer<>> bufs;
    for (auto &out : contents->outputs) {
        user_assert((int)sizes.size() == out.dimensions())
            << "Func " << out.name() << " is defined with " << out.dimensions() << " dimensions, but realize() is requesting a realization with " << sizes.size() << " dimensions.\n";
        user_assert(out.has_pure_definition() || out.has_extern_definition()) << "Can't realize Pipeline with undefined output Func: " << out.name() << ".\n";
        for (Type t : out.output_types()) {
            bufs.emplace_back(t, nullptr, sizes);
        }
    }
    Realization r(std::move(bufs));
    // Do an output bounds query if we can. Otherwise just assume the
    // output size is good.
    if (!target.has_feature(Target::NoBoundsQuery)) {
        realize(context, r, target, param_map);
    }
    for (size_t i = 0; i < r.size(); i++) {
        r[i].allocate();
    }
    // Do the actual computation
    realize(context, r, target, param_map);

    // Crop back to the requested size if necessary
    bool needs_crop = false;
    vector<std::pair<int32_t, int32_t>> crop;
    if (!target.has_feature(Target::NoBoundsQuery)) {
        crop.resize(sizes.size());
        for (size_t d = 0; d < sizes.size(); d++) {
            needs_crop |= ((r[0].dim(d).extent() != sizes[d]) ||
                           (r[0].dim(d).min() != 0));
            crop[d].first = 0;
            crop[d].second = sizes[d];
        }
    }
    for (size_t i = 0; i < r.size(); i++) {
        if (needs_crop) {
            r[i].crop(crop);
        }
        r[i].copy_to_host(context);
    }
    return r;
}

void Pipeline::add_requirement(const Expr &condition, std::vector<Expr> &error_args) {
    user_assert(defined()) << "Pipeline is undefined\n";

    // It is an error for a requirement to reference a Func or a Var
    class Checker : public IRGraphVisitor {
        using IRGraphVisitor::visit;

        void visit(const Variable *op) override {
            if (!op->param.defined()) {
                user_error << "Requirement " << condition << " refers to Var or RVar " << op->name << "\n";
            }
        }

        void visit(const Call *op) override {
            if (op->call_type == Call::Halide) {
                user_error << "Requirement " << condition << " calls Func " << op->name << "\n";
            }
            IRGraphVisitor::visit(op);
        }

        const Expr &condition;

    public:
        Checker(const Expr &c)
            : condition(c) {
            c.accept(this);
        }
    } checker(condition);

    Expr error = Internal::requirement_failed_error(condition, error_args);
    contents->requirements.emplace_back(Internal::AssertStmt::make(condition, error));
}

void Pipeline::trace_pipeline() {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->trace_pipeline = true;
}

namespace Internal {
struct JITErrorBuffer {
    enum { MaxBufSize = 4096 };
    char buf[MaxBufSize];
    std::atomic<size_t> end{0};

    void concat(const char *message) {
        size_t len = strlen(message);

        if (len && message[len - 1] != '\n') {
            // Claim some extra space for a newline.
            len++;
        }

        // Atomically claim some space in the buffer
        size_t old_end = end.fetch_add(len);

        if (old_end + len >= MaxBufSize - 1) {
            // Out of space
            return;
        }

        for (size_t i = 0; i < len - 1; i++) {
            buf[old_end + i] = message[i];
        }
        if (buf[old_end + len - 2] != '\n') {
            buf[old_end + len - 1] = '\n';
        }
    }

    std::string str() const {
        return std::string(buf, end);
    }

    static void handler(JITUserContext *ctx, const char *message) {
        if (ctx && ctx->error_buffer) {
            ctx->error_buffer->concat(message);
        }
    }
};

struct JITFuncCallContext {
    JITErrorBuffer error_buffer;
    JITUserContext *context;
    bool custom_error_handler;

    JITFuncCallContext(JITUserContext *context, const JITHandlers &pipeline_handlers)
        : context(context) {
        custom_error_handler = (context->handlers.custom_error != nullptr ||
                                pipeline_handlers.custom_error != nullptr);
        // Hook the error handler if not set
        if (!custom_error_handler) {
            context->handlers.custom_error = JITErrorBuffer::handler;
        }

        // Add the handlers stored in the pipeline for anything else
        // not set, then for anything still not set, use the global
        // active handlers.
        JITSharedRuntime::populate_jit_handlers(context, pipeline_handlers);
        context->error_buffer = &error_buffer;

        debug(2) << "custom_print: " << (void *)context->handlers.custom_print << "\n"
                 << "custom_malloc: " << (void *)context->handlers.custom_malloc << "\n"
                 << "custom_free: " << (void *)context->handlers.custom_free << "\n"
                 << "custom_do_task: " << (void *)context->handlers.custom_do_task << "\n"
                 << "custom_do_par_for: " << (void *)context->handlers.custom_do_par_for << "\n"
                 << "custom_error: " << (void *)context->handlers.custom_error << "\n"
                 << "custom_trace: " << (void *)context->handlers.custom_trace << "\n";
    }

    void report_if_error(int exit_status) {
        // Only report the errors if no custom error handler was installed
        if (exit_status && !custom_error_handler) {
            std::string output = error_buffer.str();
            if (output.empty()) {
                output = ("The pipeline returned exit status " +
                          std::to_string(exit_status) +
                          " but halide_error was never called.\n");
            }
            halide_runtime_error << output;
            error_buffer.end = 0;
        }
    }

    void finalize(int exit_status) {
        report_if_error(exit_status);
    }
};
}  // namespace Internal

struct Pipeline::JITCallArgs {
    size_t size{0};
    const void **store;

    JITCallArgs(size_t size)
        : size(size) {
        if (size > kStoreSize) {
            store = new ConstVoidPtr[size];
        } else {
            store = fixed_store;
        }
    }

    ~JITCallArgs() {
        if (store != fixed_store) {
            delete[] store;
        }
    }

private:
    static constexpr int kStoreSize = 64;
    using ConstVoidPtr = const void *;
    ConstVoidPtr fixed_store[kStoreSize];

public:
    JITCallArgs(const JITCallArgs &other) = delete;
    JITCallArgs &operator=(const JITCallArgs &other) = delete;
    JITCallArgs(JITCallArgs &&other) = delete;
    JITCallArgs &operator=(JITCallArgs &&other) = delete;
};

// Make a vector of void *'s to pass to the jit call using the
// currently bound value for all of the params and image
// params.
void Pipeline::prepare_jit_call_arguments(RealizationArg &outputs, const Target &target,
                                          const ParamMap &param_map, JITUserContext **user_context,
                                          bool is_bounds_inference, JITCallArgs &args_result) {
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    size_t total_outputs = 0;
    for (const Func &out : this->outputs()) {
        total_outputs += out.outputs();
    }
    user_assert(outputs.size() == total_outputs)
        << "Realization requires " << outputs.size() << " output(s) but pipeline produces "
        << total_outputs << " result(s).\n";

    JITModule &compiled_module = contents->jit_module;
    internal_assert(compiled_module.argv_function() ||
                    contents->wasm_module.contents.defined());

    const bool no_param_map = &param_map == &ParamMap::empty_map();

    // Come up with the void * arguments to pass to the argv function
    size_t arg_index = 0;
    for (const InferredArgument &arg : contents->inferred_args) {
        if (arg.param.defined()) {
            if (arg.param.same_as(contents->user_context_arg.param)) {
                args_result.store[arg_index++] = user_context;
            } else {
                Buffer<> *buf_out_param = nullptr;
                const Parameter &p = no_param_map ? arg.param : param_map.map(arg.param, buf_out_param);
                user_assert(is_bounds_inference || !buf_out_param)
                    << "Cannot pass Buffer<> pointers in parameters map to a compute call.\n";

                if (p.is_buffer()) {
                    // ImageParam arg
                    Buffer<> buf = p.buffer();
                    if (buf.defined()) {
                        args_result.store[arg_index++] = p.raw_buffer();
                    } else {
                        // Unbound
                        args_result.store[arg_index++] = nullptr;
                    }
                    debug(2) << "JIT input ImageParam argument ";
                } else {
                    args_result.store[arg_index++] = p.scalar_address();
                    debug(2) << "JIT input scalar argument ";
                }
            }
        } else {
            debug(2) << "JIT input Image argument ";
            internal_assert(arg.buffer.defined());
            args_result.store[arg_index++] = arg.buffer.raw_buffer();
        }
        const void *ptr = args_result.store[arg_index - 1];
        debug(2) << arg.arg.name << " @ " << ptr << "\n";
    }

    // Then the outputs
    if (outputs.r) {
        for (size_t i = 0; i < outputs.r->size(); i++) {
            const halide_buffer_t *buf = (*outputs.r)[i].raw_buffer();
            args_result.store[arg_index++] = buf;
            debug(2) << "JIT output buffer @ " << (const void *)buf << ", " << (const void *)buf->host << "\n";
        }
    } else if (outputs.buf) {
        args_result.store[arg_index++] = outputs.buf;
        debug(2) << "JIT output buffer @ " << (const void *)outputs.buf << ", " << (const void *)outputs.buf->host << "\n";
    } else {
        for (const Buffer<> &buffer : *outputs.buffer_list) {
            const halide_buffer_t *buf = buffer.raw_buffer();
            args_result.store[arg_index++] = buf;
            debug(2) << "JIT output buffer @ " << (const void *)buf << ", " << (const void *)buf->host << "\n";
        }
    }
}

std::vector<JITModule>
Pipeline::make_externs_jit_module(const Target &target,
                                  std::map<std::string, JITExtern> &externs_in_out) {
    std::vector<JITModule> result;

    // Externs that are Funcs get their own JITModule. All standalone functions are
    // held in a single JITModule at the end of the list (if there are any).
    JITModule free_standing_jit_externs;
    for (auto &iter : externs_in_out) {
        Pipeline pipeline = iter.second.pipeline();
        if (pipeline.defined()) {
            PipelineContents &pipeline_contents(*pipeline.contents);

            // Ensure that the pipeline is compiled.
            pipeline.compile_jit(target);

            free_standing_jit_externs.add_dependency(pipeline_contents.jit_module);
            free_standing_jit_externs.add_symbol_for_export(iter.first, pipeline_contents.jit_module.entrypoint_symbol());
            void *address = pipeline_contents.jit_module.entrypoint_symbol().address;
            std::vector<Type> arg_types;
            // Add the arguments to the compiled pipeline
            for (const InferredArgument &arg : pipeline_contents.inferred_args) {
                // TODO: it's not clear whether arg.arg.type is correct for
                // the arg.is_buffer() case (AFAIK, is_buffer()==true isn't possible
                // in current trunk Halide, but may be in some side branches that
                // have not yet landed, e.g. JavaScript). Forcing it to be
                // the correct type here, just in case.
                arg_types.push_back(arg.arg.is_buffer() ? type_of<struct halide_buffer_t *>() : arg.arg.type);
            }
            // Add the outputs of the pipeline
            for (size_t i = 0; i < pipeline_contents.outputs.size(); i++) {
                arg_types.push_back(type_of<struct halide_buffer_t *>());
            }
            ExternSignature signature(Int(32), false, arg_types);
            iter.second = JITExtern(ExternCFunction(address, signature));
        } else {
            free_standing_jit_externs.add_extern_for_export(iter.first, iter.second.extern_c_function());
        }
    }
    if (free_standing_jit_externs.compiled() || !free_standing_jit_externs.exports().empty()) {
        result.push_back(free_standing_jit_externs);
    }
    return result;
}

int Pipeline::call_jit_code(const Target &target, const JITCallArgs &args) {
#if defined(__has_feature)
#if __has_feature(memory_sanitizer)
    user_warning << "MSAN does not support JIT compilers of any sort, and will report "
                    "false positives when used in conjunction with the Halide JIT. "
                    "If you need to test with MSAN enabled, you must use ahead-of-time "
                    "compilation for Halide code.";
#endif
#endif
    if (target.arch == Target::WebAssembly) {
        internal_assert(contents->wasm_module.contents.defined());
        return contents->wasm_module.run(args.store);
    }
    return contents->jit_module.argv_function()(args.store);
}

void Pipeline::realize(RealizationArg outputs,
                       const Target &t,
                       const ParamMap &param_map) {
    realize(nullptr, std::move(outputs), t, param_map);
}

void Pipeline::realize(JITUserContext *context,
                       RealizationArg outputs,
                       const Target &t,
                       const ParamMap &param_map) {
    Target target = t;
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    debug(2) << "Realizing Pipeline for " << target << "\n";

    if (target.has_unknowns()) {
        // If we've already jit-compiled for a specific target, use that.
        target = get_compiled_jit_target();
        if (target.has_unknowns()) {
            // Otherwise get the target from the environment
            target = get_jit_target_from_environment();
        }
    }

    // We need to make a context for calling the jitted function to
    // carry the the set of custom handlers. Here's how handlers get
    // called when running jitted code:

    // There's a single shared module that includes runtime code like
    // posix_error_handler.cpp. This module is created the first time
    // you JIT something and is reused for all subsequent runs of
    // jitted code for any pipeline with the same target.

    // To handle events like printing, tracing, or errors, the jitted
    // code calls things like halide_error or halide_print in the
    // shared runtime, which in turn call global function pointer
    // variables in the shared runtime (e.g. halide_error_handler,
    // halide_custom_print). When the shared module is created, we set
    // those variables to point to the global handlers in
    // JITModule.cpp (e.g. error_handler_handler, print_handler).

    // Those global handlers use the user_context passed in to call
    // the right handler for this particular pipeline run. The
    // user_context is just a pointer to a JITUserContext, which is a
    // member of the JITFuncCallContext which we will declare now:

    // Ensure the module is compiled.
    compile_jit(target);

    // This has to happen after a runtime has been compiled in compile_jit.
    JITUserContext empty_jit_user_context{};
    if (!context) {
        context = &empty_jit_user_context;
    }
    JITFuncCallContext jit_call_context(context, jit_handlers());

    JITCallArgs args(contents->inferred_args.size() + outputs.size());
    prepare_jit_call_arguments(outputs, target, param_map,
                               &context, false, args);

    // The handlers in the jit_context default to the default handlers
    // in the runtime of the shared module (e.g. halide_print_impl,
    // default_trace). As an example, here's what happens with a
    // halide_print call:

    // 1) Before the pipeline runs, when the single shared runtime
    // module is created, halide_custom_print in posix_print.cpp is
    // set to print_handler in JITModule.cpp

    // 2) When the jitted module is compiled, we tell llvm to resolve
    // calls to halide_print to the halide_print in the shared module
    // we made.

    // 3) The user calls realize(), and the jitted code calls
    // halide_print in the shared runtime.

    // 4) halide_print calls the function pointer halide_custom_print,
    // which is print_handler in JITModule.cpp

    // 5) print_handler casts the user_context to a JITUserContext,
    // then calls the function pointer member handlers.custom_print,
    // which is either halide_print_impl in the runtime, or some other
    // function set by Pipeline::set_custom_print.

    // Errors are slightly different, in that we always override the
    // default when jitting.  We instead use ErrorBuffer::handler
    // above (this was set in jit_context's constructor). When
    // jit-compiled code encounters an error, it calls this handler,
    // which just records the fact there was an error and what the
    // message was, then returns back into jitted code. The jitted
    // code cleans up and returns early with an exit code. We record
    // this exit status below, then pass it to jit_context.finalize at
    // the end of this function. If it's non-zero,
    // jit_context.finalize passes the recorded error message to
    // halide_runtime_error, which either calls abort() or throws an
    // exception.

    debug(2) << "Calling jitted function\n";
    int exit_status = call_jit_code(target, args);
    debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

    // If we're profiling, report runtimes and reset profiler stats.
    if (target.has_feature(Target::Profile) || target.has_feature(Target::ProfileByTimer)) {
        JITModule::Symbol report_sym =
            contents->jit_module.find_symbol_by_name("halide_profiler_report");
        JITModule::Symbol reset_sym =
            contents->jit_module.find_symbol_by_name("halide_profiler_reset");
        if (report_sym.address && reset_sym.address) {
            void (*report_fn_ptr)(JITUserContext *) = (void (*)(JITUserContext *))(report_sym.address);
            report_fn_ptr(context);

            void (*reset_fn_ptr)() = (void (*)())(reset_sym.address);
            reset_fn_ptr();
        }
    }

    jit_call_context.finalize(exit_status);
}

void Pipeline::infer_input_bounds(RealizationArg outputs, const Target &target, const ParamMap &param_map) {
    infer_input_bounds(nullptr, std::move(outputs), target, param_map);
}

void Pipeline::infer_input_bounds(JITUserContext *context,
                                  RealizationArg outputs,
                                  const Target &target,
                                  const ParamMap &param_map) {
    user_assert(!target.has_feature(Target::NoBoundsQuery)) << "You may not call infer_input_bounds() with Target::NoBoundsQuery set.";
    compile_jit(target);

    // This has to happen after a runtime has been compiled in compile_jit.
    JITUserContext empty_user_context = {};
    if (!context) {
        context = &empty_user_context;
    }
    JITFuncCallContext jit_context(context, jit_handlers());

    size_t args_size = contents->inferred_args.size() + outputs.size();
    JITCallArgs args(args_size);
    prepare_jit_call_arguments(outputs, contents->jit_target, param_map,
                               &context, true, args);

    struct TrackedBuffer {
        // The query buffer, and a backup to check for changes. We
        // want wrappers around actual halide_buffer_ts so that we can copy
        // the metadata, not shared pointers to a single buffer, so
        // it's simpler to use the runtime buffer class.
        Runtime::Buffer<> query, orig;
    };
    vector<TrackedBuffer> tracked_buffers(args_size);

    vector<size_t> query_indices;
    for (size_t i = 0; i < contents->inferred_args.size(); i++) {
        if (args.store[i] == nullptr) {
            query_indices.push_back(i);
            InferredArgument ia = contents->inferred_args[i];
            internal_assert(ia.param.defined() && ia.param.is_buffer());
            // Make some empty Buffers of the right dimensionality
            vector<int> initial_shape(ia.param.dimensions(), 0);
            tracked_buffers[i].query = Runtime::Buffer<>(ia.param.type(), nullptr, initial_shape);
            tracked_buffers[i].orig = Runtime::Buffer<>(ia.param.type(), nullptr, initial_shape);
            args.store[i] = tracked_buffers[i].query.raw_buffer();
        }
    }

    // No need to query if all the inputs are bound already.
    if (query_indices.empty()) {
        debug(2) << "All inputs are bound. No need for bounds inference\n";
        return;
    }

    int iter = 0;
    const int max_iters = 16;
    for (iter = 0; iter < max_iters; iter++) {
        // Make a copy of the buffers that might be mutated
        for (TrackedBuffer &tb : tracked_buffers) {
            // Make a copy of the buffer sizes, etc.
            tb.orig = tb.query;
        }

        Internal::debug(2) << "Calling jitted function\n";
        int exit_status = call_jit_code(contents->jit_target, args);
        jit_context.report_if_error(exit_status);
        Internal::debug(2) << "Back from jitted function\n";
        bool changed = false;

        // Check if there were any changes
        for (TrackedBuffer &tb : tracked_buffers) {
            for (int i = 0; i < tb.query.dimensions(); i++) {
                if (tb.query.dim(i).min() != tb.orig.dim(i).min() ||
                    tb.query.dim(i).extent() != tb.orig.dim(i).extent() ||
                    tb.query.dim(i).stride() != tb.orig.dim(i).stride()) {
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }

    jit_context.finalize(0);

    user_assert(iter < max_iters)
        << "Inferring input bounds on Pipeline"
        << " didn't converge after " << max_iters
        << " iterations. There may be unsatisfiable constraints\n";

    debug(2) << "Bounds inference converged after " << iter << " iterations\n";

    // Now allocate the resulting buffers
    for (size_t i : query_indices) {
        InferredArgument ia = contents->inferred_args[i];
        Buffer<> *buf_out_param = nullptr;
        Parameter &p = param_map.map(ia.param, buf_out_param);

        if (&p != &ia.param) {
            user_assert(buf_out_param != nullptr) << "Output Buffer<> arguments to infer_input_bounds in parameters map must be passed as pointers.\n";
        }
        internal_assert(!p.buffer().defined());

        // Allocate enough memory with the right type and dimensionality.
        tracked_buffers[i].query.allocate();

        if (buf_out_param != nullptr) {
            *buf_out_param = Buffer<>(*tracked_buffers[i].query.raw_buffer());
        } else {
            // Bind this parameter to this buffer, giving away the
            // buffer. The user retrieves it via ImageParam::get.
            p.set_buffer(Buffer<>(std::move(tracked_buffers[i].query)));
        }
    }
}

void Pipeline::infer_input_bounds(const std::vector<int32_t> &sizes,
                                  const Target &target,
                                  const ParamMap &param_map) {
    infer_input_bounds(nullptr, sizes, target, param_map);
}

void Pipeline::infer_input_bounds(JITUserContext *context,
                                  const std::vector<int32_t> &sizes,
                                  const Target &target,
                                  const ParamMap &param_map) {
    user_assert(defined()) << "Can't infer input bounds on an undefined Pipeline.\n";
    vector<Buffer<>> bufs;
    for (Type t : contents->outputs[0].output_types()) {
        bufs.emplace_back(t, sizes);
    }
    Realization r(std::move(bufs));
    infer_input_bounds(context, r, target, param_map);
}

void Pipeline::invalidate_cache() {
    if (defined()) {
        contents->invalidate_cache();
    }
}

JITExtern::JITExtern(Pipeline pipeline)
    : pipeline_(std::move(pipeline)) {
}

JITExtern::JITExtern(const Func &func)
    : pipeline_(func) {
}

JITExtern::JITExtern(const ExternCFunction &extern_c_function)
    : extern_c_function_(extern_c_function) {
}

MachineParams MachineParams::generic() {
    std::string params = Internal::get_env_variable("HL_MACHINE_PARAMS");
    if (params.empty()) {
        return MachineParams(16, 16 * 1024 * 1024, 40);
    } else {
        return MachineParams(params);
    }
}

std::string MachineParams::to_string() const {
    std::ostringstream o;
    o << parallelism << "," << last_level_cache_size << "," << balance;
    return o.str();
}

MachineParams::MachineParams(const std::string &s) {
    std::vector<std::string> v = Internal::split_string(s, ",");
    user_assert(v.size() == 3) << "Unable to parse MachineParams: " << s;
    parallelism = std::atoi(v[0].c_str());
    last_level_cache_size = std::atoll(v[1].c_str());
    balance = std::atof(v[2].c_str());
}

}  // namespace Halide
