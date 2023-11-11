#include "AddImageChecks.h"
#include "ExternFuncArgument.h"
#include "Function.h"
#include "IRMutator.h"
#include "IROperator.h"
#include "IRVisitor.h"
#include "Simplify.h"
#include "Substitute.h"
#include "Target.h"

namespace Halide {
namespace Internal {

using std::map;
using std::pair;
using std::string;
using std::vector;

namespace {

/* Find all the externally referenced buffers in a stmt */
class FindBuffers : public IRGraphVisitor {
public:
    struct Result {
        Buffer<> image;
        Parameter param;
        Type type;
        int dimensions{0};
        bool used_on_host{false};
    };

    map<string, Result> buffers;
    bool in_device_loop = false;

    using IRGraphVisitor::visit;

    void visit(const For *op) override {
        op->min.accept(this);
        op->extent.accept(this);
        bool old = in_device_loop;
        if (op->device_api != DeviceAPI::None &&
            op->device_api != DeviceAPI::Host) {
            in_device_loop = true;
        }
        op->body.accept(this);
        in_device_loop = old;
    }

    void visit(const Call *op) override {
        IRGraphVisitor::visit(op);
        if (op->image.defined()) {
            Result &r = buffers[op->name];
            r.image = op->image;
            r.type = op->type.element_of();
            r.dimensions = (int)op->args.size();
            r.used_on_host = r.used_on_host || (!in_device_loop);
        } else if (op->param.defined()) {
            Result &r = buffers[op->name];
            r.param = op->param;
            r.type = op->type.element_of();
            r.dimensions = (int)op->args.size();
            r.used_on_host = r.used_on_host || (!in_device_loop);
        }
    }

    void visit(const Provide *op) override {
        IRGraphVisitor::visit(op);
        if (op->values.size() == 1) {
            auto it = buffers.find(op->name);
            if (it != buffers.end() && !in_device_loop) {
                it->second.used_on_host = true;
            }
        } else {
            for (size_t i = 0; i < op->values.size(); i++) {
                string name = op->name + "." + std::to_string(i);
                auto it = buffers.find(name);
                if (it != buffers.end() && !in_device_loop) {
                    it->second.used_on_host = true;
                }
            }
        }
    }

    void visit(const Variable *op) override {
        if (op->param.defined() &&
            op->param.is_buffer() &&
            buffers.find(op->param.name()) == buffers.end()) {
            Result r;
            r.param = op->param;
            r.type = op->param.type();
            r.dimensions = op->param.dimensions();
            r.used_on_host = false;
            buffers[op->param.name()] = r;
        } else if (op->reduction_domain.defined()) {
            // The bounds of reduction domains are not yet defined,
            // and they may be the only reference to some parameters.
            op->reduction_domain.accept(this);
        }
    }
};

class TrimStmtToPartsThatAccessBuffers : public IRMutator {
    bool touches_buffer = false;
    const map<string, FindBuffers::Result> &buffers;

    using IRMutator::visit;

    Expr visit(const Call *op) override {
        touches_buffer |=
            (buffers.count(op->name) > 0) ||
            (buffers.count(op->name + "." + std::to_string(op->value_index)));
        // Output Tuple params are in the buffers map under their qualified
        // tuple name, not the Func name.
        return IRMutator::visit(op);
    }
    Stmt visit(const Provide *op) override {
        if (op->values.size() == 1) {
            touches_buffer |= (buffers.find(op->name) != buffers.end());
        } else {
            // It's a Tuple. Just check if the first Tuple component corresponds
            // to an output buffer. If it does, they all do.
            touches_buffer |= (buffers.find(op->name + ".0") != buffers.end());
        }
        return IRMutator::visit(op);
    }
    Expr visit(const Variable *op) override {
        if (op->type.is_handle() && op->param.defined() && op->param.is_buffer()) {
            touches_buffer |= (buffers.find(op->param.name()) != buffers.end());
        }
        return IRMutator::visit(op);
    }

