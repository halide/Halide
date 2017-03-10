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

Stmt call_extern_and_assert(const string& name, const vector<Expr>& args) {
    Expr call = Call::make(Int(32), name, args, Call::Extern);
    string call_result_name = unique_name(name + "_result");
    Expr call_result_var = Variable::make(Int(32), call_result_name);
    return LetStmt::make(call_result_name, call,
                         AssertStmt::make(EQ::make(call_result_var, 0), call_result_var));
}

namespace {

DeviceAPI fixup_device_api(DeviceAPI device_api, const Target &target) {
    if (device_api == DeviceAPI::Default_GPU) {
        if (target.has_feature(Target::Metal)) {
            return DeviceAPI::Metal;
        } else if (target.has_feature(Target::OpenCL)) {
            return DeviceAPI::OpenCL;
        } else if (target.has_feature(Target::CUDA)) {
            return DeviceAPI::CUDA;
        } else if (target.has_feature(Target::OpenGLCompute)) {
            return DeviceAPI::OpenGLCompute;
        } else {
            user_error << "Schedule uses Default_GPU without a valid GPU (Metal, OpenCL CUDA, or OpenGLCompute) specified in target.\n";
        }
    }
    return device_api;
}

bool different_device_api(DeviceAPI device_api, DeviceAPI stmt_api, const Target &target) {
    device_api = fixup_device_api(device_api, target);
    stmt_api = fixup_device_api(stmt_api, target);
    return (stmt_api != DeviceAPI::None) && (device_api != stmt_api);
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
        auto it = internal.insert({ op->name, device_api });
        IRVisitor::visit(op);
        internal.erase(it.first);
    }

    void visit(const For *op) {
      if (different_device_api(device_api, op->device_api, target)) {
            debug(2) << "Buffers to track: switching from " << static_cast<int>(device_api) <<
                " to " << static_cast<int>(op->device_api) << " for loop " << op->name << "\n";
            DeviceAPI old_device_api = device_api;
            device_api = fixup_device_api(op->device_api, target);
            if (device_api == DeviceAPI::None) {
                device_api = old_device_api;
            }
            internal_assert(device_api != DeviceAPI::None);
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
            c && c->name == Call::buffer_init) {
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
        if (op->type.is_handle() && ends_with(op->name, ".buffer")) {
            buffers_to_track.insert(op->name.substr(0, op->name.size() - 7));
        }
    }

public:
    set<string> buffers_to_track;
    FindBuffersToTrack(const Target &t) : target(t), device_api(DeviceAPI::Host) {}
};

class InjectBufferCopies : public IRMutator {
    using IRMutator::visit;

    // BufferInfo tracks the state of a given buffer over an IR scope.
    // Generally the scope is a ProducerConsumer. The data herein is mutable.
    struct BufferInfo {
        bool host_touched,  // Is there definitely a host-side allocation?
            dev_touched,    // Is there definitely a device-side allocation?
            host_current,   // Is the data known to be up-to-date on the host?
            dev_current,    // Is the data known to be up-to-date on the device?
            internal,       // Did Halide allocate this buffer?
            dev_allocated,  // Has the buffer been allocated?
            on_single_device; // Used to track whether zero copy is allowed for an internal allocation.

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
                       on_single_device(false),
                       device_first_touched(DeviceAPI::None), // Meaningless initial value
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
          case DeviceAPI::Metal:
            interface_name = "halide_metal_device_interface";
            break;
          case DeviceAPI::GLSL:
            interface_name = "halide_opengl_device_interface";
            break;
          case DeviceAPI::OpenGLCompute:
            interface_name = "halide_openglcompute_device_interface";
            break;
          case DeviceAPI::Hexagon:
            interface_name = "halide_hexagon_device_interface";
            break;
          default:
            internal_error << "Bad DeviceAPI " << static_cast<int>(device_api) << "\n";
            break;
        }
        std::vector<Expr> no_args;
        return Call::make(type_of<const char *>(), interface_name, no_args, Call::Extern);
    }

