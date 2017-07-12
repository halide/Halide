#include "RDom.h"
#include "Util.h"
#include "ImageParam.h"
#include "IREquality.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "Simplify.h"

namespace Halide {

using namespace Internal;

using std::string;
using std::vector;

RVar::operator Expr() const {
    if (!min().defined() || !extent().defined()) {
        user_error << "Use of undefined RDom dimension: " <<
            (name().empty() ? "<unknown>" : name()) << "\n";
    }
    return Variable::make(Int(32), name(), domain());
}

Expr RVar::min() const {
    if (_domain.defined()) {
        return _var().min;
    } else {
        return Expr();
    }
}

Expr RVar::extent() const {
    if (_domain.defined()) {
        return _var().extent;
    } else {
        return Expr();
    }
}

const std::string &RVar::name() const {
    if (_domain.defined()) {
        return _var().var;
    } else {
        return _name;
    }
}


template <int N>
ReductionDomain build_domain(ReductionVariable (&vars)[N]) {
    vector<ReductionVariable> d(&vars[0], &vars[N]);
    ReductionDomain dom(d);
    return dom;
}

// This just initializes the predefined x, y, z, w members of RDom.
void RDom::init_vars(string name) {
    static string var_names[] = { "x", "y", "z", "w" };

    const std::vector<ReductionVariable> &dom_vars = dom.domain();
    RVar *vars[] = { &x, &y, &z, &w };

    for (size_t i = 0; i < sizeof(vars)/sizeof(vars[0]); i++) {
        if (i < dom_vars.size()) {
            *(vars[i]) = RVar(dom, i);
        } else {
            *(vars[i]) = RVar(name + "$" + var_names[i]);
        }
    }
}

RDom::RDom(ReductionDomain d) : dom(d) {
    if (d.defined()) {
        init_vars("");
    }
}

namespace {
class CheckRDomBounds : public IRGraphVisitor {

    using IRGraphVisitor::visit;

    void visit(const Call *op) {
        IRGraphVisitor::visit(op);
        if (op->call_type == Call::Halide) {
            offending_func = op->name;
        }
    }

    void visit(const Variable *op) {
        if (!op->param.defined() && !op->image.defined()) {
            offending_free_var = op->name;
        }
    }
public:
    string offending_func;
    string offending_free_var;
};
}

void RDom::initialize_from_ranges(const std::vector<std::pair<Expr, Expr>> &ranges, string name) {
    if (name.empty()) {
        name = make_entity_name(this, "Halide::RDom", 'r');
    }

    std::vector<ReductionVariable> vars;
    for (size_t i = 0; i < ranges.size(); i++) {
        CheckRDomBounds checker;
        ranges[i].first.accept(&checker);
        ranges[i].second.accept(&checker);
        user_assert(checker.offending_func.empty())
            << "The bounds of the RDom " << name
            << " in dimension " << i
            << " are:\n"
            << "  " << ranges[i].first << " ... " << ranges[i].second << "\n"
            << "These depend on a call to the Func " << checker.offending_func << ".\n"
            << "The bounds of an RDom may not depend on a call to a Func.\n";
        user_assert(checker.offending_free_var.empty())
            << "The bounds of the RDom " << name
            << " in dimension " << i
            << " are:\n"
            << "  " << ranges[i].first << " ... " << ranges[i].second << "\n"
            << "These depend on the variable " << checker.offending_free_var << ".\n"
            << "The bounds of an RDom may not depend on a free variable.\n";

        std::string rvar_uniquifier;
        switch (i) {
            case 0: rvar_uniquifier = "x"; break;
            case 1: rvar_uniquifier = "y"; break;
            case 2: rvar_uniquifier = "z"; break;
            case 3: rvar_uniquifier = "w"; break;
            default: rvar_uniquifier = std::to_string(i); break;
        }
        ReductionVariable rv;
        rv.var = name + "$" + rvar_uniquifier;
        rv.min = cast<int32_t>(ranges[i].first);
        rv.extent = cast<int32_t>(ranges[i].second);
        vars.push_back(rv);
    }
    dom = ReductionDomain(vars);
    init_vars(name);
}

RDom::RDom(const Buffer<> &b) {
    string name = unique_name('r');
    static string var_names[] = {"x", "y", "z", "w"};
    std::vector<ReductionVariable> vars;
    for (int i = 0; i < b.dimensions(); i++) {
        ReductionVariable var = {
            name + "$" + var_names[i],
            b.dim(i).min(),
            b.dim(i).extent()
        };
        vars.push_back(var);
    }

    dom = ReductionDomain(vars);
    init_vars(name);
}

RDom::RDom(ImageParam p) {
    static string var_names[] = {"x", "y", "z", "w"};
    std::vector<ReductionVariable> vars;
    for (int i = 0; i < p.dimensions(); i++) {
        ReductionVariable var = {
            p.name() + "$" + var_names[i],
            p.dim(i).min(),
            p.dim(i).extent()
        };
        vars.push_back(var);
    }

    dom = ReductionDomain(vars);
    init_vars(p.name());
}


int RDom::dimensions() const {
    return (int)dom.domain().size();
}

RVar RDom::operator[](int i) const {
    if (i == 0) return x;
    if (i == 1) return y;
    if (i == 2) return z;
    if (i == 3) return w;
    if (i < dimensions()) {
        return RVar(dom, i);
    }
    user_error << "Reduction domain index out of bounds: " << i << "\n";
    return x; // Keep the compiler happy
}

RDom::operator Expr() const {
    if (dimensions() != 1) {
        user_error << "Error: Can't treat this multidimensional RDom as an Expr:\n"
                   << (*this) << "\n"
                   << "Only single-dimensional RDoms can be cast to Expr.\n";
    }
    return Expr(x);
}

RDom::operator RVar() const {
    if (dimensions() != 1) {
        user_error << "Error: Can't treat this multidimensional RDom as an RVar:\n"
                   << (*this) << "\n"
                   << "Only single-dimensional RDoms can be cast to RVar.\n";
    }
    return x;
}

void RDom::where(Expr predicate) {
    user_assert(!dom.frozen())
        << (*this) << " cannot be given a new predicate, because it has already"
        << " been used in the update definition of some function.\n";
    user_assert(dom.defined()) << "Error: Can't add predicate to undefined RDom.\n";
    dom.where(predicate);
}

/** Emit an RVar in a human-readable form */
std::ostream &operator<<(std::ostream &stream, RVar v) {
    stream << v.name() << "(" << v.min() << ", " << v.extent() << ")";
    return stream;
}

/** Emit an RDom in a human-readable form. */
std::ostream &operator<<(std::ostream &stream, RDom dom) {
    stream << "RDom(\n";
    for (int i = 0; i < dom.dimensions(); i++) {
        stream << "  " << dom[i] << "\n";
    }
    stream << ")";
    Expr pred = simplify(dom.domain().predicate());
    if (!equal(const_true(), pred)) {
        stream << " where (\n  " << pred << ")";
    }
    stream << "\n";
    return stream;
}

}
