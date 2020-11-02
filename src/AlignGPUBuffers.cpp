#include "InjectHostDevBufferCopies.h"

#include "CodeGen_GPU_Dev.h"
#include "Debug.h"
#include "ExternFuncArgument.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Substitute.h"

#include <map>
#include <utility>

namespace Halide {
namespace Internal {

using std::set;
using std::string;
using std::vector;

namespace {

class FindTexturesInGPU : public IRVisitor {
    public:
    set<string> textures;

    private:
    bool in_gpu = false;
    DeviceAPI in_device_api = DeviceAPI::None;

    void visit(const Call *op) override {
        if (in_gpu && op->is_intrinsic(Call::image_load)) {
            debug(2) << " load call to " << op->name << " " << textures.count(op->name) << "\n";
            textures.insert(op->args[0].as<StringImm>()->value);
        }

        IRVisitor::visit(op);
    }

    void visit(const For *op) override {
        bool old_in_gpu = in_gpu;
        DeviceAPI old_in_device_api = in_device_api;
        if (op->for_type == ForType::GPUBlock ||
            op->for_type == ForType::GPUThread) {
            in_gpu = true;
            in_device_api = op->device_api;
        }
        IRVisitor::visit(op);
        in_gpu = old_in_gpu;
        in_device_api = old_in_device_api;
    }
};

class FindBufferInitType : public IRVisitor {
    public:
    Type type;

    private:
    void visit(const Call *op) override {
        if (op->name == Call::buffer_init) {
            internal_assert(op->args.size() == 10) << "don't understand the format of buffer_init";
            
            halide_type_code_t code = (halide_type_code_t)op->args[5].as<IntImm>()->value;
            int bits = op->args[6].as<IntImm>()->value;
            type = Type(code, bits, 1);
        }

        IRVisitor::visit(op);
    }
};

class AdjustAllocationStride : public IRMutator {
    Type buffer_type;
private:
    Stmt visit(const LetStmt *op) override {
        if (op->name == buffer) {
            bool old_in_buffer = in_buffer;
            debug(2) << " enter buffer " << op->name << "\n";
            internal_assert(!old_in_buffer) << " Already in buffer?!?";
            in_buffer = true;

            FindBufferInitType typeFinder;
            op->accept(&typeFinder);
            buffer_type = typeFinder.type;

            debug(2) << " found type " << buffer_type << "\n";

            Expr new_value = mutate(op->value);
            debug(2) << " new struct value " << new_value;
            debug(2) << " exit buffer " << op->name << "\n";
            in_buffer = old_in_buffer;

            return LetStmt::make(op->name, new_value, op->body);
        } else {
            return IRMutator::visit(op);
        }
    }

    Expr visit(const Call *op) override {
        if (in_buffer) {
            debug(2) << " in buffer call " << op->name << "\n";

            if (op->is_intrinsic(Call::make_struct)) {
                internal_assert(op->args.size() % 4 == 0) << "unknown format of make_struct for buffer";

                vector<Expr> args = op->args;
                if (args.size() >= 8) {
                    Expr row_width = args[1];
                    Expr current_stride = args[6];

                    // This could be symbolically fetched from runtime I guess?
                    int target_align_bytes = 32;

                    int target_align_items = target_align_bytes / buffer_type.bytes();
                    Expr target_align_expr = IntImm::make(Int(32), target_align_items);
                    
                    Expr row_tail_items = Mod::make(current_stride, target_align_expr);
                    Expr row_extra_items = Sub::make(target_align_expr, row_tail_items);

                    Expr padded_stride = Select::make(
                        EQ::make(row_tail_items, IntImm::make(Int(32), 0)),
                        current_stride,
                        Add::make(current_stride, row_extra_items)
                    );
                    args[6] = padded_stride;

                    debug(2) << " old struct: " << static_cast<Expr>(op) << "\n";
                    Expr new_call = Call::make(op->type, op->name, args, op->call_type).as<Call>();
                    debug(2) << " new struct: " << new_call << "\n";
                    return new_call;
                }
            }

            return IRMutator::visit(op);
        } else {
            return IRMutator::visit(op);
        }
    }

    string buffer;
    bool in_buffer = false;

public:
    AdjustAllocationStride(string b)
        : buffer(std::move(b)) {
    }
};

}  // namespace

Stmt align_gpu_buffers(Stmt s, const Target &t) {

    // Handle inputs and outputs
    FindTexturesInGPU finder;
    s.accept(&finder);
    for (const string& texture : finder.textures) {
        s = AdjustAllocationStride(texture + ".buffer").mutate(s);
    }

    return s;
}

}  // namespace Internal
}  // namespace Halide