    Stmt make_dev_malloc(string buf_name, DeviceAPI target_device_api, bool is_device_and_host) {
        Expr buf = Variable::make(type_of<struct buffer_t *>(), buf_name + ".buffer");
        Expr device_interface = make_device_interface_call(target_device_api);
        Stmt device_malloc = call_extern_and_assert(is_device_and_host ? "halide_device_and_host_malloc"
                                                                       : "halide_device_malloc",
                                                    {buf, device_interface});
        Stmt destructor =
            Evaluate::make(Call::make(Int(32), Call::register_destructor,
                                      {Expr(is_device_and_host ? "halide_device_and_host_free_as_destructor"
                                                         : "halide_device_free_as_destructor"), buf}, Call::Intrinsic));
        return Block::make(device_malloc, destructor);
    }

    enum CopyDirection {
        NoCopy,
        ToHost,
        ToDevice
    };

    Stmt make_buffer_copy(CopyDirection direction, string buf_name, DeviceAPI target_device_api) {
        internal_assert(direction == ToHost || direction == ToDevice) << "make_buffer_copy caller logic error.\n";
        std::vector<Expr> args;
        Expr buffer = Variable::make(type_of<struct buffer_t *>(), buf_name + ".buffer");
        args.push_back(buffer);
        if (direction == ToDevice) {
            args.push_back(make_device_interface_call(target_device_api));
        }

        std::string suffix = (direction == ToDevice) ? "device" : "host";
        return call_extern_and_assert("halide_copy_to_" + suffix, args);
    }

