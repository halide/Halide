#include "Memoization.h"
#include "Error.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Param.h"
#include "Scope.h"
#include "Util.h"
#include "Var.h"

#include <map>

namespace Halide {
namespace Internal {

namespace {

class FindParameterDependencies : public IRGraphVisitor {
public:
    FindParameterDependencies() = default;
    ~FindParameterDependencies() override = default;

    void visit_function(const Function &function) {
        function.accept(this);

        if (function.has_extern_definition()) {
            const std::vector<ExternFuncArgument> &extern_args =
                function.extern_arguments();
            for (const auto &extern_arg : extern_args) {
                if (extern_arg.is_buffer()) {
                    // Function with an extern definition
                    record(Halide::Parameter(extern_arg.buffer.type(), true,
                                             extern_arg.buffer.dimensions(),
                                             extern_arg.buffer.name()));
                } else if (extern_arg.is_image_param()) {
                    record(extern_arg.image_param);
                }
            }
        }
    }

    using IRGraphVisitor::visit;

    void visit(const Call *call) override {
        if (call->param.defined()) {
            record(call->param);
        }

        if (call->is_intrinsic(Call::memoize_expr)) {
            internal_assert(!call->args.empty());
            if (call->args.size() == 1) {
                record(call->args[0]);
            } else {
                // Do not look at anything inside a memoize_expr bracket.
                for (size_t i = 1; i < call->args.size(); i++) {
                    record(call->args[i]);
                }
            }
        } else if (call->func.defined()) {
            Function fn(call->func);
            visit_function(fn);
            IRGraphVisitor::visit(call);
        } else {
            IRGraphVisitor::visit(call);
        }
    }

    void visit(const Load *load) override {
        if (load->param.defined()) {
            record(load->param);
        }
        IRGraphVisitor::visit(load);
    }

    void visit(const Variable *var) override {
        if (var->param.defined()) {
            if (var->param.is_buffer() &&
                !var->type.is_handle()) {
                record(memoize_tag(var));
            } else {
                record(var->param);
            }
        }

        IRGraphVisitor::visit(var);
    }

    void record(const Parameter &parameter) {
        struct DependencyInfo info;

        info.type = parameter.type();

        if (parameter.is_buffer()) {
            internal_error
                << "Buffer parameter " << parameter.name()
                << " encountered in computed_cached computation.\n"
                << "Computations which depend on buffer parameters "
                << "cannot be scheduled compute_cached.\n"
                << "Use memoize_tag to provide cache key information for buffer.\n";
        } else if (info.type.is_handle()) {
            internal_error
                << "Handle parameter " << parameter.name()
                << " encountered in computed_cached computation.\n"
                << "Computations which depend on handle parameters "
                << "cannot be scheduled compute_cached.\n"
                << "Use memoize_tag to provide cache key information for handle.\n";
        } else {
            info.size_expr = info.type.bytes();
            info.value_expr = Internal::Variable::make(info.type, parameter.name(), parameter);
        }

        dependency_info[DependencyKey(info.type.bytes(), parameter.name())] = info;
    }

    void record(const Expr &expr) {
        struct DependencyInfo info;
        info.type = expr.type();
        info.size_expr = info.type.bytes();
        info.value_expr = expr;
        dependency_info[DependencyKey(info.type.bytes(), unique_name("memoize_tag"))] = info;
    }

    // Used to make sure larger parameters come before smaller ones
    // for alignment reasons.
    struct DependencyKey {
        uint32_t size;
        std::string name;

        bool operator<(const DependencyKey &rhs) const {
            if (size < rhs.size) {
                return true;
            } else if (size == rhs.size) {
                return name < rhs.name;
            }
            return false;
        }

        DependencyKey(uint32_t size_arg, const std::string &name_arg)
            : size(size_arg), name(name_arg) {
        }
    };

    struct DependencyInfo {
        Type type;
        Expr size_expr;
        Expr value_expr;
    };

