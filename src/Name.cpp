#include "Name.h"
#include "Qualify.h"

namespace Halide {
namespace Internal {

Name Name::qualify(const Name &suffix) const {
    return s + "." + suffix.s;
}
Name Name::qualify(const std::string &suffix) const {
    return s + "." + suffix;
}
Name Name::qualify(const char *suffix) const {
    return s + "." + std::string(suffix);
}
Name Name::qualify(int i) const {
    return s + "." + std::to_string(i);
}
Expr Name::qualify(const Expr &e) const {
    return Halide::Internal::qualify(s + ".", e);
}
Name Name::min() const {
    return s + ".min";
}
Name Name::max() const {
    return s + ".max";
}
Name Name::loop_max() const {
    return s + ".loop_max";
}
Name Name::loop_min() const {
    return s + ".loop_min";
}
Name Name::loop_extent() const {
    return s + ".loop_extent";
}
Name Name::outer_min() const {
    return s + ".outer_min";
}
Name Name::outer_max() const {
    return s + ".outer_max";
}
Name Name::min_realized(int dim) const {
    return s + ".min_realized." + std::to_string(dim);
}
Name Name::max_realized(int dim) const {
    return s + ".max_realized." + std::to_string(dim);
}
Name Name::extent_realized(int dim) const {
    return s + ".extent_realized." + std::to_string(dim);
}
Name Name::total_extent(int dim) const {
    return s + ".total_extent" + std::to_string(dim);
}
Name Name::total_extent_bytes() const {
    return s + ".total_extent_bytes";
}
Name Name::stride(int dim) const {
    return s + ".stride." + std::to_string(dim);
}
Name Name::extent(int dim) const {
    return s + ".extent." + std::to_string(dim);
}
Name Name::min(int dim) const {
    return s + ".min." + std::to_string(dim);
}
Name Name::tuple_component(int tuple_index) const {
    return s + "." + std::to_string(tuple_index);
}
Name Name::buffer() const {
    return s + ".buffer";
}
Name Name::stage(int stage) const {
    return s + ".s" + std::to_string(stage);
}
bool Name::belongs_to_func(const Name &func) const {
    return starts_with(s, func.s + ".");
}
bool Name::matches_var(const Name &var) const {
    return ends_with(s, "." + var.s);
}
Name Name::bounds_query() const {
    return s + ".bounds_query";
}
Name Name::bounds_query(const Name &func) const {
    return s + ".bounds_query." + func.s;
}
Name Name::outer_bounds_query() const {
    return s + ".outer_bounds_query";
}
Name Name::output(int i) const {
    return s + ".o" + std::to_string(i);
}
Name Name::unbounded() const {
    return s + ".unbounded";
}
Name Name::guarded() const {
    return s + ".guarded";
}
Name Name::unqualified() const {
    size_t last_dot = s.rfind('.');
    if (last_dot == std::string::npos) {
        return *this;
    } else {
        return s.substr(last_dot + 1);
    }
}
}  // namespace Internal
}  // namespace Halide
