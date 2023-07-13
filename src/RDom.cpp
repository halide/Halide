#include <array>
#include <utility>

#include "IREquality.h"
#include "IROperator.h"
#include "IRPrinter.h"
#include "ImageParam.h"
#include "RDom.h"
#include "Simplify.h"
#include "Util.h"

namespace Halide {

using namespace Internal;

using std::string;
using std::vector;

namespace {

const char *const dom_var_names[] = {"$x", "$y", "$z", "$w"};

// T is an ImageParam, Buffer<>, Input<Buffer<>>
template<typename T>
Internal::ReductionDomain make_dom_from_dimensions(const T &t, const std::string &name) {
    std::vector<Internal::ReductionVariable> vars;
    for (int i = 0; i < t.dimensions(); i++) {
        vars.push_back({name + dom_var_names[i],
                        t.dim(i).min(),
                        t.dim(i).extent()});
    }

    return Internal::ReductionDomain(vars);
}

}  // namespace

RVar::operator Expr() const {
    if (!min().defined() || !extent().defined()) {
        user_error << "Use of undefined RDom dimension: " << (name().empty() ? "<unknown>" : name()) << "\n";
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

template<int N>
ReductionDomain build_domain(ReductionVariable (&vars)[N]) {
    vector<ReductionVariable> d(&vars[0], &vars[N]);
    ReductionDomain dom(d);
    return dom;
}

// This just initializes the predefined x, y, z, w members of RDom.
void RDom::init_vars(const string &name) {
    const std::vector<ReductionVariable> &dom_vars = dom.domain();
    std::array<RVar *, 4> vars = {{&x, &y, &z, &w}};

    for (size_t i = 0; i < vars.size(); i++) {
        if (i < dom_vars.size()) {
            *(vars[i]) = RVar(dom, i);
        } else {
            *(vars[i]) = RVar(name + dom_var_names[i]);
        }
    }
}

RDom::RDom(const ReductionDomain &d)
    : dom(d) {
    if (d.defined()) {
        init_vars("");
    }
}

namespace {
class CheckRDomBounds : public IRGraphVisitor {

    using IRGraphVisitor::visit;

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);
        if (op->call_type == Call::Halide) {
            offending_func = op->name;
        }
    }

    void visit(const Variable *op) override {
        if (!op->param.defined() &&
            !op->image.defined() &&
            !internal_vars.contains(op->name)) {
            offending_free_var = op->name;
        }
    }

    void visit(const Let *op) override {
        ScopedBinding<int> bind(internal_vars, op->name, 0);
        IRGraphVisitor::visit(op);
    }
    Scope<int> internal_vars;

public:
    string offending_func;
    string offending_free_var;
};
}  // namespace

void RDom::validate_min_extent(const Expr &min, const Expr &extent) {
    user_assert(lossless_cast(Int(32), min).defined())
        << "RDom min cannot be represented as an int32: " << min;
    user_assert(lossless_cast(Int(32), extent).defined())
        << "RDom extent cannot be represented as an int32: " << extent;
}

void RDom::initialize_from_region(const Region &region, string name) {
    if (name.empty()) {
        name = make_entity_name(this, "Halide:.*:RDom", 'r');
    }

    std::vector<ReductionVariable> vars;
    for (size_t i = 0; i < region.size(); i++) {
        CheckRDomBounds checker;
        user_assert(region[i].min.defined() && region[i].extent.defined())
            << "The RDom " << name << " may not be constructed with undefined Exprs.\n";
        region[i].min.accept(&checker);
        region[i].extent.accept(&checker);
        user_assert(checker.offending_func.empty())
            << "The bounds of the RDom " << name
            << " in dimension " << i
            << " are:\n"
            << "  " << region[i].min << " ... " << region[i].extent << "\n"
            << "These depend on a call to the Func " << checker.offending_func << ".\n"
            << "The bounds of an RDom may not depend on a call to a Func.\n";
        user_assert(checker.offending_free_var.empty())
            << "The bounds of the RDom " << name
            << " in dimension " << i
            << " are:\n"
            << "  " << region[i].min << " ... " << region[i].extent << "\n"
            << "These depend on the variable " << checker.offending_free_var << ".\n"
            << "The bounds of an RDom may not depend on a free variable.\n";

        std::string rvar_uniquifier;
        switch (i) {
        case 0:
            rvar_uniquifier = "x";
            break;
        case 1:
            rvar_uniquifier = "y";
            break;
        case 2:
            rvar_uniquifier = "z";
            break;
        case 3:
            rvar_uniquifier = "w";
            break;
        default:
            rvar_uniquifier = std::to_string(i);
            break;
        }
        ReductionVariable rv;
        rv.var = name + "$" + rvar_uniquifier;
        rv.min = cast<int32_t>(region[i].min);
        rv.extent = cast<int32_t>(region[i].extent);
        vars.push_back(rv);
    }
    dom = ReductionDomain(vars);
    init_vars(name);
}

RDom::RDom(const Buffer<> &b) {
    std::string name = unique_name('r');
    dom = make_dom_from_dimensions(b, name);
    init_vars(name);
}

RDom::RDom(const OutputImageParam &p) {
    const std::string &name = p.name();
    dom = make_dom_from_dimensions(p, name);
    init_vars(name);
}

int RDom::dimensions() const {
    return (int)dom.domain().size();
}

RVar RDom::operator[](int i) const {
    switch (i) {
    case 0:
        return x;
    case 1:
        return y;
    case 2:
        return z;
    case 3:
        return w;
    default:
        if (i < dimensions()) {
            return RVar(dom, i);
        } else {
            user_error << "Reduction domain index out of bounds: " << i << "\n";
            return x;  // Keep the compiler happy
        }
    }
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
    dom.where(std::move(predicate));
}

/** Emit an RVar in a human-readable form */
std::ostream &operator<<(std::ostream &stream, const RVar &v) {
    stream << v.name() << "(" << v.min() << ", " << v.extent() << ")";
    return stream;
}

/** Emit an RDom in a human-readable form. */
std::ostream &operator<<(std::ostream &stream, const RDom &dom) {
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

}  // namespace Halide