    Stmt visit(const Block *op) override {
        bool old_touches_buffer = touches_buffer;
        touches_buffer = false;
        Stmt first = mutate(op->first);
        old_touches_buffer |= touches_buffer;
        if (!touches_buffer) {
            first = Evaluate::make(0);
        }
        touches_buffer = false;
        Stmt rest = mutate(op->rest);
        old_touches_buffer |= touches_buffer;
        if (!touches_buffer) {
            rest = Evaluate::make(0);
        }
        touches_buffer = old_touches_buffer;
        return Block::make(first, rest);
    }

public:
    TrimStmtToPartsThatAccessBuffers(const map<string, FindBuffers::Result> &bufs)
        : buffers(bufs) {
    }
};

Stmt add_image_checks_inner(Stmt s,
                            const vector<Function> &outputs,
                            const Target &t,
                            const vector<string> &order,
                            const map<string, Function> &env,
                            const FuncValueBounds &fb,
                            bool will_inject_host_copies) {

    bool no_asserts = t.has_feature(Target::NoAsserts);
    bool no_bounds_query = t.has_feature(Target::NoBoundsQuery);

    // First hunt for all the referenced buffers
    FindBuffers finder;
    map<string, FindBuffers::Result> &bufs = finder.buffers;

    // Add the output buffer(s).
    for (const Function &f : outputs) {
        for (size_t i = 0; i < f.values().size(); i++) {
            FindBuffers::Result output_buffer;
            output_buffer.type = f.values()[i].type();
            output_buffer.param = f.output_buffers()[i];
            output_buffer.dimensions = f.dimensions();
            if (f.values().size() > 1) {
                bufs[f.name() + '.' + std::to_string(i)] = output_buffer;
            } else {
                bufs[f.name()] = output_buffer;
            }
        }
    }

    // Add the input buffer(s) and annotate which output buffers are
    // used on host.
    s.accept(&finder);

    Scope<Interval> empty_scope;
    Stmt sub_stmt = TrimStmtToPartsThatAccessBuffers(bufs).mutate(s);
    map<string, Box> boxes = boxes_touched(sub_stmt, empty_scope, fb);

    // Now iterate through all the buffers, creating a list of lets
    // and a list of asserts.
    vector<pair<string, Expr>> lets_overflow;
    vector<pair<string, Expr>> lets_required;
    vector<pair<string, Expr>> lets_constrained;
    vector<pair<string, Expr>> lets_proposed;
    vector<Stmt> dims_no_overflow_asserts;
    vector<Stmt> asserts_required;
    vector<Stmt> asserts_constrained;
    vector<Stmt> asserts_proposed;
    vector<Stmt> asserts_type_checks;
    vector<Stmt> asserts_host_alignment;
    vector<Stmt> asserts_host_non_null;
    vector<Stmt> asserts_device_not_dirty;
    vector<Stmt> buffer_rewrites;
    vector<Stmt> msan_checks;

    // Inject the code that conditionally returns if we're in inference mode
    Expr maybe_return_condition = const_false();

    // We're also going to apply the constraints to the required min
    // and extent. To do this we have to substitute all references to
    // the actual sizes of the input images in the constraints with
    // references to the required sizes.
    map<string, Expr> replace_with_required;

    for (const pair<const string, FindBuffers::Result> &buf : bufs) {
        const string &name = buf.first;

        for (int i = 0; i < buf.second.dimensions; i++) {
            string dim = std::to_string(i);

            Expr min_required = Variable::make(Int(32), name + ".min." + dim + ".required");
            replace_with_required[name + ".min." + dim] = min_required;

            Expr extent_required = Variable::make(Int(32), name + ".extent." + dim + ".required");
            replace_with_required[name + ".extent." + dim] = simplify(extent_required);

            Expr stride_required = Variable::make(Int(32), name + ".stride." + dim + ".required");
            replace_with_required[name + ".stride." + dim] = stride_required;
        }
    }

    // We also want to build a map that lets us replace values passed
    // in with the constrained version. This is applied to the rest of
    // the lowered pipeline to take advantage of the constraints,
    // e.g. for constant folding.
    map<string, Expr> replace_with_constrained;

    for (pair<const string, FindBuffers::Result> &buf : bufs) {
        const string &name = buf.first;
        Buffer<> &image = buf.second.image;
        Parameter &param = buf.second.param;
        Type type = buf.second.type;
        int dimensions = buf.second.dimensions;
        bool used_on_host = buf.second.used_on_host;

        // Detect if this is one of the outputs of a multi-output pipeline.
        bool is_output_buffer = false;
        bool is_secondary_output_buffer = false;
        string buffer_name = name;
        for (const Function &f : outputs) {
            for (size_t i = 0; i < f.output_buffers().size(); i++) {
                if (param.defined() &&
                    param.same_as(f.output_buffers()[i])) {
                    is_output_buffer = true;
                    // If we're one of multiple output buffers, we should use the
                    // region inferred for the func in general.
                    buffer_name = f.name();
                    if (i > 0) {
                        is_secondary_output_buffer = true;
                    }
                }
            }
        }

        Box touched = boxes[buffer_name];
        internal_assert(touched.empty() || (int)(touched.size()) == dimensions);

        // The buffer may be used in one or more extern stage. If so we need to
        // expand the box touched to include the results of the
        // top-level bounds query calls to those extern stages.
        if (param.defined()) {
            // Find the extern users.
            vector<string> extern_users;
            for (const auto &func_name : order) {
                Function f = env.find(func_name)->second;
                if (f.has_extern_definition() &&
                    !f.extern_definition_proxy_expr().defined()) {
                    const vector<ExternFuncArgument> &args = f.extern_arguments();
                    for (const auto &arg : args) {
                        if ((arg.image_param.defined() &&
                             arg.image_param.name() == param.name()) ||
                            (arg.buffer.defined() &&
                             arg.buffer.name() == param.name())) {
                            extern_users.push_back(func_name);
                        }
                    }
                }
            }

            // Expand the box by the result of the bounds query from each.
            for (auto &extern_user : extern_users) {
                Box query_box;
                Expr query_buf = Variable::make(type_of<struct halide_buffer_t *>(),
                                                param.name() + ".bounds_query." + extern_user);
                for (int j = 0; j < dimensions; j++) {
                    Expr min = Call::make(Int(32), Call::buffer_get_min,
                                          {query_buf, j}, Call::Extern);
                    Expr max = Call::make(Int(32), Call::buffer_get_max,
                                          {query_buf, j}, Call::Extern);
                    query_box.push_back(Interval(min, max));
                }
                merge_boxes(touched, query_box);
            }
        }

        ReductionDomain rdom;

        // An expression returning whether or not we're in inference mode
        string buf_name = name + ".buffer";
        Expr handle = Variable::make(type_of<halide_buffer_t *>(), buf_name,
                                     image, param, rdom);
        Expr inference_mode = Call::make(Bool(), Call::buffer_is_bounds_query,
                                         {handle}, Call::Extern);
        maybe_return_condition = maybe_return_condition || inference_mode;

        // Come up with a name to refer to this buffer in the error messages
        string error_name = (is_output_buffer ? "Output" : "Input");
        error_name += " buffer " + name;

        if (!is_output_buffer && t.has_feature(Target::MSAN)) {
            Expr buffer = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
            Stmt check_contents = Evaluate::make(
                Call::make(Int(32), "halide_msan_check_buffer_is_initialized", {buffer, Expr(buf_name)}, Call::Extern));
            msan_checks.push_back(check_contents);
        }

        // Check the type matches the internally-understood type
        {
            string type_name = name + ".type";
            Expr type_var = Variable::make(UInt(32), type_name, image, param, rdom);
            uint32_t correct_type_bits = ((halide_type_t)type).as_u32();
            Expr correct_type_expr = make_const(UInt(32), correct_type_bits);
            Expr error = Call::make(Int(32), "halide_error_bad_type",
                                    {error_name, type_var, correct_type_expr},
                                    Call::Extern);
            Stmt type_check = AssertStmt::make(type_var == correct_type_expr, error);
            asserts_type_checks.push_back(type_check);
        }

        // Check the dimensions matches the internally-understood dimensions
        {
            string dimensions_name = name + ".dimensions";
            Expr dimensions_given = Variable::make(Int(32), dimensions_name, image, param, rdom);
            Expr error = Call::make(Int(32), "halide_error_bad_dimensions",
                                    {error_name,
                                     dimensions_given, make_const(Int(32), dimensions)},
                                    Call::Extern);
            asserts_type_checks.push_back(
                AssertStmt::make(dimensions_given == dimensions, error));
        }

        if (touched.maybe_unused()) {
            debug(3) << "Image " << name << " is only used when " << touched.used << "\n";
        }

        // Check that the region passed in (after applying constraints) is within the region used
        if (debug::debug_level() >= 3) {
            debug(3) << "In image " << name << " region touched is:\n";
            for (int j = 0; j < dimensions; j++) {
                debug(3) << "  " << j << ": " << (touched.empty() ? Expr() : touched[j].min)
                         << " .. "
                         << (touched.empty() ? Expr() : touched[j].max)
                         << "\n";
            }
        }

        for (int j = 0; j < dimensions; j++) {
            string dim = std::to_string(j);
            string actual_min_name = name + ".min." + dim;
            string actual_extent_name = name + ".extent." + dim;
            string actual_stride_name = name + ".stride." + dim;
            Expr actual_min = Variable::make(Int(32), actual_min_name, image, param, rdom);
            Expr actual_extent = Variable::make(Int(32), actual_extent_name, image, param, rdom);
            Expr actual_stride = Variable::make(Int(32), actual_stride_name, image, param, rdom);

            if (!touched.empty() && !touched[j].is_bounded()) {
                user_error << "Buffer " << name
                           << " may be accessed in an unbounded way in dimension "
                           << j << "\n";
            }

            Expr min_required = touched.empty() ? actual_min : touched[j].min;
            Expr extent_required = touched.empty() ? actual_extent : (touched[j].max + 1 - touched[j].min);

            if (touched.maybe_unused()) {
                min_required = select(touched.used, min_required, actual_min);
                extent_required = select(touched.used, extent_required, actual_extent);
            }

            string min_required_name = name + ".min." + dim + ".required";
            string extent_required_name = name + ".extent." + dim + ".required";

            Expr min_required_var = Variable::make(Int(32), min_required_name);
            Expr extent_required_var = Variable::make(Int(32), extent_required_name);

            lets_required.emplace_back(extent_required_name, extent_required);
            lets_required.emplace_back(min_required_name, min_required);

            Expr actual_max = actual_min + actual_extent - 1;
            Expr max_required = min_required_var + extent_required_var - 1;

            if (touched.maybe_unused()) {
                max_required = select(touched.used, max_required, actual_max);
            }

            Expr oob_condition = actual_min <= min_required_var && actual_max >= max_required;

            Expr oob_error = Call::make(Int(32), "halide_error_access_out_of_bounds",
                                        {error_name, j, min_required_var, max_required, actual_min, actual_max},
                                        Call::Extern);

            asserts_required.push_back(AssertStmt::make(oob_condition, oob_error));

            // Come up with a required stride to use in bounds
            // inference mode. We don't assert it. It's just used to
            // apply the constraints to to come up with a proposed
            // stride. Strides actually passed in may not be in this
            // order (e.g if storage is swizzled relative to dimension
            // order).
            Expr stride_required;
            if (j == 0) {
                stride_required = 1;
            } else {
                string last_dim = std::to_string(j - 1);
                stride_required = (Variable::make(Int(32), name + ".stride." + last_dim + ".required") *
                                   Variable::make(Int(32), name + ".extent." + last_dim + ".required"));
            }
            lets_required.emplace_back(name + ".stride." + dim + ".required", stride_required);

            // On 32-bit systems, insert checks to make sure the total
            // size of all input and output buffers is <= 2^31 - 1.
            // And that no product of extents overflows 2^31 - 1. This
            // second test is likely only needed if a fuse directive
            // is used in the schedule to combine multiple extents,
            // but it is here for extra safety. On 64-bit targets with the
            // LargeBuffers feature, the maximum size is 2^63 - 1.
            Expr max_size = make_const(UInt(64), t.maximum_buffer_size());
            Expr max_extent = make_const(UInt(64), 0x7fffffff);
            Expr actual_size = abs(cast<int64_t>(actual_extent) * actual_stride);
            Expr allocation_size_error = Call::make(Int(32), "halide_error_buffer_allocation_too_large",
                                                    {name, actual_size, max_size}, Call::Extern);
            Stmt check = AssertStmt::make(actual_size <= max_size, allocation_size_error);
            dims_no_overflow_asserts.push_back(check);

            // Don't repeat extents check for secondary buffers as extents must be the same as for the first one.
            if (!is_secondary_output_buffer) {
                if (j == 0) {
                    lets_overflow.emplace_back(name + ".total_extent." + dim, cast<int64_t>(actual_extent));
                } else {
                    max_size = cast<int64_t>(max_size);
                    Expr last_dim = Variable::make(Int(64), name + ".total_extent." + std::to_string(j - 1));
                    Expr this_dim = actual_extent * last_dim;
                    Expr this_dim_var = Variable::make(Int(64), name + ".total_extent." + dim);
                    lets_overflow.emplace_back(name + ".total_extent." + dim, this_dim);
                    Expr error = Call::make(Int(32), "halide_error_buffer_extents_too_large",
                                            {name, this_dim_var, max_size}, Call::Extern);
                    Stmt check = AssertStmt::make(this_dim_var <= max_size, error);
                    dims_no_overflow_asserts.push_back(check);
                }

                // It is never legal to have a negative buffer extent.
                Expr negative_extent_condition = actual_extent >= 0;
                Expr negative_extent_error = Call::make(Int(32), "halide_error_buffer_extents_negative",
                                                        {error_name, j, actual_extent}, Call::Extern);
                asserts_required.push_back(AssertStmt::make(negative_extent_condition, negative_extent_error));
            }
        }

        // Create code that mutates the input buffers if we're in bounds inference mode.
        BufferBuilder builder;
        builder.buffer_memory = Variable::make(type_of<struct halide_buffer_t *>(), buf_name);
        builder.shape_memory = Call::make(type_of<struct halide_dimension_t *>(),
                                          Call::buffer_get_shape, {builder.buffer_memory},
                                          Call::Extern);
        builder.type = type;
        builder.dimensions = dimensions;
        for (int i = 0; i < dimensions; i++) {
            string dim = std::to_string(i);
            builder.mins.push_back(Variable::make(Int(32), name + ".min." + dim + ".proposed"));
            builder.extents.push_back(Variable::make(Int(32), name + ".extent." + dim + ".proposed"));
            builder.strides.push_back(Variable::make(Int(32), name + ".stride." + dim + ".proposed"));
        }
        Stmt rewrite = Evaluate::make(builder.build());

        rewrite = IfThenElse::make(inference_mode, rewrite);
        buffer_rewrites.push_back(rewrite);

        // Build the constraints tests and proposed sizes.
        vector<pair<Expr, Expr>> constraints;
        for (int i = 0; i < dimensions; i++) {
            string dim = std::to_string(i);
            string min_name = name + ".min." + dim;
            string stride_name = name + ".stride." + dim;
            string extent_name = name + ".extent." + dim;

            Expr stride_constrained, extent_constrained, min_constrained;

            Expr stride_orig = Variable::make(Int(32), stride_name, image, param, rdom);
            Expr extent_orig = Variable::make(Int(32), extent_name, image, param, rdom);
            Expr min_orig = Variable::make(Int(32), min_name, image, param, rdom);

            Expr stride_required = Variable::make(Int(32), stride_name + ".required");
            Expr extent_required = Variable::make(Int(32), extent_name + ".required");
            Expr min_required = Variable::make(Int(32), min_name + ".required");

            Expr stride_proposed = Variable::make(Int(32), stride_name + ".proposed");
            Expr extent_proposed = Variable::make(Int(32), extent_name + ".proposed");
            Expr min_proposed = Variable::make(Int(32), min_name + ".proposed");

            debug(2) << "Injecting constraints for " << name << "." << i << "\n";
            if (is_secondary_output_buffer) {
                // For multi-output (Tuple) pipelines, output buffers
                // beyond the first implicitly have their min and extent
                // constrained to match the first output.

                if (param.defined()) {
                    user_assert(!param.extent_constraint(i).defined() &&
                                !param.min_constraint(i).defined())
                        << "Can't constrain the min or extent of an output buffer beyond the "
                        << "first. They are implicitly constrained to have the same min and extent "
                        << "as the first output buffer.\n";

                    stride_constrained = param.stride_constraint(i);
                } else if (image.defined() && (int)i < image.dimensions()) {
                    stride_constrained = image.dim(i).stride();
                }

                std::string min0_name = buffer_name + ".0.min." + dim;
                if (replace_with_constrained.count(min0_name) > 0) {
                    min_constrained = replace_with_constrained[min0_name];
                } else {
                    min_constrained = Variable::make(Int(32), min0_name);
                }

                std::string extent0_name = buffer_name + ".0.extent." + dim;
                if (replace_with_constrained.count(extent0_name) > 0) {
                    extent_constrained = replace_with_constrained[extent0_name];
                } else {
                    extent_constrained = Variable::make(Int(32), extent0_name);
                }
            } else if (image.defined() && (int)i < image.dimensions()) {
                stride_constrained = image.dim(i).stride();
                extent_constrained = image.dim(i).extent();
                min_constrained = image.dim(i).min();
            } else if (param.defined()) {
                stride_constrained = param.stride_constraint(i);
                extent_constrained = param.extent_constraint(i);
                min_constrained = param.min_constraint(i);
            }

            if (stride_constrained.defined()) {
                // Come up with a suggested stride by passing the
                // required region through this constraint.
                constraints.emplace_back(stride_orig, stride_constrained);
                stride_constrained = substitute(replace_with_required, stride_constrained);
                lets_proposed.emplace_back(stride_name + ".proposed", stride_constrained);
            } else {
                lets_proposed.emplace_back(stride_name + ".proposed", stride_required);
            }

            if (min_constrained.defined()) {
                constraints.emplace_back(min_orig, min_constrained);
                min_constrained = substitute(replace_with_required, min_constrained);
                lets_proposed.emplace_back(min_name + ".proposed", min_constrained);
            } else {
                lets_proposed.emplace_back(min_name + ".proposed", min_required);
            }

            if (extent_constrained.defined()) {
                constraints.emplace_back(extent_orig, extent_constrained);
                extent_constrained = substitute(replace_with_required, extent_constrained);
                lets_proposed.emplace_back(extent_name + ".proposed", extent_constrained);
            } else {
                lets_proposed.emplace_back(extent_name + ".proposed", extent_required);
            }

            // In bounds inference mode, make sure the proposed
            // versions still satisfy the constraints.
            Expr max_proposed = min_proposed + extent_proposed - 1;
            Expr max_required = min_required + extent_required - 1;
            Expr check = (min_proposed <= min_required) && (max_proposed >= max_required);
            Expr error = Call::make(Int(32), "halide_error_constraints_make_required_region_smaller",
                                    {error_name, i, min_proposed, max_proposed, min_required, max_required},
                                    Call::Extern);
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error));

            // stride_required is just a suggestion. It's ok if the
            // constraints shuffle them around in ways that make it
            // smaller.
            /*
            check = (stride_proposed >= stride_required);
            error = "Applying the constraints to the required stride made it smaller";
            asserts_proposed.push_back(AssertStmt::make((!inference_mode) || check, error, vector<Expr>()));
            */
        }

