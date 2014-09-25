#include "InjectHostDevBufferCopies.h"
#include "IRMutator.h"
#include "Debug.h"
#include "IRPrinter.h"
#include "CodeGen_GPU_Dev.h"
#include "IROperator.h"
#include <map>

namespace Halide {
namespace Internal {

using std::string;
using std::map;
using std::vector;
using std::set;
using std::pair;

// If a buffer never makes it outside of Halide (which can happen if
// it's an input, output, or used in an extern stage), and is never
// used inside a kernel, or is local to a kernel, it's pointless to
// track it for the purposes of copy_to_dev, copy_to_host, etc. This
// class finds the buffers worth tracking.
class FindBuffersToTrack : public IRVisitor {
    map<string, bool> internal;
    bool in_device_code;

    using IRVisitor::visit;

    void visit(const Allocate *op) {
        internal[op->name] = in_device_code;
        IRVisitor::visit(op);
    }

    void visit(const For *op) {
        if (!in_device_code && CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            in_device_code = true;
            IRVisitor::visit(op);
            in_device_code = false;
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const LetStmt *op) {
        // The let that defines the buffer_t is not interesting, and
        // nothing before that let could be interesting either
        // (because the buffer doesn't exist yet).
        const Call *c = op->value.as<Call>();
        if (ends_with(op->name, ".buffer") &&
            c && c->name == Call::create_buffer_t) {
            buffers_to_track.erase(op->name.substr(0, op->name.size() - 7));
        }

        IRVisitor::visit(op);
    }

    void visit(const Load *op) {
        if (internal.find(op->name) == internal.end() ||
            internal[op->name] != in_device_code) {
            buffers_to_track.insert(op->name);
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (internal.find(op->name) == internal.end() ||
            internal[op->name] != in_device_code) {
            buffers_to_track.insert(op->name);
        }
        IRVisitor::visit(op);
    }

    void visit(const Variable *op) {
        if (op->type == Handle() && ends_with(op->name, ".buffer")) {
            buffers_to_track.insert(op->name.substr(0, op->name.size() - 7));
        }
    }

public:
    set<string> buffers_to_track;
    FindBuffersToTrack() : in_device_code(false) {}
};



class InjectBufferCopies : public IRMutator {
    using IRMutator::visit;

    struct BufferInfo {
        bool host_touched,  // Is there definitely a host-side allocation?
            dev_touched,    // Is there definitely a device-side allocation?
            host_current,   // Is the data known to be up-to-date on the host?
            dev_current,    // Is the data known to be up-to-date on the device?
            host_read,      // There has been a host-side read. Might need a copy-to-host.
            host_write,     // There has been a host-side write. Might need a copy-to-host.
            dev_read,       // There has been a device-side read. Might need a copy-to-dev.
            dev_write,      // There has been a device-side write. Might need a copy-to-dev.
            internal;       // Did Halide allocate this buffer?
        // The compute_at loop level for this buffer. It's at this
        // loop level that all copies should occur. Empty string for
        // input buffers and compute_root things.
        string loop_level;

        BufferInfo() : host_touched(false),
                       dev_touched(false),
                       host_current(false),
                       dev_current(false),
                       host_read(false),
                       host_write(false),
                       dev_read(false),
                       dev_write(false),
                       internal(false) {}
    };

    map<string, BufferInfo> state;
    string loop_level;
    const set<string> &buffers_to_track;
    bool in_device_code;

    // Prepend code to the statement that copies everything marked as
    // a bad read to host or dev.
    Stmt do_copies(Stmt s) {
        if (in_device_code) {
            return s;
        }

        debug(4) << "At loop level " << loop_level << "\n";

        for (map<string, BufferInfo>::iterator iter = state.begin();
             iter != state.end(); ++iter) {

            string direction;
            BufferInfo &buf = iter->second;
            if (buf.loop_level != loop_level) {
                continue;
            }

            debug(4) << "do_copies for " << iter->first << "\n"
                     << buf.host_current << ", " << buf.dev_current << "\n"
                     << buf.host_read << ", " << buf.dev_read << "\n"
                     << buf.host_write << ", " << buf.dev_write << "\n"
                     << buf.host_touched << ", " << buf.dev_touched << "\n"
                     << buf.internal << "\n";


            // Update whether there needs to be a host or dev-side allocation
            buf.host_touched = buf.host_write || buf.host_read || buf.host_touched;
            buf.dev_touched = buf.dev_write || buf.dev_read || buf.dev_touched;

            if ((buf.host_read || buf.host_write) && !buf.host_current && (!buf.internal || buf.dev_touched)) {
                // Needs a copy to host.
                internal_assert(!buf.dev_read && !buf.dev_write);
                direction = "host";
                buf.host_current = true;
                buf.dev_current = buf.dev_current && !buf.host_write;
                debug(4) << "Needs copy to host\n";
            } else if (buf.host_write) {
                // Invalidate the device version, if any.
                internal_assert(!buf.dev_read && !buf.dev_write);
                buf.dev_current = false;
            } else if ((buf.dev_read || buf.dev_write) && !buf.dev_current && (!buf.internal || buf.host_touched)) {
                // Needs a copy-to-dev.
                internal_assert(!buf.host_read && !buf.host_write);
                direction = "dev";
                buf.dev_current = true;
                buf.host_current = buf.host_current && !buf.dev_write;
                debug(4) << "Needs copy to dev\n";
            } else if (buf.dev_write) {
                // Invalidate the host version.
                internal_assert(!buf.host_read && !buf.host_write);
                buf.host_current = false;
            }

            Expr buffer = Variable::make(Handle(), iter->first + ".buffer");
            Expr t = make_one(UInt(8));

            if (buf.host_write) {
                // If we just invalidated the dev pointer, we need to set the host dirty bit.
                Expr set_host_dirty = Call::make(Int(32), Call::set_host_dirty, vec(buffer, t), Call::Intrinsic);
                s = Block::make(s, Evaluate::make(set_host_dirty));
            }

            if (buf.dev_write) {
                // If we just invalidated the host pointer, we need to set the dev dirty bit.
                Expr set_dev_dirty = Call::make(Int(32), Call::set_dev_dirty, vec(buffer, t), Call::Intrinsic);
                s = Block::make(s, Evaluate::make(set_dev_dirty));
            }

            // Clear the pending action bits.
            buf.host_write = buf.host_read = buf.dev_read = buf.dev_write = false;

            if (!direction.empty()) {
                // Make the copy
                Expr copy = Call::make(Int(32), "halide_copy_to_" + direction, vec(buffer), Call::Extern);
                Stmt check = AssertStmt::make(copy == 0,
                                              "Failed to copy buffer " + iter->first +
                                              " to " + direction + ".");
                s = Block::make(check, s);
            }
        }
        return s;
    }

    bool should_track(const string &buf) {
        return buffers_to_track.count(buf) != 0;
    }

    void visit(const Store *op) {
        IRMutator::visit(op);

        if (!should_track(op->name)) {
            return;
        }

        if (in_device_code) {
            state[op->name].dev_write = true;
        } else {
            state[op->name].host_write = true;
        }
    }

    void visit(const Load *op) {
        IRMutator::visit(op);

        if (!should_track(op->name)) {
            return;
        }

        if (in_device_code) {
            state[op->name].dev_read = true;
        } else {
            state[op->name].host_read = true;
        }
    }

    void visit(const Call *op) {
        if (op->name == Call::address_of && op->call_type == Call::Intrinsic) {
            // We're after storage flattening, so the sole arg should be a load.
            internal_assert(op->args.size() == 1);
            const Load *l = op->args[0].as<Load>();
            internal_assert(l);
            Expr new_index = mutate(l->index);
            if (l->index.same_as(new_index)) {
                expr = op;
            } else {
                Expr new_load = Load::make(l->type, l->name, new_index, Buffer(), Parameter());
                expr = Call::make(op->type, op->name, vec(new_load), Call::Intrinsic);
            }
        } else if (op->name == Call::glsl_texture_load && op->call_type == Call::Intrinsic) {
            // counts as a device read
            internal_assert(in_device_code);
            internal_assert(op->args.size() >= 2);
            const Variable *buffer_var = op->args[1].as<Variable>();
            internal_assert(buffer_var && ends_with(buffer_var->name, ".buffer"));
            string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
            state[buf_name].dev_read = true;
            IRMutator::visit(op);
        } else if (op->name == Call::glsl_texture_store && op->call_type == Call::Intrinsic) {
            // counts as a device store
            internal_assert(in_device_code);
            internal_assert(op->args.size() >= 2);
            const Variable *buffer_var = op->args[1].as<Variable>();
            internal_assert(buffer_var && ends_with(buffer_var->name, ".buffer"));
            string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
            state[buf_name].dev_write = true;
            IRMutator::visit(op);
        } else {
            IRMutator::visit(op);
        }
    }

    Stmt make_dev_malloc(string buf_name) {
        Expr buf = Variable::make(Handle(), buf_name + ".buffer");
        Expr call = Call::make(Int(32), "halide_dev_malloc", vec(buf), Call::Extern);
        string msg = "Failed to allocate device buffer for " + buf_name;
        return AssertStmt::make(call == 0, msg);
    }

    void visit(const Pipeline *op) {
        if (in_device_code) {
            IRMutator::visit(op);
            return;
        }

        bool is_output = true;
        // The buffers associated with this pipeline should get this loop level
        for (map<string, BufferInfo>::iterator iter = state.begin();
             iter != state.end(); ++iter) {
            const string &buf_name = iter->first;
            if (buf_name == op->name || starts_with(buf_name, op->name + ".")) {
                iter->second.loop_level = loop_level;
                is_output = false;
           }
        }

        Stmt produce = mutate(op->produce);
        produce = do_copies(produce);

        Stmt update;
        if (op->update.defined()) {
            update = mutate(op->update);
            update = do_copies(update);
        }

        Stmt consume = mutate(op->consume);
        consume = do_copies(consume);

        if (produce.same_as(op->produce) &&
            update.same_as(op->update) &&
            consume.same_as(op->consume)) {
            stmt = op;
        } else {
            stmt = Pipeline::make(op->name, produce, update, consume);
        }

        // Need a dev malloc for all output buffers touched on device
        if (is_output) {
            for (map<string, BufferInfo>::iterator iter = state.begin();
                 iter != state.end(); ++iter) {
                const string &buf_name = iter->first;
                if ((buf_name == op->name || starts_with(buf_name, op->name + ".")) &&
                    iter->second.dev_touched) {
                    // Inject a dev_malloc
                    stmt = Block::make(make_dev_malloc(buf_name), stmt);
                }
            }
        }
    }

    void visit(const Variable *op) {
        IRMutator::visit(op);

        // Direct access to a buffer inside its allocate node
        // (e.g. being passed to an extern stage) implies that its
        // host allocation must exist. Direct access outside an
        // allocate node is just bounds inference to an input or
        // output.
        if (ends_with(op->name, ".buffer")) {
            string buf_name = op->name.substr(0, op->name.size() - 7);
            if (state.find(buf_name) != state.end()) {
                state[buf_name].host_touched = true;
            }
        }

    }

    void visit(const Allocate *op) {
        if (in_device_code ||
            !should_track(op->name)) {
            IRMutator::visit(op);
            return;
        }

        string buf_name = op->name;

        state[buf_name].internal = true;

        IRMutator::visit(op);
        op = stmt.as<Allocate>();
        internal_assert(op);

        // If this buffer is only ever touched on gpu, nuke the host-side allocation.
        if (!state[buf_name].host_touched) {
            stmt = Allocate::make(op->name, op->type, op->extents, const_false(), op->body);
        }

        state.erase(buf_name);
    }

    void visit(const LetStmt *op) {
        IRMutator::visit(op);
        if (in_device_code) {
            return;
        }

        op = stmt.as<LetStmt>();
        internal_assert(op);

        if (ends_with(op->name, ".buffer")) {
            string buf_name = op->name.substr(0, op->name.size() - 7);
            if (!should_track(buf_name)) {
                return;
            }
            if (state[buf_name].dev_touched) {
                // Inject a dev_malloc
                Stmt dev_malloc = make_dev_malloc(buf_name);
                Stmt body = Block::make(dev_malloc, op->body);

                // Use null as a host pointer if there's no host allocation
                Expr val = op->value;
                if (!state[buf_name].host_touched) {
                    const Call *create_buffer_t = val.as<Call>();
                    internal_assert(create_buffer_t && create_buffer_t->name == Call::create_buffer_t);
                    vector<Expr> args = create_buffer_t->args;
                    args[0] = Call::make(Handle(), Call::null_handle, vector<Expr>(), Call::Intrinsic);
                    val = Call::make(Handle(), Call::create_buffer_t, args, Call::Intrinsic);
                }

                stmt = LetStmt::make(op->name, val, body);
            }
        }
    }

    void visit(const IfThenElse *op) {
        if (in_device_code) {
            IRMutator::visit(op);
            return;
        }

        Expr cond = mutate(op->condition);

        // If one path writes on host and one path writes on device,
        // what is the state after the if-then-else? We'll need to
        // make a copy of the state map, go down each path, and then
        // unify them.
        map<string, BufferInfo> copy = state;

        Stmt then_case = mutate(op->then_case);
        then_case = do_copies(then_case);

        copy.swap(state);
        Stmt else_case = mutate(op->else_case);
        else_case = do_copies(else_case);

        for (map<string, BufferInfo>::iterator iter = copy.begin();
             iter != copy.end(); ++iter) {
            const string &buf_name = iter->first;
            if (loop_level != iter->second.loop_level) {
                continue;
            }

            BufferInfo &then_state = iter->second;
            BufferInfo &else_state = state[buf_name];
            BufferInfo merged_state;

            merged_state.loop_level = loop_level;
            merged_state.host_touched   = then_state.host_touched || else_state.host_touched;
            merged_state.dev_touched    = then_state.dev_touched || else_state.dev_touched;
            merged_state.host_current = then_state.host_current && else_state.host_current;
            merged_state.dev_current  = then_state.dev_current && else_state.dev_current;

            state[buf_name] = merged_state;
        }

        if (cond.same_as(op->condition) &&
            then_case.same_as(op->then_case) &&
            else_case.same_as(op->else_case)) {
            stmt = op;
        } else {
            stmt = IfThenElse::make(cond, then_case, else_case);
        }
    }

    void visit(const Block *op) {
        if (in_device_code) {
            IRMutator::visit(op);
            return;
        }

        Stmt first = mutate(op->first);
        first = do_copies(first);

        Stmt rest = op->rest;
        if (rest.defined()) {
            rest = mutate(rest);
            rest = do_copies(rest);
        }

        stmt = Block::make(first, rest);
    }

    void visit(const For *op) {
        string old_loop_level = loop_level;
        loop_level = op->name;
        if (!in_device_code && CodeGen_GPU_Dev::is_gpu_var(op->name)) {
            in_device_code = true;
            IRMutator::visit(op);
            in_device_code = false;
        } else {
            IRMutator::visit(op);
        }
        loop_level = old_loop_level;
    }

public:
    InjectBufferCopies(const set<string> &i) : loop_level(""), buffers_to_track(i), in_device_code(false) {}

};

Stmt inject_host_dev_buffer_copies(Stmt s) {
    FindBuffersToTrack f;
    s.accept(&f);

    debug(4) << "Tracking host <-> dev copies for the following buffers:\n";
    for (set<string>::iterator iter = f.buffers_to_track.begin();
         iter != f.buffers_to_track.end(); ++iter) {
        debug(4) << *iter << "\n";
    }

    return InjectBufferCopies(f.buffers_to_track).mutate(s);
}

class InjectDevFrees : public IRMutator {
    using IRMutator::visit;

    // We assume buffers are uniquely named at this point
    set<string> needs_freeing;

    void visit(const Call *op) {
        if (op->name == "halide_copy_to_dev" || op->name == "halide_dev_malloc") {
            internal_assert(op->args.size() == 1);
            const Variable *var = op->args[0].as<Variable>();
            internal_assert(var);
            needs_freeing.insert(var->name);
        }
        expr = op;
    }

    void visit(const Free *op) {
        string buf_name = op->name + ".buffer";
        if (needs_freeing.count(buf_name)) {
            Expr buf = Variable::make(Handle(), buf_name);
            Expr free_call = Call::make(Int(32), "halide_dev_free", vec(buf), Call::Extern);
            Stmt check = AssertStmt::make(free_call == 0, "Failed to free device buffer for " + op->name);
            stmt = Block::make(check, op);
        } else {
            stmt = op;
        }
    }
};

Stmt inject_dev_frees(Stmt s) {
    return InjectDevFrees().mutate(s);
}

}
}
