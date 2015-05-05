#include <algorithm>

#include "Pipeline.h"
#include "Argument.h"
#include "Func.h"
#include "IRVisitor.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "Lower.h"
#include "Output.h"

using namespace Halide::Internal;

namespace Halide {

using std::vector;
using std::string;

/** An inferred arguments. Inferred args are either Params,
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

    /** Clear all cached state */
    void invalidate_cache() {
        module = Module("", Target());
        jit_module = JITModule();
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

    PipelineContents() :
        module("", Target()) {
        user_context_arg.arg = Argument("__user_context", Argument::InputScalar, Handle(), 0);
        user_context_arg.param = Parameter(type_of<void*>(), false, 0, "__user_context",
                                                     /*is_explicit_name*/ true, /*register_instance*/ false);
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
            f.accept(this);
        }
    }

private:
    vector<Function> outputs;

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
        op->func.accept(this);
        include_buffer(op->image);
        include_parameter(op->param);
    }
};

} // namespace Internal

vector<Argument> Pipeline::infer_arguments() {
    user_assert(defined()) << "Can't infer arguments on an undefined Pipeline\n";

    // TODO: cache this too

    // Infer an arguments vector by walking the IR
    InferArguments infer_args(contents.ptr->inferred_args,
                                        contents.ptr->outputs);

    // Sort the Arguments with all buffers first (alphabetical by name),
    // followed by all non-buffers (alphabetical by name).
    std::sort(contents.ptr->inferred_args.begin(), contents.ptr->inferred_args.end());

    // Return the inferred argument types, minus any constant images
    // (we'll embed those in the binary by default).
    vector<Argument> result;
    for (const InferredArgument &arg : contents.ptr->inferred_args) {
        if (!arg.buffer.defined()) {
            result.push_back(arg.arg);
        }
    }

    // Add the user context argument.
    contents.ptr->inferred_args.push_back(contents.ptr->user_context_arg);

    return result;
}

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

    // TODO: Cache the lowered module

    // TODO: This is a bit of a wart. Right now, IR cannot directly
    // reference Buffers because neither CodeGen_LLVM nor
    // CodeGen_C can generate the correct buffer unpacking code.

    // To work around this, we generate two functions. The private
    // function is one where every buffer referenced is an argument,
    // and the public function is a wrapper that calls the private
    // function, passing the global buffers to the private
    // function. This works because the public function does not
    // attempt to directly access any of the fields of the buffer.

    vector<IRMutator *> custom_passes;
    for (CustomLoweringPass p : contents.ptr->custom_lowering_passes) {
        custom_passes.push_back(p.pass);
    }

    Stmt private_body = lower(contents.ptr->outputs, target, custom_passes);

    string private_name = "__" + fn_name;

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
    Module module(fn_name, target);

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

    module.append(LoweredFunc(fn_name, public_args, public_body, LoweredFunc::External));

    return module;
}

void *Pipeline::compile_jit(const Target &target_arg) {
    user_assert(defined()) << "Pipeline is undefined\n";

    // TODO: reuse cached JITModule
    Target target(target_arg);
    target.set_feature(Target::JIT);
    target.set_feature(Target::UserContext);

    // Infer an arguments vector
    infer_arguments();

    // Come up with a name for the generated function
    string name = contents.ptr->outputs[0].name();
    for (size_t i = 0; i < name.size(); i++) {
        if (!isalnum(name[i])) {
            name[i] = '_';
        }
    }

    vector<Argument> args;
    for (const InferredArgument &arg : contents.ptr->inferred_args) {
        args.push_back(arg.arg);
    }

    // Compile to a module
    Module module = compile_to_module(args, name, target);

    // Make sure we're not embedding any images
    internal_assert(module.buffers.empty());

    // Compile to jit module
    JITModule jit_module(module, module.functions.back());

    if (debug::debug_level >= 3) {
        compile_module_to_native(module, name + ".bc", name + ".s");
        compile_module_to_text(module, name + ".stmt");
    }

    contents.ptr->jit_module = jit_module;

    return jit_module.main_function();
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

void Pipeline::add_custom_lowering_pass(IRMutator *pass, void (*deleter)(IRMutator *)) {
    user_assert(defined()) << "Pipeline is undefined\n";
    contents.ptr->invalidate_cache();
    CustomLoweringPass p = {pass, deleter};
    contents.ptr->custom_lowering_passes.push_back(p);
}

Pipeline::~Pipeline() {
    clear_custom_lowering_passes();
}

void Pipeline::clear_custom_lowering_passes() {
    if (!defined()) return;
    contents.ptr->invalidate_cache();
    for (size_t i = 0; i < custom_lowering_passes().size(); i++) {
        if (custom_lowering_passes()[i].deleter) {
            custom_lowering_passes()[i].deleter(custom_lowering_passes()[i].pass);
        }
    }
    contents.ptr->custom_lowering_passes.clear();
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

Realization realize(int x_size,
                    const Target &target) {
    return realize({x_size}, target);
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

void Pipeline::realize(Realization dst, const Target &target) {
    user_assert(defined()) << "Can't realize an undefined Pipeline\n";
    JITModule &compiled_module = contents.ptr->jit_module;

    if (!compiled_module.argv_function()) {
        compile_jit(target);
    }

    internal_assert(compiled_module.argv_function());

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

    JITFuncCallContext jit_context(jit_handlers(), contents.ptr->user_context_arg.param);

    // Come up with the void * arguments to pass to the argv function
    const vector<InferredArgument> &input_args = contents.ptr->inferred_args;
    vector<const void *> arg_values;

    // First the inputs
    for (InferredArgument arg : input_args) {
        if (arg.param.defined() && arg.param.is_buffer()) {
            // ImageParam arg
            Buffer buf = arg.param.get_buffer();
            user_assert(buf.defined())
                << "Can't realize a pipeline because ImageParam "
                << arg.param.name() << " is not bound to a Buffer\n";
            arg_values.push_back(buf.raw_buffer());
        } else if (arg.param.defined()) {
            arg_values.push_back(arg.param.get_scalar_address());
        } else {
            internal_assert(arg.buffer.defined());
            arg_values.push_back(arg.buffer.raw_buffer());
        }

    }

    // Then the outputs
    for (Buffer buf : dst.as_vector()) {
        internal_assert(buf.defined()) << "Realization contains undefined Buffer\n";
        arg_values.push_back(buf.raw_buffer());
    }

    for (size_t i = 0; i < arg_values.size(); i++) {
        debug(2) << "Arg " << i << " = " << arg_values[i] << " (" << *(void * const *)arg_values[i] << ")\n";
        internal_assert(arg_values[i])
            << "An argument to a jitted function is null\n";
    }

    // Always add a custom error handler to capture any error messages.
    // (If there is a user-set error handler, it will be called as well.)

    debug(2) << "Calling jitted function\n";
    int exit_status = compiled_module.argv_function()(&(arg_values[0]));
    debug(2) << "Back from jitted function. Exit status was " << exit_status << "\n";

    jit_context.finalize(exit_status);
}

void Pipeline::infer_input_bounds(int x_size, int y_size, int z_size, int w_size) {
    // TODO
}

void Pipeline::infer_input_bounds(Realization dst) {
    // TODO
}

void Pipeline::infer_input_bounds(Buffer dst) {
    // TODO
}


}  // namespace Halide
