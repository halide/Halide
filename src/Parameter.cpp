#include "Parameter.h"

#include "Argument.h"
#include "Float16.h"
#include "IR.h"
#include "IRMutator.h"
#include "IROperator.h"

namespace Halide {
namespace Internal {

struct ParameterContents {
    mutable RefCount ref_count;
    const Type type;
    const int dimensions;
    const std::string name;
    Buffer<> buffer;
    std::optional<halide_scalar_value_t> scalar_data;
    int host_alignment;
    std::vector<BufferConstraint> buffer_constraints;
    Expr scalar_default, scalar_min, scalar_max, scalar_estimate;
    const bool is_buffer;
    MemoryType memory_type = MemoryType::Auto;

    ParameterContents(Type t, bool b, int d, const std::string &n)
        : type(t), dimensions(d), name(n), buffer(Buffer<>()),
          host_alignment(t.bytes()), buffer_constraints(std::max(0, dimensions)), is_buffer(b) {
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

}  // namespace Internal

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

void Parameter::check_type(const Type &t) const {
    // Allow set_scalar<uint64_t>() for all Handle types
    user_assert(type() == t || (type().is_handle() && t == UInt(64)))
        << "Param<" << type()
        << "> cannot be accessed as scalar of type " << t << "\n";
}

Parameter::Parameter(const Type &t, bool is_buffer, int d)
    : contents(new Internal::ParameterContents(t, is_buffer, d, Internal::unique_name('p'))) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
}

Parameter::Parameter(const Type &t, bool is_buffer, int d, const std::string &name)
    : contents(new Internal::ParameterContents(t, is_buffer, d, name)) {
    internal_assert(is_buffer || d == 0) << "Scalar parameters should be zero-dimensional";
}

Parameter::Parameter(const Type &t, int dimensions, const std::string &name,
                     const Buffer<void> &buffer, int host_alignment, const std::vector<BufferConstraint> &buffer_constraints,
                     MemoryType memory_type)
    : contents(new Internal::ParameterContents(t, /*is_buffer*/ true, dimensions, name)) {
    contents->buffer = buffer;
    contents->host_alignment = host_alignment;
    contents->buffer_constraints = buffer_constraints;
    contents->memory_type = memory_type;
}

Parameter::Parameter(const Type &t, int dimensions, const std::string &name,
                     const std::optional<halide_scalar_value_t> &scalar_data, const Expr &scalar_default, const Expr &scalar_min,
                     const Expr &scalar_max, const Expr &scalar_estimate)
    : contents(new Internal::ParameterContents(t, /*is_buffer*/ false, dimensions, name)) {
    contents->scalar_data = scalar_data;
    contents->scalar_default = scalar_default;
    contents->scalar_min = scalar_min;
    contents->scalar_max = scalar_max;
    contents->scalar_estimate = scalar_estimate;
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

bool Parameter::has_scalar_value() const {
    return defined() && !contents->is_buffer && contents->scalar_data.has_value();
}

Expr Parameter::scalar_expr() const {
    const auto sv = scalar_data_checked();
    const Type t = type();
    if (t.is_float()) {
        switch (t.bits()) {
        case 16:
            if (t.is_bfloat()) {
                return Expr(bfloat16_t::make_from_bits(sv.u.u16));
            } else {
                return Expr(float16_t::make_from_bits(sv.u.u16));
            }
        case 32:
            return Expr(sv.u.f32);
        case 64:
            return Expr(sv.u.f64);
        default:
            break;
        }
    } else if (t.is_int()) {
        switch (t.bits()) {
        case 8:
            return Expr(sv.u.i8);
        case 16:
            return Expr(sv.u.i16);
        case 32:
            return Expr(sv.u.i32);
        case 64:
            return Expr(sv.u.i64);
        default:
            break;
        }
    } else if (t.is_uint()) {
        switch (t.bits()) {
        case 1:
            return Internal::make_bool(sv.u.b);
        case 8:
            return Expr(sv.u.u8);
        case 16:
            return Expr(sv.u.u16);
        case 32:
            return Expr(sv.u.u32);
        case 64:
            return Expr(sv.u.u64);
        default:
            break;
        }
    } else if (t.is_handle()) {
        // handles are always uint64 internally.
        switch (t.bits()) {
        case 64:
            return Expr(sv.u.u64);
        default:
            break;
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
    if (!is_buffer()) {
        return nullptr;
    }
    return contents->buffer.raw_buffer();
}

void Parameter::set_buffer(const Buffer<> &b) {
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

const void *Parameter::read_only_scalar_address() const {
    check_is_scalar();
    // Use explicit if here (rather than user_assert) so that we don't
    // have to disable bugprone-unchecked-optional-access in clang-tidy,
    // which is a useful check.
    const auto &sv = contents->scalar_data;
    if (sv.has_value()) {
        return std::addressof(sv.value());
    } else {
        user_error << "Parameter " << name() << " does not have a valid scalar value.\n";
        return nullptr;
    }
}

std::optional<halide_scalar_value_t> Parameter::scalar_data() const {
    return defined() ? contents->scalar_data : std::nullopt;
}

halide_scalar_value_t Parameter::scalar_data_checked() const {
    check_is_scalar();
    // Use explicit if here (rather than user_assert) so that we don't
    // have to disable bugprone-unchecked-optional-access in clang-tidy,
    // which is a useful check.
    halide_scalar_value_t result;
    const auto &sv = contents->scalar_data;
    if (sv.has_value()) {
        result = sv.value();
    } else {
        user_error << "Parameter " << name() << " does not have a valid scalar value.\n";
        result.u.u64 = 0;  // silence "possibly uninitialized" compiler warning
    }
    return result;
}

halide_scalar_value_t Parameter::scalar_data_checked(const Type &val_type) const {
    check_type(val_type);
    return scalar_data_checked();
}

void Parameter::set_scalar(const Type &val_type, halide_scalar_value_t val) {
    check_type(val_type);
    contents->scalar_data = std::optional<halide_scalar_value_t>(val);
}

/** Tests if this handle is the same as another handle */
bool Parameter::same_as(const Parameter &other) const {
    return contents.same_as(other.contents);
}

/** Tests if this handle is non-nullptr */
bool Parameter::defined() const {
    return contents.defined();
}

// Helper function to remove any references in a parameter constraint to the
// parameter itself, to avoid creating a reference count cycle and causing a
// leak. Note that it's still possible to create a cycle by having two different
// Parameters each have constraints that reference the other.
namespace Internal {

Expr remove_self_references(const Parameter &p, const Expr &e) {
    class RemoveSelfReferences : public IRMutator {
        using IRMutator::visit;

        Expr visit(const Variable *var) override {
            if (var->param.same_as(p)) {
                internal_assert(starts_with(var->name, p.name() + "."));
                return Variable::make(var->type, var->name);
            } else {
                internal_assert(!starts_with(var->name, p.name() + "."));
            }
            return var;
        }

    public:
        const Parameter &p;
        RemoveSelfReferences(const Parameter &p)
            : p(p) {
        }
    } mutator{p};
    return mutator.mutate(e);
}

Expr restore_self_references(const Parameter &p, const Expr &e) {
    class RestoreSelfReferences : public IRMutator {
        using IRMutator::visit;

        Expr visit(const Variable *var) override {
            if (!var->image.defined() &&
                !var->param.defined() &&
                !var->reduction_domain.defined() &&
                Internal::starts_with(var->name, p.name() + ".")) {
                return Internal::Variable::make(var->type, var->name, p);
            }
            return var;
        }

    public:
        const Parameter &p;
        RestoreSelfReferences(const Parameter &p)
            : p(p) {
        }
    } mutator{p};
    return mutator.mutate(e);
}

}  // namespace Internal

void Parameter::set_min_constraint(int dim, const Expr &e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].min = Internal::remove_self_references(*this, e);
}

void Parameter::set_extent_constraint(int dim, const Expr &e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].extent = Internal::remove_self_references(*this, e);
}

void Parameter::set_stride_constraint(int dim, const Expr &e) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].stride = Internal::remove_self_references(*this, e);
}

void Parameter::set_min_constraint_estimate(int dim, const Expr &min) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].min_estimate = Internal::remove_self_references(*this, min);
}

