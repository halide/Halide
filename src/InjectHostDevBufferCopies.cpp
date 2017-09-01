#include "InjectHostDevBufferCopies.h"

#include "CodeGen_GPU_Dev.h"
#include "Debug.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"

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
    return Call::make(type_of<const char *>(), interface_name, {}, Call::Extern);
}


class FindBufferUsage : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Variable *op) {
        if (is_buffer_var(op)) {
            // Passing the buffer variable out of Halide counts
            // as a read/write.
            devices_touched.insert(current_device_api);
            devices_writing.insert(current_device_api);
        }
    }

    void visit(const Load *op) {
        IRVisitor::visit(op);
        if (op->name == buffer) {
            devices_touched.insert(current_device_api);
        }
    }

    void visit(const Store *op) {
        IRVisitor::visit(op);
        if (op->name == buffer) {
            devices_touched.insert(current_device_api);
            devices_writing.insert(current_device_api);
        }
    }

    bool is_buffer_var(Expr e) {
        const Variable *var = e.as<Variable>();
        return var && (var->name == buffer + ".buffer");
    }

    void visit(const Call *op) {
        if (op->is_intrinsic(Call::image_load)) {
            internal_assert(op->args.size() >= 1);
            if (is_buffer_var(op->args[1])) {
                devices_touched.insert(current_device_api);
            }
            for (size_t i = 0; i < op->args.size(); i++) {
                if (i == 1) continue;
                op->args[i].accept(this);
            }
        } else if (op->is_intrinsic(Call::image_store)) {
            internal_assert(op->args.size() >= 1);
            if (is_buffer_var(op->args[1])) {
                devices_touched.insert(current_device_api);
                devices_writing.insert(current_device_api);
            }
            for (size_t i = 0; i < op->args.size(); i++) {
                if (i == 1) continue;
                op->args[i].accept(this);
            }
        } else if (op->call_type == Call::Extern && op->func.defined()) {
            // This is a call to an extern stage
            Function f(op->func);

            internal_assert((f.extern_arguments().size() + f.outputs()) == op->args.size()) <<
                "Mismatch between args size and extern_arguments size in call to " << op->name << "\n";

            // Check each buffer arg
            for (size_t i = 0; i < op->args.size(); i++) {
                if (is_buffer_var(op->args[i])) {
                    DeviceAPI extern_device_api = f.extern_function_device_api();
                    touched_by_extern = true;
                    if (i >= f.extern_arguments().size()) {
                        // An output. The extern stage is responsible
                        // for dealing with any device transitions for
                        // inputs.
                        devices_touched.insert(extern_device_api);
                        devices_writing.insert(extern_device_api);
                    }
                } else {
                    op->args[i].accept(this);
                }
            }
        } else {
            IRVisitor::visit(op);
        }
    }

    void visit(const For *op) {
        internal_assert(op->device_api != DeviceAPI::Default_GPU)
            << "A GPU API should have been selected by this stage in lowering\n";
        DeviceAPI old = current_device_api;
        if (op->device_api != DeviceAPI::None) {
            current_device_api = op->device_api;
        }
        IRVisitor::visit(op);
        current_device_api = old;
    }

    string buffer;
    DeviceAPI current_device_api;
public:
    std::set<DeviceAPI> devices_writing, devices_touched;
    // Any buffer passed to an extern stage may have had its dirty
    // bits and device allocation messed with.
    bool touched_by_extern = false;

    FindBufferUsage(const std::string &buf, DeviceAPI d) : buffer(buf), current_device_api(d) {}
};

// Inject the device copies, mallocs, and dirty flag setting for a
// single allocation. Sticks to the same loop level as the original
// allocation and treats the stmt as a serial sequence of leaf
// stmts. We walk this sequence of leaves, tracking what we know about
// the buffer as we go, sniffing usage within each leaf using
// FindBufferUsage, and injecting device buffer logic as needed.
class InjectBufferCopiesForSingleBuffer : public IRMutator {
    using IRMutator::visit;

    // The buffer being managed
    string buffer;

    enum FlagState {
        Unknown,
        False,
        True
    };

