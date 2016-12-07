#include "Halide.h"

using namespace Halide;
using namespace Halide::Internal;

using std::vector;

class EveryAccessUsesConstIndex : public IRVisitor {
    using IRVisitor::visit;
    const std::string &name;

    void visit(const Load *op) {
        IRVisitor::visit(op);
        if (op->name == name && !is_const(op->index)) {
            result = false;
        }
    }

    void visit(const Store *op) {
        IRVisitor::visit(op);
        if (op->name == name && !is_const(op->index)) {
            result = false;
        }
    }

public:

    EveryAccessUsesConstIndex(const std::string &n) : name(n) {}
    bool result = true;
};

class FragmentAllocation : public IRMutator {
    using IRVisitor::visit;
    const std::string &name;

    void visit(const Load *op) {
        if (op->name == name) {
            const int64_t *idx = as_const_int(op->index);
            const Ramp *ramp = op->index.as<Ramp>();
            if (idx) {
                expr = Load::make(op->type, op->name + "." + std::to_string(*idx),
                                  0, op->image, op->param);
            } else if (ramp) {
                vector<Expr> lanes;
                for (int i = 0; i < ramp->lanes; i++) {
                    const int64_t *base = as_const_int(ramp->base);
                    const int64_t *stride = as_const_int(ramp->stride);
                    int64_t idx = (*base) + (*stride) * i;
                    lanes.push_back(
                        Load::make(op->type.element_of(),
                                   op->name + "." + std::to_string(idx),
                                   0, op->image, op->param)
                        );
                }
                expr = Call::make(op->type, Call::concat_vectors, lanes, Call::PureIntrinsic);
            } else {
                assert(false);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Store *op) {
        if (op->name == name) {
            const int64_t *idx = as_const_int(op->index);
            const Ramp *ramp = op->index.as<Ramp>();
            if (idx) {
                stmt = Store::make(op->name + "." + std::to_string(*idx),
                                   mutate(op->value), 0, op->param);
            } else if (ramp) {
                vector<Stmt> stores;
                std::string tmp = unique_name('t');
                Expr value_var = Variable::make(op->value.type(), tmp);
                for (int i = 0; i < ramp->lanes; i++) {
                    const int64_t *base = as_const_int(ramp->base);
                    const int64_t *stride = as_const_int(ramp->stride);
                    int64_t idx = (*base) + (*stride) * i;
                    Expr val = Call::make(op->value.type().element_of(), Call::shuffle_vector, {value_var, i}, Call::PureIntrinsic);
                    stores.push_back(
                        Store::make(op->name + "." + std::to_string(idx),
                                    val, 0, op->param)
                        );
                }
                stmt = Block::make(stores);
                stmt = LetStmt::make(tmp, mutate(op->value), stmt);
            } else {
                assert(false);
            }
        } else {
            IRMutator::visit(op);
        }
    }

    void visit(const Free *op) {
        if (op->name == name) {
            stmt = Evaluate::make(0);
        } else {
            IRVisitor::visit(op);
        }
    }

public:

    FragmentAllocation(const std::string &n) : name(n) {}
    bool result = true;
};

class PTXRegisterFragment : public IRMutator {
    using IRMutator::visit;

    void visit(const Allocate *op) {
        if (!in_thread_loop) {
            IRMutator::visit(op);
            return;
        }

        Stmt body = mutate(op->body);
        size_t sz = op->constant_allocation_size();
        bool success = false;
        if (op->extents.size()) {
            EveryAccessUsesConstIndex check(op->name);
            op->body.accept(&check);
            if (check.result) {
                FragmentAllocation f(op->name);
                body = f.mutate(body);
                success = true;
            }
        }

        if (success) {
            for (size_t i = sz; i > 0; i--) {
                body = Allocate::make(op->name + "." + std::to_string(i-1),
                                      op->type, {}, const_true(), body);
            }
            stmt = body;
        } else {
            stmt = Allocate::make(op->name, op->type, op->extents, const_true(), body);
        }
    }

    bool in_thread_loop = false;

    void visit(const For *op) {
        if (ends_with(op->name, ".__thread_id_x")) {
            in_thread_loop = true;
            IRMutator::visit(op);
            in_thread_loop = false;
        } else {
            IRMutator::visit(op);
        }
    }
};
int main() {
    const int size = 1024;

    ImageParam A(Float(32), 2), B(Float(32), 2);

    Var x, y;

    Func prod("prod");
    RDom r(0, size);

    prod(x, y) = 0.0f;
    prod(x, y) += A(x, r.x) * B(r.x, y);

    Func out;
    out(x, y) = prod(x, y);

    out.bound(x, 0, size).bound(y, 0, size);

    Var xi, yi, xii, yii;
    //out.tile(x, y, xi, yi, 8, 8).vectorize(xi, 4).unroll(xi).unroll(yi).gpu_tile(x, y, 8, 8);
    out.tile(x, y, xi, yi, 16, 8).vectorize(xi, 4).unroll(xi).unroll(yi).gpu_tile(x, y, 8, 8);
    prod.compute_at(out, Var::gpu_threads()).update().reorder(x, y, r.x);
    Var t;
    prod.unroll(x).unroll(y).update()
        .tile(x, y, xi, yi, 2, 2).vectorize(xi).unroll(yi)
        .tile(x, y, xii, yii, 2, 2).unroll(xii).unroll(yii)
        .unroll(x).unroll(y);

    out.add_custom_lowering_pass(new PTXRegisterFragment);

    A.set_host_alignment(16).set_bounds(0, 0, size).set_stride(1, size);
    B.set_host_alignment(16).set_bounds(0, 0, size).set_stride(1, size);
    out.output_buffer().set_host_alignment(16).set_bounds(0, 0, size).set_stride(1, size);

    vector<Argument> args = {A, B};
    out.compile_to_assembly("cuda_mat_mul.s", args, "mat_mul", Target("host-cuda-cuda_capability_50"));
    out.compile_to_header("cuda_mat_mul.h", args, "mat_mul", Target("host-cuda-cuda_capability_50"));

    return 0;
}