    std::map<DependencyKey, DependencyInfo> dependency_info;
};

typedef std::pair<FindParameterDependencies::DependencyKey, FindParameterDependencies::DependencyInfo> DependencyKeyInfoPair;
typedef std::pair<const FindParameterDependencies::DependencyKey, FindParameterDependencies::DependencyInfo> ConstDependencyKeyInfoPair;

class KeyInfo {
    FindParameterDependencies dependencies;
    Expr key_size_expr;
    const std::string &top_level_name;
    const std::string &function_name;
    int memoize_instance;

    size_t parameters_alignment() {
        int32_t max_alignment = 0;
        // Find maximum natural alignment needed.
        for (const ConstDependencyKeyInfoPair &i : dependencies.dependency_info) {
            int alignment = i.second.type.bytes();
            if (alignment > max_alignment) {
                max_alignment = alignment;
            }
        }
        // Make sure max_alignment is a power of two and has maximum value of 32
        int i = 0;
        while (i < 4 && max_alignment > (1 << i)) {
            i = i + 1;
        }
        return size_t(1) << i;
    }

    // TODO: Using the full names in the key results in a (hopefully incredibly
    // slight) performance difference based on how one names filters and
    // functions. It is arguably a little easier to debug if something
    // goes wrong as one doesn't need to destructure the cache key by hand
    // in the debugger. Also, if a pointer is used, a counter must also be
    // put in the cache key to avoid aliasing on reuse of the address in
    // JIT situations where code is regenerated into the same region of
    // memory.
    //
    // There is a plan to change the hash function used in the cache and
    // after that happens, we'll measure performance again and maybe decide
    // to choose one path or the other (see Git history for the implementation.
    // It was deleted as part of the address_of intrinsic cleanup).

public:
    KeyInfo(const Function &function, const std::string &name, int memoize_instance)
        : top_level_name(name),
          function_name(function.origin_name()),
          memoize_instance(memoize_instance) {
        dependencies.visit_function(function);
        size_t size_so_far = 0;
        size_so_far += Handle().bytes() + 4;

        size_t needed_alignment = parameters_alignment();
        if (needed_alignment > 1) {
            size_so_far = (size_so_far + needed_alignment - 1) & ~(needed_alignment - 1);
        }
        key_size_expr = (int32_t)size_so_far;

        for (const ConstDependencyKeyInfoPair &i : dependencies.dependency_info) {
            key_size_expr += i.second.size_expr;
        }
    }

    // Return the number of bytes needed to store the cache key
    // for the target function. Make sure it takes 4 bytes in cache key.
    Expr key_size() {
        return cast<int32_t>(key_size_expr);
    }

    // Code to fill in the Allocation named key_name with the byte of
    // the key. The Allocation is guaranteed to be 1d, of type uint8_t
    // and of the size returned from key_size
    Stmt generate_key(const std::string &key_name) {
        std::vector<Stmt> writes;
        Expr index = Expr(0);

        // Store a pointer to a string identifying the filter and
        // function. Assume this will be unique due to CSE. This can
        // break with loading and unloading of code, though the name
        // mechanism can also break in those conditions.
        writes.push_back(Store::make(key_name,
                                     StringImm::make(std::to_string(top_level_name.size()) + ":" + top_level_name +
                                                     std::to_string(function_name.size()) + ":" + function_name),
                                     (index / Handle().bytes()), Parameter(), const_true(), ModulusRemainder()));
        size_t alignment = Handle().bytes();
        index += Handle().bytes();

        // Halide compilation is not threadsafe anyway...
        writes.push_back(Store::make(key_name,
                                     memoize_instance,
                                     (index / Int(32).bytes()),
                                     Parameter(), const_true(), ModulusRemainder()));
        alignment += 4;
        index += 4;

        size_t needed_alignment = parameters_alignment();
        if (needed_alignment > 1) {
            while (alignment % needed_alignment) {
                writes.push_back(Store::make(key_name, Cast::make(UInt(8), 0),
                                             index, Parameter(), const_true(), ModulusRemainder()));
                index = index + 1;
                alignment++;
            }
        }

        for (const ConstDependencyKeyInfoPair &i : dependencies.dependency_info) {
            writes.push_back(Store::make(key_name,
                                         i.second.value_expr,
                                         (index / i.second.size_expr),
                                         Parameter(), const_true(), ModulusRemainder()));
            index += i.second.size_expr;
        }
        Stmt blocks = Block::make(writes);

        return blocks;
    }