void Parameter::set_extent_constraint_estimate(int dim, const Expr &extent) {
    check_is_buffer();
    check_dim_ok(dim);
    contents->buffer_constraints[dim].extent_estimate = Internal::remove_self_references(*this, extent);
}

void Parameter::set_host_alignment(int bytes) {
    check_is_buffer();
    contents->host_alignment = bytes;
}

Expr Parameter::min_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return Internal::restore_self_references(*this, contents->buffer_constraints[dim].min);
}

Expr Parameter::extent_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return Internal::restore_self_references(*this, contents->buffer_constraints[dim].extent);
}

Expr Parameter::stride_constraint(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return Internal::restore_self_references(*this, contents->buffer_constraints[dim].stride);
}

Expr Parameter::min_constraint_estimate(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return Internal::restore_self_references(*this, contents->buffer_constraints[dim].min_estimate);
}

Expr Parameter::extent_constraint_estimate(int dim) const {
    check_is_buffer();
    check_dim_ok(dim);
    return Internal::restore_self_references(*this, contents->buffer_constraints[dim].extent_estimate);
}

int Parameter::host_alignment() const {
    check_is_buffer();
    return contents->host_alignment;
}

const std::vector<BufferConstraint> &Parameter::buffer_constraints() const {
    check_is_buffer();
    return contents->buffer_constraints;
}

void Parameter::set_default_value(const Expr &e) {
    check_is_scalar();
    if (e.defined()) {
        user_assert(e.type() == contents->type)
            << "Can't set parameter " << name()
            << " of type " << contents->type
            << " to have default value " << e
            << " of type " << e.type() << "\n";

        user_assert(is_const(e))
            << "Default value for parameter " << name()
            << " must be constant: " << e << "\n";
    }
    contents->scalar_default = e;
}

Expr Parameter::default_value() const {
    check_is_scalar();
    return contents->scalar_default;
}

void Parameter::set_min_value(const Expr &e) {
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

void Parameter::set_max_value(const Expr &e) {
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
    contents->scalar_estimate = std::move(e);
}

Expr Parameter::estimate() const {
    check_is_scalar();
    return contents->scalar_estimate;
}

ArgumentEstimates Parameter::get_argument_estimates() const {
    ArgumentEstimates argument_estimates;
    if (!is_buffer()) {
        argument_estimates.scalar_def = default_value();
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

void Parameter::store_in(MemoryType memory_type) {
    check_is_buffer();
    contents->memory_type = memory_type;
}

MemoryType Parameter::memory_type() const {
    // check_is_buffer();
    return contents->memory_type;
}

namespace Internal {

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
