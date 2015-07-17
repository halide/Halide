#include "Memoization.h"
#include "Error.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "Param.h"
#include "Util.h"
#include "Var.h"

#include <map>

namespace Halide {
namespace Internal {

namespace {

class FindParameterDependencies : public IRGraphVisitor {
public:
    FindParameterDependencies() { }
    ~FindParameterDependencies() { }

    void visit_function(const Function &function) {
        function.accept(this);

        if (function.has_extern_definition()) {
            const std::vector<ExternFuncArgument> &extern_args =
                function.extern_arguments();
            for (size_t i = 0; i < extern_args.size(); i++) {
                if (extern_args[i].is_buffer()) {
                    // Function with an extern definition
                    record(Halide::Internal::Parameter(extern_args[i].buffer.type(), true,
                                                       extern_args[i].buffer.dimensions(),
                                                       extern_args[i].buffer.name()));
                } else if (extern_args[i].is_image_param()) {
                    record(extern_args[i].image_param);
                }
            }
        }
    }

    using IRGraphVisitor::visit;

    void visit(const Call *call) {
        if (call->param.defined()) {
            record(call->param);
        }

        if (call->call_type == Call::Intrinsic && call->name == Call::memoize_expr) {
            internal_assert(call->args.size() > 0);
            if (call->args.size() == 1) {
                record(call->args[0]);
            } else {
                for (size_t i = 1; i < call->args.size(); i++) {
                    record(call->args[i]);
                }
            }
        } else {
            // Do not look at anything inside a memoize_expr bracket.
            visit_function(call->func);
            IRGraphVisitor::visit(call);
        }
    }


    void visit(const Load *load) {
        if (load->param.defined()) {
            record(load->param);
        }
        IRGraphVisitor::visit(load);
    }

    void visit(const Variable *var) {
        if (var->param.defined()) {
            record(var->param);
        }
        IRGraphVisitor::visit(var);
    }