    // Returns a bool expression, which either evaluates to true,
    // in which case the Allocation named by storage will be computed,
    // or false, in which case it will be assumed the buffer was populated
    // by the code in this call.
    Expr generate_lookup(const std::string &key_allocation_name, const std::string &computed_bounds_name,
                         int32_t tuple_count, const std::string &storage_base_name) {
        std::vector<Expr> args;
        args.push_back(Variable::make(type_of<uint8_t *>(), key_allocation_name));
        args.push_back(key_size());
        args.push_back(Variable::make(type_of<halide_buffer_t *>(), computed_bounds_name));
        args.emplace_back(tuple_count);
        std::vector<Expr> buffers;
        if (tuple_count == 1) {
            buffers.push_back(Variable::make(type_of<halide_buffer_t *>(), storage_base_name + ".buffer"));
        } else {
            for (int32_t i = 0; i < tuple_count; i++) {
                buffers.push_back(Variable::make(type_of<halide_buffer_t *>(), storage_base_name + "." + std::to_string(i) + ".buffer"));
            }
        }
        args.push_back(Call::make(type_of<halide_buffer_t **>(), Call::make_struct, buffers, Call::Intrinsic));

        return Call::make(Int(32), "halide_memoization_cache_lookup", args, Call::Extern);
    }

    // Returns a statement which will store the result of a computation under this key
    Stmt store_computation(const std::string &key_allocation_name, const std::string &computed_bounds_name,
                           const std::string &eviction_key_name, int32_t tuple_count, const std::string &storage_base_name) {
        std::vector<Expr> args;
        args.push_back(Variable::make(type_of<uint8_t *>(), key_allocation_name));
        args.push_back(key_size());
        args.push_back(Variable::make(type_of<halide_buffer_t *>(), computed_bounds_name));
        args.emplace_back(tuple_count);
        std::vector<Expr> buffers;
        if (tuple_count == 1) {
            buffers.push_back(Variable::make(type_of<halide_buffer_t *>(), storage_base_name + ".buffer"));
        } else {
            for (int32_t i = 0; i < tuple_count; i++) {
                buffers.push_back(Variable::make(type_of<halide_buffer_t *>(), storage_base_name + "." + std::to_string(i) + ".buffer"));
            }
        }
        args.push_back(Call::make(type_of<halide_buffer_t **>(), Call::make_struct, buffers, Call::Intrinsic));
        if (!eviction_key_name.empty()) {
            args.push_back(make_const(Bool(), true));
            args.push_back(Variable::make(UInt(64), eviction_key_name));
        } else {
            args.push_back(make_const(Bool(), false));
            args.push_back(make_const(UInt(64), 0));
        }
        // This is actually a void call. How to indicate that? Look at Extern_ stuff.
        return Evaluate::make(Call::make(Int(32), "halide_memoization_cache_store", args, Call::Extern));
    }
};

// Inject caching structure around memoized realizations.
class InjectMemoization : public IRMutator {
public:
    const std::map<std::string, Function> &env;
    int memoize_instance;
    const std::string &top_level_name;
    const std::vector<Function> &outputs;

    InjectMemoization(const std::map<std::string, Function> &e,
                      int memoize_instance,
                      const std::string &name,
                      const std::vector<Function> &outputs)
        : env(e), memoize_instance(memoize_instance), top_level_name(name), outputs(outputs) {
    }

private:
    using IRMutator::visit;