        // Assert all the conditions, and set the new values
        for (const auto &constraint : constraints) {
            Expr var = constraint.first;
            const string &name = var.as<Variable>()->name;
            Expr constrained_var = Variable::make(Int(32), name + ".constrained");

            std::ostringstream ss;
            ss << constraint.second;
            string constrained_var_str = ss.str();

            lets_constrained.emplace_back(name + ".constrained", constraint.second);

            // Substituting in complex expressions is not typically a good idea
            if (constraint.second.as<Variable>() ||
                is_const(constraint.second)) {
                replace_with_constrained[name] = constrained_var;
            }

            Expr error = 0;
            if (!no_asserts) {
                error = Call::make(Int(32), "halide_error_constraint_violated",
                                   {name, var, constrained_var_str, constrained_var},
                                   Call::Extern);
            }

            // Check the var passed in equals the constrained version (when not in inference mode)
            asserts_constrained.push_back(AssertStmt::make(var == constrained_var, error));
        }

        // For the buffers used on host, check the host field is non-null
        Expr host_ptr = Variable::make(Handle(), name, image, param, ReductionDomain());
        if (used_on_host) {
            Expr error = Call::make(Int(32), "halide_error_host_is_null",
                                    {error_name}, Call::Extern);
            Expr check = (host_ptr != make_zero(host_ptr.type()));
            if (touched.maybe_unused()) {
                check = !touched.used || check;
            }
            asserts_host_non_null.push_back(AssertStmt::make(check, error));

            if (!will_inject_host_copies) {
                Expr device_dirty = Variable::make(Bool(), name + ".device_dirty",
                                                   image, param, ReductionDomain());

                Expr error = Call::make(Int(32), "halide_error_device_dirty_with_no_device_support",
                                        {error_name}, Call::Extern);

                // If we have no device support, we can't handle
                // device_dirty, so every buffer touched needs checking.
                asserts_device_not_dirty.push_back(AssertStmt::make(!device_dirty, error));
            }
        }