    void record(const Parameter &parameter) {
        struct DependencyInfo info;

        info.type = parameter.type();

        if (parameter.is_buffer()) {
            internal_error << "Buffer parameter " << parameter.name() <<
                " encountered in computed_cached computation.\n" <<
                "Computations which depend on buffer parameters " <<
                "cannot be scheduled compute_cached.\n" <<
                "Use memoize_tag to provide cache key information for buffer.\n";
        } else if (info.type.is_handle()) {
            internal_error << "Handle parameter " << parameter.name() <<
                " encountered in computed_cached computation.\n" <<
                "Computations which depend on buffer parameters " <<
                "cannot be scheduled compute_cached.\n" <<
                "Use memoize_tag to provide cache key information for buffer.\n";
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
        dependency_info[DependencyKey(info.type.bytes(), unique_name("memoize_tag", false))] = info;
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

class KeyInfo {
    FindParameterDependencies dependencies;
    Expr key_size_expr;
    const std::string &top_level_name;
    const std::string &function_name;

    size_t parameters_alignment() {
        int32_t max_alignment = 0;
        // Find maximum natural alignment needed.
        for (const DependencyKeyInfoPair &i : dependencies.dependency_info) {
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
        return (size_t)(1 << i);
    }

    Stmt call_copy_memory(const std::string &key_name, const std::string &value, Expr index) {
        Expr dest = Call::make(Handle(), Call::address_of,
                               {Load::make(UInt(8), key_name, index, Buffer(), Parameter())},
                               Call::Intrinsic);
        Expr src = StringImm::make(value);
        Expr copy_size = (int32_t)value.size();

        return Evaluate::make(Call::make(UInt(8), Call::copy_memory,
                                         {dest, src, copy_size}, Call::Intrinsic));
    }

public:
  KeyInfo(const Function &function, const std::string &name)
        : top_level_name(name), function_name(function.name())
    {
        dependencies.visit_function(function);
        size_t size_so_far = 0;

        size_so_far = 4 + (int32_t)((top_level_name.size() + 3) & ~3);
        size_so_far += 4 + function_name.size();

        size_t needed_alignment = parameters_alignment();
        if (needed_alignment > 1) {
            size_so_far = (size_so_far + needed_alignment) & ~(needed_alignment - 1);
        }
        key_size_expr = (int32_t)size_so_far;

        for (const DependencyKeyInfoPair &i : dependencies.dependency_info) {
            key_size_expr += i.second.size_expr;
        }
    }

    // Return the number of bytes needed to store the cache key
    // for the target function. Make sure it takes 4 bytes in cache key.
    Expr key_size() { return cast<int32_t>(key_size_expr); };

    // Code to fill in the Allocation named key_name with the byte of
    // the key. The Allocation is guaranteed to be 1d, of type uint8_t
    // and of the size returned from key_size
    Stmt generate_key(std::string key_name) {
        std::vector<Stmt> writes;
        Expr index = Expr(0);

        // In code below, casts to vec type is done because stores to
        // the buffer can be unaligned.

        Expr top_level_name_size = (int32_t)top_level_name.size();
        writes.push_back(Store::make(key_name,
                                     Cast::make(Int(32), top_level_name_size),
                                     (index / Int(32).bytes())));
        index += 4;
        writes.push_back(call_copy_memory(key_name, top_level_name, index));
        // Align to four byte boundary again.
        index += top_level_name_size;
        size_t alignment = 4 + top_level_name.size();
        while (alignment % 4) {
            writes.push_back(Store::make(key_name, Cast::make(UInt(8), 0), index));
            index = index + 1;
            alignment++;
        }

        Expr name_size = (int32_t)function_name.size();
        writes.push_back(Store::make(key_name,
                                     Cast::make(Int(32), name_size),
                                     (index / Int(32).bytes())));
        index += 4;
        writes.push_back(call_copy_memory(key_name, function_name, index));
        index += name_size;

        alignment += 4 + function_name.size();
        size_t needed_alignment = parameters_alignment();
        if (needed_alignment > 1) {
            while (alignment % needed_alignment) {
                writes.push_back(Store::make(key_name, Cast::make(UInt(8), 0), index));
                index = index + 1;
                alignment++;
            }
        }

        for (const DependencyKeyInfoPair &i : dependencies.dependency_info) {
            writes.push_back(Store::make(key_name,
                                         i.second.value_expr,
                                         (index / i.second.size_expr)));
            index += i.second.size_expr;
        }
        Stmt blocks;
        for (size_t i = writes.size(); i > 0; i--) {
            blocks = Block::make(writes[i - 1], blocks);
        }

        return blocks;
    }

    // Returns a bool expression, which either evaluates to true,
    // in which case the Allocation named by storage will be computed,
    // or false, in which case it will be assumed the buffer was populated
    // by the code in this call.
    Expr generate_lookup(std::string key_allocation_name, std::string computed_bounds_name,
                         int32_t tuple_count, std::string storage_base_name) {
        std::vector<Expr> args;
        args.push_back(Call::make(type_of<uint8_t *>(), Call::address_of,
                                  {Load::make(type_of<uint8_t>(), key_allocation_name, Expr(0), Buffer(), Parameter())},
                                  Call::Intrinsic));
        args.push_back(key_size());
        args.push_back(Variable::make(type_of<buffer_t *>(), computed_bounds_name));
        args.push_back(tuple_count);
        std::vector<Expr> buffers;
        if (tuple_count == 1) {
            buffers.push_back(Variable::make(type_of<buffer_t *>(), storage_base_name + ".buffer"));
        } else {
            for (int32_t i = 0; i < tuple_count; i++) {
                buffers.push_back(Variable::make(type_of<buffer_t *>(), storage_base_name + "." + std::to_string(i) + ".buffer"));
            }
        }
        args.push_back(Call::make(type_of<buffer_t **>(), Call::make_struct, buffers, Call::Intrinsic));

        return Call::make(Bool(1), "halide_memoization_cache_lookup", args, Call::Extern);
    }

    // Returns a statement which will store the result of a computation under this key
    Stmt store_computation(std::string key_allocation_name, std::string computed_bounds_name,
                           int32_t tuple_count, std::string storage_base_name) {
        std::vector<Expr> args;
        args.push_back(Call::make(type_of<uint8_t *>(), Call::address_of,
                                  {Load::make(type_of<uint8_t>(), key_allocation_name, Expr(0), Buffer(), Parameter())},
                                  Call::Intrinsic));
        args.push_back(key_size());
        args.push_back(Variable::make(type_of<buffer_t *>(), computed_bounds_name));
        args.push_back(tuple_count);
        std::vector<Expr> buffers;
        if (tuple_count == 1) {
            buffers.push_back(Variable::make(type_of<buffer_t *>(), storage_base_name + ".buffer"));
        } else {
            for (int32_t i = 0; i < tuple_count; i++) {
                buffers.push_back(Variable::make(type_of<buffer_t *>(), storage_base_name + "." + std::to_string(i) + ".buffer"));
            }
        }
        args.push_back(Call::make(type_of<buffer_t **>(), Call::make_struct, buffers, Call::Intrinsic));

        // This is actually a void call. How to indicate that? Look at Extern_ stuff.
        return Evaluate::make(Call::make(Bool(), "halide_memoization_cache_store", args, Call::Extern));
    }

    // Returns a statement which will store the result of a computation under this key
    Stmt release_cache_entry(std::string key_allocation_name, std::string computed_bounds_name,
                             int32_t tuple_count, std::string storage_base_name) {
        std::vector<Expr> args;
        args.push_back(Call::make(type_of<uint8_t *>(), Call::address_of, 
                                  {Load::make(type_of<uint8_t>(), key_allocation_name, Expr(0), Buffer(), Parameter())},
                                  Call::Intrinsic));
        args.push_back(key_size());
        args.push_back(Variable::make(type_of<buffer_t *>(), computed_bounds_name));
        args.push_back(tuple_count);
        std::vector<Expr> buffers;
        if (tuple_count == 1) {
            buffers.push_back(Variable::make(type_of<buffer_t *>(), storage_base_name + ".buffer"));
        } else {
            for (int32_t i = 0; i < tuple_count; i++) {
                buffers.push_back(Variable::make(type_of<buffer_t *>(), storage_base_name + "." + std::to_string(i) + ".buffer"));
            }
        }
        args.push_back(Call::make(type_of<buffer_t **>(), Call::make_struct, buffers, Call::Intrinsic));

        // This is actually a void call. How to indicate that? Look at Extern_ stuff.
        return Evaluate::make(Call::make(Bool(), "halide_memoization_cache_release", args, Call::Extern));
    }
};

}

// Inject caching structure around memoized realizations.
class InjectMemoization : public IRMutator {
public:
  const std::map<std::string, Function> &env;
  const std::string &top_level_name;

  InjectMemoization(const std::map<std::string, Function> &e, const std::string &name) :
      env(e), top_level_name(name) {}
private:

    using IRMutator::visit;

    void visit(const ProducerConsumer *op) {
        std::map<std::string, Function>::const_iterator iter = env.find(op->name);
        if (iter != env.end() &&
            iter->second.schedule().memoized()) {

            const Function f(iter->second);

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

            Stmt produce = mutate(op->produce);
            Stmt update = mutate(op->update);
            Stmt consume = mutate(op->consume);

            KeyInfo key_info(f, top_level_name);

            std::string cache_key_name = op->name + ".cache_key";
            std::string cache_miss_name = op->name + ".cache_miss";
            std::string computed_bounds_name = op->name + ".computed_bounds.buffer";

            Expr cache_miss = Variable::make(Bool(), cache_miss_name);

            Stmt cache_store_back =
                IfThenElse::make(cache_miss, key_info.store_computation(cache_key_name, computed_bounds_name, f.outputs(), op->name));
            Stmt cache_release = key_info.release_cache_entry(cache_key_name, computed_bounds_name, f.outputs(), op->name);

            Stmt mutated_produce = IfThenElse::make(cache_miss, produce);
            Stmt mutated_update =
                update.defined() ? IfThenElse::make(cache_miss, update) :
                                       update;
            Stmt mutated_consume = Block::make(consume, cache_release);
            mutated_consume = Block::make(cache_store_back, mutated_consume);

            Stmt mutated_pipeline = ProducerConsumer::make(op->name, mutated_produce, mutated_update, mutated_consume);
            Stmt cache_lookup = LetStmt::make(cache_miss_name, key_info.generate_lookup(cache_key_name, computed_bounds_name, f.outputs(), op->name), mutated_pipeline);

            std::vector<Expr> computed_bounds_args;
            Expr null_handle = Call::make(Handle(), Call::null_handle, std::vector<Expr>(), Call::Intrinsic);
            computed_bounds_args.push_back(null_handle);
            computed_bounds_args.push_back(f.output_types()[0].bytes());
            std::string max_stage_num = std::to_string(f.updates().size());
            for (int32_t i = 0; i < f.dimensions(); i++) {
                Expr min = Variable::make(Int(32), op->name + ".s" + max_stage_num + "." + f.args()[i] + ".min");
                Expr max = Variable::make(Int(32), op->name + ".s" + max_stage_num + "." + f.args()[i] + ".max");
                computed_bounds_args.push_back(min);
                computed_bounds_args.push_back(max - min);
                computed_bounds_args.push_back(0); // TODO: Verify there is no use for the stride.
            }

            Expr computed_bounds = Call::make(Handle(), Call::create_buffer_t,
                                              computed_bounds_args,
                                              Call::Intrinsic);
            Stmt computed_bounds_let = LetStmt::make(computed_bounds_name, computed_bounds, cache_lookup);

            Stmt generate_key = Block::make(key_info.generate_key(cache_key_name), computed_bounds_let);
            Stmt cache_key_alloc =
                Allocate::make(cache_key_name, UInt(8), {key_info.key_size()},
                               const_true(), generate_key);

            stmt = cache_key_alloc;
        } else {
            IRMutator::visit(op);
        }
    }
};

Stmt inject_memoization(Stmt s, const std::map<std::string, Function> &env,
                        const std::string &name) {
    InjectMemoization injector(env, name);

    return injector.mutate(s);
}

}
}