    Stmt visit(const Realize *op) override {
        std::map<std::string, Function>::const_iterator iter = env.find(op->name);
        if (iter != env.end() &&
            iter->second.schedule().memoized()) {

            const Function f(iter->second);

            for (const Function &o : outputs) {
                if (f.same_as(o)) {
                    user_error << "Function " << f.name() << " cannot be memoized because "
                               << "it an output of pipeline " << top_level_name << ".\n";
                }
            }

            // There are currently problems with the cache key
            // construction getting moved above the scope of use if
            // the the compute and store levels are different. It also
            // has implications for the cache compute/allocated bounds
            // logic. And it isn't clear it is useful for
            // anything. Hence this is currently an error.
            if (!f.schedule().compute_level().match(f.schedule().store_level())) {
                user_error << "Function " << f.name() << " cannot be memoized because "
                           << "it has compute and storage scheduled at different loop levels.\n";
            }

            Stmt mutated_body = mutate(op->body);

            KeyInfo key_info(f, top_level_name, memoize_instance);

            std::string cache_key_name = op->name + ".cache_key";
            std::string cache_result_name = op->name + ".cache_result";
            std::string cache_miss_name = op->name + ".cache_miss";
            std::string computed_bounds_name = op->name + ".computed_bounds.buffer";
            std::string eviction_key_name = op->name + ".cache_eviction_key";

            Stmt eviction_key_marker;
            const Expr &eviction_key = iter->second.schedule().memoize_eviction_key();
            bool has_eviction_key = eviction_key.defined();
            if (has_eviction_key) {
                internal_assert(eviction_key.type() == UInt(64)) << "Logic error: bad type for memoization eviction key in expr: " << eviction_key << " .\n";
                eviction_key_marker = LetStmt::make(eviction_key_name, eviction_key, mutated_body);
            }

            Stmt cache_miss_marker = LetStmt::make(cache_miss_name,
                                                   Cast::make(Bool(), Variable::make(Int(32), cache_result_name)),
                                                   has_eviction_key ? eviction_key_marker : mutated_body);
            Stmt cache_lookup_check = Block::make(AssertStmt::make(NE::make(Variable::make(Int(32), cache_result_name), -1),
                                                                   Call::make(Int(32), "halide_error_out_of_memory", {}, Call::Extern)),
                                                  cache_miss_marker);

            Stmt cache_lookup = LetStmt::make(cache_result_name,
                                              key_info.generate_lookup(cache_key_name, computed_bounds_name, f.outputs(), op->name),
                                              cache_lookup_check);

            BufferBuilder builder;
            builder.dimensions = f.dimensions();
            std::string max_stage_num = std::to_string(f.updates().size());
            for (const std::string &arg : f.args()) {
                std::string prefix = op->name + ".s" + max_stage_num + "." + arg;
                Expr min = Variable::make(Int(32), prefix + ".min");
                Expr max = Variable::make(Int(32), prefix + ".max");
                builder.mins.push_back(min);
                builder.extents.push_back(max + 1 - min);
            }
            Expr computed_bounds = builder.build();

            Stmt computed_bounds_let = LetStmt::make(computed_bounds_name, computed_bounds, cache_lookup);

            Stmt generate_key = Block::make(key_info.generate_key(cache_key_name), computed_bounds_let);
            Stmt cache_key_alloc =
                Allocate::make(cache_key_name, UInt(8), MemoryType::Stack, {key_info.key_size()},
                               const_true(), generate_key);

            return Realize::make(op->name, op->types, op->memory_type, op->bounds, op->condition, cache_key_alloc);
        } else {
            return IRMutator::visit(op);
        }
    }

    Stmt visit(const ProducerConsumer *op) override {
        std::map<std::string, Function>::const_iterator iter = env.find(op->name);
        if (iter != env.end() &&
            iter->second.schedule().memoized()) {

            // The error checking should have been done inside Realization node
            // of this producer, so no need to do it here.

            Stmt body = mutate(op->body);

            std::string cache_miss_name = op->name + ".cache_miss";
            Expr cache_miss = Variable::make(Bool(), cache_miss_name);

            if (op->is_producer) {
                Stmt mutated_body = IfThenElse::make(cache_miss, body);
                return ProducerConsumer::make(op->name, op->is_producer, mutated_body);
            } else {
                const Function f(iter->second);
                KeyInfo key_info(f, top_level_name, memoize_instance);

                std::string cache_key_name = op->name + ".cache_key";
                std::string computed_bounds_name = op->name + ".computed_bounds.buffer";
                std::string eviction_key_name;

                if (f.schedule().memoize_eviction_key().defined()) {
                    eviction_key_name = op->name + ".cache_eviction_key";
                }
                Stmt cache_store_back =
                    IfThenElse::make(cache_miss,
                                     key_info.store_computation(cache_key_name, computed_bounds_name,
                                                                eviction_key_name, f.outputs(), op->name));

                Stmt mutated_body = Block::make(cache_store_back, body);
                return ProducerConsumer::make(op->name, op->is_producer, mutated_body);
            }
        } else {
            return IRMutator::visit(op);
        }
    }
};

}  // namespace

Stmt inject_memoization(const Stmt &s, const std::map<std::string, Function> &env,
                        const std::string &name,
                        const std::vector<Function> &outputs) {
    // Cache keys use the addresses of names of Funcs. For JIT, a
    // counter for the pipeline is needed as the address may be reused
    // across pipelines. This isn't a problem when using full names as
    // the function names already are uniquefied by a counter.
    static std::atomic<int> memoize_instance{0};

    InjectMemoization injector(env, memoize_instance++, name, outputs);

    return injector.mutate(s);
}

namespace {

class RewriteMemoizedAllocations : public IRMutator {
public:
    RewriteMemoizedAllocations(const std::map<std::string, Function> &e)
        : env(e) {
    }

private:
    const std::map<std::string, Function> &env;
    std::map<std::string, std::vector<const Allocate *>> pending_memoized_allocations;
    std::string innermost_realization_name;

