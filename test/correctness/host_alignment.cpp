#include "Halide.h"
#include <map>
#include <stdio.h>
#include <string>

namespace {

using std::map;
using std::string;
using std::vector;
using namespace Halide;
using namespace Halide::Internal;

class FindErrorHandler : public IRVisitor {
public:
    bool result;
    FindErrorHandler()
        : result(false) {
    }
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

class ParseCondition : public IRVisitor {
public:
    Expr left, right;

    using IRVisitor::visit;
    void visit(const Mod *op) override {
        left = op->a;
        right = op->b;
        return;
    }
};
class CountHostAlignmentAsserts : public IRVisitor {
public:
    int count;
    std::map<string, int> alignments_needed;
    CountHostAlignmentAsserts(std::map<string, int> m)
        : count(0),
          alignments_needed(m) {
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
            if (p.left.defined() && p.right.defined()) {
                const Call *reinterpret_call = p.left.as<Call>();
                if (!reinterpret_call ||
                    !reinterpret_call->is_intrinsic(Call::reinterpret)) return;
                Expr name = reinterpret_call->args[0];
                const Variable *V = name.as<Variable>();
                string name_host_ptr = V->name;
                int expected_alignment = alignments_needed[name_host_ptr];
                if (is_const(p.right, expected_alignment)) {
                    count++;
                    alignments_needed.erase(name_host_ptr);
                }
            }
        }
    }
};

void set_alignment_host_ptr(ImageParam &i, int align, std::map<string, int> &m) {
    i.set_host_alignment(align);
    m.insert(std::pair<string, int>(i.name(), align));
}

int count_host_alignment_asserts(Func f, std::map<string, int> m) {
    Target t = get_jit_target_from_environment();
    t.set_feature(Target::NoBoundsQuery);
    f.compute_root();
    Stmt s = Internal::lower_main_stmt({f.function()}, f.name(), t);
    CountHostAlignmentAsserts c(m);
    s.accept(&c);
    return c.count;
}

int test() {
    Var x, y, c;
    std::map<string, int> m;
    ImageParam i1(Int(8), 1);
    ImageParam i2(Int(8), 1);
    ImageParam i3(Int(8), 1);

    set_alignment_host_ptr(i1, 128, m);
    set_alignment_host_ptr(i2, 32, m);

    Func f("f");
    f(x) = i1(x) + i2(x) + i3(x);
    f.output_buffer().set_host_alignment(128);
    m.insert(std::pair<string, int>("f", 128));
    int cnt = count_host_alignment_asserts(f, m);
    if (cnt != 3) {
        printf("Error: expected 3 host alignment assertions in code, but got %d\n", cnt);
        return -1;
    }

    printf("Success!\n");
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    return test();
}