    struct State {
        // What do we know about the dirty flags and the existence of a device allocation?
        FlagState device_dirty = Unknown, host_dirty = Unknown, device_allocation_exists = Unknown;

        // If it exists on a known device API, which device does it exist
        // on? Meaningless if device_allocation_exists is not True.
        DeviceAPI current_device = DeviceAPI::None;

        void union_with(const State &other) {
            if (device_dirty != other.device_dirty) {
                device_dirty = Unknown;
            }
            if (host_dirty != other.host_dirty) {
                host_dirty = Unknown;
            }
            if (device_allocation_exists != other.device_allocation_exists ||
                other.current_device != current_device) {
                device_allocation_exists = Unknown;
                current_device = DeviceAPI::None;
            }
        }
    } state;

    Expr buffer_var() {
        return Variable::make(type_of<struct halide_buffer_t *>(), buffer + ".buffer");
    }

    Stmt make_device_malloc(DeviceAPI target_device_api) {
        Expr device_interface = make_device_interface_call(target_device_api);
        Stmt device_malloc = call_extern_and_assert("halide_device_malloc",
                                                    {buffer_var(), device_interface});
        return device_malloc;
    }

    Stmt make_copy_to_host() {
        return call_extern_and_assert("halide_copy_to_host", {buffer_var()});
    }

    Stmt make_copy_to_device(DeviceAPI target_device_api) {
        Expr device_interface = make_device_interface_call(target_device_api);
        return call_extern_and_assert("halide_copy_to_device", {buffer_var(), device_interface});
    }

    Stmt make_host_dirty() {
        return Evaluate::make(Call::make(Int(32), Call::buffer_set_host_dirty,
                                         {buffer_var(), const_true()}, Call::Extern));
    }

    Stmt make_device_dirty() {
        return Evaluate::make(Call::make(Int(32), Call::buffer_set_device_dirty,
                                         {buffer_var(), const_true()}, Call::Extern));
    }

    Stmt do_copies(Stmt s) {
        // Sniff what happens to the buffer inside the stmt
        FindBufferUsage finder(buffer, DeviceAPI::Host);
        s.accept(&finder);

        // Insert any appropriate copies/allocations before, and set
        // dirty flags after. Do not recurse into the stmt.

        // First figure out what happened
        bool touched_on_host = finder.devices_touched.count(DeviceAPI::Host);
        bool touched_on_device = finder.devices_touched.size() > (touched_on_host ? 1 : 0);
        bool written_on_host = finder.devices_writing.count(DeviceAPI::Host);
        bool written_on_device = finder.devices_writing.size() > (touched_on_host ? 1 : 0);

        DeviceAPI touching_device = DeviceAPI::None;
        for (DeviceAPI d : finder.devices_touched) {
            if (d == DeviceAPI::Host) continue;
            internal_assert(touching_device == DeviceAPI::None)
                << "Buffer " << buffer << " was touched on multiple devices within a single leaf Stmt!\n";
            touching_device = d;
        }

        // Then figure out what to do
        bool needs_device_malloc = (written_on_device &&
                                    (state.device_allocation_exists != True));

        // TODO: If only written on device, and entirely clobbered on
        // device, a copy-to-device is not actually necessary.
        bool needs_copy_to_device = (touched_on_device &&
                                     ((state.host_dirty != False) ||
                                      (state.device_allocation_exists != False &&
                                       state.current_device != touching_device)));

        if (needs_copy_to_device) {
            // halide_copy_to_device already does a halide_device_malloc if necessary.
            needs_device_malloc = false;
        }

        bool needs_copy_to_host = (touched_on_host &&
                                   (state.device_dirty != False));

        bool needs_host_dirty = (written_on_host &&
                                 (state.host_dirty != True));

        bool needs_device_dirty = (written_on_device &&
                                   (state.device_dirty != True));

        // Then do it, updating what we know about the buffer
        if (needs_copy_to_host) {
            s = Block::make(make_copy_to_host(), s);
            state.device_dirty = False;
        }

        if (needs_copy_to_device) {
            s = Block::make(make_copy_to_device(touching_device), s);
            state.host_dirty = False;
            state.device_allocation_exists = True;
            state.current_device = touching_device;
        }

        if (needs_host_dirty) {
            s = Block::make(s, make_host_dirty());
            state.host_dirty = True;
        }

        if (needs_device_dirty) {
            s = Block::make(s, make_device_dirty());
            state.device_dirty = True;
        }

        if (needs_device_malloc) {
            s = Block::make(make_device_malloc(touching_device), s);
            state.device_allocation_exists = True;
            state.current_device = touching_device;
        }

        if (finder.touched_by_extern) {
            // This buffer was passed to an extern stage. Unless we
            // explicitly marked it after the stmt ran, we no longer
            // know the state of the dirty bits.
            if (!needs_host_dirty) {
                state.host_dirty = Unknown;
            }
            if (!needs_device_dirty) {
                state.device_dirty = Unknown;
            }
            // Also, the extern stage may have gifted a host
            // allocation, or flipped the buffer to another device.
            state.device_allocation_exists = Unknown;
            state.current_device = DeviceAPI::None;
        }

        if (!finder.devices_touched.empty() ||
            finder.touched_by_extern) {
            last_use = s;
        }

        return s;
    }

