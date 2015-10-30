#include "StoreForwarding.h"
#include "IRMutator.h"
#include "Substitute.h"
#include "IROperator.h"
#include "Simplify.h"
#include "IREquality.h"

namespace Halide {
namespace Internal {

using std::vector;
using std::string;

class FindLoadsAndStores : public IRVisitor {
    void visit(const Load *op) {
        debug(0) << "Got a load: " << op->name << "\n";
        loads.push_back(op);
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        debug(0) << "Got a load: " << op->name << "\n";
        stores.push_back(op);
        IRVisitor::visit(op);
    }

    void visit(const IfThenElse *op) {
        // Conservatively don't enter a conditional
        op->condition.accept(this);
    }

    void visit(const For *op) {
        // We don't want to lift loads and stores that depend on inner
        // loop variables. For now we just don't descend into inner
        // loops (TODO: fix this).
    }

    // Consider lets

public:
    vector<const Load *> loads;
    vector<const Store *> stores;


};

class UseScratch : public IRMutator {
    const Load *load;
    const Store *store;
    const string &name;

    void visit(const Load *op) {
        if (equal(Expr(op), Expr(load))) {
            expr = Load::make(op->type, name, make_zero(Int(32, op->type.width)),
                              Buffer(), Parameter());
        } else {
            IRMutator::visit(op);
        }
    }    

    void visit(const Store *op) {
        if (op->name == store->name &&
            equal(op->index, store->index)) {
            Expr value = mutate(op->value);

            string var_name = unique_name('t');
            Expr var = Variable::make(value.type(), var_name);
            Stmt store_to_orig = Store::make(op->name, var, op->index);
            Stmt store_to_scratch = Store::make(name, var, make_zero(Int(32, value.type().width)));
            stmt = Block::make(store_to_orig, store_to_scratch);
            stmt = LetStmt::make(var_name, value, stmt);

            // TODO: What if the load is in the store index itself?
        } else {
            IRMutator::visit(op);
        }
    }
    
public:
    UseScratch(const Load *l, const Store *s, const string &n) : load(l), store(s), name(n) {}
};

class StoreForwarding : public IRMutator {
    void visit(const For *op) {
        Stmt body = mutate(op->body);

        // Find all the unconditional loads and stores
        FindLoadsAndStores f;
        op->body.accept(&f);

        struct ScratchBuf {
            string name;
            Expr initial_value;
        };
            
        vector<ScratchBuf> scratch_buffers;
        
        // If any of the loads will reuse the value stored on the
        // previous iteration, then make a stack variable to hold the
        // value as well, and load from that instead.
        for (const Load *load : f.loads) {
            for (const Store *store : f.stores) {
                if (load->name != store->name) continue;
                if (load->index.type() != store->index.type()) continue;
                Expr next_loop_var = Variable::make(Int(32), op->name) + 1;
                Expr next_load_index = substitute(op->name, next_loop_var, load->index);
                if (is_zero(simplify(next_load_index - store->index))) {
                    debug(0) << "Found opportunity\n"
                             << " load: " << load->index << "\n"
                             << " store: " << store->index << "\n";

                    // Make a scratch buffer
                    Expr init = substitute(op->name, op->min, load);
                    ScratchBuf b = {unique_name("b"), init};
                    scratch_buffers.push_back(b);

                    // Augment the store with a store to the scratch buffer
                    body = UseScratch(load, store, b.name).mutate(body);

                    // TODO: should probably restart after this
                } else {
                    debug(0) << "Missed opportunity\n"
                             << " load: " << load->index << "\n"
                             << " store: " << store->index << "\n";
                }
            }
        }

        stmt = For::make(op->name, op->min, op->extent, op->for_type, op->device_api, body);

        for (auto b : scratch_buffers) {
            // Make and initialize the scratch buffer
            // TODO: What if the loop extent is zero? Need an if statement
            Stmt store_init =
                Store::make(b.name, b.initial_value, make_zero(Int(32, b.initial_value.type().width)));            
            stmt = Block::make(store_init, stmt);            
            stmt = Allocate::make(b.name, b.initial_value.type(), {}, const_true(), stmt);
        }
    }
};

Stmt store_forwarding(Stmt s) {
    return StoreForwarding().mutate(s);
}


}
}
