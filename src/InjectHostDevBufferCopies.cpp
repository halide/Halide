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

DeviceAPI fixup_device_api(DeviceAPI device_api, const Target &target) {
    if (device_api == DeviceAPI::Default_GPU) {
        if (target.has_feature(Target::OpenCL)) {
            return DeviceAPI::OpenCL;
        } else if (target.has_feature(Target::CUDA)) {
            return DeviceAPI::CUDA;
        } else {
            user_error << "Schedule uses Default_GPU without a valid GPU (OpenCL or CUDA) specified in target.\n";
        }
    }
    return device_api;
}

static bool different_device_api(DeviceAPI device_api, DeviceAPI stmt_api, const Target &target) {
    device_api = fixup_device_api(device_api, target);
    stmt_api = fixup_device_api(stmt_api, target);
    return (stmt_api != DeviceAPI::Parent) && (device_api != stmt_api);
}

// If a buffer never makes it outside of Halide (i.e. if it is not
// used as an input, output, or in an extern stage), and is never used
// inside a kernel, or is local to a kernel, it's pointless to track
// it for the purposes of copy_to_device, copy_to_host, etc. This
// class finds the buffers worth tracking.
class FindBuffersToTrack : public IRVisitor {
    map<string, DeviceAPI> internal;
    const Target &target;
    DeviceAPI device_api;

    using IRVisitor::visit;

    void visit(const Allocate *op) {
        debug(2) << "Buffers to track: Setting Allocate for loop " << op->name << " to " << static_cast<int>(device_api) << "\n";
        internal_assert(internal.find(op->name) == internal.end()) << "Duplicate Allocate node in FindBuffersToTrack.\n";
        pair<map<string, DeviceAPI>::iterator, bool> it = internal.insert(make_pair(op->name, device_api));
        IRVisitor::visit(op);
        internal.erase(it.first);
    }

    void visit(const For *op) {
      if (different_device_api(device_api, op->device_api, target)) {
            debug(2) << "Buffers to track: switching from " << static_cast<int>(device_api) <<
                " to " << static_cast<int>(op->device_api) << " for loop " << op->name << "\n";
            DeviceAPI old_device_api = device_api;
            device_api = fixup_device_api(op->device_api, target);
            if (device_api == DeviceAPI::Parent) {
                device_api = old_device_api;
            }
            internal_assert(device_api != DeviceAPI::Parent);
            IRVisitor::visit(op);
            device_api = old_device_api;
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
            different_device_api(device_api, internal[op->name], target)) {
            buffers_to_track.insert(op->name);
        }
        IRVisitor::visit(op);
    }