    // We want to break things down into a serial sequence of leaf
    // stmts, and possibly do copies and update state around each
    // leaf.

    void visit(const For *op) {
        // All copies happen at the same loop level as the allocation
        stmt = do_copies(op);
    }

    void visit(const Evaluate *op) {
        stmt = do_copies(op);
    }

    void visit(const LetStmt *op) {
        // If op->value uses the buffer, we need to treat this as a
        // single leaf. Otherwise we can recurse.
        FindBufferUsage finder(buffer, DeviceAPI::Host);
        op->value.accept(&finder);
        if (finder.devices_touched.empty() &&
            !finder.touched_by_extern) {
            IRMutator::visit(op);
        } else {
            stmt = do_copies(op);
        }
    }

    void visit(const AssertStmt *op) {
        stmt = do_copies(op);
    }

    // Check if a stmt has any for loops (and hence possible device
    // transitions).
    class HasLoops : public IRVisitor {
        using IRVisitor::visit;
        void visit(const For *op) {
            result = true;
        }
    public:
        bool result = false;
    };

    void visit(const Block *op) {
        // If both sides of the block have no loops (and hence no
        // device transitions), treat it as a single leaf. This stops
        // host dirties from getting in between blocks of store stmts
        // that could be interleaved.
        HasLoops loops;
        op->accept(&loops);
        if (loops.result) {
            IRMutator::visit(op);
        } else {
            stmt = do_copies(op);
        }
    }

    void visit(const Store *op) {
        stmt = do_copies(op);
    }

    void visit(const IfThenElse *op) {
        State old = state;
        Stmt then_case = mutate(op->then_case);
        State then_state = state;
        state = old;
        Stmt else_case = mutate(op->else_case);
        state.union_with(then_state);
        stmt = IfThenElse::make(op->condition, then_case, else_case);
    }

public:
    InjectBufferCopiesForSingleBuffer(const std::string &b, bool is_external) : buffer(b) {
        if (is_external) {
            // The state of the buffer is totally unknown, which is
            // the default constructor for this->state
        } else {
            // This is a fresh allocation
            state.device_allocation_exists = False;
            state.device_dirty = False;
            state.host_dirty = False;
            state.current_device = DeviceAPI::None;
        }
    }

    Stmt last_use;
};

// Inject the buffer-handling logic for all internal
// allocations. Inputs and outputs are handled below.
class InjectBufferCopies : public IRMutator {
    using IRMutator::visit;

    // Inject the registration of a device destructor just after the
    // .buffer symbol is defined (which is safely before the first
    // device_malloc).
    class InjectDeviceDestructor : public IRMutator {
        using IRMutator::visit;

