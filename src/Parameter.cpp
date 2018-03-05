#include "IR.h"
#include "IROperator.h"
#include "ObjectInstanceRegistry.h"
#include "Parameter.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

struct ParameterContents {
    mutable RefCount ref_count;
    const Type type;
    const int dimensions;
    const std::string name;
    const std::string handle_type;
    Buffer<> buffer;
    uint64_t data;
    int host_alignment;
    std::vector<Expr> min_constraint;
    std::vector<Expr> extent_constraint;
    std::vector<Expr> stride_constraint;
    std::vector<Expr> min_constraint_estimate;
    std::vector<Expr> extent_constraint_estimate;
    Expr min_value, max_value;
    Expr estimate;
    const bool is_buffer;
    const bool is_explicit_name;

    ParameterContents(Type t, bool b, int d, const std::string &n, bool e)
        : type(t), dimensions(d), name(n), buffer(Buffer<>()), data(0),
          host_alignment(t.bytes()), is_buffer(b), is_explicit_name(e) {

        min_constraint.resize(dimensions);
        extent_constraint.resize(dimensions);
        stride_constraint.resize(dimensions);
        min_constraint_estimate.resize(dimensions);
        extent_constraint_estimate.resize(dimensions);

        // stride_constraint[0] defaults to 1. This is important for
        // dense vectorization. You can unset it by setting it to a
        // null expression. (param.set_stride(0, Expr());)
        if (dimensions > 0) {
            stride_constraint[0] = 1;
        }
    }
};

template<>
RefCount &ref_count<Halide::Internal::ParameterContents>(const ParameterContents *p) {return p->ref_count;}

template<>
void destroy<Halide::Internal::ParameterContents>(const ParameterContents *p) {delete p;}

void Parameter::check_defined() const {
    user_assert(defined()) << "Parameter is undefined\n";
}

void Parameter::check_is_buffer() const {
    check_defined();
    user_assert(contents->is_buffer) << "Parameter " << name() << " is not a Buffer\n";
}

void Parameter::check_is_scalar() const {
    check_defined();
    user_assert(!contents->is_buffer) << "Parameter " << name() << " is a Buffer\n";
}

void Parameter::check_dim_ok(int dim) const {
    user_assert(dim >= 0 && dim < dimensions())
        << "Dimension " << dim << " is not in the range [0, " << dimensions() - 1 << "]\n";
}

Parameter::Parameter(Type t, bool is_buffer, int d) :
    contents(new ParameterContents(t, is_buffer, d, unique_name('p'), false)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
}

Parameter::Parameter(Type t, bool is_buffer, int d, const std::string &name, bool is_explicit_name) :
    contents(new ParameterContents(t, is_buffer, d, name, is_explicit_name)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
}

Type Parameter::type() const {
    check_defined();
    return contents->type;
}

int Parameter::dimensions() const {
    check_defined();
    return contents->dimensions;
}

const std::string &Parameter::name() const {
    check_defined();
    return contents->name;
}

bool Parameter::is_explicit_name() const {
    check_defined();
    return contents->is_explicit_name;
}

bool Parameter::is_buffer() const {
    check_defined();
    return contents->is_buffer;
}

Expr Parameter::scalar_expr() const {
    check_is_scalar();
    const Type t = type();
    if (t.is_float()) {
        switch (t.bits()) {
        case 16: return Expr(scalar<float16_t>());
        case 32: return Expr(scalar<float>());
        case 64: return Expr(scalar<double>());
        }
    } else if (t.is_int()) {
        switch (t.bits()) {
        case 8: return Expr(scalar<int8_t>());
        case 16: return Expr(scalar<int16_t>());
        case 32: return Expr(scalar<int32_t>());
        case 64: return Expr(scalar<int64_t>());
        }
    } else if (t.is_uint()) {
        switch (t.bits()) {
        case 1: return make_bool(scalar<bool>());
        case 8: return Expr(scalar<uint8_t>());
        case 16: return Expr(scalar<uint16_t>());
        case 32: return Expr(scalar<uint32_t>());
        case 64: return Expr(scalar<uint64_t>());
        }
    } else if (t.is_handle()) {
        // handles are always uint64 internally.
        switch (t.bits()) {
        case 64: return Expr(scalar<uint64_t>());
        }
    }
    internal_error << "Unsupported type " << t << " in scalar_expr\n";
    return Expr();
}

Buffer<> Parameter::buffer() const {
    check_is_buffer();
    return contents->buffer;
}

void Parameter::set_buffer(Buffer<> b) {
    check_is_buffer();
    if (b.defined()) {
        user_assert(contents->type == b.type())
            << "Can't bind Parameter " << name()
            << " of type " << contents->type
            << " to Buffer " << b.name()
            << " of type " << Type(b.type()) << "\n";
    }
    contents->buffer = b;
}

void *Parameter::scalar_address() const {
    check_is_scalar();
    return &contents->data;
}

