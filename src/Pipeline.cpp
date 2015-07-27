#include <algorithm>
#include <iostream>
#include <fstream>

#include "Pipeline.h"
#include "Argument.h"
#include "CodeGen_JavaScript.h"
#include "Func.h"
#include "IRVisitor.h"
#include "JavaScriptExecutor.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "Lower.h"
#include "Output.h"
#include "PrintLoopNest.h"

using namespace Halide::Internal;

namespace Halide {

using std::vector;
using std::string;
using std::set;

/** An inferred argument. Inferred args are either Params,
 * ImageParams, or Buffers. The first two are handled by the param
 * field, and global images are tracked via the buf field. These
 * are used directly when jitting, or used for validation when
 * compiling with an explicit argument list. */
struct InferredArgument {
    Argument arg;
    Parameter param;
    Buffer buffer;

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

    // Cache compiled JavaScript is JITting with Target::JavaScript. */
    std::string cached_javascript;

    /** Clear all cached state */
    void invalidate_cache() {
        module = Module("", Target());
        jit_module = JITModule();
        jit_target = Target();
        inferred_args.clear();
        cached_javascript.clear();
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
        user_context_arg.arg = Argument("__user_context", Argument::InputScalar, Handle(), 0);
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

Pipeline::Pipeline() : contents(NULL) {
}

bool Pipeline::defined() const {
    return contents.defined();
}

Pipeline::Pipeline(Func output) : contents(new PipelineContents) {
    output.compute_root().store_root();
    output.function().freeze();
    contents.ptr->outputs.push_back(output.function());
}

Pipeline::Pipeline(const vector<Func> &outputs) : contents(new PipelineContents) {
    for (Func f: outputs) {
        f.compute_root().store_root();
        f.function().freeze();
        contents.ptr->outputs.push_back(f.function());
    }
}

vector<Func> Pipeline::outputs() {
    vector<Func> funcs;
    for (Function f : contents.ptr->outputs) {
        funcs.push_back(Func(f));
    }
    return funcs;
}

void Pipeline::compile_to(const Outputs &output_files,
                          const vector<Argument> &args,
                          const string &fn_name,
                          const Target &target) {
    user_assert(defined()) << "Can't compile undefined Pipeline.\n";

    for (Function f : contents.ptr->outputs) {
        user_assert(f.has_pure_definition() || f.has_extern_definition())
            << "Can't compile undefined Func.\n";
    }

    Module m = compile_to_module(args, fn_name, target);

    llvm::LLVMContext context;
    llvm::Module *llvm_module = compile_module_to_llvm_module(m, context);

    if (!output_files.object_name.empty()) {
        if (target.arch == Target::PNaCl) {
            compile_llvm_module_to_llvm_bitcode(llvm_module, output_files.object_name);
        } else {
            compile_llvm_module_to_object(llvm_module, output_files.object_name);
        }
    }
    if (!output_files.assembly_name.empty()) {
        if (target.arch == Target::PNaCl) {
            compile_llvm_module_to_llvm_assembly(llvm_module, output_files.assembly_name);
        } else {
            compile_llvm_module_to_assembly(llvm_module, output_files.assembly_name);
        }
    }
    if (!output_files.bitcode_name.empty()) {
        compile_llvm_module_to_llvm_bitcode(llvm_module, output_files.bitcode_name);
    }

    delete llvm_module;
}


void Pipeline::compile_to_bitcode(const string &filename,
                                  const vector<Argument> &args,
                                  const string &fn_name,
                                  const Target &target) {
    compile_module_to_llvm_bitcode(compile_to_module(args, fn_name, target), filename);
}

void Pipeline::compile_to_object(const string &filename,
                                 const vector<Argument> &args,
                                 const string &fn_name,
                                 const Target &target) {
    compile_module_to_object(compile_to_module(args, fn_name, target), filename);
}

void Pipeline::compile_to_header(const string &filename,
                                 const vector<Argument> &args,
                                 const string &fn_name,
                                 const Target &target) {
    compile_module_to_c_header(compile_to_module(args, fn_name, target), filename);
}

void Pipeline::compile_to_assembly(const string &filename,
                                   const vector<Argument> &args,
                                   const string &fn_name,
                                   const Target &target) {
    compile_module_to_assembly(compile_to_module(args, fn_name, target), filename);
}


void Pipeline::compile_to_c(const string &filename,
                            const vector<Argument> &args,
                            const string &fn_name,
                            const Target &target) {
    compile_module_to_c_source(compile_to_module(args, fn_name, target), filename);
}

void Pipeline::compile_to_javascript(const string &filename,
                                     const vector<Argument> &args,
                                     const string &fn_name,
                                     const Target &target) {
    compile_module_to_javascript_source(compile_to_module(args, fn_name, target), filename);
}

void Pipeline::print_loop_nest() {
    user_assert(defined()) << "Can't print loop nest of undefined Pipeline.\n";
    std::cerr << Halide::Internal::print_loop_nest(contents.ptr->outputs);
}

void Pipeline::compile_to_lowered_stmt(const string &filename,
                                       const vector<Argument> &args,
                                       StmtOutputFormat fmt,
                                       const Target &target) {
    Module m = compile_to_module(args, "", target);
    if (fmt == HTML) {
        compile_module_to_html(m, filename);
    } else {
        compile_module_to_text(m, filename);
    }
}

void Pipeline::compile_to_file(const string &filename_prefix,
                               const vector<Argument> &args,
                               const Target &target) {
    Module m = compile_to_module(args, filename_prefix, target);
    compile_module_to_c_header(m, filename_prefix + ".h");

    if (target.arch == Target::PNaCl) {
        compile_module_to_llvm_bitcode(m, filename_prefix + ".o");
    } else {
        compile_module_to_object(m, filename_prefix + ".o");
    }
}

namespace Internal {

class InferArguments : public IRGraphVisitor {
public:
    vector<InferredArgument> &args;

    InferArguments(vector<InferredArgument> &a,
                   const vector<Function> &o) : args(a), outputs(o) {
        args.clear();
        for (const Function &f : outputs) {
            visit_function(f);
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
                    visit_function(extern_arg.func);
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
            Buffer()};
        args.push_back(a);
    }

    void include_buffer(Buffer b) {
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
        visit_function(op->func);
        include_buffer(op->image);
        include_parameter(op->param);
    }
};

} // namespace Internal

vector<Argument> Pipeline::infer_arguments() {
    user_assert(defined()) << "Can't infer arguments on an undefined Pipeline\n";

    if (contents.ptr->inferred_args.empty()) {
        // Infer an arguments vector by walking the IR
        InferArguments infer_args(contents.ptr->inferred_args,
                                  contents.ptr->outputs);

        // Sort the Arguments with all buffers first (alphabetical by name),
        // followed by all non-buffers (alphabetical by name).
        std::sort(contents.ptr->inferred_args.begin(), contents.ptr->inferred_args.end());

        // Add the user context argument.
        contents.ptr->inferred_args.push_back(contents.ptr->user_context_arg);
    }

    // Return the inferred argument types, minus any constant images
    // (we'll embed those in the binary by default), and minus the user_context arg.
    vector<Argument> result;
    for (const InferredArgument &arg : contents.ptr->inferred_args) {
        debug(1) << "Inferred argument: " << arg.arg.type << " " << arg.arg.name << "\n";
        if (!arg.buffer.defined() &&
            arg.arg.name != contents.ptr->user_context_arg.arg.name) {
            result.push_back(arg.arg);
        }
    }


    return result;
}

class FindExterns : public IRGraphVisitor {
    using IRGraphVisitor::visit;

    bool buffer_like_name(const std::string &name) {
        bool is_bounds_query = name.find(".bounds_query") != std::string::npos;
        return is_bounds_query ||
            (ends_with(name, ".buffer") || ends_with(name, ".tmp_buffer"));
    }

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);

        if (op->call_type == Call::Extern &&
            externs.count(op->name) == 0) {
            void *address = get_symbol_address(op->name.c_str());
            if (address == NULL && !starts_with(op->name, "_")) {
                std::string underscored_name = "_" + op->name;
                address = get_symbol_address(underscored_name.c_str());
            }
            if (address != NULL) {
                struct JITExtern jit_extern;
                jit_extern.c_function = address;
                jit_extern.signature.is_void_return = op->type.bits == 0;
                jit_extern.signature.ret_type = op->type;
                for (Expr e : op->args) {
                    ScalarOrBufferT this_arg;
                    this_arg.scalar_type = e.type();
                    if (e.type().is_handle()) {
                        const Variable *v = e.as<Variable>();
                        // This code heuristically matches the patterns define_extern uses to pass buffer arguments and result parameters.
                        // TODO: Come up with a way to keep the buffer typing through lowering.
                        this_arg.is_buffer = (v != NULL) && buffer_like_name(v->name);
                    } else {
                        this_arg.is_buffer = false;
                    }
                    jit_extern.signature.arg_types.push_back(this_arg);
                }
                externs[op->name] = jit_extern;
            }
        }
    }

public:
    FindExterns(std::map<std::string, JITExtern> &externs) : externs(externs) {
    }

