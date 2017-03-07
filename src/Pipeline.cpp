#include <algorithm>

#include "Pipeline.h"
#include "Argument.h"
#include "Func.h"
#include "IRVisitor.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "Lower.h"
#include "Outputs.h"
#include "PrintLoopNest.h"

using namespace Halide::Internal;

namespace Halide {

using std::vector;
using std::string;
using std::set;

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

/** An inferred argument. Inferred args are either Params,
 * ImageParams, or Buffers. The first two are handled by the param
 * field, and global images are tracked via the buf field. These
 * are used directly when jitting, or used for validation when
 * compiling with an explicit argument list. */
struct InferredArgument {
    Argument arg;
    Parameter param;
    Buffer<> buffer;

    bool operator<(const InferredArgument &other) const {
        if (arg.is_buffer() && !other.arg.is_buffer()) {
            return true;
        } else if (other.arg.is_buffer() && !arg.is_buffer()) {
            return false;
        } else {
            return arg.name < other.arg.name;
        }
    }
};

struct PipelineContents {
    mutable RefCount ref_count;

    // Cached lowered stmt
    Module module;

    // Cached jit-compiled code
    JITModule jit_module;
    Target jit_target;

    /** Clear all cached state */
    void invalidate_cache() {
        module = Module("", Target());
        jit_module = JITModule();
        jit_target = Target();
        inferred_args.clear();
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

    /** The inferred arguments. */
    vector<InferredArgument> inferred_args;

    /** List of C funtions and Funcs to satisfy HalideExtern* and
     * define_extern calls. */
    std::map<std::string, JITExtern> jit_externs;

    PipelineContents() :
        module("", Target()) {
        // user_context needs to be a const void * (not a non-const void *)
        // to maintain backwards compatibility with existing code.
        user_context_arg.arg = Argument("__user_context", Argument::InputScalar, type_of<const void*>(), 0);
        user_context_arg.param = Parameter(Handle(), false, 0, "__user_context",
                                           /*is_explicit_name*/ true, /*register_instance*/ false);
    }

    ~PipelineContents() {
        clear_custom_lowering_passes();
    }

    void clear_custom_lowering_passes() {
        invalidate_cache();
        for (size_t i = 0; i < custom_lowering_passes.size(); i++) {
            if (custom_lowering_passes[i].deleter) {
                custom_lowering_passes[i].deleter(custom_lowering_passes[i].pass);
            }
        }
        custom_lowering_passes.clear();
    }
};

namespace Internal {
template<>
EXPORT RefCount &ref_count<PipelineContents>(const PipelineContents *p) {
    return p->ref_count;
}

template<>
EXPORT void destroy<PipelineContents>(const PipelineContents *p) {
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

void Pipeline::compile_to(const Outputs &output_files,
                          const vector<Argument> &args,
                          const string &fn_name,
                          const Target &target) {
    user_assert(defined()) << "Can't compile undefined Pipeline.\n";

    for (Function f : contents->outputs) {
        user_assert(f.has_pure_definition() || f.has_extern_definition())
            << "Can't compile undefined Func.\n";
    }

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

void Pipeline::print_loop_nest() {
    user_assert(defined()) << "Can't print loop nest of undefined Pipeline.\n";
    std::cerr << Halide::Internal::print_loop_nest(contents->outputs);
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

namespace Internal {

class InferArguments : public IRGraphVisitor {
public:
    vector<InferredArgument> &args;

    InferArguments(vector<InferredArgument> &a, const vector<Function> &o, Stmt body)
        : args(a), outputs(o) {
        args.clear();
        for (const Function &f : outputs) {
            visit_function(f);
        }
        if (body.defined()) {
            body.accept(this);
        }
    }

private:
    vector<Function> outputs;
    set<string> visited_functions;

    using IRGraphVisitor::visit;

    bool already_have(const string &name) {
        // Ignore dependencies on the output buffers
        for (const Function &output : outputs) {
            if (name == output.name() || starts_with(name, output.name() + ".")) {
                return true;
            }
        }
        for (const InferredArgument &arg : args) {
            if (arg.arg.name == name) {
                return true;
            }
        }
        return false;
    }

    void visit_exprs(const std::vector<Expr>& v) {
        for (Expr i : v) {
            visit_expr(i);
        }
    }

    void visit_expr(Expr e) {
        if (!e.defined()) return;
        e.accept(this);
    }

    void visit_function(const Function& func) {
        if (visited_functions.count(func.name())) return;
        visited_functions.insert(func.name());

        func.accept(this);

        // Function::accept hits all the Expr children of the
        // Function, but misses the buffers and images that might be
        // extern arguments.
        if (func.has_extern_definition()) {
            for (const ExternFuncArgument &extern_arg : func.extern_arguments()) {
                if (extern_arg.is_func()) {
                    visit_function(Function(extern_arg.func));
                } else if (extern_arg.is_buffer()) {
                    include_buffer(extern_arg.buffer);
                } else if (extern_arg.is_image_param()) {
                    include_parameter(extern_arg.image_param);
                }
            }
        }
    }

    void include_parameter(Parameter p) {
        if (!p.defined()) return;
        if (already_have(p.name())) return;

        Expr def, min, max;
        if (!p.is_buffer()) {
            def = p.get_scalar_expr();
            min = p.get_min_value();
            max = p.get_max_value();
        }

        InferredArgument a = {
            Argument(p.name(),
                     p.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                     p.type(), p.dimensions(), def, min, max),
            p,
            Buffer<>()};
        args.push_back(a);

        // Visit child expressions
        if (!p.is_buffer()) {
            visit_expr(def);
            visit_expr(min);
            visit_expr(max);
        } else {
            for (int i = 0; i < p.dimensions(); i++) {
                visit_expr(p.min_constraint(i));
                visit_expr(p.extent_constraint(i));
                visit_expr(p.stride_constraint(i));
            }
        }
    }

    void include_buffer(Buffer<> b) {
        if (!b.defined()) return;
        if (already_have(b.name())) return;

        InferredArgument a = {
            Argument(b.name(), Argument::InputBuffer, b.type(), b.dimensions()),
            Parameter(),
            b};
        args.push_back(a);
    }

    void visit(const Load *op) {
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Variable *op) {
        IRGraphVisitor::visit(op);
        include_parameter(op->param);
        include_buffer(op->image);
    }

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->func.defined()) {
            Function fn(op->func);
            visit_function(fn);
        }
        include_buffer(op->image);
        include_parameter(op->param);
    }
};

} // namespace Internal

vector<Argument> Pipeline::infer_arguments(Stmt body) {

    user_assert(defined()) << "Can't infer arguments on an undefined Pipeline\n";

    // Infer an arguments vector by walking the IR
    InferArguments infer_args(contents->inferred_args,
                              contents->outputs,
                              body);

    // Sort the Arguments with all buffers first (alphabetical by name),
    // followed by all non-buffers (alphabetical by name).
    std::sort(contents->inferred_args.begin(), contents->inferred_args.end());

    // Add the user context argument.
    contents->inferred_args.push_back(contents->user_context_arg);

    // Return the inferred argument types, minus any constant images
    // (we'll embed those in the binary by default), and minus the user_context arg.
    vector<Argument> result;
    for (const InferredArgument &arg : contents->inferred_args) {
        debug(1) << "Inferred argument: " << arg.arg.type << " " << arg.arg.name << "\n";
        if (!arg.buffer.defined() &&
            arg.arg.name != contents->user_context_arg.arg.name) {
            result.push_back(arg.arg);
        }
    }


    return result;
}

vector<Argument> Pipeline::infer_arguments() {
    return infer_arguments(Stmt());
}

/** Check that all the necessary arguments are in an args vector. Any
 * images in the source that aren't in the args vector are returned. */
vector<Buffer<>> Pipeline::validate_arguments(const vector<Argument> &args, Stmt body) {
    infer_arguments(body);

    vector<Buffer<>> images_to_embed;

    for (const InferredArgument &arg : contents->inferred_args) {

        if (arg.param.same_as(contents->user_context_arg.param)) {
            // The user context is always in the inferred args, but is
            // not required to be in the args list.
            continue;
        }

        internal_assert(arg.arg.is_input()) << "Expected only input Arguments here";

        bool found = false;
        for (Argument a : args) {
            found |= (a.name == arg.arg.name);
        }

        if (arg.buffer.defined() && !found) {
            // It's a raw Buffer used that isn't in the args
            // list. Embed it in the output instead.
            images_to_embed.push_back(arg.buffer);
            debug(1) << "Embedding image " << arg.buffer.name() << "\n";
        } else if (!found) {
            std::ostringstream err;
            err << "Generated code refers to ";
            if (arg.arg.is_buffer()) {
                err << "image ";
            }
            err << "parameter " << arg.arg.name
                << ", which was not found in the argument list.\n";

            err << "\nArgument list specified: ";
            for (size_t i = 0; i < args.size(); i++) {
                err << args[i].name << " ";
            }
            err << "\n\nParameters referenced in generated code: ";
            for (const InferredArgument &ia : contents->inferred_args) {
                if (ia.arg.name != contents->user_context_arg.arg.name) {
                    err << ia.arg.name << " ";
                }
            }
            err << "\n\n";
            user_error << err.str();
        }
    }

    return images_to_embed;
}

vector<Argument> Pipeline::build_public_args(const vector<Argument> &args, const Target &target) const {
    // Get all the arguments/global images referenced in this function.
    vector<Argument> public_args = args;

    // If the target specifies user context but it's not in the args
    // vector, add it at the start (the jit path puts it in there
    // explicitly).
    const bool requires_user_context = target.has_feature(Target::UserContext);
    bool has_user_context = false;
    for (Argument arg : args) {
        if (arg.name == contents->user_context_arg.arg.name) {
            has_user_context = true;
        }
    }
    if (requires_user_context && !has_user_context) {
        public_args.insert(public_args.begin(), contents->user_context_arg.arg);
    }

    // Add the output buffer arguments
    for (Function out : contents->outputs) {
        for (Parameter buf : out.output_buffers()) {
            public_args.push_back(Argument(buf.name(),
                                           Argument::OutputBuffer,
                                           buf.type(), buf.dimensions()));
        }
    }
    return public_args;
}

Module Pipeline::compile_to_module(const vector<Argument> &args,
                                   const string &fn_name,
                                   const Target &target,
                                   const Internal::LoweredFunc::LinkageType linkage_type) {
    user_assert(defined()) << "Can't compile undefined Pipeline\n";
    string new_fn_name(fn_name);
    if (new_fn_name.empty()) {
        new_fn_name = generate_function_name();
    }
    internal_assert(!new_fn_name.empty()) << "new_fn_name cannot be empty\n";
    // TODO: Assert that the function name is legal

    // TODO: This is a bit of a wart. Right now, IR cannot directly
    // reference Buffers because neither CodeGen_LLVM nor
    // CodeGen_C can generate the correct buffer unpacking code.

    // To work around this, we generate two functions. The private
    // function is one where every buffer referenced is an argument,
    // and the public function is a wrapper that calls the private
    // function, passing the global buffers to the private
    // function. This works because the public function does not
    // attempt to directly access any of the fields of the buffer.

    Stmt private_body;

    const Module &old_module = contents->module;
    if (!old_module.functions().empty() &&
        old_module.target() == target) {
        internal_assert(old_module.functions().size() == 2);
        // We can avoid relowering and just reuse the private body
        // from the old module. We expect two functions in the old
        // module: the private one then the public one.
        private_body = old_module.functions().front().body;
        debug(2) << "Reusing old module\n";
    } else {
        vector<IRMutator *> custom_passes;
        for (CustomLoweringPass p : contents->custom_lowering_passes) {
            custom_passes.push_back(p.pass);
        }

        private_body = lower(contents->outputs, fn_name, target, custom_passes);
    }

    std::vector<std::string> namespaces;
    std::string simple_new_fn_name = extract_namespaces(new_fn_name, namespaces);
    string private_name = "__" + simple_new_fn_name;

    // Get all the arguments/global images referenced in this function.
    vector<Argument> public_args = build_public_args(args, target);

    vector<Buffer<>> global_images = validate_arguments(public_args, private_body);

    // Create a module with all the global images in it.
    Module module(simple_new_fn_name, target);

    // Add all the global images to the module, and add the global
    // images used to the private argument list.
    vector<Argument> private_args = public_args;
    for (Buffer<> buf : global_images) {
        module.append(buf);
        private_args.push_back(Argument(buf.name(),
                                        Argument::InputBuffer,
                                        buf.type(), buf.dimensions()));
    }

    module.append(LoweredFunc(private_name, private_args,
                              private_body, LoweredFunc::Internal));

    // Generate a call to the private function, adding an arguments
    // for the global images.
    vector<Expr> private_params;
    for (Argument arg : private_args) {
        if (arg.is_buffer()) {
            private_params.push_back(Variable::make(type_of<void*>(), arg.name + ".buffer"));
        } else {
            private_params.push_back(Variable::make(arg.type, arg.name));
        }
    }
    string private_result_name = unique_name(private_name + "_result");
    Expr private_result_var = Variable::make(Int(32), private_result_name);
    Expr call_private = Call::make(Int(32), private_name, private_params, Call::Extern);
    Stmt public_body = AssertStmt::make(private_result_var == 0, private_result_var);
    public_body = LetStmt::make(private_result_name, call_private, public_body);

    module.append(LoweredFunc(new_fn_name, public_args, public_body, linkage_type));

    contents->module = module;

    return module;
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

void *Pipeline::compile_jit(const Target &target_arg) {
    user_assert(defined()) << "Pipeline is undefined\n";

    Target target(target_arg);
    target.set_feature(Target::JIT);
    target.set_feature(Target::UserContext);

    debug(2) << "jit-compiling for: " << target_arg.to_string() << "\n";

    // If we're re-jitting for the same target, we can just keep the
    // old jit module.
    if (contents->jit_target == target &&
        contents->jit_module.compiled()) {
        debug(2) << "Reusing old jit module compiled for :\n" << contents->jit_target.to_string() << "\n";
        return contents->jit_module.main_function();
    }

    contents->jit_target = target;

    // Infer an arguments vector
    infer_arguments();

    // Come up with a name for the generated function
    string name = generate_function_name();

    vector<Argument> args;
    for (const InferredArgument &arg : contents->inferred_args) {
        args.push_back(arg.arg);
    }

    // Compile to a module
    Module module = compile_to_module(args, name, target);

    // We need to infer the arguments again, because compiling (GPU
    // and offload targets) might have added new buffers we need to
    // embed.
    auto f = module.get_function_by_name(name);
    infer_arguments(f.body);

    std::map<std::string, JITExtern> lowered_externs = contents->jit_externs;
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

    return jit_module.main_function();
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

void Pipeline::add_custom_lowering_pass(IRMutator *pass, void (*deleter)(IRMutator *)) {
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

Realization Pipeline::realize(vector<int32_t> sizes,
                              const Target &target) {
    user_assert(defined()) << "Pipeline is undefined\n";
    vector<Buffer<>> bufs;
    for (auto & out : contents->outputs) {
        for (Type t : out.output_types()) {
            bufs.emplace_back(t, sizes);
        }
    }
    Realization r(bufs);
    realize(r, target);
    for (size_t i = 0; i < r.size(); i++) {
        r[i].copy_to_host();
    }
    return r;
}

Realization Pipeline::realize(int x_size, int y_size, int z_size, int w_size,
                              const Target &target) {
    return realize({x_size, y_size, z_size, w_size}, target);
}

Realization Pipeline::realize(int x_size, int y_size, int z_size,
                              const Target &target) {
    return realize({x_size, y_size, z_size}, target);
}

Realization Pipeline::realize(int x_size, int y_size,
                              const Target &target) {
    return realize({x_size, y_size}, target);
}

Realization Pipeline::realize(int x_size,
                              const Target &target) {
    // Use an explicit vector here, since {x_size} can be interpreted
    // as a scalar initializer
    vector<int32_t> v = {x_size};
    return realize(v, target);
}

Realization Pipeline::realize(const Target &target) {
    return realize(vector<int32_t>(), target);
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
    Parameter &user_context_param;
    bool custom_error_handler;

    JITFuncCallContext(const JITHandlers &handlers, Parameter &user_context_param)
        : user_context_param(user_context_param) {
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
        user_context_param.set_scalar(&jit_context);

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
        user_context_param.set_scalar((void *)nullptr); // Don't leave param hanging with pointer to stack.
    }
};

}  // namespace

// Make a vector of void *'s to pass to the jit call using the
// currently bound value for all of the params and image
// params.
vector<const void *> Pipeline::prepare_jit_call_arguments(Realization dst, const Target &target) {
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    compile_jit(target);

    JITModule &compiled_module = contents->jit_module;
    internal_assert(compiled_module.argv_function());

    struct OutputBufferType {
        Function func;
        Type type;
        int dims;
    };
    vector<OutputBufferType> output_buffer_types;
    for (Function f : contents->outputs) {
        for (Type t : f.output_types()) {
            OutputBufferType obt = {f, t, f.dimensions()};
            output_buffer_types.push_back(obt);
        }
    }

    user_assert(output_buffer_types.size() == dst.size())
        << "Realization contains wrong number of Images (" << dst.size()
        << ") for realizing pipeline with " << output_buffer_types.size()
        << " outputs\n";

    // Check the type and dimensionality of the buffer
    for (size_t i = 0; i < dst.size(); i++) {
        Function func = output_buffer_types[i].func;
        int  dims = output_buffer_types[i].dims;
        Type type = output_buffer_types[i].type;
        user_assert(dst[i].dimensions() == dims)
            << "Can't realize Func \"" << func.name()
            << "\" into Buffer at " << (void *)dst[i].data()
            << " because Buffer is " << dst[i].dimensions()
            << "-dimensional, but Func \"" << func.name()
            << "\" is " << dims << "-dimensional.\n";
        user_assert(dst[i].type() == type)
            << "Can't realize Func \"" << func.name()
            << "\" into Buffer at " << (void *)dst[i].data()
            << " because Buffer has type " << Type(dst[i].type())
            << ", but Func \"" << func.name()
            << "\" has type " << type << ".\n";
    }

    // Come up with the void * arguments to pass to the argv function
    const vector<InferredArgument> &input_args = contents->inferred_args;
    vector<const void *> arg_values;

    // First the inputs
    for (InferredArgument arg : input_args) {
        if (arg.param.defined() && arg.param.is_buffer()) {
            // ImageParam arg
            Buffer<> buf = arg.param.get_buffer();
            if (buf.defined()) {
                arg_values.push_back(buf.raw_buffer());
            } else {
                // Unbound
                arg_values.push_back(nullptr);
            }
            debug(1) << "JIT input ImageParam argument ";
        } else if (arg.param.defined()) {
            arg_values.push_back(arg.param.get_scalar_address());
            debug(1) << "JIT input scalar argument ";
        } else {
            debug(1) << "JIT input Image argument ";
            internal_assert(arg.buffer.defined());
            arg_values.push_back(arg.buffer.raw_buffer());
        }
        const void *ptr = arg_values.back();
        debug(1) << arg.arg.name << " @ " << ptr << "\n";
    }

    // Then the outputs
    for (size_t i = 0; i < dst.size(); i++) {
        arg_values.push_back(dst[i].raw_buffer());
        const void *ptr = arg_values.back();
        debug(1) << "JIT output buffer @ " << ptr << ", " << dst[i].data() << "\n";
    }

    return arg_values;
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
                // in current mtrunk Halide, but may be in some side branches that
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

void Pipeline::realize(Realization dst, const Target &t) {
    Target target = t;
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    debug(2) << "Realizing Pipeline for " << target.to_string() << "\n";

    for (size_t i = 0; i < dst.size(); i++) {
        user_assert(dst[i].data() != nullptr)
            << "Buffer at " << &(dst[i]) << " is unallocated. "
            << "The Buffers in a Realization passed to realize must all be allocated\n";
    }

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

    vector<const void *> args = prepare_jit_call_arguments(dst, target);

    for (size_t i = 0; i < contents->inferred_args.size(); i++) {
        const InferredArgument &arg = contents->inferred_args[i];
        const void *arg_value = args[i];
        if (arg.param.defined()) {
            user_assert(arg_value != nullptr)
                << "Can't realize a pipeline because ImageParam "
                << arg.param.name() << " is not bound to a Buffer\n";
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

    JITFuncCallContext jit_context(jit_handlers(), contents->user_context_arg.param);

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
    int exit_status = contents->jit_module.argv_function()(&(args[0]));
    debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

    // If we're profiling, report runtimes and reset profiler stats.
    if (target.has_feature(Target::Profile)) {
        JITModule::Symbol report_sym =
            contents->jit_module.find_symbol_by_name("halide_profiler_report");
        JITModule::Symbol reset_sym =
            contents->jit_module.find_symbol_by_name("halide_profiler_reset");
        if (report_sym.address && reset_sym.address) {
            void *uc = jit_context.user_context_param.get_scalar<void *>();
            void (*report_fn_ptr)(void *) = (void (*)(void *))(report_sym.address);
            report_fn_ptr(uc);

            void (*reset_fn_ptr)() = (void (*)())(reset_sym.address);
            reset_fn_ptr();
        }
    }

    jit_context.finalize(exit_status);
}

void Pipeline::infer_input_bounds(Realization dst) {

    Target target = get_jit_target_from_environment();

    vector<const void *> args = prepare_jit_call_arguments(dst, target);

    struct TrackedBuffer {
        // The query buffer, and a backup to check for changes. We
        // want wrappers around actual buffer_ts so that we can copy
        // the metadata, not shared pointers to a single buffer, so
        // it's simpler to use the runtime buffer class.
        Runtime::Buffer<> query, orig;
    };
    vector<TrackedBuffer> tracked_buffers(args.size());

    vector<size_t> query_indices;
    for (size_t i = 0; i < contents->inferred_args.size(); i++) {
        if (args[i] == nullptr) {
            query_indices.push_back(i);
            InferredArgument ia = contents->inferred_args[i];
            internal_assert(ia.param.defined() && ia.param.is_buffer());
            // Make some empty Buffers of the right dimensionality
            vector<int> initial_shape(ia.param.dimensions(), 0);
            tracked_buffers[i].query = Runtime::Buffer<>(ia.param.type(), nullptr, initial_shape);
            tracked_buffers[i].orig = Runtime::Buffer<>(ia.param.type(), nullptr, initial_shape);
            args[i] = tracked_buffers[i].query.raw_buffer();
        }
    }

    // No need to query if all the inputs are bound already.
    if (query_indices.empty()) {
        debug(1) << "All inputs are bound. No need for bounds inference\n";
        return;
    }

    JITFuncCallContext jit_context(jit_handlers(), contents->user_context_arg.param);

    int iter = 0;
    const int max_iters = 16;
    for (iter = 0; iter < max_iters; iter++) {
        // Make a copy of the buffers that might be mutated
        for (TrackedBuffer &tb : tracked_buffers) {
            // Make a copy of the buffer sizes, etc.
            tb.orig = tb.query;
        }

        Internal::debug(2) << "Calling jitted function\n";
        int exit_status = contents->jit_module.argv_function()(&(args[0]));
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

    debug(1) << "Bounds inference converged after " << iter << " iterations\n";

    // Now allocate the resulting buffers
    for (size_t i : query_indices) {
        InferredArgument ia = contents->inferred_args[i];
        internal_assert(!ia.param.get_buffer().defined());

        // Allocate enough memory with the right type and dimensionality.
        tracked_buffers[i].query.allocate();

        // Bind this parameter to this buffer, giving away the
        // buffer. The user retrieves it via ImageParam::get.
        ia.param.set_buffer(Buffer<>(std::move(tracked_buffers[i].query)));
    }
}

void Pipeline::infer_input_bounds(int x_size, int y_size, int z_size, int w_size) {
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
    infer_input_bounds(r);
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