        void visit(const LetStmt *op) {
            if (op->name == buffer) {
                Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), buffer);
                Stmt destructor =
                    Evaluate::make(Call::make(Int(32), Call::register_destructor,
                                              {Expr("halide_device_free_as_destructor"), buf}, Call::Intrinsic));
                Stmt body = Block::make(destructor, op->body);
                stmt = LetStmt::make(op->name, op->value, body);
            } else {
                IRMutator::visit(op);
            }
        }

        string buffer;
    public:
        InjectDeviceDestructor(string b) : buffer(b) {}
    };

    // Find the let stmt that defines the .buffer and insert inside of
    // it a combined host/dev allocation, a destructor registration,
    // and an Allocate node that takes its host field from the
    // .buffer.
    class InjectCombinedAllocation : public IRMutator {

        using IRMutator::visit;

        void visit(const LetStmt *op) {
            if (op->name == buffer + ".buffer") {
                Expr buf = Variable::make(type_of<struct halide_buffer_t *>(), buffer + ".buffer");
                Stmt body = op->body;

                // The allocate node is innermost
                Expr host = Call::make(Handle(), Call::buffer_get_host, {buf}, Call::Extern);
                body = Allocate::make(buffer, type, extents, condition, body,
                                      host, "halide_device_host_nop_free");

                // Then the destructor
                Stmt destructor =
                    Evaluate::make(Call::make(Int(32), Call::register_destructor,
                                              {Expr("halide_device_and_host_free_as_destructor"), buf},
                                              Call::Intrinsic));
                body = Block::make(destructor, body);

                // Then the device_and_host malloc
                Expr device_interface = make_device_interface_call(device_api);
                Stmt device_malloc = call_extern_and_assert("halide_device_and_host_malloc",
                                                            {buf, device_interface});
                if (!is_one(condition)) {
                    device_malloc = IfThenElse::make(condition, device_malloc);
                }
                body = Block::make(device_malloc, body);

                // In the value, we want to use null for the initial value of the host field.
                Expr value = substitute(buffer, reinterpret(Handle(), make_zero(UInt(64))), op->value);

                // Rewrap the letstmt
                stmt = LetStmt::make(op->name, value, body);
            } else {
                IRMutator::visit(op);
            }
        }

        string buffer;
        Type type;
        vector<Expr> extents;
        Expr condition;
        DeviceAPI device_api;
    public:
        InjectCombinedAllocation(string b, Type t, vector<Expr> e, Expr c, DeviceAPI d) :
            buffer(b), type(t), extents(e), condition(c), device_api(d) {}
    };

    class FreeAfterLastUse : public IRMutator {
        Stmt last_use;
        Stmt free_stmt;
    public:
        using IRMutator::mutate;

        Stmt mutate(const Stmt &s) {
            if (s.same_as(last_use)) {
                return Block::make(last_use, free_stmt);
            } else {
                return IRMutator::mutate(s);
            }
        }

        FreeAfterLastUse(Stmt s, Stmt f) : last_use(s), free_stmt(f) {}
    };

    void visit(const Allocate *op) {
        FindBufferUsage finder(op->name, DeviceAPI::Host);
        op->body.accept(&finder);

        bool touched_on_host = finder.devices_touched.count(DeviceAPI::Host);
        bool touched_on_device = finder.devices_touched.size() > (touched_on_host ? 1 : 0);

        if (!touched_on_device && !finder.touched_by_extern) {
            // Boring.
            IRMutator::visit(op);
            return;
        }

        Stmt body = mutate(op->body);

        InjectBufferCopiesForSingleBuffer injector(op->name, false);
        body = injector.mutate(body);

        string buffer_name = op->name + ".buffer";
        Expr buffer = Variable::make(Handle(), buffer_name);

        // Device what type of allocation to make.

        if (touched_on_host && finder.devices_touched.size() == 2) {
            // Touched on a single device and the host. Use a combined allocation.
            DeviceAPI touching_device = DeviceAPI::None;
            for (DeviceAPI d : finder.devices_touched) {
                if (d != DeviceAPI::Host) {
                    touching_device = d;
                }
            }

            // Make a device_and_host_free stmt
            if (injector.last_use.defined()) {
                Stmt device_free = call_extern_and_assert("halide_device_and_host_free", {buffer});
                body = FreeAfterLastUse(injector.last_use, device_free).mutate(body);
            }

            stmt = InjectCombinedAllocation(op->name, op->type, op->extents,
                                            op->condition, touching_device).mutate(body);
        } else {
            // Only touched on host but passed to an extern stage, or
            // only touched on device, or touched on multiple
            // devices. Do separate device and host allocations.

            // Add a device destructor
            body = InjectDeviceDestructor(buffer_name).mutate(body);

            // Make a device_free stmt
            if (injector.last_use.defined()) {
                Stmt device_free = call_extern_and_assert("halide_device_free", {buffer});
                body = FreeAfterLastUse(injector.last_use, device_free).mutate(body);
            }

            Expr condition = op->condition;
            if (finder.devices_touched.size() == 1 &&
                !touched_on_host &&
                !finder.touched_by_extern) {
                // Only touched on one device, and never passed to an extern stage.
                condition = const_false();
                // There's no host allocation, so substitute any
                // references to it (e.g. the one in the make_buffer
                // call) with NULL.
                body = substitute(op->name, reinterpret(Handle(), make_zero(UInt(64))), body);
            }

            stmt = Allocate::make(op->name, op->type, op->extents, condition, body, op->new_expr, op->free_function);
        }
    }

    void visit(const For *op) {
        if (op->device_api != DeviceAPI::Host &&
            op->device_api != DeviceAPI::None) {
            // Don't enter device loops
            stmt = op;
        } else {
            IRMutator::visit(op);
        }
    }
};