    // Prepend code to the statement that copies everything marked as
    // a dev read to host or dev.
    Stmt do_copies(Stmt s) {
        internal_assert(s.defined());

        // Cannot do any sort of buffer copying in device code yet.
        if (device_api != DeviceAPI::Host) {
            return s;
        }

        debug(4) << "At loop level " << loop_level << "\n";

        for (pair<const string, BufferInfo> &i : state) {
            CopyDirection direction = NoCopy;
            BufferInfo &buf = i.second;
            if (buf.loop_level != loop_level) {
                continue;
            }

            debug(4) << "do_copies for " << i.first << "\n"
                     << "Host current: " << buf.host_current << " Device current: " << buf.dev_current << "\n"
                     << "Host touched: " << buf.host_touched << " Device touched: " << buf.dev_touched << "\n"
                     << "Internal: " << buf.internal << " Device touching first: "
                     << static_cast<int>(buf.device_first_touched) << "\n"
                     << "Current device: " << static_cast<int>(buf.current_device) << "\n";
            DeviceAPI touching_device = DeviceAPI::None;
            bool host_read = false;
            size_t non_host_devices_reading_count = 0;
            DeviceAPI reading_device = DeviceAPI::None;
            for (DeviceAPI dev : buf.devices_reading) {
                debug(4) << "Device " << static_cast<int>(dev) << " read buffer\n";
                if (dev != DeviceAPI::Host) {
                    non_host_devices_reading_count++;
                    reading_device = dev;
                    touching_device = dev;
                } else {
                    host_read = true;
                }
            }
            bool host_wrote = false;
            size_t non_host_devices_writing_count = 0;
            DeviceAPI writing_device = DeviceAPI::None;
            for (DeviceAPI dev : buf.devices_writing) {
                debug(4) << "Device " << static_cast<int>(dev) << " wrote buffer\n";
                if (dev != DeviceAPI::Host) {
                    non_host_devices_writing_count++;
                    writing_device = dev;
                    touching_device = dev;
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
                // Needs a copy to host. It's OK if the device is also
                // reading it if it's not being written to on the
                // host.
                internal_assert(!device_wrote && !(host_wrote && device_read));
                direction = ToHost;
                buf.host_current = true;
                buf.dev_current = buf.dev_current && !host_wrote;
                debug(4) << "Needs copy to host\n";
            } else if (host_wrote) {
                // Invalidate the device version, if any.
                internal_assert(!device_read && !device_wrote);
                buf.dev_current = false;
                debug(4) << "Invalidating dev_current\n";
            }

            if ((device_read || device_wrote) &&
                ((!buf.dev_current || (buf.current_device != touching_device)) ||
                 (!buf.internal || buf.host_touched))) {
                // Needs a copy-to-dev. It's OK if the host is also
                // reading it if it's not being written to on the
                // device.
                internal_assert(!host_wrote && !(device_wrote && host_read));
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

            Expr buffer = Variable::make(type_of<struct buffer_t *>(), i.first + ".buffer");

            if (host_wrote) {
                debug(4) << "Setting host dirty for " << i.first << "\n";
                // If we just invalidated the dev pointer, we need to set the host dirty bit.
                Expr set_host_dirty = Call::make(Int(32), Call::buffer_set_host_dirty,
                                                 {buffer, const_true()}, Call::Extern);
                s = Block::make(s, Evaluate::make(set_host_dirty));
            }

            if (device_wrote) {
                // If we just invalidated the host pointer, we need to set the dev dirty bit.
                Expr set_dev_dirty = Call::make(Int(32), Call::buffer_set_dev_dirty,
                                                {buffer, const_true()}, Call::Extern);
                s = Block::make(s, Evaluate::make(set_dev_dirty));
            }

            // Clear the pending action bits.
            buf.devices_reading.clear();
            buf.devices_writing.clear();

            if (direction != NoCopy && touching_device != DeviceAPI::Host) {
                internal_assert(s.defined());
                s = Block::make(make_buffer_copy(direction, i.first, touching_device), s);
            }

            buf.on_single_device =
                (non_host_devices_reading_count <= 1) &&
                (non_host_devices_writing_count <= 1) &&
                buf.device_first_touched != DeviceAPI::None;

            // Inject a dev_malloc if needed.
            if (!buf.dev_allocated &&
                buf.device_first_touched != DeviceAPI::Host &&
                buf.device_first_touched != DeviceAPI::None) {
                debug(4) << "Injecting device malloc for " << i.first << " on " <<
                    static_cast<int>(buf.device_first_touched) << "\n";
                Stmt dev_malloc = make_dev_malloc(i.first, buf.device_first_touched, false);
                internal_assert(s.defined());
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
        if (op->is_intrinsic(Call::address_of)) {
            // We're after storage flattening, so the sole arg should be a load.
            internal_assert(op->args.size() == 1);
            const Load *l = op->args[0].as<Load>();
            internal_assert(l);
            Expr new_index = mutate(l->index);
            if (l->index.same_as(new_index)) {
                expr = op;
            } else {
                Expr new_load = Load::make(l->type, l->name, new_index, Buffer<>(),
                                           Parameter(), const_true(l->type.lanes()));
                expr = Call::make(op->type, op->name, {new_load}, Call::Intrinsic);
            }
        } else if (op->is_intrinsic(Call::image_load)) {
            // counts as a device read
            internal_assert(device_api == DeviceAPI::GLSL);
            internal_assert(op->args.size() >= 2);
            const Variable *buffer_var = op->args[1].as<Variable>();
            internal_assert(buffer_var && ends_with(buffer_var->name, ".buffer"));
            string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
            debug(4) << "Adding image read via image_load for " << buffer_var->name << "\n";
            state[buf_name].devices_reading.insert(device_api);
            IRMutator::visit(op);
        } else if (op->is_intrinsic(Call::image_store)) {
            // counts as a device store
            internal_assert(device_api == DeviceAPI::GLSL);
            internal_assert(op->args.size() >= 2);
            const Variable *buffer_var = op->args[1].as<Variable>();
            internal_assert(buffer_var && ends_with(buffer_var->name, ".buffer"));
            string buf_name = buffer_var->name.substr(0, buffer_var->name.size() - 7);
            debug(4) << "Adding image write via image_store for " << buffer_var->name << "\n";
            state[buf_name].devices_writing.insert(device_api);
            IRMutator::visit(op);
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const ProducerConsumer *op) {
        if (device_api != DeviceAPI::Host) {
            IRMutator::visit(op);
            return;
        }

        Stmt body = mutate(op->body);
        body = do_copies(body);
        if (body.same_as(op->body)) {
            stmt = op;
        } else {
            stmt = ProducerConsumer::make(op->name, op->is_producer, body);
        }

        if (op->is_producer) {
            bool is_output = true;
            // The buffers associated with this pipeline should get this loop level
            for (pair<const string, BufferInfo> &i : state) {
                const string &buf_name = i.first;
                if (buf_name == op->name || starts_with(buf_name, op->name + ".")) {
                    i.second.loop_level = loop_level;
                    is_output = false;
               }
            }

            // Need to make all output buffers touched on device valid
            if (is_output) {
                for (pair<const string, BufferInfo> &i : state) {
                    const string &buf_name = i.first;
                    if ((buf_name == op->name || starts_with(buf_name, op->name + ".")) &&
                        i.second.dev_touched && i.second.current_device != DeviceAPI::Host) {
                        // Inject a device copy, which will make sure the device buffer is allocated
                        // on the right device and that the host dirty bit is false so the device
                        // can write. (Which will involve copying to the device if host was dirty
                        // for the passed in buffer.)
                        debug(4) << "Injecting device copy for output " << buf_name << " on " <<
                            static_cast<int>(i.second.current_device) << "\n";
                        stmt = Block::make(make_buffer_copy(ToDevice, buf_name, i.second.current_device), stmt);
                    }
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
        {
          BufferInfo &buf_init(state[buf_name]);

          buf_init.internal = true;
          buf_init.dev_allocated = false;
        }

        IRMutator::visit(op);
        op = stmt.as<Allocate>();
        internal_assert(op);

        BufferInfo &buf_info(state[buf_name]);

        if (buf_info.dev_touched) {
            user_assert(op->extents.size() <= 4)
                << "Buffer " << op->name
                << " cannot be used on the GPU, because it has more than four dimensions.\n";
        }

        // If this buffer is only ever touched on gpu, nuke the host-side allocation.
        if (!buf_info.host_touched) {
            debug(4) << "Eliding host alloc for " << op->name << "\n";
            stmt = Allocate::make(op->name, op->type, op->extents, const_false(), op->body);
        } else if (buf_info.on_single_device &&
                   buf_info.dev_touched) {
            debug(4) << "Making combined host/device alloc for " << op->name << "\n";
            Stmt inner_body = op->body;
            std::vector<const LetStmt *> body_lets;
            // Find LetStmt setting up buffer Variable for op->name as it will
            // now go outside.
            const LetStmt *buffer_init_let = nullptr;
            while (const LetStmt *inner_let = inner_body.as<LetStmt>()) {
                inner_body = inner_let->body;
                if (inner_let->name == op->name + ".buffer") {
                    buffer_init_let = inner_let;
                    break;
                }
                body_lets.push_back(inner_let);
            }

            Stmt combined_malloc = make_dev_malloc(op->name, buf_info.device_first_touched, true);

            // Create a new Allocation scope inside the buffer
            // creation, use the host pointer as the allocation and
            // set the destructor to a nop.  (The Allocation
            // destructor cannot be used as it takes the host pointer
            // as it's argument and we need the complete buffer_t.  it
            // would be possible to keep a map between host pointers
            // and dev ones to facilitate this, but it seems better to
            // just register a destructor with the buffer creation.)
            inner_body = Allocate::make(op->name, op->type, op->extents, op->condition, inner_body,
                                        Call::make(Handle(), Call::buffer_get_host,
                                                   { Variable::make(type_of<struct buffer_t *>(), op->name + ".buffer") },
                                                   Call::Extern),
                                        "halide_device_host_nop_free"); // TODO: really should not have to introduce this routine to get a nop free
            // Wrap combined malloc around Allocate.
            inner_body = Block::make(combined_malloc, inner_body);

            // Rewrite original buffer_init call and wrap it around the combined malloc.
            std::vector<Expr> create_buffer_args;
            internal_assert(buffer_init_let) << "Could not find definition of " << op->name << ".buffer\n";

            const Call *possible_create_buffer = buffer_init_let->value.as<Call>();
            if (possible_create_buffer != nullptr &&
                possible_create_buffer->name == Call::buffer_init) {
                // Use the same args, but with a zero host pointer.
                create_buffer_args = possible_create_buffer->args;
                create_buffer_args[1] = make_zero(Handle());
            }

            Expr buf = Call::make(type_of<struct buffer_t *>(), Call::buffer_init,
                                  create_buffer_args, Call::Extern);
            stmt = LetStmt::make(op->name + ".buffer", buf, inner_body);

            // Rebuild any wrapped lets outside the one for the _halide_buffer_init
            for (size_t i = body_lets.size(); i > 0; i--) {
                stmt = LetStmt::make(body_lets[i - 1]->name, body_lets[i - 1]->value, stmt);
            }
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

            Expr value = op->value;
            if (!state[buf_name].host_touched) {
                // Use null as a host pointer if there's no host allocation
                const Call *create_buffer = op->value.as<Call>();
                internal_assert(create_buffer && create_buffer->name == Call::buffer_init);
                vector<Expr> args = create_buffer->args;
                args[1] = make_zero(Handle());
                value = Call::make(type_of<struct buffer_t *>(), Call::buffer_init, args, Call::Extern);
                stmt = LetStmt::make(op->name, value, op->body);
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
        if (else_case.defined()) {
            else_case = do_copies(else_case);
        }

        for (const pair<string, BufferInfo> &i : copy) {
            const string &buf_name = i.first;

            const BufferInfo &then_state = i.second;
            const BufferInfo &else_state = state[buf_name];
            BufferInfo merged_state;

            internal_assert(then_state.loop_level == else_state.loop_level)
                << "then_state and else_state should have the same loop level for " << buf_name;

            merged_state.loop_level = then_state.loop_level;
            merged_state.host_touched   = then_state.host_touched || else_state.host_touched;
            merged_state.dev_touched    = then_state.dev_touched || else_state.dev_touched;
            merged_state.host_current = then_state.host_current && else_state.host_current;
            merged_state.dev_current  = then_state.dev_current && else_state.dev_current &&
                                        then_state.current_device == else_state.current_device;

            if (then_state.device_first_touched == else_state.device_first_touched) {
                merged_state.on_single_device = then_state.on_single_device && else_state.on_single_device;
                merged_state.device_first_touched = then_state.device_first_touched;
            } else {
                merged_state.on_single_device = false;
                merged_state.device_first_touched = DeviceAPI::None;
            }

            // Merge the device read/write set of then and else case
            merged_state.devices_reading = then_state.devices_reading;
            merged_state.devices_reading.insert(else_state.devices_reading.begin(),
                                                else_state.devices_reading.end());
            merged_state.devices_writing = then_state.devices_writing;
            merged_state.devices_writing.insert(else_state.devices_writing.begin(),
                                                else_state.devices_writing.end());

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
            if (device_api == DeviceAPI::None) {
                device_api = old_device_api;
            }
            internal_assert(device_api != DeviceAPI::None);
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

}  // namespace

Stmt inject_host_dev_buffer_copies(Stmt s, const Target &t) {
    FindBuffersToTrack f(t);
    s.accept(&f);

    debug(4) << "Tracking host <-> dev copies for the following buffers:\n";
    for (const std::string &i : f.buffers_to_track) {
        debug(4) << i << "\n";
    }

    return InjectBufferCopies(f.buffers_to_track, t).mutate(s);
}

}
}
