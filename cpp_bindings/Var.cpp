#include "Var.h"
#include "Expr.h"


struct RVar::Contents {
    Contents(Expr min, Expr size, const std::string &name) : min(min), size(size), name(name) {}
    Expr min, size;
    std::string name;
};

RVar::RVar(const Expr &min, const Expr &size) : 
    contents(new Contents(min, size, uniqueName('r'))) {}

RVar::RVar(const Expr &min, const Expr &size, const std::string &name) :
    contents(new Contents(min, size, name)) {}

const Expr &RVar::min() const {
    return contents->min;
}

const Expr &RVar::size() const {
    return contents->size;
}

const std::string &RVar::name() const {
    return contents->name;
}

bool RVar::operator==(const RVar &other) const {
    return name() == other.name();
}