/** Tests if this handle is the same as another handle */
bool Parameter::same_as(const Parameter &other) const {
    return contents.same_as(other.contents);
}

/** Tests if this handle is non-nullptr */
bool Parameter::defined() const {
    return contents.defined();
}

void Parameter::set_min_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->min_constraint[dim] = e;
}

void Parameter::set_extent_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->extent_constraint[dim] = e;
}

void Parameter::set_stride_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->stride_constraint[dim] = e;
}

void Parameter::set_min_constraint_estimate(int dim, Expr min) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->min_constraint_estimate[dim] = min;
}

void Parameter::set_extent_constraint_estimate(int dim, Expr extent) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->extent_constraint_estimate[dim] = extent;
}

void Parameter::set_host_alignment(int bytes) {
    check_is_buffer();
    contents->host_alignment = bytes;
}

Expr Parameter::min_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->min_constraint[dim];
}

Expr Parameter::extent_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->extent_constraint[dim];
}

Expr Parameter::stride_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->stride_constraint[dim];
}

Expr Parameter::min_constraint_estimate(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->min_constraint_estimate[dim];
}

Expr Parameter::extent_constraint_estimate(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->extent_constraint_estimate[dim];
}

int Parameter::host_alignment() const {
    check_is_buffer();
    return contents->host_alignment;
}
void Parameter::set_min_value(Expr e) {
    check_is_scalar();
    if (e.defined()) {
        user_assert(e.type() == contents->type)
            << "Can't set parameter " << name()
            << " of type " << contents->type
            << " to have min value " << e
            << " of type " << e.type() << "\n";
    }
    contents->min_value = e;
}

Expr Parameter::min_value() const {
    check_is_scalar();
    return contents->min_value;
}

void Parameter::set_max_value(Expr e) {
    check_is_scalar();
    if (e.defined()) {
        user_assert(e.type() == contents->type)
            << "Can't set parameter " << name()
            << " of type " << contents->type
            << " to have max value " << e
            << " of type " << e.type() << "\n";
    }
    contents->max_value = e;
}

Expr Parameter::max_value() const {
    check_is_scalar();
    return contents->max_value;
}

void Parameter::set_estimate(Expr e) {
    check_is_scalar();
    contents->estimate = e;
}

Expr Parameter::estimate() const {
    check_is_scalar();
    return contents->estimate;
}

void check_call_arg_types(const std::string &name, std::vector<Expr> *args, int dims) {
    user_assert(args->size() == (size_t)dims)
        << args->size() << "-argument call to \""
        << name << "\", which has " << dims << " dimensions.\n";

    for (size_t i = 0; i < args->size(); i++) {
        user_assert((*args)[i].defined())
            << "Argument " << i << " to call to \"" << name << "\" is an undefined Expr\n";
        Type t = (*args)[i].type();
        if (t.is_float() || (t.is_uint() && t.bits() >= 32) || (t.is_int() && t.bits() > 32)) {
            user_error << "Implicit cast from " << t << " to int in argument " << (i+1)
                       << " in call to \"" << name << "\" is not allowed. Use an explicit cast.\n";
        }
        // We're allowed to implicitly cast from other varieties of int
        if (t != Int(32)) {
            (*args)[i] = Cast::make(Int(32), (*args)[i]);
        }
    }
}

void RegisteredParameter::register_if_needed() {
    if (defined()) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, nullptr);
    }
}

void RegisteredParameter::unregister_if_needed() {
    if (defined()) {
        // This will assert-fail if not registered.
        ObjectInstanceRegistry::unregister_instance(this);
    }
}

RegisteredParameter::RegisteredParameter(Type t, bool is_buffer, int d, const std::string &name, bool is_explicit_name)
    : Parameter(t, is_buffer, d, name, is_explicit_name) {
    register_if_needed();
}

RegisteredParameter::RegisteredParameter(const Parameter& param) : Parameter(param) {
    register_if_needed();
}

RegisteredParameter& RegisteredParameter::operator=(const Parameter& param) {
    unregister_if_needed();
    Parameter::operator=(param);
    register_if_needed();
    return *this;
}

RegisteredParameter::RegisteredParameter(const RegisteredParameter& param) : Parameter(param) {
    register_if_needed();
}

RegisteredParameter& RegisteredParameter::operator=(const RegisteredParameter& param) {
    unregister_if_needed();
    Parameter::operator=(param);
    register_if_needed();
    return *this;
}

RegisteredParameter::RegisteredParameter(RegisteredParameter&& that) {
    that.unregister_if_needed();
    this->contents = std::move(that.contents);
    that.contents = nullptr;
    this->register_if_needed();
}

RegisteredParameter& RegisteredParameter::operator=(RegisteredParameter&& that) {
    that.unregister_if_needed();
    this->unregister_if_needed();

    this->contents = std::move(that.contents);
    that.contents = nullptr;

    this->register_if_needed();
    return *this;
}

RegisteredParameter::~RegisteredParameter() {
    unregister_if_needed();
}


}
}
