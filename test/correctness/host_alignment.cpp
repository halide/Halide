#include "Halide.h"
#include <gtest/gtest.h>

using std::map;
using std::string;
using std::vector;
using namespace Halide;
using namespace Halide::Internal;

namespace {

class FindErrorHandler final : public IRVisitor {
public:
    bool result = false;
    using IRVisitor::visit;
    void visit(const Call *op) override {
        if (op->name == "halide_error_unaligned_host_ptr" &&
            op->call_type == Call::Extern) {
            result = true;
            return;
        }
        IRVisitor::visit(op);
    }
};

class ParseCondition final : public IRVisitor {
public:
    Expr condition;

    using IRVisitor::visit;
    void visit(const Mod *op) override {
        condition = op;
    }

    void visit(const Call *op) override {
        if (op->is_intrinsic(Call::bitwise_and)) {
            condition = op;
        } else {
            IRVisitor::visit(op);
        }
    }
};

class CountHostAlignmentAsserts final : public IRVisitor {
public:
    int count = 0;
    std::map<string, int> alignments_needed;
    CountHostAlignmentAsserts(std::map<string, int> m)
        : alignments_needed(m) {
    }

    using IRVisitor::visit;

    void visit(const AssertStmt *op) override {
        Expr m = op->message;
        FindErrorHandler f;
        m.accept(&f);
        if (f.result) {
            Expr c = op->condition;
            ParseCondition p;
            c.accept(&p);
            if (p.condition.defined()) {
                Expr left, right;
                if (const Mod *mod = p.condition.as<Mod>()) {
                    left = mod->a;
                    right = mod->b;
                } else if (const Call *call = Call::as_intrinsic(p.condition, {Call::bitwise_and})) {
                    left = call->args[0];
                    right = call->args[1];
                }
                const Reinterpret *reinterpret = left.as<Reinterpret>();
                if (!reinterpret) return;
                Expr name = reinterpret->value;
                const Variable *V = name.as<Variable>();
                string name_host_ptr = V->name;
                int expected_alignment = alignments_needed[name_host_ptr];
                if (is_const(right, expected_alignment) || is_const(right, expected_alignment - 1)) {
                    count++;
                    alignments_needed.erase(name_host_ptr);
                }
            }
        }
    }
};

void set_alignment_host_ptr(OutputImageParam i, int align, std::map<string, int> &m) {
    i.set_host_alignment(align);
    m.insert({i.name(), align});
}

int count_host_alignment_asserts(Func f, std::map<string, int> m) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::NoBoundsQuery);
    f.compute_root();
    Stmt s = lower_main_stmt({f.function()}, f.name(), t);
    CountHostAlignmentAsserts c(m);
    s.accept(&c);
    return c.count;
}

}  // namespace

// TODO: fails when multiple GPU targets are enabled
TEST(HostAlignmentTest, Basic) {
    Var x, y, c;
    std::map<string, int> m;
    ImageParam i1(Int(8), 1);
    ImageParam i2(Int(8), 1);
    ImageParam i3(Int(8), 1);

    Func f("f");
    f(x) = i1(x) + i2(x) + i3(x);

    set_alignment_host_ptr(i1, 128, m);
    set_alignment_host_ptr(i2, 32, m);
    set_alignment_host_ptr(f.output_buffer(), 128, m);
    ASSERT_EQ(count_host_alignment_asserts(f, m), 3);
}
