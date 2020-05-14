#include <iostream>
#include <sstream>

#include "Halide.h"
#include "parser.h"
#include "halide_benchmark.h"

using namespace Halide;
using namespace Halide::Internal;
using namespace Halide::Tools;

namespace {

class NodeCounter : public IRVisitor {
public:
    int count = 0;

protected:
    void visit(const IntImm *op) override { count++; IRVisitor::visit(op); }
    void visit(const UIntImm *op) override { count++; IRVisitor::visit(op); }
    void visit(const FloatImm *op) override { count++; IRVisitor::visit(op); }
    void visit(const StringImm *op) override { count++; IRVisitor::visit(op); }
    void visit(const Cast *op) override { count++; IRVisitor::visit(op); }
    void visit(const Variable *op) override { count++; IRVisitor::visit(op); }
    void visit(const Add *op) override { count++; IRVisitor::visit(op); }
    void visit(const Sub *op) override { count++; IRVisitor::visit(op); }
    void visit(const Mul *op) override { count++; IRVisitor::visit(op); }
    void visit(const Div *op) override { count++; IRVisitor::visit(op); }
    void visit(const Mod *op) override { count++; IRVisitor::visit(op); }
    void visit(const Min *op) override { count++; IRVisitor::visit(op); }
    void visit(const Max *op) override { count++; IRVisitor::visit(op); }
    void visit(const EQ *op) override { count++; IRVisitor::visit(op); }
    void visit(const NE *op) override { count++; IRVisitor::visit(op); }
    void visit(const LT *op) override { count++; IRVisitor::visit(op); }
    void visit(const LE *op) override { count++; IRVisitor::visit(op); }
    void visit(const GT *op) override { count++; IRVisitor::visit(op); }
    void visit(const GE *op) override { count++; IRVisitor::visit(op); }
    void visit(const And *op) override { count++; IRVisitor::visit(op); }
    void visit(const Or *op) override { count++; IRVisitor::visit(op); }
    void visit(const Not *op) override { count++; IRVisitor::visit(op); }
    void visit(const Select *op) override { count++; IRVisitor::visit(op); }
    void visit(const Load *op) override { count++; IRVisitor::visit(op); }
    void visit(const Ramp *op) override { count++; IRVisitor::visit(op); }
    void visit(const Broadcast *op) override { count++; IRVisitor::visit(op); }
    void visit(const Call *op) override { count++; IRVisitor::visit(op); }
    void visit(const Let *op) override { count++; IRVisitor::visit(op); }
    void visit(const LetStmt *op) override { count++; IRVisitor::visit(op); }
    void visit(const AssertStmt *op) override { count++; IRVisitor::visit(op); }
    void visit(const ProducerConsumer *op) override { count++; IRVisitor::visit(op); }
    void visit(const For *op) override { count++; IRVisitor::visit(op); }
    void visit(const Store *op) override { count++; IRVisitor::visit(op); }
    void visit(const Provide *op) override { count++; IRVisitor::visit(op); }
    void visit(const Allocate *op) override { count++; IRVisitor::visit(op); }
    void visit(const Free *op) override { count++; IRVisitor::visit(op); }
    void visit(const Realize *op) override { count++; IRVisitor::visit(op); }
    void visit(const Block *op) override { count++; IRVisitor::visit(op); }
    void visit(const IfThenElse *op) override { count++; IRVisitor::visit(op); }
    void visit(const Evaluate *op) override { count++; IRVisitor::visit(op); }
    void visit(const Shuffle *op) override { count++; IRVisitor::visit(op); }
    void visit(const Prefetch *op) override { count++; IRVisitor::visit(op); }
    void visit(const Fork *op) override { count++; IRVisitor::visit(op); }
    void visit(const Acquire *op) override { count++; IRVisitor::visit(op); }
    void visit(const Atomic *op) override { count++; IRVisitor::visit(op); }
};

}  // namespace

int main(int argc, char **argv) {
    Var loop0, loop1, loop2;
    Var anon0, anon1, anon2, anon3, anon4, anon5;
    std::vector<Expr> exprs = {
        (((((min(((loop0 * -3) + anon1), 3) + (loop0 * 3)) + anon2) * 2) + 7) / 3),
        ((((anon1 + anon2) + loop0) / 2) - ((((select((0 < loop0), 1, 0) + loop0) + anon1) + anon2) / 2)),
        ((((anon1 + anon2) + loop0) / 2) - (((select((0 < loop0), 1, 0) + loop0) + (anon1 + anon2)) / 2)),
        ((((loop0 - ((((anon1 + loop0) + -1) / 3) * 3)) + anon1) + 2) / 3),
        ((((loop0 - ((((anon1 + loop0) + -2) / 4) * 4)) + anon1) + 2) / 4),
        ((((loop0 - ((((select((0 < loop0), 4, -1) + loop0) + anon1) / 3) * 3)) + anon1) + 5) / 3),
        ((((loop0 - ((((select((0 < loop0), 5, -2) + loop0) + anon1) / 4) * 4)) + anon1) + 6) / 4),
        ((((loop0 - (max((((anon1 + loop0) + -1) / 2), ((max(anon2, 0) + anon3) - (anon4 * 8))) * 2)) + anon1) + 3) / 2),
        ((((loop0 - (max((((anon1 + loop0) + -1) / 3), ((max(anon2, 0) + anon3) - (anon4 * 8))) * 3)) + anon1) + 5) / 3),
        ((((loop0 - (max((((anon1 + loop0) + -2) / 4), ((max(anon2, 0) + anon3) - (anon4 * 8))) * 4)) + anon1) + 6) / 4),
        ((((loop0 - (min(select((0 < loop0), (((anon1 + loop0) / 2) + 1), (((anon1 + loop0) + -1) / 2)), (((anon1 + loop0) + -1) / 2)) * 2)) + anon1) + 1) / 2),
        ((((loop0 - (select((0 < loop0), (((anon1 + loop0) / 2) + 1), (((anon1 + loop0) + -1) / 2)) * 2)) + anon1) + 3) / 2),
    };

    if (argc > 1) {
        std::cout << "Using exprs in file: " << argv[1] << "\n";
        exprs = parse_halide_exprs_from_file(argv[1]);
        std::cout << exprs.size() << " exprs parsed.\n";
    }

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
        std::cout << "serialize time: " << print_time_usec << " usec, " << print_time_usec / nodes << " usec/node\n";
    }

    simplify_time_total *= 1000000.0;
    print_time_total *= 1000000.0;

    std::cout << "\n\ntotal IR nodes seen:  " << nodes_total << "\n";
    std::cout << "avg nodes/Expr:  " << (double) nodes_total / (double) exprs.size() << "\n";
    std::cout << "avg simplify time:  " << simplify_time_total << " usec, avg per node " << simplify_time_total / nodes_total << " usec\n";
    std::cout << "avg simplify time:  " << print_time_total << " usec, avg per node " << print_time_total / nodes_total << " usec\n";

    std::cout << "\n(Ignore: " << tracker << ")\n";
    return 0;
}