        // and check alignment of the host field
        if (param.defined() && param.host_alignment() != param.type().bytes()) {
            int alignment_required = param.host_alignment();
            Expr u64t_host_ptr = reinterpret<uint64_t>(host_ptr);
            Expr align_condition = (u64t_host_ptr % alignment_required) == 0;
            Expr error = Call::make(Int(32), "halide_error_unaligned_host_ptr",
                                    {name, alignment_required}, Call::Extern);
            asserts_host_alignment.push_back(AssertStmt::make(align_condition, error));
        }
    }

    auto prepend_stmts = [&](vector<Stmt> *stmts) {
        while (!stmts->empty()) {
            s = Block::make(std::move(stmts->back()), s);
            stmts->pop_back();
        }
    };

    auto prepend_lets = [&](vector<pair<string, Expr>> *lets) {
        while (!lets->empty()) {
            auto &p = lets->back();
            s = LetStmt::make(p.first, std::move(p.second), s);
            lets->pop_back();
        }
    };

    if (!no_asserts) {
        // Inject the code that checks the host pointers.
        prepend_stmts(&asserts_host_non_null);
        prepend_stmts(&asserts_host_alignment);
        prepend_stmts(&asserts_device_not_dirty);
        prepend_stmts(&dims_no_overflow_asserts);
        prepend_lets(&lets_overflow);
    }

