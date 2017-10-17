#include "IR.h"
#include "IROperator.h"
#include "ObjectInstanceRegistry.h"
#include "Parameter.h"
#include "Simplify.h"
#include "Param.h"

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
    const bool is_registered;
    const bool is_bound_before_lowering;
    ParameterContents(Type t, bool b, int d, const std::string &n, bool e, bool r, bool is_bound_before_lowering)
        : type(t), dimensions(d), name(n), buffer(Buffer<>()), data(0),
          host_alignment(t.bytes()), is_buffer(b), is_explicit_name(e), is_registered(r),
          is_bound_before_lowering(is_bound_before_lowering) {

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
EXPORT RefCount &ref_count<Halide::Internal::ParameterContents>(const ParameterContents *p) {return p->ref_count;}

template<>
EXPORT void destroy<Halide::Internal::ParameterContents>(const ParameterContents *p) {delete p;}

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

Parameter::Parameter() : contents(nullptr) {
    // Undefined Parameters are never registered.
}

Parameter::Parameter(Type t, bool is_buffer, int d) :
    contents(new ParameterContents(t, is_buffer, d, unique_name('p'), false, true, false)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
    // Note that is_registered is always true here; this is just using a parallel code structure for clarity.
    if (contents.defined() && contents->is_registered) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, nullptr);
    }
}

Parameter::Parameter(Type t, bool is_buffer, int d, const std::string &name, bool is_explicit_name, bool register_instance, bool is_bound_before_lowering) :
    contents(new ParameterContents(t, is_buffer, d, name, is_explicit_name, register_instance, is_bound_before_lowering)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
    if (contents.defined() && contents->is_registered) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, nullptr);
    }
}

Parameter::Parameter(const Parameter& that) : contents(that.contents) {
    if (contents.defined() && contents->is_registered) {
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, nullptr);
    }
}

Parameter& Parameter::operator=(const Parameter& that) {
    bool was_registered = contents.defined() && contents->is_registered;
    contents = that.contents;
    bool should_be_registered = contents.defined() && contents->is_registered;
    if (should_be_registered && !was_registered) {
        // This can happen if you do:
        // Parameter p; // undefined
        // p = make_interesting_parameter();
        ObjectInstanceRegistry::register_instance(this, 0, ObjectInstanceRegistry::FilterParam, this, nullptr);
    } else if (!should_be_registered && was_registered) {
        // This can happen if you do:
        // Parameter p = make_interesting_parameter();
        // p = Parameter();
        ObjectInstanceRegistry::unregister_instance(this);
    }
    return *this;
}

