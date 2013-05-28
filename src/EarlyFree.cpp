#include "EarlyFree.h"
#include "IRVisitor.h"
#include "IRMutator.h"
#include "Log.h"
#include "IRPrinter.h"
#include "IROperator.h"
#include "Scope.h"
#include <map>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::make_pair;
using std::string;
using std::vector;

class FindLastUse : public IRVisitor {
public:
    string func;
    Stmt last_use;

    FindLastUse(string s) : func(s), in_loop(false) {}

private:
    bool in_loop;
    Stmt containing_stmt;

    using IRVisitor::visit;

    void visit(const For *loop) {
        loop->min.accept(this);
        loop->extent.accept(this);
        bool old_in_loop = in_loop;
        in_loop = true;
        loop->body.accept(this);
        in_loop = old_in_loop;
    }

    void visit(const Load *load) {
        if (func == load->name) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(load);
    }

    void visit(const Store *store) {
        if (func == store->name) {
            last_use = containing_stmt;
        }
        IRVisitor::visit(store);
    }

    void visit(const Pipeline *pipe) {
        if (in_loop) {
            IRVisitor::visit(pipe);
        } else {

            Stmt old_containing_stmt = containing_stmt;            

            containing_stmt = pipe->produce;
            pipe->produce.accept(this);
            
            if (pipe->update.defined()) {
                containing_stmt = pipe->update;
                pipe->update.accept(this);
            }
            
            containing_stmt = old_containing_stmt;
            pipe->consume.accept(this);
        }
    }

    void visit(const Block *block) {
        if (in_loop) {
            IRVisitor::visit(block);
        } else {
            Stmt old_containing_stmt = containing_stmt;            
            containing_stmt = block->first;
            block->first.accept(this);           
            if (block->rest.defined()) {
                containing_stmt = block->rest;
                block->rest.accept(this);            
            }
            containing_stmt = old_containing_stmt;
        }
    }
        
};

class InjectMarker : public IRMutator {
public:
    string func;
    Stmt last_use;

private:    

    using IRMutator::visit;

    Stmt inject_marker(Stmt s) {
        if (s.same_as(last_use)) {
            return Block::make(s, Free::make(func));
        } else {
            return mutate(s);
        }
    }

    void visit(const Pipeline *pipe) {
        Stmt new_produce = inject_marker(pipe->produce);
        Stmt new_update;
        if (pipe->update.defined()) {
            new_update = inject_marker(pipe->update);
        }
        Stmt new_consume = inject_marker(pipe->consume);
        if (new_produce.same_as(pipe->produce) &&
            new_update.same_as(pipe->update) &&
            new_consume.same_as(pipe->consume)) {
            stmt = pipe;
        } else {
            stmt = Pipeline::make(pipe->name, new_produce, new_update, new_consume);
        }
    }

    void visit(const Block *block) {        
        Stmt new_first = inject_marker(block->first);
        Stmt new_rest = inject_marker(block->rest);
        if (new_first.same_as(block->first) &&
            new_rest.same_as(block->rest)) {
            stmt = block;
        } else {
            stmt = Block::make(new_first, new_rest);
        }        
    }    

};

class InjectEarlyFrees : public IRMutator {
    using IRMutator::visit;

    void visit(const Allocate *alloc) {
        IRMutator::visit(alloc);
        alloc = stmt.as<Allocate>();
        assert(alloc);

        FindLastUse last_use(alloc->name);
        stmt.accept(&last_use);

        log(1) << "Last use of " << alloc->name << " is " << last_use.last_use << "\n";

        if (last_use.last_use.defined()) {
            InjectMarker inject_marker;
            inject_marker.func = alloc->name;
            inject_marker.last_use = last_use.last_use;
            stmt = inject_marker.mutate(stmt);
        } else {            
            stmt = Allocate::make(alloc->name, alloc->type, alloc->size, 
                                Block::make(alloc->body, Free::make(alloc->name)));
        }

    }
};

Stmt inject_early_frees(Stmt s) {
    InjectEarlyFrees early_frees;
    return early_frees.mutate(s);    
}

// TODO: test

}
}