    // Replace uses of the var with the constrained versions in the
    // rest of the program. We also need to respect the existence of
    // constrained versions during storage flattening and bounds
    // inference.
    s = substitute(replace_with_constrained, s);

    // Now we add a bunch of code to the top of the pipeline. This is
    // all in reverse order compared to execution, as we incrementally
    // prepending code.

    // Inject the code that checks the constraints are correct. We
    // need these regardless of how NoAsserts is set, because they are
    // what gets Halide to actually exploit the constraint.
    prepend_stmts(&asserts_constrained);

    if (!no_asserts) {
        prepend_stmts(&asserts_required);
        prepend_stmts(&asserts_type_checks);
    }

    // Inject the code that returns early for inference mode.
    if (!no_bounds_query) {
        s = IfThenElse::make(!maybe_return_condition, s);
        prepend_stmts(&buffer_rewrites);
    }

    if (!no_asserts) {
        prepend_stmts(&asserts_proposed);
    }

    // Inject the code that defines the proposed sizes.
    prepend_lets(&lets_proposed);

    // Inject the code that defines the constrained sizes.
    prepend_lets(&lets_constrained);

    // Inject the code that defines the required sizes produced by bounds inference.
    prepend_lets(&lets_required);

    // Inject the code that checks that does msan checks. (Note that this ignores no_asserts.)
    prepend_stmts(&msan_checks);