    void visit(const Store *op) {
        if (internal.find(op->name) == internal.end() ||
            different_device_api(device_api, internal[op->name], target)) {
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
    FindBuffersToTrack(const Target &t) : target(t), device_api(DeviceAPI::Host) {}
};



class InjectBufferCopies : public IRMutator {
    using IRMutator::visit;

    // BufferInfo tracks the state of a givven buffer over an IR scope.
    // Generally the scope is a Pipeline. The data herein is mutable.
    struct BufferInfo {
        bool host_touched,  // Is there definitely a host-side allocation?
            dev_touched,    // Is there definitely a device-side allocation?
            host_current,   // Is the data known to be up-to-date on the host?
            dev_current,    // Is the data known to be up-to-date on the device?
            internal,       // Did Halide allocate this buffer?
            dev_allocated;  // Has the buffer been allocated?
        // The compute_at loop level for this buffer. It's at this
        // loop level that all copies should occur. Empty string for
        // input buffers and compute_root things.
        string loop_level;
        DeviceAPI device_first_touched;      // First device to read or write in
        DeviceAPI current_device;            // Valid if dev_current is true
        std::set<DeviceAPI> devices_reading; // List of devices, including host, reading buffer in visited scope
        std::set<DeviceAPI> devices_writing; // List of devices, including host, writing buffer in visited scope

        BufferInfo() : host_touched(false),
                       dev_touched(false),
                       host_current(false),
                       dev_current(false),
                       internal(false),
                       dev_allocated(true),  // This is true unless we know for sure it is not allocated (this BufferInfo is from an Allocate node).
                       device_first_touched(DeviceAPI::Parent), // Meaningless initial value
                       current_device(DeviceAPI::Host) {}
    };

    map<string, BufferInfo> state;
    string loop_level;
    const set<string> &buffers_to_track;
    const Target &target;
    DeviceAPI device_api;

    Expr make_device_interface_call(DeviceAPI device_api) {
        std::string interface_name;
        switch (device_api) {
          case DeviceAPI::CUDA:
            interface_name = "halide_cuda_device_interface";
            break;
          case DeviceAPI::OpenCL:
            interface_name = "halide_opencl_device_interface";
            break;
          case DeviceAPI::GLSL:
            interface_name = "halide_opengl_device_interface";
            break;
          default:
            internal_error << "Bad DeviceAPI " << static_cast<int>(device_api) << "\n";
            break;
        }
        std::vector<Expr> no_args;
        return Call::make(Handle(), interface_name, no_args, Call::Extern);
    }

    Stmt make_dev_malloc(string buf_name, DeviceAPI target_device_api) {
        Expr buf = Variable::make(Handle(), buf_name + ".buffer");
        Expr device_interface = make_device_interface_call(target_device_api);
        Expr call = Call::make(Int(32), "halide_device_malloc", vec(buf, device_interface), Call::Extern);
        string msg = "Failed to allocate device buffer for " + buf_name;
        return AssertStmt::make(call == 0, msg);
    }

    enum CopyDirection {
      NoCopy,
      ToHost,
      ToDevice
    };

    Stmt make_buffer_copy(CopyDirection direction, string buf_name, DeviceAPI target_device_api) {
        internal_assert(direction == ToHost || direction == ToDevice) << "make_buffer_copy caller logic error.\n";
        std::vector<Expr> args;
        Expr buffer = Variable::make(Handle(), buf_name + ".buffer");
        args.push_back(buffer);
        if (direction == ToDevice) {
            args.push_back(make_device_interface_call(target_device_api));
        }

        std::string suffix = (direction == ToDevice) ? "device" : "host";
        Expr copy = Call::make(Int(32), "halide_copy_to_" + suffix, args, Call::Extern);
        Stmt check = AssertStmt::make(copy == 0,
                                      "Failed to copy buffer " + buf_name +
                                      " to " + suffix + ".");
        return check;
    }

    // Prepend code to the statement that copies everything marked as
    // a dev read to host or dev.
    Stmt do_copies(Stmt s) {
        // Cannot do any sort of buffer copying in device code yet.
        if (device_api != DeviceAPI::Host) {
            return s;
        }

        debug(4) << "At loop level " << loop_level << "\n";

        for (map<string, BufferInfo>::iterator iter = state.begin();
             iter != state.end(); ++iter) {

            CopyDirection direction = NoCopy;
            BufferInfo &buf = iter->second;
            if (buf.loop_level != loop_level) {
                continue;
            }

            debug(4) << "do_copies for " << iter->first << "\n"
                     << "Host current: " << buf.host_current << " Device current: " << buf.dev_current << "\n"
                     << "Host touched: " << buf.host_touched << " Device touched: " << buf.dev_touched << "\n"
                     << "Internal: " << buf.internal << " Device touching first: "
                     << static_cast<int>(buf.device_first_touched) << "\n"
                     << "Current device: " << static_cast<int>(buf.current_device) << "\n";
            DeviceAPI touching_device = DeviceAPI::Parent;
            bool host_read = false;
            size_t non_host_devices_reading_count = 0;
            DeviceAPI reading_device = DeviceAPI::Parent;
            for (std::set<DeviceAPI>::const_iterator dev = buf.devices_reading.begin(); dev != buf.devices_reading.end(); dev++) {
                debug(4) << "Device " << static_cast<int>(*dev) << " read buffer\n";
                if (*dev != DeviceAPI::Host) {
                    non_host_devices_reading_count++;
                    reading_device = *dev;
                    touching_device = *dev;
                } else {
                    host_read = true;
                }
            }
            bool host_wrote = false;
            size_t non_host_devices_writing_count = 0;
            DeviceAPI writing_device = DeviceAPI::Parent;
            for (std::set<DeviceAPI>::const_iterator dev = buf.devices_writing.begin(); dev != buf.devices_writing.end(); dev++) {
                debug(4) << "Device " << static_cast<int>(*dev) << " wrote buffer\n";
                if (*dev != DeviceAPI::Host) {
                    non_host_devices_writing_count++;
                    writing_device = *dev;
                    touching_device = *dev;
                } else {
                    host_wrote = true;
                }
            }

            // Ideally this will support multi-device buffers someday, but not now.
            internal_assert(non_host_devices_reading_count <= 1);
            internal_assert(non_host_devices_writing_count <= 1);
            internal_assert((non_host_devices_reading_count == 0 || non_host_devices_writing_count == 0) ||
                            reading_device == writing_device);

            bool device_read = non_host_devices_reading_count > 0;
            bool device_wrote = non_host_devices_writing_count > 0;

            // Update whether there needs to be a host or dev-side allocation
            buf.host_touched = host_wrote || host_read || buf.host_touched;
            if (!buf.dev_touched && (device_wrote || device_read)) {
                buf.dev_touched = true;
                buf.device_first_touched = touching_device;
            }

            if ((host_read || host_wrote) && !buf.host_current && (!buf.internal || buf.dev_touched)) {
                // Needs a copy to host.
                internal_assert(!device_read && !device_wrote);
                direction = ToHost;
                buf.host_current = true;
                buf.dev_current = buf.dev_current && !host_wrote;
                debug(4) << "Needs copy to host\n";
            } else if (host_wrote) {
                // Invalidate the device version, if any.
                internal_assert(!device_read && !device_wrote);
                buf.dev_current = false;
                debug(4) << "Invalidating dev_current\n";
            } else if ((device_read || device_wrote) &&
                       ((!buf.dev_current || (buf.current_device != touching_device)) ||
                        (!buf.internal || buf.host_touched))) {
                // Needs a copy-to-dev.
                internal_assert(!host_read && !host_wrote);
                direction = ToDevice;
                // If the buffer will need to be moved from one device to another,
                // a host allocation will be required.
                buf.host_touched = buf.host_touched || (buf.current_device != touching_device);
                buf.dev_current = true;
                buf.current_device = touching_device;
                buf.host_current = buf.host_current && !device_wrote;
                debug(4) << "Needs copy to dev\n";
            } else if (device_wrote) {
                // Invalidate the host version.
                internal_assert(!host_read && !host_wrote);
                buf.host_current = false;
                debug(4) << "Invalidating host_current\n";
            }

            Expr buffer = Variable::make(Handle(), iter->first + ".buffer");
            Expr t = make_one(UInt(8));

            if (host_wrote) {
                debug(4) << "Setting host dirty for " << iter->first << "\n";
                // If we just invalidated the dev pointer, we need to set the host dirty bit.
                Expr set_host_dirty = Call::make(Int(32), Call::set_host_dirty, vec(buffer, t), Call::Intrinsic);
                s = Block::make(s, Evaluate::make(set_host_dirty));
            }

            if (device_wrote) {
                // If we just invalidated the host pointer, we need to set the dev dirty bit.
                Expr set_dev_dirty = Call::make(Int(32), Call::set_dev_dirty, vec(buffer, t), Call::Intrinsic);
                s = Block::make(s, Evaluate::make(set_dev_dirty));
            }

            // Clear the pending action bits.
            buf.devices_reading.clear();
            buf.devices_writing.clear();

            if (direction != NoCopy && touching_device != DeviceAPI::Host) {
                s = Block::make(make_buffer_copy(direction, iter->first, touching_device), s);
            }

            // Inject a dev_malloc if needed.
            if (!buf.dev_allocated && buf.device_first_touched != DeviceAPI::Host && buf.device_first_touched != DeviceAPI::Parent) {
                debug(4) << "Injecting device malloc for " << iter->first << " on " <<
                    static_cast<int>(buf.device_first_touched) << "\n";
                Stmt dev_malloc = make_dev_malloc(iter->first, buf.device_first_touched);
                s = Block::make(dev_malloc, s);
                buf.dev_allocated = true;
            }
        }

        debug(4) << "\n"; // Make series of do_copies a bit more readable.

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

        debug(4) << "Device " << static_cast<int>(device_api) << " writes buffer " << op->name << "\n";
        state[op->name].devices_writing.insert(device_api);
    }

    void visit(const Load *op) {
        IRMutator::visit(op);

        if (!should_track(op->name)) {
            return;
        }

        debug(4) << "Device " << static_cast<int>(device_api) << " reads buffer " << op->name << "\n";
        state[op->name].devices_reading.insert(device_api);
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
            internal_assert(device_api == DeviceAPI::GLSL);
            internal_assert(op->args.size() >= 2);
            const Variable *buffer_var = op->args[1].as<Variable>();
            internal_assert(buffer_var && ends_with(buffer_var->name, ".buffer"));
            string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
            debug(4) << "Adding GLSL read via glsl_texture_load for " << buffer_var->name << "\n";
            state[buf_name].devices_reading.insert(DeviceAPI::GLSL);
            IRMutator::visit(op);
        } else if (op->name == Call::glsl_texture_store && op->call_type == Call::Intrinsic) {
            // counts as a device store
            internal_assert(device_api == DeviceAPI::GLSL);
            internal_assert(op->args.size() >= 2);
            const Variable *buffer_var = op->args[1].as<Variable>();
            internal_assert(buffer_var && ends_with(buffer_var->name, ".buffer"));
            string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
            debug(4) << "Adding GLSL write via glsl_texture_load for " << buffer_var->name << "\n";
            state[buf_name].devices_writing.insert(DeviceAPI::GLSL);
            IRMutator::visit(op);
        } else if (op->call_type == Call::Extern) {
            // Check if we are passing a variable tagged with ".buffer" to an
            // extern call node. If so, track the buffer. We don't have enough
            // information to determine if it is read from or written to.
            for (int i = 0; i != op->args.size(); ++i) {
                const Variable *buffer_var = op->args[i].as<Variable>();
                if (buffer_var && ends_with(buffer_var->name, ".buffer")) {
                    debug(4) << "Adding extern read/write for " << buffer_var->name << "\n";
                    string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
                    state[buf_name].devices_reading.insert(DeviceAPI::GLSL);
                    state[buf_name].devices_writing.insert(DeviceAPI::GLSL);
                }
                IRMutator::visit(op);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Pipeline *op) {
        if (device_api != DeviceAPI::Host) {
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

        // Need to make all output buffers touched on device valid
        if (is_output) {
            for (map<string, BufferInfo>::iterator iter = state.begin();
                 iter != state.end(); ++iter) {
                const string &buf_name = iter->first;
                if ((buf_name == op->name || starts_with(buf_name, op->name + ".")) &&
                    iter->second.dev_touched && iter->second.current_device != DeviceAPI::Host) {
                    // Inject a device copy, which will make sure the device buffer is allocated
                    // on the right device and that the host dirty bit is false so the device
                    // can write. (Which will involve copying to the device if host was dirty
                    // for the passed in buffer.)
                    debug(4) << "Injecting device copy for output " << buf_name << " on " <<
                        static_cast<int>(iter->second.current_device) << "\n";
                    stmt = Block::make(make_buffer_copy(ToDevice, buf_name, iter->second.current_device), stmt);
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
        if (device_api != DeviceAPI::Host ||
            !should_track(op->name)) {
            IRMutator::visit(op);
            return;
        }

        string buf_name = op->name;

        state[buf_name].internal = true;
        state[buf_name].dev_allocated = false;

        IRMutator::visit(op);
        op = stmt.as<Allocate>();
        internal_assert(op);

        // If this buffer is only ever touched on gpu, nuke the host-side allocation.
        if (!state[buf_name].host_touched) {
            debug(4) << "Eliding host alloc for " << op->name << "\n";
            stmt = Allocate::make(op->name, op->type, op->extents, const_false(), op->body);
        }
        state.erase(buf_name);
    }

    void visit(const LetStmt *op) {
        IRMutator::visit(op);
        if (device_api != DeviceAPI::Host) {
            return;
        }

        op = stmt.as<LetStmt>();
        internal_assert(op);

        if (ends_with(op->name, ".buffer")) {
            string buf_name = op->name.substr(0, op->name.size() - 7);
            if (!should_track(buf_name)) {
                return;
            }
            if (!state[buf_name].host_touched) {
                // Use null as a host pointer if there's no host allocation
                const Call *create_buffer_t = op->value.as<Call>();
                internal_assert(create_buffer_t && create_buffer_t->name == Call::create_buffer_t);
                vector<Expr> args = create_buffer_t->args;
                args[0] = Call::make(Handle(), Call::null_handle, vector<Expr>(), Call::Intrinsic);
                Expr val = Call::make(Handle(), Call::create_buffer_t, args, Call::Intrinsic);

                stmt = LetStmt::make(op->name, val, op->body);
            }
        }
    }

    void visit(const IfThenElse *op) {
        if (device_api != DeviceAPI::Host) {
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
            merged_state.dev_current  = then_state.dev_current && else_state.dev_current &&
                                        then_state.current_device == else_state.current_device;

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
        if (device_api != DeviceAPI::Host) {
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

        if (different_device_api(device_api, op->device_api, target)) {
            debug(4) << "Switching from device_api " << static_cast<int>(device_api) << " to op->device_api " <<
                static_cast<int>(op->device_api) << " in for loop " << op->name <<"\n";
            DeviceAPI old_device_api = device_api;
            device_api = fixup_device_api(op->device_api, target);
            if (device_api == DeviceAPI::Parent) {
                device_api = old_device_api;
            }
            internal_assert(device_api != DeviceAPI::Parent);
            IRMutator::visit(op);
            device_api = old_device_api;
        } else {
            IRMutator::visit(op);
        }
        loop_level = old_loop_level;
    }

public:
    InjectBufferCopies(const set<string> &i, const Target &t) : loop_level(""), buffers_to_track(i), target(t), device_api(DeviceAPI::Host) {}

};

Stmt inject_host_dev_buffer_copies(Stmt s, const Target &t) {
    FindBuffersToTrack f(t);
    s.accept(&f);

    debug(4) << "Tracking host <-> dev copies for the following buffers:\n";
    for (set<string>::iterator iter = f.buffers_to_track.begin();
         iter != f.buffers_to_track.end(); ++iter) {
        debug(4) << *iter << "\n";
    }

    return InjectBufferCopies(f.buffers_to_track, t).mutate(s);
}

class InjectDevFrees : public IRMutator {
    using IRMutator::visit;

    // We assume buffers are uniquely named at this point
    set<string> needs_freeing;

    void visit(const Call *op) {
        if (op->name == "halide_copy_to_device" || op->name == "halide_device_malloc") {
            internal_assert(op->args.size() == 2);
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
            Expr free_call = Call::make(Int(32), "halide_device_free", vec(buf), Call::Extern);
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
