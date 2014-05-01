#include "Caching.h"
#include "IRMutator.h"
#include "Var.h"

namespace Halide {
namespace Internal {

namespace {

class FindParameterDependencies : public IRGraphVisitor {
public:
    FindParameterDependencies() { }
    ~FindParameterDependencies() { }

    void visit_function(const Function &function) {
        if (function.has_pure_definition()) {
            const std::vector<Expr> &values = function.values();
            for (size_t i = 0; i < values.size(); i++) {
                values[i].accept(this);
            }
        }

        const std::vector<ReductionDefinition> &reductions =
            function.reductions();
        for (size_t i = 0; i < reductions.size(); i++) {
            const std::vector<Expr> &values = reductions[i].values;
            for (size_t j = 0; j < values.size(); j++) {
                values[j].accept(this);
            }

            const std::vector<Expr> &args = reductions[i].args;
            for (size_t j = 0; j < args.size(); j++) {
                args[j].accept(this);
            }
        
            if (reductions[i].domain.defined()) {
                const std::vector<ReductionVariable> &rvars =
                    reductions[i].domain.domain();
                for (size_t j = 0; j < rvars.size(); j++) {
                    rvars[j].min.accept(this);
                    rvars[j].extent.accept(this);
                }
            }
        }

        if (function.has_extern_definition()) {
            const std::vector<ExternFuncArgument> &extern_args =
                function.extern_arguments();
            for (size_t i = 0; i < extern_args.size(); i++) {
                if (extern_args[i].is_func()) {
                    visit_function(extern_args[i].func);
                } else if (extern_args[i].is_expr()) {
                    extern_args[i].expr.accept(this);
                } else if (extern_args[i].is_buffer()) {
                    // Function with an extern definition
  		    record(Halide::Internal::Parameter(extern_args[i].buffer.type(), true,
						       extern_args[i].buffer.name()));
                } else if (extern_args[i].is_image_param()) {
		    record(extern_args[i].image_param);
                } else {
                    assert(!extern_args[i].defined() && "Unexpected ExternFunctionArgument type.");
                }
            }
        }
        const std::vector<Parameter> &output_buffers =
            function.output_buffers();
        for (size_t i = 0; i < output_buffers.size(); i++) {
            for (int j = 0; j < std::min(function.dimensions(), 4); j++) {
                if (output_buffers[i].min_constraint(i).defined()) {
                    output_buffers[i].min_constraint(i).accept(this);
                }
                if (output_buffers[i].stride_constraint(i).defined()) {
                    output_buffers[i].stride_constraint(i).accept(this);
                }
                if (output_buffers[i].extent_constraint(i).defined()) {
                    output_buffers[i].extent_constraint(i).accept(this);
                }
            }
        }
    }

    using IRGraphVisitor::visit;

    void visit(Call *call) {
        if (call->param.defined()) {
            record(call->param);
        }

        visit_function(call->func);

        IRGraphVisitor::visit(call);
    }


    void visit(Load *load) {
        if (load->param.defined()) {
            record(load->param);
        }
        IRGraphVisitor::visit(load);
    }

    void visit(Variable *var) {
        if (var->param.defined()) {
            record(var->param);
        }
        IRGraphVisitor::visit(var);
    }

    void record(const Parameter &) {
    }

    void record(const Argument &) {
    }
};

class KeyInfo {
    FindParameterDependencies dependencies;
public:
    KeyInfo(const Function &function) {
        dependencies.visit_function(function);
    }

    // Return the number of bytes needed to store the cache key
    // for the target function of this clas.
    Expr key_size() { return 1; };

    // Code to fill in the Allocation named key_name with the byte of
    // the key. The Allocation is guaranteed to be 1d, of type uint8_t
    // and of the size returned from key_size
    Stmt generate_key(std::string key_name) { return Stmt(); };

    // Returns a bool expression, which either evaluates to true,
    // in which case the Allocation named by storage will be computed,
    // or false, in which case it will be assumed the buffer was populated
    // by the code in this call.
    Expr generate_lookup(std::string key_allocation_name, std::string storage_allocation_name) { return Cast::make(UInt(1), 1); }

    // Returns a statement which will store the result of a computation under this key
    Stmt store_computation(std::string key_allocation_name, std::string storage_allocation_name) {
        return AssertStmt::make(Cast::make(UInt(1), true), "cache store back", std::vector<Expr>());
    };
};

}

// Inject caching structure around compute_cached realizations.
class InjectCaching : public IRMutator {
public:
    const std::map<std::string, Function> &env;

    InjectCaching(const std::map<std::string, Function> &e) :
        env(e) {}
private:

    using IRMutator::visit;

    void visit(const Pipeline *op) {
        std::map<std::string, Function>::const_iterator f = env.find(op->name);
        if (f != env.end() &&
            f->second.schedule().cached) {
            KeyInfo key_info(f->second);

            std::string cache_key_name = op->name + ".cache_key";
            std::string cache_miss_name = op->name + ".cache_miss";
            std::string buffer_name = op->name + ".buffer";

            Expr cache_miss = Variable::make(UInt(1), cache_miss_name);
            Stmt mutated_produce =
                op->produce.defined() ? IfThenElse::make(cache_miss, op->produce) :
                                        op->produce;
            Stmt mutated_update =
                op->update.defined() ? IfThenElse::make(cache_miss, op->update) :
                                       op->update;
            Stmt cache_store_back =
                IfThenElse::make(cache_miss, key_info.store_computation(cache_key_name, buffer_name)); 
            Stmt mutated_consume = 
                op->consume.defined() ? Block::make(cache_store_back, op->consume) :
                                        cache_store_back;

            Stmt mutated_pipeline = Pipeline::make(op->name, mutated_produce, mutated_update, mutated_consume);
            Stmt cache_lookup = LetStmt::make(cache_miss_name, key_info.generate_lookup(cache_key_name, buffer_name), mutated_pipeline);
            Stmt cache_key_alloc = Allocate::make(cache_key_name, UInt(8), vec(key_info.key_size()), cache_lookup);

            stmt = cache_key_alloc;
        } else {
            stmt = op;
        }
    }
};

Stmt inject_caching(Stmt s, const std::map<std::string, Function> &env) {
  InjectCaching injector(env);

  return injector.mutate(s);
}

}
}