    std::string get_realization_name(const std::string &allocation_name) {
        std::string realization_name = allocation_name;
        size_t off = realization_name.rfind('.');
        if (off != std::string::npos) {
            size_t i = off + 1;
            while (i < realization_name.size() && isdigit(realization_name[i])) {
                i++;
            }
            if (i == realization_name.size()) {
                realization_name = realization_name.substr(0, off);
            }
        }
        return realization_name;
    }

    using IRMutator::visit;

    Stmt visit(const Allocate *allocation) override {
        std::string realization_name = get_realization_name(allocation->name);
        std::map<std::string, Function>::const_iterator iter = env.find(realization_name);

        if (iter != env.end() && iter->second.schedule().memoized()) {
            ScopedValue<std::string> old_innermost_realization_name(innermost_realization_name, realization_name);
            pending_memoized_allocations[innermost_realization_name].push_back(allocation);
            return mutate(allocation->body);
        } else {
            return IRMutator::visit(allocation);
        }
    }

    Expr visit(const Call *call) override {
        if (!innermost_realization_name.empty() &&
            call->name == Call::buffer_init) {
            internal_assert(call->args.size() >= 3)
                << "RewriteMemoizedAllocations: _halide_buffer_init call with fewer than two args.\n";

            // Grab the host pointer argument
            const Variable *var = call->args[2].as<Variable>();
            if (var && get_realization_name(var->name) == innermost_realization_name) {
                // Rewrite _halide_buffer_init to use a nullptr handle for address.
                std::vector<Expr> args = call->args;
                args[2] = make_zero(Handle());
                return Call::make(type_of<struct halide_buffer_t *>(), Call::buffer_init,
                                  args, Call::Extern);
            }
        }

        // If any part of the match failed, do default mutator action.
        return IRMutator::visit(call);
    }

    Stmt visit(const LetStmt *let) override {
        if (let->name == innermost_realization_name + ".cache_miss") {
            Expr value = mutate(let->value);
            Stmt body = mutate(let->body);

            std::vector<const Allocate *> &allocations = pending_memoized_allocations[innermost_realization_name];

            for (size_t i = allocations.size(); i > 0; i--) {
                const Allocate *allocation = allocations[i - 1];

                // Make the allocation node
                body = Allocate::make(allocation->name, allocation->type, allocation->memory_type, allocation->extents, allocation->condition, body,
                                      Call::make(Handle(), Call::buffer_get_host,
                                                 {Variable::make(type_of<struct halide_buffer_t *>(), allocation->name + ".buffer")}, Call::Extern),
                                      "halide_memoization_cache_release");
            }

            pending_memoized_allocations.erase(innermost_realization_name);

            return LetStmt::make(let->name, value, body);
        } else {
            return IRMutator::visit(let);
        }
    }
};

}  // namespace

Stmt rewrite_memoized_allocations(const Stmt &s, const std::map<std::string, Function> &env) {

    RewriteMemoizedAllocations rewriter(env);

    return rewriter.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