// Find the site in the IR where we want to inject the copies/dirty
// flags for the inputs and outputs. It's the innermost IR node that
// contains all ProducerConsumer nodes. Often this is the outermost
// ProducerConsumer node. Sometimes it's a Block containing a pair of
// them.
class FindOutermostProduce : public IRVisitor {
    using IRVisitor::visit;

    void visit(const Block *op) {
        op->first.accept(this);
        if (result.defined()) {
            result = op;
        } else {
            op->rest.accept(this);
        }
    }

    void visit(const ProducerConsumer *op) {
        result = op;
    }

public:
    Stmt result;
};

// Inject the buffer handling code for the inputs and outputs at the
// appropriate site.
class InjectBufferCopiesForInputsAndOutputs : public IRMutator {
    Stmt site;

    // Find all references to external buffers.
    class FindInputsAndOutputs : public IRVisitor {
        using IRVisitor::visit;

        void include(const Parameter &p) {
            if (p.defined()) {
                result.insert(p.name());
            }
        }

        void include(const Buffer<> &b) {
            if (b.defined()) {
                result.insert(b.name());
            }
        }

        void visit(const Variable *op) {
            include(op->param);
            include(op->image);
        }

        void visit(const Load *op) {
            include(op->param);
            include(op->image);
            IRVisitor::visit(op);
        }

        void visit(const Store *op) {
            include(op->param);
            IRVisitor::visit(op);
        }

    public:
        set<string> result;
    };

public:
    using IRMutator::mutate;

    Stmt mutate(const Stmt &s) {
        if (s.same_as(site)) {
            FindInputsAndOutputs finder;
            s.accept(&finder);
            Stmt new_stmt = s;
            for (const string &buf : finder.result) {
                new_stmt = InjectBufferCopiesForSingleBuffer(buf, true).mutate(new_stmt);
            }
            return new_stmt;
        } else {
            return IRMutator::mutate(s);
        }
    }

    InjectBufferCopiesForInputsAndOutputs(Stmt s) : site(s) {}
};

}  // namespace

Stmt inject_host_dev_buffer_copies(Stmt s, const Target &t) {
    // Handle internal allocations
    s = InjectBufferCopies().mutate(s);

    // Handle inputs and outputs
    FindOutermostProduce outermost;
    s.accept(&outermost);
    if (outermost.result.defined()) {
        // If the entire pipeline simplified away, or just dispatches
        // to another pipeline, there may be no outermost produce.
        s = InjectBufferCopiesForInputsAndOutputs(outermost.result).mutate(s);
    }

    return s;
}

}
}