    return s;
}

}  // namespace

// The following function repeats the arguments list it just passes
// through six times. Surely there is a better way?
Stmt add_image_checks(const Stmt &s,
                      const vector<Function> &outputs,
                      const Target &t,
                      const vector<string> &order,
                      const map<string, Function> &env,
                      const FuncValueBounds &fb,
                      bool will_inject_host_copies) {

    // Checks for images go at the marker deposited by computation
    // bounds inference.
    class Injector : public IRMutator {
        using IRMutator::visit;

        Expr visit(const Variable *op) override {
            // In the bounds inference lets we skip over, respect any buffer
            // constraints.

            // Note that in the case where the constraint doesn't hold, this
            // changes the value of this Expr! This is safe because these lets
            // are internal names, and no user-provided constraints can depend
            // on them, so changing their value to use the constraint value
            // instead of the actual buffer value can't possibly change whether
            // or not the constraint check is going to pass.
            const Parameter &p = op->param;
            if (p.defined() && p.is_buffer()) {
                for (int i = 0; i < p.dimensions(); i++) {
                    if (p.min_constraint(i).defined() &&
                        op->name == p.name() + ".min." + std::to_string(i)) {
                        return p.min_constraint(i);
                    }
                    if (p.extent_constraint(i).defined() &&
                        op->name == p.name() + ".extent." + std::to_string(i)) {
                        return p.extent_constraint(i);
                    }
                    if (p.stride_constraint(i).defined() &&
                        op->name == p.name() + ".stride." + std::to_string(i)) {
                        return p.stride_constraint(i);
                    }
                }
            }
            return op;
        }

        Stmt visit(const Block *op) override {
            const Evaluate *e = op->first.as<Evaluate>();
            if (e && Call::as_intrinsic(e->value, {Call::add_image_checks_marker})) {
                return add_image_checks_inner(op->rest, outputs, t, order, env, fb, will_inject_host_copies);
            } else {
                return IRMutator::visit(op);
            }
        }

        const vector<Function> &outputs;
        const Target &t;
        const vector<string> &order;
        const map<string, Function> &env;
        const FuncValueBounds &fb;
        bool will_inject_host_copies;

    public:
        Injector(const vector<Function> &outputs,
                 const Target &t,
                 const vector<string> &order,
                 const map<string, Function> &env,
                 const FuncValueBounds &fb,
                 bool will_inject_host_copies)
            : outputs(outputs), t(t), order(order), env(env), fb(fb), will_inject_host_copies(will_inject_host_copies) {
        }
    } injector(outputs, t, order, env, fb, will_inject_host_copies);

    return injector.mutate(s);
}

}  // namespace Internal
}  // namespace Halide
