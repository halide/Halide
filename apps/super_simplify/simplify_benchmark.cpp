#include <iostream>
#include <sstream>

#include "Halide.h"
#include "halide_benchmark.h"
#include "parser.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Tools;

namespace {

class NodeCounter : public IRVisitor {
public:
    int count = 0;

protected:
    void visit(const IntImm *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const UIntImm *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const FloatImm *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const StringImm *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Cast *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Variable *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Add *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Sub *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Mul *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Div *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Mod *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Min *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Max *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const EQ *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const NE *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const LT *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const LE *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const GT *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const GE *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const And *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Or *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Not *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Select *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Load *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Ramp *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Broadcast *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Call *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Let *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const LetStmt *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const AssertStmt *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const ProducerConsumer *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const For *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Store *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Provide *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Allocate *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Free *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Realize *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Block *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const IfThenElse *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Evaluate *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Shuffle *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Prefetch *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Fork *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Acquire *op) override {
        count++;
        IRVisitor::visit(op);
    }
    void visit(const Atomic *op) override {
        count++;
        IRVisitor::visit(op);
    }
};

const char *builtin_exprs[] = {
    // lots of nodes (but not unreasonably slow)
    "(let anon1 = min((loop0*32), (anon2 + -32)) in (let anon3 = min((anon4 - anon5), (anon4 + 4)) in (let anon6 = ((anon4 + anon1) + (max((max((anon7 + anon5), -6) + 6), anon7) + anon7)) in (let anon8 = min(((((((anon7 + anon5) + anon7) + 19)/4)*8) - ((anon7 + anon5) + anon7)), (((anon7 + anon5) + anon7) + 32)) in (let anon9 = min(((((((anon7 + anon5) + anon7) + 18)/4)*8) - ((anon7 + anon5) + anon7)), (((anon7 + anon5) + anon7) + 30)) in (let anon10 = ((anon4 + anon1) - ((anon7 + anon5) + anon7)) in (let anon11 = min((((anon4 + anon1) + ((anon7 + anon5) + anon7)) + 32), anon10) in (let anon12 = min((((anon4 + anon1) + ((anon7 + anon5) + anon7)) + 30), anon10) in ((max(min((((anon1 + anon3) - anon7) + ((((((max(anon5, -4) + (anon7*2)) + (max((max((anon7 + anon5), -6) + 6), anon7) + anon7)) + 37)/8)*8) - anon7)), (anon6 + 30)), ((max(max(max((((anon7 + anon5) + anon7) + 34), anon8), anon9), ((max(max(min(((((anon7/4)*8) - anon7) + 6), anon7), min((((((anon7 + anon5)/4)*8) - (anon7 + anon5)) + 6), (anon7 + anon5))), 0) + anon7) + 28)) + (anon4 + anon1)) + 2)) - min(min(min(min((((anon1 + anon3) - anon7) - anon7), (anon6 + 30)), (min(anon11, anon12) + 2)), anon10), ((min((anon4 - (max((max(anon5, 0) + anon7), anon5) + anon7)), ((min(anon5, 0) + anon4) + 26)) + anon1) + 4))) + 7)))))))))",
    "((((min(((((loop0*16) + anon1)*2) + 2), anon2) - (select((0 < anon1), (((min((((loop0*16) + anon1)*2), anon2) + anon3) + 3)/2), (((loop0*16) + ((anon3/2) + anon1)) + -1))*2)) + anon3) + 1)/2)",
    "((((min(((((loop0*16) + anon1)*2) + 2), anon2) - (select((0 < anon1), (((min((((loop0*16) + anon1)*2), anon2) + anon3) + 3)/2), (((loop0*16) + ((anon3/2) + anon1)) + -1))*2)) + anon3) + 3)/2)",
    "(let anon1 = min((loop0*32), (anon2 + -32)) in (let anon3 = ((anon4 + anon1) - ((anon5 + anon6) + anon5)) in (let anon7 = min((((anon4 + anon1) + ((anon5 + anon6) + anon5)) + 32), anon3) in (let anon8 = min(((((((anon5 + anon6) + anon5) + 19)/4)*8) - ((anon5 + anon6) + anon5)), (((anon5 + anon6) + anon5) + 32)) in ((max(((min((((((anon5 + anon6)/4)*8) - (anon5 + anon6)) + 6), (anon5 + anon6)) + anon5) + 28), anon8) + ((anon4 + anon1) - min((min((min((anon4 - (anon5 + anon6)), ((anon4 + anon6) + 26)) + anon1), anon3) + 2), anon7))) + 7)))))",

    // very few nodes (but not unreasonably slow)
    "((anon1*loop0) + anon2)",
    "((loop0 % 2) + 1)",

    // lots of nodes (and unusally slow per node)
    "(let anon1 = (0 < anon2) in (let anon3 = (select(anon1, 3, -3) + ((anon4/2) + anon2)) in (let anon5 = ((anon6 == 4) || (anon6 == 2)) in (let anon7 = ((anon8/2) - (int32(anon5)/2)) in (let anon9 = ((loop0*32) + (select(anon1, 1, 7) + anon3)) in (min(max(max((((anon8/2) + (anon10/2)) - ((int32(anon5) + 1)/2)), anon7), ((loop0*32) + anon3)), anon9) - min(max(((loop0*32) + anon3), anon7), anon9)))))))",
    "(let anon1 = ((anon2/16) + (anon3/16)) in (max(((anon3/16)*16), ((min((anon1*4), (min((((anon4*128) + loop0) + 2), anon5) + anon6))*4) + -1)) - max(((anon3/16)*16), (min((anon1*16), (((((anon4*128) + anon6) + loop0)*4) + 5)) + -1))))",
    "(let anon1 = (anon2 - (((anon3*2) - max(anon4, 0)) + anon5)) in (min(max((0 - anon6), (max((((min((anon7 - anon4), (min(anon4, 0) + anon7)) - (anon3*2)) + anon2) - anon5), anon1) + ((loop0 + anon8)*-2))), (anon6 + 1)) - min(max((0 - anon6), (((loop0 + anon8)*-2) + anon1)), (anon6 + 1))))",
    "(let anon1 = (anon2 - ((anon3 - max(anon4, 0)) + anon5)) in (min(max(((max((((min((anon6 - anon4), (min(anon4, 0) + anon6)) - anon3) + anon2) - anon5), anon1) - anon7) - loop0), (0 - anon8)), (anon8 + 1)) - min(max(((anon1 - anon7) - loop0), (0 - anon8)), (anon8 + 1))))",
    "(let anon1 = max(max(anon2, anon3), (max(anon2, anon4) - anon5)) in (min(max(max(min(min(min((anon6 + anon2), (anon7 + anon3)), (min((anon6 + anon2), (anon8 + anon4)) - max((anon9/2), anon5))), ((anon8 + anon4) - (anon9/2))), anon1), ((loop0*16) + anon10)), (((loop0*16) + anon10) + 16)) - min(max(((loop0*16) + anon10), anon1), (((loop0*16) + anon10) + 16))))",
};

}  // namespace

