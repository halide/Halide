#include <algorithm>

#include "Argument.h"
#include "FindCalls.h"
#include "Func.h"
#include "IRVisitor.h"
#include "InferArguments.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "Lower.h"
#include "Outputs.h"
#include "ParamMap.h"
#include "Pipeline.h"
#include "PrintLoopNest.h"
#include "RealizationOrder.h"
#include "WasmExecutor.h"

using namespace Halide::Internal;

namespace Halide {

using std::set;
using std::string;
using std::vector;

namespace {

std::string output_name(const string &filename, const string &fn_name, const char* ext) {
    return !filename.empty() ? filename : (fn_name + ext);
}

std::string output_name(const string &filename, const Module &m, const char* ext) {
    return output_name(filename, m.name(), ext);
}

Outputs static_library_outputs(const string &filename_prefix, const Target &target) {
    Outputs outputs = Outputs().c_header(filename_prefix + ".h");
    if (target.os == Target::Windows && !target.has_feature(Target::MinGW)) {
        outputs = outputs.static_library(filename_prefix + ".lib");
    } else {
        outputs = outputs.static_library(filename_prefix + ".a");
    }
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

    bool trace_pipeline;

    PipelineContents() :
        module("", Target()), trace_pipeline(false) {
        user_context_arg.arg = Argument("__user_context", Argument::InputScalar, type_of<const void*>(), 0, ArgumentEstimates{});
        user_context_arg.param = Parameter(Handle(), false, 0, "__user_context");
    }

    ~PipelineContents() {
        clear_custom_lowering_passes();
    }

    void clear_custom_lowering_passes() {
        invalidate_cache();
        for (size_t i = 0; i < custom_lowering_passes.size(); i++) {
            if (custom_lowering_passes[i].deleter) {
                custom_lowering_passes[i].deleter();
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
}

Pipeline::Pipeline() : contents(nullptr) {
}

bool Pipeline::defined() const {
    return contents.defined();
}

Pipeline::Pipeline(Func output) : contents(new PipelineContents) {
    output.function().freeze();
    contents->outputs.push_back(output.function());
}

Pipeline::Pipeline(const vector<Func> &outputs) : contents(new PipelineContents) {
    for (Func f: outputs) {
        f.function().freeze();
        contents->outputs.push_back(f.function());
    }
}

vector<Func> Pipeline::outputs() const {
    vector<Func> funcs;
    for (Function f : contents->outputs) {
        funcs.push_back(Func(f));
    }
    return funcs;
}

std::function<std::string(Pipeline, const Target &, const MachineParams &)> *Pipeline::get_custom_auto_scheduler_ptr() {
    static std::function<string(Pipeline, const Target &, const MachineParams &)> custom_auto_scheduler = nullptr;
    return &custom_auto_scheduler;
}

string Pipeline::auto_schedule(const Target &target, const MachineParams &arch_params) {
    auto custom_auto_scheduler = *get_custom_auto_scheduler_ptr();
    if (custom_auto_scheduler) {
        return custom_auto_scheduler(*this, target, arch_params);
    }

    user_assert(target.arch == Target::X86 || target.arch == Target::ARM ||
                target.arch == Target::POWERPC || target.arch == Target::MIPS)
        << "Automatic scheduling is currently supported only on these architectures.";
    return generate_schedules(contents->outputs, target, arch_params);
}

void Pipeline::set_custom_auto_scheduler(std::function<string(Pipeline, const Target &, const MachineParams &)> auto_scheduler) {
    *get_custom_auto_scheduler_ptr() = auto_scheduler;
}

Func Pipeline::get_func(size_t index) {
    // Compute an environment
    std::map<string, Function> env;
    for (Function f : contents->outputs) {
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

void Pipeline::compile_to(const Outputs &output_files,
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
    m.compile(Outputs().bitcode(output_name(filename, m, ".bc")));
}

void Pipeline::compile_to_llvm_assembly(const string &filename,
                                        const vector<Argument> &args,
                                        const string &fn_name,
                                        const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(Outputs().llvm_assembly(output_name(filename, m, ".ll")));
}

void Pipeline::compile_to_object(const string &filename,
                                 const vector<Argument> &args,
                                 const string &fn_name,
                                 const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    const char* ext = target.os == Target::Windows && !target.has_feature(Target::MinGW) ? ".obj" : ".o";
    m.compile(Outputs().object(output_name(filename, m, ext)));
}

void Pipeline::compile_to_header(const string &filename,
                                 const vector<Argument> &args,
                                 const string &fn_name,
                                 const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(Outputs().c_header(output_name(filename, m, ".h")));
}

void Pipeline::compile_to_assembly(const string &filename,
                                   const vector<Argument> &args,
                                   const string &fn_name,
                                   const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(Outputs().assembly(output_name(filename, m, ".s")));
}


void Pipeline::compile_to_c(const string &filename,
                            const vector<Argument> &args,
                            const string &fn_name,
                            const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(Outputs().c_source(output_name(filename, m, ".c")));
}

void Pipeline::compile_to_python_extension(const string &filename,
                                           const vector<Argument> &args,
                                           const string &fn_name,
                                           const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    m.compile(Outputs().python_extension(output_name(filename, m, ".py.c")));
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
    Outputs outputs;
    if (fmt == HTML) {
        outputs = Outputs().stmt_html(output_name(filename, m, ".html"));
    } else {
        outputs = Outputs().stmt(output_name(filename, m, ".stmt"));
    }
    m.compile(outputs);
}

void Pipeline::compile_to_static_library(const string &filename_prefix,
                                         const vector<Argument> &args,
                                         const std::string &fn_name,
                                         const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    Outputs outputs = static_library_outputs(filename_prefix, target);
    m.compile(outputs);
}

void Pipeline::compile_to_multitarget_static_library(const std::string &filename_prefix,
                                                     const std::vector<Argument> &args,
                                                     const std::vector<Target> &targets) {
    auto module_producer = [this, &args](const std::string &name, const Target &target) -> Module {
        return compile_to_module(args, name, target);
    };
    Outputs outputs = static_library_outputs(filename_prefix, targets.back());
    compile_multitarget(generate_function_name(), outputs, targets, module_producer);
}

void Pipeline::compile_to_file(const string &filename_prefix,
                               const vector<Argument> &args,
                               const std::string &fn_name,
                               const Target &target) {
    Module m = compile_to_module(args, fn_name, target);
    Outputs outputs = Outputs().c_header(filename_prefix + ".h");

    if (target.os == Target::Windows && !target.has_feature(Target::MinGW)) {
        outputs = outputs.object(filename_prefix + ".obj");
    } else {
        outputs = outputs.object(filename_prefix + ".o");
    }
    m.compile(outputs);
}

vector<Argument> Pipeline::infer_arguments(Stmt body) {
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
                for (Expr e : op->args) {
                    arg_types.push_back(e.type().element_of());
                }
                ExternCFunction f(address, ExternSignature(op->type.element_of(), op->type.bits() == 0, arg_types));
                JITExtern jit_extern(f);
                debug(2) << "FindExterns adds: " << op->name << "\n";
                externs.emplace(op->name, jit_extern);
            }
        }
    }

public:
    FindExterns(std::map<std::string, JITExtern> &externs) : externs(externs) {}

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

    for (Function f : contents->outputs) {
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
    for (Argument arg : lowering_args) {
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
        for (CustomLoweringPass p : contents->custom_lowering_passes) {
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
    for (size_t i = 0; i < name.size(); i++) {
        if (!isalnum(name[i])) {
            name[i] = '_';
        }
    }
    return name;
}

void Pipeline::compile_jit(const Target &target_arg) {
    user_assert(defined()) << "Pipeline is undefined\n";

    Target target(target_arg);
    target.set_feature(Target::JIT);
    target.set_feature(Target::UserContext);

    debug(2) << "jit-compiling for: " << target_arg << "\n";

    // If we're re-jitting for the same target, we can just keep the
    // old jit module.
    if (contents->jit_target == target) {
        if (target.arch == Target::WebAssembly) {
            if (contents->wasm_module.contents.defined()) {
                debug(2) << "Reusing old wasm module compiled for :\n" << contents->jit_target << "\n";
                return;
            }
        }
        if (contents->jit_module.compiled()) {
            debug(2) << "Reusing old jit module compiled for :\n" << contents->jit_target << "\n";
            return;
        }
    }

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
            make_externs_jit_module(target, lowered_externs)
        );
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
        module.compile(Outputs().bitcode(file_name));
    }

    contents->jit_module = jit_module;
}


void Pipeline::set_error_handler(void (*handler)(void *, const char *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_handlers.custom_error = handler;
}

void Pipeline::set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                    void (*cust_free)(void *, void *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_handlers.custom_malloc = cust_malloc;
    contents->jit_handlers.custom_free = cust_free;
}

void Pipeline::set_custom_do_par_for(int (*cust_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_handlers.custom_do_par_for = cust_do_par_for;
}

void Pipeline::set_custom_do_task(int (*cust_do_task)(void *, int (*)(void *, int, uint8_t *), int, uint8_t *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_handlers.custom_do_task = cust_do_task;
}

void Pipeline::set_custom_trace(int (*trace_fn)(void *, const halide_trace_event_t *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_handlers.custom_trace = trace_fn;
}

void Pipeline::set_custom_print(void (*cust_print)(void *, const char *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents->jit_handlers.custom_print = cust_print;
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
    CustomLoweringPass p = {pass, deleter};
    contents->custom_lowering_passes.push_back(p);
}

void Pipeline::clear_custom_lowering_passes() {
    if (!defined()) return;
    contents->clear_custom_lowering_passes();
}

const vector<CustomLoweringPass> &Pipeline::custom_lowering_passes() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents->custom_lowering_passes;
}

const JITHandlers &Pipeline::jit_handlers() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents->jit_handlers;
}

Realization Pipeline::realize(vector<int32_t> sizes, const Target &target,
                              const ParamMap &param_map) {
    user_assert(defined()) << "Pipeline is undefined\n";
    vector<Buffer<>> bufs;
    for (auto & out : contents->outputs) {
        user_assert(out.has_pure_definition() || out.has_extern_definition()) <<
            "Can't realize Pipeline with undefined output Func: " << out.name() << ".\n";
        for (Type t : out.output_types()) {
            bufs.emplace_back(t, nullptr, sizes);
        }
    }
    Realization r(bufs);
    // Do an output bounds query if we can. Otherwise just assume the
    // output size is good.
    if (!target.has_feature(Target::NoBoundsQuery)) {
        realize(r, target, param_map);
    }
    for (size_t i = 0; i < r.size(); i++) {
        r[i].allocate();
    }
    // Do the actual computation
    realize(r, target, param_map);

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
        r[i].copy_to_host();
    }
    return r;
}

Realization Pipeline::realize(int x_size, int y_size, int z_size, int w_size, const Target &target,
                              const ParamMap &param_map) {
  return realize({x_size, y_size, z_size, w_size}, target, param_map);
}

Realization Pipeline::realize(int x_size, int y_size, int z_size, const Target &target,
                              const ParamMap &param_map) {
  return realize({x_size, y_size, z_size}, target, param_map);
}

Realization Pipeline::realize(int x_size, int y_size, const Target &target,
                              const ParamMap &param_map) {
  return realize({x_size, y_size}, target, param_map);
}

Realization Pipeline::realize(int x_size, const Target &target,
                              const ParamMap &param_map) {
    // Use an explicit vector here, since {x_size} can be interpreted
    // as a scalar initializer
    vector<int32_t> v = {x_size};
    return realize(v, target, param_map);
}

Realization Pipeline::realize(const Target &target,
                              const ParamMap &param_map) {
    return realize(vector<int32_t>(), target, param_map);
}

void Pipeline::add_requirement(Expr condition, std::vector<Expr> &error_args) {
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
        Checker(const Expr &c) : condition(c) {
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

namespace {

struct ErrorBuffer {
    enum { MaxBufSize = 4096 };
    char buf[MaxBufSize];
    int end;

    ErrorBuffer() {
        end = 0;
    }

    void concat(const char *message) {
        size_t len = strlen(message);

        if (len && message[len-1] != '\n') {
            // Claim some extra space for a newline.
            len++;
        }

        // Atomically claim some space in the buffer
#ifdef _MSC_VER
        int old_end = _InterlockedExchangeAdd((volatile long *)(&end), len);
#else
        int old_end = __sync_fetch_and_add(&end, len);
#endif

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

    static void handler(void *ctx, const char *message) {
        if (ctx) {
            JITUserContext *ctx1 = (JITUserContext *)ctx;
            ErrorBuffer *buf = (ErrorBuffer *)ctx1->user_context;
            buf->concat(message);
        }
    }
};

struct JITFuncCallContext {
    ErrorBuffer error_buffer;
    JITUserContext jit_context;
    bool custom_error_handler;

    JITFuncCallContext(const JITHandlers &handlers) {
        void *user_context = nullptr;
        JITHandlers local_handlers = handlers;
        if (local_handlers.custom_error == nullptr) {
            custom_error_handler = false;
            local_handlers.custom_error = ErrorBuffer::handler;
            user_context = &error_buffer;
        } else {
            custom_error_handler = true;
        }
        JITSharedRuntime::init_jit_user_context(jit_context, user_context, local_handlers);

        debug(2) << "custom_print: " << (void *)jit_context.handlers.custom_print << '\n'
                 << "custom_malloc: " << (void *)jit_context.handlers.custom_malloc << '\n'
                 << "custom_free: " << (void *)jit_context.handlers.custom_free << '\n'
                 << "custom_do_task: " << (void *)jit_context.handlers.custom_do_task << '\n'
                 << "custom_do_par_for: " << (void *)jit_context.handlers.custom_do_par_for << '\n'
                 << "custom_error: " << (void *)jit_context.handlers.custom_error << '\n'
                 << "custom_trace: " << (void *)jit_context.handlers.custom_trace << '\n';
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

}  // namespace

struct Pipeline::JITCallArgs {
    size_t size{0};
    const void **store;

    JITCallArgs(size_t size) : size(size) {
        if (size > kStoreSize) {
            store = new ConstVoidPtr[size];
        } else {
            store = fixed_store;
        }
    }

    ~JITCallArgs() {
        if (store != fixed_store) {
            delete [] store;
        }
    }

private:
    static constexpr int kStoreSize = 64;
    using ConstVoidPtr = const void *;
    ConstVoidPtr fixed_store[kStoreSize];

    JITCallArgs(const JITCallArgs &) = delete;
    JITCallArgs(JITCallArgs &&) = delete;
    void operator=(const JITCallArgs &) = delete;
};

// Make a vector of void *'s to pass to the jit call using the
// currently bound value for all of the params and image
// params.
void Pipeline::prepare_jit_call_arguments(RealizationArg &outputs, const Target &target,
                                          const ParamMap &param_map, void *user_context,
                                          bool is_bounds_inference, JITCallArgs &args_result) {
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

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
    for (std::map<std::string, JITExtern>::iterator iter = externs_in_out.begin();
         iter != externs_in_out.end();
         iter++) {
        Pipeline pipeline = iter->second.pipeline();
        if (pipeline.defined()) {
            PipelineContents &pipeline_contents(*pipeline.contents);

            // Ensure that the pipeline is compiled.
            pipeline.compile_jit(target);

            free_standing_jit_externs.add_dependency(pipeline_contents.jit_module);
            free_standing_jit_externs.add_symbol_for_export(iter->first, pipeline_contents.jit_module.entrypoint_symbol());
            void *address = pipeline_contents.jit_module.entrypoint_symbol().address;
            std::vector<Type> arg_types;
            // Add the arguments to the compiled pipeline
            for (const InferredArgument &arg : pipeline_contents.inferred_args) {
                // TODO: it's not clear whether arg.arg.type is correct for
                // the arg.is_buffer() case (AFAIK, is_buffer()==true isn't possible
                // in current trunk Halide, but may be in some side branches that
                // have not yet landed, e.g. JavaScript). Forcing it to be
                // the correct type here, just in case.
                arg_types.push_back(arg.arg.is_buffer() ?
                                    type_of<struct buffer_t *>() :
                                    arg.arg.type);
            }
            // Add the outputs of the pipeline
            for (size_t i = 0; i < pipeline_contents.outputs.size(); i++) {
                arg_types.push_back(type_of<struct buffer_t *>());
            }
            ExternSignature signature(Int(32), false, arg_types);
            iter->second = ExternCFunction(address, signature);
        } else {
            free_standing_jit_externs.add_extern_for_export(iter->first, iter->second.extern_c_function());
        }
    }
    if (free_standing_jit_externs.compiled() || !free_standing_jit_externs.exports().empty()) {
        result.push_back(free_standing_jit_externs);
    }
    return result;
}

int Pipeline::call_jit_code(const Target &target, const JITCallArgs &args) {
    if (target.arch == Target::WebAssembly) {
        internal_assert(contents->wasm_module.contents.defined());
        return contents->wasm_module.run(args.store);
    }
    return contents->jit_module.argv_function()(args.store);
}

void Pipeline::realize(RealizationArg outputs, const Target &t,
                       const ParamMap &param_map) {
    Target target = t;
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    debug(2) << "Realizing Pipeline for " << target << "\n";

    // If target is unspecified...
    if (target.os == Target::OSUnknown) {
        // If we've already jit-compiled for a specific target, use that.
        if (contents->jit_module.compiled()) {
            target = contents->jit_target;
        } else {
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
    JITFuncCallContext jit_context(jit_handlers());
    void *user_context_storage = &jit_context.jit_context;

    JITCallArgs args(contents->inferred_args.size() + outputs.size());
    prepare_jit_call_arguments(outputs, target, param_map,
                               &user_context_storage, false, args);


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
    if (target.has_feature(Target::Profile)) {
        JITModule::Symbol report_sym =
            contents->jit_module.find_symbol_by_name("halide_profiler_report");
        JITModule::Symbol reset_sym =
            contents->jit_module.find_symbol_by_name("halide_profiler_reset");
        if (report_sym.address && reset_sym.address) {
            void *uc = &jit_context.jit_context;
            void (*report_fn_ptr)(void *) = (void (*)(void *))(report_sym.address);
            report_fn_ptr(uc);

            void (*reset_fn_ptr)() = (void (*)())(reset_sym.address);
            reset_fn_ptr();
        }
    }

    jit_context.finalize(exit_status);
}

void Pipeline::infer_input_bounds(RealizationArg outputs, const ParamMap &param_map) {
    Target target = get_jit_target_from_environment();

    compile_jit(target);

    // This has to happen after a runtime has been compiled in compile_jit.
    JITFuncCallContext jit_context(jit_handlers());
    void *user_context_storage = &jit_context.jit_context;

    size_t args_size = contents->inferred_args.size() + outputs.size();
    JITCallArgs args(args_size);
    prepare_jit_call_arguments(outputs, target, param_map,
                               &user_context_storage, true, args);

    struct TrackedBuffer {
        // The query buffer, and a backup to check for changes. We
        // want wrappers around actual buffer_ts so that we can copy
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
        int exit_status = call_jit_code(target, args);
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

void Pipeline::infer_input_bounds(int x_size, int y_size, int z_size, int w_size,
                                  const ParamMap &param_map) {
    user_assert(defined()) << "Can't infer input bounds on an undefined Pipeline.\n";

    vector<int> size;
    if (x_size) size.push_back(x_size);
    if (y_size) size.push_back(y_size);
    if (z_size) size.push_back(z_size);
    if (w_size) size.push_back(w_size);

    vector<Buffer<>> bufs;
    for (Type t : contents->outputs[0].output_types()) {
        bufs.emplace_back(t, size);
    }
    Realization r(bufs);
    infer_input_bounds(r, param_map);
}

void Pipeline::invalidate_cache() {
    if (defined()) {
        contents->invalidate_cache();
    }
}

JITExtern::JITExtern(Pipeline pipeline)
    : pipeline_(pipeline) {
}

JITExtern::JITExtern(Func func)
    : pipeline_(func) {
}

JITExtern::JITExtern(const ExternCFunction &extern_c_function)
    : extern_c_function_(extern_c_function) {
}

}  // namespace Halide
