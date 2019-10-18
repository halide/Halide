#include "Parameter.h"
#include "Argument.h"
#include "IR.h"
#include "IROperator.h"
#include "ObjectInstanceRegistry.h"
#include "Simplify.h"

namespace Halide {
namespace Internal {

struct BufferConstraint {
    Expr min, extent, stride;
    Expr min_estimate, extent_estimate;
};

struct ParameterContents {
    mutable RefCount ref_count;
    const Type type;
    const int dimensions;
    const std::string name;
    Buffer<> buffer;
    uint64_t data;
    int host_alignment;
    std::vector<BufferConstraint> buffer_constraints;
    Expr scalar_min, scalar_max, scalar_estimate;
    const bool is_buffer;

    ParameterContents(Type t, bool b, int d, const std::string &n)
        : type(t), dimensions(d), name(n), buffer(Buffer<>()), data(0),
          host_alignment(t.bytes()), buffer_constraints(dimensions), is_buffer(b) {
        // stride_constraint[0] defaults to 1. This is important for
        // dense vectorization. You can unset it by setting it to a
        // null expression. (param.set_stride(0, Expr());)
        if (dimensions > 0) {
            buffer_constraints[0].stride = 1;
        }
    }
};

template<>
RefCount &ref_count<Halide::Internal::ParameterContents>(const ParameterContents *p) noexcept {
    return p->ref_count;
}

template<>
void destroy<Halide::Internal::ParameterContents>(const ParameterContents *p) {
    delete p;
}

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

Parameter::Parameter(Type t, bool is_buffer, int d)
    : contents(new ParameterContents(t, is_buffer, d, unique_name('p'))) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
}

Parameter::Parameter(Type t, bool is_buffer, int d, const std::string &name)
    : contents(new ParameterContents(t, is_buffer, d, name)) {
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

bool Parameter::is_buffer() const {
    check_defined();
    return contents->is_buffer;
}

Expr Parameter::scalar_expr() const {
    check_is_scalar();
    const Type t = type();
    if (t.is_float()) {
        switch (t.bits()) {
        case 16:
            if (t.is_bfloat()) {
                return Expr(scalar<bfloat16_t>());
            } else {
                return Expr(scalar<float16_t>());
            }
        case 32:
            return Expr(scalar<float>());
        case 64:
            return Expr(scalar<double>());
        }
    } else if (t.is_int()) {
        switch (t.bits()) {
        case 8:
            return Expr(scalar<int8_t>());
        case 16:
            return Expr(scalar<int16_t>());
        case 32:
            return Expr(scalar<int32_t>());
        case 64:
            return Expr(scalar<int64_t>());
        }
    } else if (t.is_uint()) {
        switch (t.bits()) {
        case 1:
            return make_bool(scalar<bool>());
        case 8:
            return Expr(scalar<uint8_t>());
        case 16:
            return Expr(scalar<uint16_t>());
        case 32:
            return Expr(scalar<uint32_t>());
        case 64:
            return Expr(scalar<uint64_t>());
        }
    } else if (t.is_handle()) {
        // handles are always uint64 internally.
        switch (t.bits()) {
        case 64:
            return Expr(scalar<uint64_t>());
        }
    }
    internal_error << "Unsupported type " << t << " in scalar_expr\n";
    return Expr();
}

Buffer<> Parameter::buffer() const {
    check_is_buffer();
    return contents->buffer;
}

const halide_buffer_t *Parameter::raw_buffer() const {
    if (!is_buffer()) return nullptr;
    return contents->buffer.raw_buffer();
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
    contents->buffer_constraints[dim].min = e;
}

void Parameter::set_extent_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].extent = e;
}

void Parameter::set_stride_constraint(int dim, Expr e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].stride = e;
}

void Parameter::set_min_constraint_estimate(int dim, Expr min) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].min_estimate = min;
}

void Parameter::set_extent_constraint_estimate(int dim, Expr extent) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].extent_estimate = extent;
}

void Parameter::set_host_alignment(int bytes) {
    check_is_buffer();
    contents->host_alignment = bytes;
}

Expr Parameter::min_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->buffer_constraints[dim].min;
}

Expr Parameter::extent_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->buffer_constraints[dim].extent;
}

Expr Parameter::stride_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->buffer_constraints[dim].stride;
}

Expr Parameter::min_constraint_estimate(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->buffer_constraints[dim].min_estimate;
}

Expr Parameter::extent_constraint_estimate(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return contents->buffer_constraints[dim].extent_estimate;
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

        user_assert(is_const(e))
            << "Min value for parameter " << name()
            << " must be constant: " << e << "\n";
    }
    contents->scalar_min = e;
}

Expr Parameter::min_value() const {
    check_is_scalar();
    return contents->scalar_min;
}

void Parameter::set_max_value(Expr e) {
    check_is_scalar();
    if (e.defined()) {
        user_assert(e.type() == contents->type)
            << "Can't set parameter " << name()
            << " of type " << contents->type
            << " to have max value " << e
            << " of type " << e.type() << "\n";

        user_assert(is_const(e))
            << "Max value for parameter " << name()
            << " must be constant: " << e << "\n";
    }
    contents->scalar_max = e;
}

Expr Parameter::max_value() const {
    check_is_scalar();
    return contents->scalar_max;
}

void Parameter::set_estimate(Expr e) {
    check_is_scalar();
    contents->scalar_estimate = e;
}

Expr Parameter::estimate() const {
    check_is_scalar();
    return contents->scalar_estimate;
}

ArgumentEstimates Parameter::get_argument_estimates() const {
    ArgumentEstimates argument_estimates;
    if (!is_buffer()) {
        argument_estimates.scalar_def = scalar_expr();
        argument_estimates.scalar_min = min_value();
        argument_estimates.scalar_max = max_value();
        argument_estimates.scalar_estimate = estimate();
    } else {
        argument_estimates.buffer_estimates.resize(dimensions());
        for (int i = 0; i < dimensions(); i++) {
            argument_estimates.buffer_estimates[i].min = min_constraint_estimate(i);
            argument_estimates.buffer_estimates[i].extent = extent_constraint_estimate(i);
        }
    }
    return argument_estimates;
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
            user_error << "Implicit cast from " << t << " to int in argument " << (i + 1)
                       << " in call to \"" << name << "\" is not allowed. Use an explicit cast.\n";
        }
        // We're allowed to implicitly cast from other varieties of int
        if (t != Int(32)) {
            (*args)[i] = Cast::make(Int(32), (*args)[i]);
        }
    }
}

}  // namespace Internal
}  // namespace Halide
