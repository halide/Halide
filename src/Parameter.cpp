#include "IR.h"
#include "ObjectInstanceRegistry.h"
#include "Parameter.h"

namespace Halide {
namespace Internal {

struct ParameterContents {
    mutable RefCount ref_count;
    Type type;
    const bool is_buffer;
    int dimensions;
    const bool is_explicit_name;
    const bool is_registered;
    std::string name;
    Buffer buffer;
    uint64_t data;
    Expr min_constraint[4];
    Expr extent_constraint[4];
    Expr stride_constraint[4];
    Expr min_value, max_value;
    ParameterContents(Type t, bool b, int d, const std::string &n, bool e, bool r)
        : type(t), is_buffer(b), dimensions(d), is_explicit_name(e), is_registered(r), name(n), buffer(Buffer()), data(0) {
        // stride_constraint[0] defaults to 1. This is important for
        // dense vectorization. You can unset it by setting it to a
        // null expression. (param.set_stride(0, Expr());)
        stride_constraint[0] = 1;
    }
};

template<>
EXPORT RefCount &ref_count<Halide::Internal::ParameterContents>(const ParameterContents *p) {return p->ref_count;}

template<>
EXPORT void destroy<Halide::Internal::ParameterContents>(const ParameterContents *p) {delete p;}

void Parameter::check_defined() const {
    user_assert(defined()) << "Parameter is undefined\n";
}

void Parameter::check_is_buffer() const {
    check_defined();
    user_assert(contents.ptr->is_buffer) << "Parameter " << name() << " is not a Buffer\n";
}

void Parameter::check_is_scalar() const {
    check_defined();
    user_assert(!contents.ptr->is_buffer) << "Parameter " << name() << " is a Buffer\n";
}

void Parameter::check_dim_ok(int dim) const {
    user_assert(dim >= 0 && dim < dimensions())
        << "Dimension " << dim << " is not in the range [0, " << dimensions() - 1 << "]\n";
}

Parameter::Parameter() : contents(NULL) {
    // Undefined Parameters are never registered.
}

Parameter::Parameter(Type t, bool is_buffer, int d) :
    contents(new ParameterContents(t, is_buffer, d, unique_name('p'), false, true)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
    // Note that is_registered is always true here; this is just using a parallel code structure for clarity.
    if (contents.defined() && contents.ptr->is_registered) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, NULL);
    }
}

Parameter::Parameter(Type t, bool is_buffer, int d, const std::string &name, bool is_explicit_name, bool register_instance) :
    contents(new ParameterContents(t, is_buffer, d, name, is_explicit_name, register_instance)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
    if (contents.defined() && contents.ptr->is_registered) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, NULL);
    }
}

Parameter::Parameter(const Parameter& that) : contents(that.contents) {
    if (contents.defined() && contents.ptr->is_registered) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, NULL);
    }
}

Parameter& Parameter::operator=(const Parameter& that) {
    bool was_registered = contents.defined() && contents.ptr->is_registered;
    contents = that.contents;
    bool should_be_registered = contents.defined() && contents.ptr->is_registered;
    if (should_be_registered && !was_registered) {
        // This can happen if you do:
        // Parameter p; // undefined
        // p = make_interesting_parameter();
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, NULL);
    } else if (!should_be_registered && was_registered) {
        // This can happen if you do:
        // Parameter p = make_interesting_parameter();
        // p = Parameter();
        ObjectInstanceRegistry::unregister_instance(this);
    }
    return *this;
}

Parameter::~Parameter() {
    if (contents.defined() && contents.ptr->is_registered) {
        ObjectInstanceRegistry::unregister_instance(this);
    }
}

Type Parameter::type() const {
    check_defined();
    return contents.ptr->type;
}

int Parameter::dimensions() const {
    check_defined();
    return contents.ptr->dimensions;
}

const std::string &Parameter::name() const {
    check_defined();
    return contents.ptr->name;
}

bool Parameter::is_explicit_name() const {
    check_defined();
    return contents.ptr->is_explicit_name;
}

bool Parameter::is_buffer() const {
    check_defined();
    return contents.ptr->is_buffer;
}


Buffer Parameter::get_buffer() const {
    check_is_buffer();
    return contents.ptr->buffer;
}

void Parameter::set_buffer(Buffer b) {
    check_is_buffer();
    if (b.defined()) {
        user_assert(contents.ptr->type == b.type())
            << "Can't bind Parameter " << name()
            << " of type " << contents.ptr->type
            << " to Buffer " << b.name()
            << " of type " << b.type() << "\n";
    }
    contents.ptr->buffer = b;
}

void *Parameter::get_scalar_address() const {
    check_is_scalar();
    return &contents.ptr->data;
}

/** Tests if this handle is the same as another handle */
bool Parameter::same_as(const Parameter &other) const {
    return contents.ptr == other.contents.ptr;
}

/** Tests if this handle is non-NULL */
bool Parameter::defined() const {
    return contents.defined();
}

void Parameter::set_min_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents.ptr->min_constraint[dim] = e;
}

void Parameter::set_extent_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents.ptr->extent_constraint[dim] = e;
}

void Parameter::set_stride_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents.ptr->stride_constraint[dim] = e;
}

Expr Parameter::min_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents.ptr->min_constraint[dim];
}

Expr Parameter::extent_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents.ptr->extent_constraint[dim];
}

Expr Parameter::stride_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents.ptr->stride_constraint[dim];
}

void Parameter::set_min_value(Expr e) {
    check_is_scalar();
    user_assert(e.type() == contents.ptr->type)
        << "Can't set parameter " << name()
        << " of type " << contents.ptr->type
        << " to have min value " << e
        << " of type " << e.type() << "\n";
    contents.ptr->min_value = e;
}

Expr Parameter::get_min_value() const {
    check_is_scalar();
    return contents.ptr->min_value;
}

void Parameter::set_max_value(Expr e) {
    check_is_scalar();
    user_assert(e.type() == contents.ptr->type)
        << "Can't set parameter " << name()
        << " of type " << contents.ptr->type
        << " to have max value " << e
        << " of type " << e.type() << "\n";
    contents.ptr->max_value = e;
}

Expr Parameter::get_max_value() const {
    check_is_scalar();
    return contents.ptr->max_value;
}

void check_call_arg_types(const std::string &name, std::vector<Expr> *args, int dims) {
    user_assert(args->size() == (size_t)dims)
        << args->size() << "-argument call to \""
        << name << "\", which has " << dims << " dimensions.\n";

    for (size_t i = 0; i < args->size(); i++) {
        user_assert((*args)[i].defined())
            << "Argument " << i << " to call to \"" << name << "\" is an undefined Expr\n";
        Type t = (*args)[i].type();
        if (t.is_float() || (t.is_uint() && t.bits >= 32) || (t.is_int() && t.bits > 32)) {
            user_error << "Implicit cast from " << t << " to int in argument " << (i+1)
                       << " in call to \"" << name << "\" is not allowed. Use an explicit cast.\n";
        }
        // We're allowed to implicitly cast from other varieties of int
        if (t != Int(32)) {
            (*args)[i] = Internal::Cast::make(Int(32), (*args)[i]);
        }
    }
}



}
}