    std::map<std::string, JITExtern> &externs;
};

/** Check that all the necessary arguments are in an args vector. Any
 * images in the source that aren't in the args vector are returned. */
vector<Buffer> Pipeline::validate_arguments(const vector<Argument> &args) {
    infer_arguments();

    vector<Buffer> images_to_embed;

    for (const InferredArgument &arg : contents.ptr->inferred_args) {

        if (arg.param.same_as(contents.ptr->user_context_arg.param)) {
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
            for (const InferredArgument &ia : contents.ptr->inferred_args) {
                if (ia.arg.name != contents.ptr->user_context_arg.arg.name) {
                    err << ia.arg.name << " ";
                }
            }
            err << "\n\n";
            user_error << err.str();
        }
    }

    return images_to_embed;
}


Module Pipeline::compile_to_module(const vector<Argument> &args,
                                   const string &fn_name,
                                   const Target &target) {
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

    const Module &old_module = contents.ptr->module;
    if (!old_module.functions.empty() &&
        old_module.target() == target) {
        internal_assert(old_module.functions.size() == 2);
        // We can avoid relowering and just reuse the private body
        // from the old module. We expect two functions in the old
        // module: the private one then the public one.
        private_body = old_module.functions[0].body;
        debug(2) << "Reusing old module\n";
    } else {
        vector<IRMutator *> custom_passes;
        for (CustomLoweringPass p : contents.ptr->custom_lowering_passes) {
            custom_passes.push_back(p.pass);
        }

        private_body = lower(contents.ptr->outputs, fn_name, target, custom_passes);
    }

    string private_name = "__" + new_fn_name;

    // Get all the arguments/global images referenced in this function.
    vector<Argument> public_args = args;

    // If the target specifies user context but it's not in the args
    // vector, add it at the start (the jit path puts it in there
    // explicitly).
    bool requires_user_context = target.has_feature(Target::UserContext);
    bool has_user_context = false;
    for (Argument arg : args) {
        if (arg.name == contents.ptr->user_context_arg.arg.name) {
            has_user_context = true;
        }
    }
    if (requires_user_context && !has_user_context) {
        public_args.insert(public_args.begin(), contents.ptr->user_context_arg.arg);
    }

    vector<Buffer> global_images = validate_arguments(public_args);

    // Add the output buffer arguments
    for (Function out : contents.ptr->outputs) {
        for (Parameter buf : out.output_buffers()) {
            public_args.push_back(Argument(buf.name(),
                                           Argument::OutputBuffer,
                                           buf.type(), buf.dimensions()));
        }
    }

    // Create a module with all the global images in it.
    Module module(new_fn_name, target);

    // Add all the global images to the module, and add the global
    // images used to the private argument list.
    vector<Argument> private_args = public_args;
    for (Buffer buf : global_images) {
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
    string private_result_name = unique_name(private_name + "_result", false);
    Expr private_result_var = Variable::make(Int(32), private_result_name);
    Expr call_private = Call::make(Int(32), private_name, private_params, Call::Extern);
    Stmt public_body = AssertStmt::make(private_result_var == 0, private_result_var);
    public_body = LetStmt::make(private_result_name, call_private, public_body);

    module.append(LoweredFunc(new_fn_name, public_args, public_body, LoweredFunc::External));

    contents.ptr->module = module;

    return module;
}

std::string Pipeline::generate_function_name() {
    user_assert(defined()) << "Pipeline is undefined\n";
    // Come up with a name for a generated function
    string name = contents.ptr->outputs[0].name();
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

    debug(2) << "jit-compiling for: " << target_arg.to_string() << "\n";

    std::map<std::string, JITExtern> externs_js = contents.ptr->jit_externs;
    // TODO: see if JS and non-JS can share more code.
    if (target.has_feature(Target::JavaScript)) {
    #if defined(WITH_JAVASCRIPT_V8) || defined(WITH_JAVASCRIPT_SPIDERMONKEY)
        if (!contents.ptr->cached_javascript.empty()) {
            return;
        }
    #else
        user_error << "Cannot JIT JavaScript without a JavaScript execution engine (e.g. V8) configured at compile time.\n";
    #endif
    } else {
        // If we're re-jitting for the same target, we can just keep the
        // old jit module.
        if (contents.ptr->jit_target == target &&
            contents.ptr->jit_module.compiled()) {
            debug(2) << "Reusing old jit module compiled for :\n" << contents.ptr->jit_target.to_string() << "\n";
            return;
        }
    }

    contents.ptr->jit_target = target;

    // Infer an arguments vector
    infer_arguments();

    // Come up with a name for the generated function
    string name = generate_function_name();

    vector<Argument> args;
    for (const InferredArgument &arg : contents.ptr->inferred_args) {
        args.push_back(arg.arg);
    }

    // Compile to a module
    Module module = compile_to_module(args, name, target);

    if (target.has_feature(Target::JavaScript)) {
        std::stringstream out(std::ios_base::out);
        CodeGen_JavaScript cg(out);
        cg.compile(module);
        contents.ptr->cached_javascript = out.str();
        if (debug::debug_level >= 3) {
            std::ofstream js_debug((name + ".js").c_str());
            js_debug << contents.ptr->cached_javascript;
        }
    } else {
        // Make sure we're not embedding any images
        internal_assert(module.buffers.empty());

        std::map<std::string, JITExtern> lowered_externs = contents.ptr->jit_externs;
        // Compile to jit module
        JITModule jit_module(module, module.functions.back(),
                             make_externs_jit_module(target_arg, lowered_externs));

        if (debug::debug_level >= 3) {
            compile_module_to_native(module, name + ".bc", name + ".s");
            compile_module_to_text(module, name + ".stmt");
        }

        contents.ptr->jit_module = jit_module;
    }
}

void Pipeline::set_error_handler(void (*handler)(void *, const char *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_handlers.custom_error = handler;
}

void Pipeline::set_custom_allocator(void *(*cust_malloc)(void *, size_t),
                                    void (*cust_free)(void *, void *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_handlers.custom_malloc = cust_malloc;
    contents.ptr->jit_handlers.custom_free = cust_free;
}

void Pipeline::set_custom_do_par_for(int (*cust_do_par_for)(void *, int (*)(void *, int, uint8_t *), int, int, uint8_t *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_handlers.custom_do_par_for = cust_do_par_for;
}

void Pipeline::set_custom_do_task(int (*cust_do_task)(void *, int (*)(void *, int, uint8_t *), int, uint8_t *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_handlers.custom_do_task = cust_do_task;
}

void Pipeline::set_custom_trace(int (*trace_fn)(void *, const halide_trace_event *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_handlers.custom_trace = trace_fn;
}

void Pipeline::set_custom_print(void (*cust_print)(void *, const char *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_handlers.custom_print = cust_print;
}

void Pipeline::set_jit_externs(const std::map<std::string, JITExtern> &externs) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->jit_externs = externs;
    invalidate_cache();
}

const std::map<std::string, JITExtern> &Pipeline::get_jit_externs() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents.ptr->jit_externs;
}

void Pipeline::add_custom_lowering_pass(IRMutator *pass, void (*deleter)(IRMutator *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->invalidate_cache();
    CustomLoweringPass p = {pass, deleter};
    contents.ptr->custom_lowering_passes.push_back(p);
}

void Pipeline::clear_custom_lowering_passes() {
    if (!defined()) return;
    contents.ptr->clear_custom_lowering_passes();
}

const vector<CustomLoweringPass> &Pipeline::custom_lowering_passes() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents.ptr->custom_lowering_passes;
}

const JITHandlers &Pipeline::jit_handlers() {
    user_assert(defined()) << "Pipeline is undefined\n";
    return contents.ptr->jit_handlers;
}

void Pipeline::realize(Buffer b, const Target &target) {
    realize(Realization({b}), target);
}

Realization Pipeline::realize(vector<int32_t> sizes,
                              const Target &target) {
    user_assert(defined()) << "Pipeline is undefined\n";
    vector<Buffer> bufs;
    for (Type t : contents.ptr->outputs[0].output_types()) {
        bufs.push_back(Buffer(t, sizes));
    }
    Realization r(bufs);
    realize(r, target);
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

    JITFuncCallContext(const JITHandlers &handlers, Parameter &user_context_param)
        : user_context_param(user_context_param) {
        void *user_context = NULL;
        JITHandlers local_handlers = handlers;
        if (local_handlers.custom_error == NULL) {
            local_handlers.custom_error = ErrorBuffer::handler;
            user_context = &error_buffer;
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
        if (exit_status) {
            std::string output = error_buffer.str();
            if (!output.empty()) {
                // Only report the errors if no custom error handler was installed
                halide_runtime_error << error_buffer.str();
                error_buffer.end = 0;
            }
        }
    }

    void finalize(int exit_status) {
        report_if_error(exit_status);
        user_context_param.set_scalar((void *)NULL); // Don't leave param hanging with pointer to stack.
    }
};
}

// Make a vector of void *'s to pass to the jit call using the
// currently bound value for all of the params and image
// params. Unbound image params produce null values.
std::pair<vector<const void *>, vector<Argument>>
Pipeline::prepare_jit_call_arguments(Realization dst, const Target &target) {
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    compile_jit(target);

    JITModule &compiled_module = contents.ptr->jit_module;
    internal_assert(compiled_module.argv_function() ||
                    !contents.ptr->cached_javascript.empty());

    struct OutputBufferType {
        Function func;
        Type type;
        int dims;
    };
    vector<OutputBufferType> output_buffer_types;
    for (Function f : contents.ptr->outputs) {
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
            << "\" into Buffer \"" << dst[i].name()
            << "\" because Buffer \"" << dst[i].name()
            << "\" is " << dst[i].dimensions() << "-dimensional"
            << ", but Func \"" << func.name()
            << "\" is " << dims << "-dimensional.\n";
        user_assert(dst[i].type() == type)
            << "Can't realize Func \"" << func.name()
            << "\" into Buffer \"" << dst[i].name()
            << "\" because Buffer \"" << dst[i].name()
            << "\" has type " << dst[i].type()
            << ", but Func \"" << func.name()
            << "\" has type " << type << ".\n";
    }

    // Come up with the void * arguments to pass to the argv function
    const vector<InferredArgument> &input_args = contents.ptr->inferred_args;
    vector<const void *> arg_values;
    vector<Argument> arg_types;

    // First the inputs
    for (InferredArgument arg : input_args) {
        if (arg.param.defined() && arg.param.is_buffer()) {
            // ImageParam arg
            Buffer buf = arg.param.get_buffer();
            if (buf.defined()) {
                arg_values.push_back(buf.raw_buffer());
            } else {
                // Unbound
                arg_values.push_back(NULL);
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
        arg_types.push_back(arg.arg);
        const void *ptr = arg_values.back();
        debug(1) << arg.arg.name << " @ " << ptr << "\n";
    }

    // Then the outputs
    for (Buffer buf : dst.as_vector()) {
        internal_assert(buf.defined()) << "Can't realize into an undefined Buffer\n";
        arg_values.push_back(buf.raw_buffer());
        arg_types.push_back(Argument(buf.name(), Argument::OutputBuffer, buf.type(), buf.dimensions()));
        const void *ptr = arg_values.back();
        debug(1) << "JIT output buffer " << buf.name()
                 << " @ " << ptr << "\n";
    }

    return std::make_pair(arg_values, arg_types);
}

std::vector<JITModule>
Pipeline::make_externs_jit_module(const Target &target,
                                  std::map<std::string, JITExtern> &externs_in_out) {
    std::vector<JITModule> result;

    // Turn off JavaScript when compiling dependent Funcs so they are compiled to native.
    Target no_js_target = target;
    no_js_target.set_feature(Target::JavaScript, false);
    no_js_target.set_feature(Target::JavaScript_V8, false);
    no_js_target.set_feature(Target::JavaScript_SpiderMonkey, false);

    // Externs that are Funcs get their own JITModule. All standalone functions are
    // held in a single JITModule at the end of the list (if there are any).
    JITModule free_standing_jit_externs;
    for (std::map<std::string, JITExtern>::iterator iter = externs_in_out.begin();
         iter != externs_in_out.end();
         iter++) {
        JITExtern &jit_extern(iter->second);
        if (iter->second.pipeline.defined()) {
            PipelineContents &pipeline_contents(*jit_extern.pipeline.contents.ptr);

            // Ensure that the pipeline is compiled.
            jit_extern.pipeline.compile_jit(no_js_target);

            free_standing_jit_externs.add_dependency(pipeline_contents.jit_module);
            free_standing_jit_externs.add_symbol_for_export(iter->first, pipeline_contents.jit_module.entrypoint_symbol());
            iter->second.c_function = pipeline_contents.jit_module.entrypoint_symbol().address;
            iter->second.signature.is_void_return = false;
            iter->second.signature.ret_type = Int(32);
            // Add the arguments to the compiled pipeline
            for (const InferredArgument &arg : pipeline_contents.inferred_args) {
                 ScalarOrBufferT arg_type_info;
                 arg_type_info.is_buffer = arg.arg.is_buffer();
                 if (!arg_type_info.is_buffer) {
                     arg_type_info.scalar_type = arg.arg.type;
                 }
                 iter->second.signature.arg_types.push_back(arg_type_info);
            }
            // Add the outputs of the pipeline
            for (size_t i = 0; i < pipeline_contents.outputs.size(); i++) {
                ScalarOrBufferT arg_type_info;
                arg_type_info.is_buffer = true;
                iter->second.signature.arg_types.push_back(arg_type_info);
            }
            iter->second.pipeline = Pipeline();
        } else {
            free_standing_jit_externs.add_extern_for_export(iter->first, jit_extern.signature, jit_extern.c_function);
        }
    }
    if (free_standing_jit_externs.compiled() || !free_standing_jit_externs.exports().empty()) {
        result.push_back(free_standing_jit_externs);
    }
    return result;
}

int Pipeline::call_jit_code(const Target &target, std::pair<std::vector<const void *>, std::vector<Argument>> &args) {
    int exit_status = 0;
    #if defined(WITH_JAVASCRIPT_V8) || defined(WITH_JAVASCRIPT_SPIDERMONKEY)
    if (target.has_feature(Target::JavaScript)) {
        internal_assert(!contents.ptr->cached_javascript.empty());

        // Sanitise the name of the generated function
        string js_fn_name = contents.ptr->module.name();
        for (size_t i = 0; i < js_fn_name.size(); i++) {
            if (!isalnum(js_fn_name[i])) {
                js_fn_name[i] = '_';
            }
        }

        std::map<std::string, JITExtern> externs_js;
        externs_js = contents.ptr->jit_externs;
        FindExterns find_externs(externs_js);
        for (LoweredFunc &f : contents.ptr->module.functions) {
            f.body.accept(&find_externs);
        }
        externs_js = filter_externs(externs_js);

        std::vector<std::pair<Argument, const void *> > js_args;
        for (size_t i = 0; i < args.second.size(); i++) {
            js_args.push_back(std::make_pair(args.second[i], args.first[i]));
        }

        Internal::debug(2) << "Calling jitted JavaScript function " << js_fn_name << "\n";
        std::vector<JITModule> extern_deps = make_externs_jit_module(target, externs_js);
        exit_status = run_javascript(target, contents.ptr->cached_javascript, js_fn_name, js_args, externs_js, extern_deps);
        Internal::debug(2) << "Back from jitted JavaScript function. Exit status was " << exit_status << "\n";
    } else
    #endif
    {
        debug(2) << "Calling jitted function\n";
        int exit_status = contents.ptr->jit_module.argv_function()(&(args.first[0]));
        debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";
    }

    return exit_status;
}

void Pipeline::realize(Realization dst, const Target &t) {
    Target target = t;
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";

    debug(2) << "Realizing Pipeline for " << target.to_string() << "\n";

    // If target is unspecified...
    if (target.os == Target::OSUnknown) {
        // If we've already jit-compiled for a specific target, use that.
        if (contents.ptr->jit_module.compiled()) {
            target = contents.ptr->jit_target;
        } else {
            // Otherwise get the target from the environment
            target = get_jit_target_from_environment();
        }
    }

    std::pair<vector<const void *>, vector<Argument>> args = prepare_jit_call_arguments(dst, target);

    for (size_t i = 0; i < contents.ptr->inferred_args.size(); i++) {
        const InferredArgument &arg = contents.ptr->inferred_args[i];
        const void *arg_value = args.first[i];
        if (arg.param.defined()) {
            user_assert(arg_value != NULL)
                << "Can't realize a pipeline because ImageParam "
                << arg.param.name() << " is not bound to a Buffer\n";
        }
    }

    JITFuncCallContext jit_context(jit_handlers(), contents.ptr->user_context_arg.param);

    int exit_status = call_jit_code(target, args);

    // If we're profiling, report runtimes and reset profiler stats.
    if (target.has_feature(Target::Profile)) {
        JITModule::Symbol report_sym =
            contents.ptr->jit_module.find_symbol_by_name("halide_profiler_report");
        JITModule::Symbol reset_sym =
            contents.ptr->jit_module.find_symbol_by_name("halide_profiler_reset");
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

    std::pair<vector<const void *>, vector<Argument>> args = prepare_jit_call_arguments(dst, target);

    struct TrackedBuffer {
        // The query buffer.
        buffer_t query;
        // A backup copy of it to test if it changed.
        buffer_t orig;
    };
    vector<TrackedBuffer> tracked_buffers(args.first.size());

    vector<size_t> query_indices;
    for (size_t i = 0; i < args.first.size(); i++) {
        if (args.first[i] == NULL) {
            query_indices.push_back(i);
            memset(&tracked_buffers[i], 0, sizeof(TrackedBuffer));
            args.first[i] = &tracked_buffers[i].query;
        }
    }

    // No need to query if all the inputs are bound already.
    if (query_indices.empty()) {
        debug(1) << "All inputs are bound. No need for bounds inference\n";
        return;
    }

    JITFuncCallContext jit_context(jit_handlers(), contents.ptr->user_context_arg.param);

    int iter = 0;
    const int max_iters = 16;
    for (iter = 0; iter < max_iters; iter++) {
        // Make a copy of the buffers that might be mutated
        for (TrackedBuffer &tb : tracked_buffers) {
            tb.orig = tb.query;
        }

        Internal::debug(2) << "Calling jitted function\n";
        int exit_status = call_jit_code(target, args);
        jit_context.report_if_error(exit_status);
        Internal::debug(2) << "Back from jitted function\n";
        bool changed = false;

        // Check if there were any changed
        for (TrackedBuffer &tb : tracked_buffers) {
            if (memcmp(&tb.query, &tb.orig, sizeof(buffer_t))) {
                changed = true;
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
        InferredArgument ia = contents.ptr->inferred_args[i];
        internal_assert(!ia.param.get_buffer().defined());
        buffer_t buf = tracked_buffers[i].query;

        Internal::debug(1) << "Inferred bounds for " << ia.param.name() << ": ("
                           << buf.min[0] << ","
                           << buf.min[1] << ","
                           << buf.min[2] << ","
                           << buf.min[3] << ")..("
                           << buf.min[0] + buf.extent[0] << ","
                           << buf.min[1] + buf.extent[1] << ","
                           << buf.min[2] + buf.extent[2] << ","
                           << buf.min[3] + buf.extent[3] << ")\n";

        // Figure out how much memory to allocate for this buffer
        size_t min_idx = 0, max_idx = 0;
        for (int d = 0; d < 4; d++) {
            if (buf.stride[d] > 0) {
                min_idx += buf.min[d] * buf.stride[d];
                max_idx += (buf.min[d] + buf.extent[d] - 1) * buf.stride[d];
            } else {
                max_idx += buf.min[d] * buf.stride[d];
                min_idx += (buf.min[d] + buf.extent[d] - 1) * buf.stride[d];
            }
        }
        size_t total_size = (max_idx - min_idx);
        while (total_size & 0x1f) total_size++;

        // Allocate enough memory with the right dimensionality.
        Buffer buffer(ia.param.type(), total_size,
                      buf.extent[1] > 0 ? 1 : 0,
                      buf.extent[2] > 0 ? 1 : 0,
                      buf.extent[3] > 0 ? 1 : 0);

        // Rewrite the buffer fields to match the ones returned
        for (int d = 0; d < 4; d++) {
            buffer.raw_buffer()->min[d] = buf.min[d];
            buffer.raw_buffer()->stride[d] = buf.stride[d];
            buffer.raw_buffer()->extent[d] = buf.extent[d];
        }
        ia.param.set_buffer(buffer);
    }
}

void Pipeline::infer_input_bounds(int x_size, int y_size, int z_size, int w_size) {
    user_assert(defined()) << "Can't infer input bounds on an undefined Pipeline.\n";

    vector<Buffer> bufs;
    for (Type t : contents.ptr->outputs[0].output_types()) {
        bufs.push_back(Buffer(t, x_size, y_size, z_size, w_size));
    }
    Realization r(bufs);
    infer_input_bounds(r);
}


void Pipeline::infer_input_bounds(Buffer dst) {
    infer_input_bounds(Realization({dst}));
}

void Pipeline::invalidate_cache() {
    if (defined()) {
        contents.ptr->invalidate_cache();
    }
}

JITExtern::JITExtern()
    : pipeline(), c_function(NULL) {
}

JITExtern::JITExtern(Pipeline pipeline)
    : pipeline(pipeline), c_function(NULL) {
}

JITExtern::JITExtern(Func func)
    : pipeline(func), c_function(NULL) {
}

}  // namespace Halide