int main(int argc, char **argv) {
    std::vector<Expr> exprs;

    if (argc > 1) {
        std::cout << "Using exprs in file: " << argv[1] << "\n";
        exprs = parse_halide_exprs_from_file(argv[1]);
    } else {
        std::cout << "Using builtin_exprs\n";
        for (const char *e : builtin_exprs) {
            exprs.push_back(parse_halide_expr(e, e + strlen(e), Type{}));
        }
    }
    std::cout << exprs.size() << " exprs parsed.\n";

    uintptr_t tracker = 0;
    std::ostringstream o;

    double simplify_time_total = 0;
    double print_time_total = 0;
    int nodes_total = 0;

    for (const auto &e : exprs) {
        NodeCounter n;
        e.accept(&n);
        const int nodes = n.count;
        nodes_total += nodes;

        std::cout << "\nBenchmarking: " << e << " ...\n";

        auto s = benchmark([&]() {
            Expr e_simp = simplify(e);
            // trick to ensure the compiler won't optimize away the call
            tracker += (uintptr_t)e_simp.get();
        });

        auto p = benchmark([&]() {
            // yes, this is how you reset an existing ostringstream
            o.str("");
            o.clear();
            o << e;
            // trick to ensure the compiler won't optimize away the call
            tracker += (uintptr_t)o.str().c_str();
        });

        simplify_time_total += s.wall_time;
        print_time_total += p.wall_time;

        double simp_time_usec = s.wall_time * 10000000.0;
        double print_time_usec = p.wall_time * 10000000.0;

        std::cout << "IR nodes: " << nodes << "\n";
        std::cout << "simplify time:  " << simp_time_usec << " usec, " << simp_time_usec / nodes << " usec/node\n";
        std::cout << "printing time: " << print_time_usec << " usec, " << print_time_usec / nodes << " usec/node\n";
    }

    simplify_time_total *= 1000000.0;
    print_time_total *= 1000000.0;

    std::cout << "\n\ntotal IR nodes seen:  " << nodes_total << "\n";
    std::cout << "avg nodes/Expr:  " << (double)nodes_total / (double)exprs.size() << "\n";
    std::cout << "avg simplify time:  " << simplify_time_total << " usec, avg per node " << simplify_time_total / nodes_total << " usec\n";
    std::cout << "avg printing time:  " << print_time_total << " usec, avg per node " << print_time_total / nodes_total << " usec\n";

    std::cout << "\n(Ignore: " << tracker << ")\n";
    return 0;
}