Parameter::~Parameter() {
    if (contents.defined() && contents->is_registered) {
        ObjectInstanceRegistry::unregister_instance(this);
    }
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

bool Parameter::is_bound_before_lowering() const {
    check_defined();
    return contents->is_bound_before_lowering;
}

Expr Parameter::get_scalar_expr() const {
    check_is_scalar();
    const Type t = type();
    if (t.is_float()) {
        switch (t.bits()) {
        case 32: return Expr(get_scalar<float>());
        case 64: return Expr(get_scalar<double>());
        }
    } else if (t.is_int()) {
        switch (t.bits()) {
        case 8: return Expr(get_scalar<int8_t>());
        case 16: return Expr(get_scalar<int16_t>());
        case 32: return Expr(get_scalar<int32_t>());
        case 64: return Expr(get_scalar<int64_t>());
        }
    } else if (t.is_uint()) {
        switch (t.bits()) {
        case 1: return make_bool(get_scalar<bool>());
        case 8: return Expr(get_scalar<uint8_t>());
        case 16: return Expr(get_scalar<uint16_t>());
        case 32: return Expr(get_scalar<uint32_t>());
        case 64: return Expr(get_scalar<uint64_t>());
        }
    } else if (t.is_handle()) {
        // handles are always uint64 internally.
        switch (t.bits()) {
        case 64: return Expr(get_scalar<uint64_t>());
        }
    }
    internal_error << "Unsupported type " << t << " in get_scalar_expr\n";
    return Expr();
}

Buffer<> Parameter::get_buffer() const {
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

void *Parameter::get_scalar_address() const {
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

Expr Parameter::get_min_value() const {
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

Expr Parameter::get_max_value() const {
    check_is_scalar();
    return contents->max_value;
}

void Parameter::set_estimate(Expr e) {
    check_is_scalar();
    contents->estimate = e;
}

Expr Parameter::get_estimate() const {
    check_is_scalar();
    return contents->estimate;
}

Dimension::Dimension(const Internal::Parameter &p, int d) : param(p), d(d) {
    user_assert(param.defined())
        << "Can't access the dimensions of an undefined Parameter\n";
    user_assert(param.is_buffer())
        << "Can't access the dimensions of a scalar Parameter\n";
    user_assert(d >= 0 && d < param.dimensions())
        << "Can't access dimension " << d
        << " of a " << param.dimensions() << "-dimensional Parameter\n";
}

Expr Dimension::min() const {
    std::ostringstream s;
    s << param.name() << ".min." << d;
    return Variable::make(Int(32), s.str(), param);
}

Expr Dimension::extent() const {
    std::ostringstream s;
    s << param.name() << ".extent." << d;
    return Variable::make(Int(32), s.str(), param);
}

Expr Dimension::max() const {
    return min() + extent() - 1;
}

Expr Dimension::min_estimate() const {
    return param.min_constraint_estimate(d);
}

Expr Dimension::extent_estimate() const {
    return param.extent_constraint_estimate(d);
}

Expr Dimension::stride() const {
    std::ostringstream s;
    s << param.name() << ".stride." << d;
    return Variable::make(Int(32), s.str(), param);
}

Dimension Dimension::set_extent(Expr extent) {
    param.set_extent_constraint(d, extent);
    return *this;
}

Dimension Dimension::set_min(Expr min) {
    param.set_min_constraint(d, min);
    return *this;
}

Dimension Dimension::set_stride(Expr stride) {
    param.set_stride_constraint(d, stride);
    return *this;
}

Dimension Dimension::set_bounds(Expr min, Expr extent) {
    return set_min(min).set_extent(extent);
}

Dimension Dimension::set_min_estimate(Expr min) {
    param.set_min_constraint_estimate(d, min);
    return *this;
}

Dimension Dimension::set_extent_estimate(Expr stride) {
    param.set_extent_constraint_estimate(d, stride);
    return *this;
}

Dimension Dimension::set_bounds_estimate(Expr min, Expr extent) {
    return set_min_estimate(min).set_extent_estimate(extent);
}

Dimension Dimension::dim(int i) {
    return Dimension(param, i);
}

const Dimension Dimension::dim(int i) const {
    return Dimension(param, i);
}

Dimension DimensionedParameter::dim(int i) {
    return Dimension(parameter(), i);
}

const Dimension DimensionedParameter::dim(int i) const {
    return Dimension(parameter(), i);
}

int DimensionedParameter::host_alignment() const {
    return parameter().host_alignment();
}

DimensionedParameter &DimensionedParameter::set_host_alignment(int bytes) {
    parameter().set_host_alignment(bytes);
    return *this;
}

int DimensionedParameter::dimensions() const {
    return parameter().dimensions();
}

Expr DimensionedParameter::left() const {
    user_assert(dimensions() > 0) << "Can't ask for the left of a zero-dimensional image\n";
    return dim(0).min();
}

Expr DimensionedParameter::right() const {
    user_assert(dimensions() > 0) << "Can't ask for the right of a zero-dimensional image\n";
    return dim(0).max();
}

Expr DimensionedParameter::top() const {
    user_assert(dimensions() > 1) << "Can't ask for the top of a zero- or one-dimensional image\n";
    return dim(1).min();
}

Expr DimensionedParameter::bottom() const {
    user_assert(dimensions() > 1) << "Can't ask for the bottom of a zero- or one-dimensional image\n";
    return dim(1).max();
}

Expr DimensionedParameter::width() const {
    user_assert(dimensions() > 0) << "Can't ask for the width of a zero-dimensional image\n";
    return dim(0).extent();
}

Expr DimensionedParameter::height() const {
    user_assert(dimensions() > 1) << "Can't ask for the height of a zero or one-dimensional image\n";
    return dim(1).extent();
}

Expr DimensionedParameter::channels() const {
    user_assert(dimensions() > 2) << "Can't ask for the channels of an image with fewer than three dimensions\n";
    return dim(2).extent();
}

DimensionedParameter::operator ExternFuncArgument() const {
    return parameter();
}

DimensionedParameter::operator DimensionedParam() const {
    return DimensionedParam(parameter());
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



}
}
