#include "UnpackBuffers.h"
#include "IROperator.h"
#include "IRVisitor.h"

#include <map>

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::set;
using std::string;
using std::vector;

namespace {

struct BufferInfo {
    Expr handle;
    int dimensions;
};

class FindBufferSymbols : public IRVisitor {
    using IRVisitor::visit;

    void visit_param(std::string_view ref_name, const Parameter &param) {
        if (param.defined() && param.is_buffer()) {
            std::string_view name = param.name();
            Expr var = Variable::make(type_of<halide_buffer_t *>(), concat(name, ".buffer"), param);
            buffers.emplace(name, BufferInfo{var, param.dimensions()});
        }
    }

    void visit_buffer(std::string_view ref_name, const Buffer<> &buffer) {
        if (buffer.defined()) {
            std::string_view name = buffer.name();
            Expr var = Variable::make(type_of<halide_buffer_t *>(), concat(name, ".buffer"), buffer);
            buffers.emplace(name, BufferInfo{var, buffer.dimensions()});
        }
    }

    void visit(const Variable *op) override {
        visit_param(op->name, op->param);
        visit_buffer(op->name, op->image);
        symbols.emplace(op->name);
    }

    void visit(const Load *op) override {
        visit_param(op->name, op->param);
        visit_buffer(op->name, op->image);
        symbols.emplace(op->name);
        IRVisitor::visit(op);
    }

    void visit(const Store *op) override {
        visit_param(op->name, op->param);
        symbols.emplace(op->name);
        IRVisitor::visit(op);
    }

public:
    StringSet symbols;
    StringMap<BufferInfo> buffers;
};

}  // namespace

Stmt unpack_buffers(Stmt s) {
    FindBufferSymbols finder;
    s.accept(&finder);

    vector<pair<string, Expr>> lets;

    for (auto &p : finder.buffers) {
        const string &name = p.first;
        const BufferInfo &info = p.second;
        vector<Expr> args = {info.handle};

        string host_var = name;
        Expr host_val = Call::make(type_of<void *>(), Call::buffer_get_host, args, Call::Extern);
        lets.emplace_back(host_var, host_val);

        string dev_var = name + ".device";
        Expr dev_val = Call::make(type_of<uint64_t>(), Call::buffer_get_device, args, Call::Extern);
        lets.emplace_back(dev_var, dev_val);

        string dev_interface_var = name + ".device_interface";
        Expr dev_interface_val = Call::make(type_of<const halide_device_interface_t *>(),
                                            Call::buffer_get_device_interface, args, Call::Extern);
        lets.emplace_back(dev_interface_var, dev_interface_val);

        string type_code_var = name + ".type";
        Expr type_code_val = Call::make(UInt(32), Call::buffer_get_type, args, Call::Extern);
        lets.emplace_back(type_code_var, type_code_val);

        string host_dirty_var = name + ".host_dirty";
        Expr host_dirty_val = Call::make(Bool(), Call::buffer_get_host_dirty, args, Call::Extern);
        lets.emplace_back(host_dirty_var, host_dirty_val);

        string dev_dirty_var = name + ".device_dirty";
        Expr dev_dirty_val = Call::make(Bool(), Call::buffer_get_device_dirty, args, Call::Extern);
        lets.emplace_back(dev_dirty_var, dev_dirty_val);

        string dimensions_var = name + ".dimensions";
        Expr dimensions_val = Call::make(Int(32), Call::buffer_get_dimensions, args, Call::Extern);
        lets.emplace_back(dimensions_var, dimensions_val);

        for (int i = 0; i < info.dimensions; i++) {
            vector<Expr> args = {info.handle, i};

            string min_var = name + ".min." + std::to_string(i);
            Expr min_val = Call::make(Int(32), Call::buffer_get_min, args, Call::Extern);
            lets.emplace_back(min_var, min_val);

            string extent_var = name + ".extent." + std::to_string(i);
            Expr extent_val = Call::make(Int(32), Call::buffer_get_extent, args, Call::Extern);
            lets.emplace_back(extent_var, extent_val);

            string stride_var = name + ".stride." + std::to_string(i);
            Expr stride_val = Call::make(Int(32), Call::buffer_get_stride, args, Call::Extern);
            lets.emplace_back(stride_var, stride_val);
        }
    }

    while (!lets.empty()) {
        pair<string, Expr> l = lets.back();
        lets.pop_back();
        if (finder.symbols.count(l.first)) {
            s = LetStmt::make(l.first, l.second, s);
        }
    }

    // Create buffer is not null assertions
    for (auto &p : finder.buffers) {
        Expr buf = p.second.handle;
        Expr cond = reinterpret<uint64_t>(buf) != 0;
        Expr error = Call::make(Int(32), "halide_error_buffer_argument_is_null",
                                {Expr(p.first)}, Call::Extern);
        Stmt check = AssertStmt::make(cond, error);
        s = Block::make(check, s);
    }

    return s;
}

}  // namespace Internal
}  // namespace Halide
