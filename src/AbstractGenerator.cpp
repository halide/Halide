#include "AbstractGenerator.h"
#include "BoundaryConditions.h"
#include "Derivative.h"
#include "Generator.h"

namespace Halide {
namespace Internal {

namespace {

Argument to_argument(const Internal::Parameter &param) {
    return Argument(param.name(),
                    param.is_buffer() ? Argument::InputBuffer : Argument::InputScalar,
                    param.type(),
                    param.dimensions(),
                    param.get_argument_estimates());
}

}  // namespace

Module AbstractGenerator::build_module(const std::string &function_name) {
    const LinkageType linkage_type = LinkageType::ExternalPlusMetadata;

    Pipeline pipeline = build_pipeline();

    AutoSchedulerResults auto_schedule_results;
    const auto context = this->context();
    const auto &asp = context.autoscheduler_params();
    if (!asp.name.empty()) {
        debug(1) << "Applying autoscheduler " << asp.name << " to Generator " << name() << " ...\n";
        auto_schedule_results = pipeline.apply_autoscheduler(context.target(), asp);
    } else {
        debug(1) << "Applying autoscheduler (NONE) to Generator " << name() << " ...\n";
    }

    std::vector<Argument> filter_arguments;
    const auto arg_infos = arginfos();
    for (const auto &a : arg_infos) {
        if (a.dir != ArgInfoDirection::Input) {
            continue;
        }
        for (const auto &p : input_parameter(a.name)) {
            filter_arguments.push_back(to_argument(p));
        }
    }

    Module result = pipeline.compile_to_module(filter_arguments, function_name, context.target(), linkage_type);

    for (const auto &a : arg_infos) {
        if (a.dir != ArgInfoDirection::Output) {
            continue;
        }
        const std::vector<Func> output_funcs = output_func(a.name);
        for (size_t i = 0; i < output_funcs.size(); ++i) {
            const Func &f = output_funcs[i];

            const std::string &from = f.name();
            std::string to = a.name;
            if (output_funcs.size() > 1) {
                to += "_" + std::to_string(i);
            }

            const int tuple_size = f.outputs();
            for (int t = 0; t < tuple_size; ++t) {
                const std::string suffix = (tuple_size > 1) ? ("." + std::to_string(t)) : "";
                result.remap_metadata_name(from + suffix, to + suffix);
            }
        }
    }

    result.set_auto_scheduler_results(auto_schedule_results);

    return result;
}

Module AbstractGenerator::build_gradient_module(const std::string &function_name) {
    constexpr int DBG = 1;

    // I doubt these ever need customizing; if they do, we can make them arguments to this function.
    const std::string grad_input_pattern = "_grad_loss_for_$OUT$";
    const std::string grad_output_pattern = "_grad_loss_$OUT$_wrt_$IN$";
    const LinkageType linkage_type = LinkageType::ExternalPlusMetadata;

    user_assert(!function_name.empty()) << "build_gradient_module(): function_name cannot be empty\n";

    Pipeline original_pipeline = build_pipeline();

    std::vector<Func> original_outputs = original_pipeline.outputs();

    // Construct the adjoint pipeline, which has:
    // - All the same inputs as the original, in the same order
    // - Followed by one grad-input for each original output
    // - Followed by one output for each unique pairing of original-output + original-input.

    // First: the original inputs. Note that scalar inputs remain scalar,
    // rather being promoted into zero-dimensional buffers.
    std::vector<Argument> gradient_inputs;
    const auto arg_infos = arginfos();
    for (const auto &a : arg_infos) {
        if (a.dir != ArgInfoDirection::Input) {
            continue;
        }
        for (const auto &p : input_parameter(a.name)) {
            gradient_inputs.push_back(to_argument(p));
            debug(DBG) << "    gradient copied input is: " << gradient_inputs.back().name << "\n";
        }
    }

    // Next: add a grad-input for each *original* output; these will
    // be the same shape as the output (so we should copy estimates from
    // those outputs onto these estimates).
    // - If an output is an Array, we'll have a separate input for each array element.

    std::vector<ImageParam> d_output_imageparams;
    for (const auto &a : arg_infos) {
        if (a.dir != ArgInfoDirection::Output) {
            continue;
        }
        for (const auto &f : output_func(a.name)) {
            const Parameter &p = f.output_buffer().parameter();
            const std::string &output_name = p.name();
            // output_name is something like "funcname_i"
            const std::string grad_in_name = replace_all(grad_input_pattern, "$OUT$", output_name);
            // TODO(srj): does it make sense for gradient to be a non-float type?
            // For now, assume it's always float32 (unless the output is already some float).
            const Type grad_in_type = p.type().is_float() ? p.type() : Float(32);
            const int grad_in_dimensions = p.dimensions();
            const ArgumentEstimates grad_in_estimates = p.get_argument_estimates();
            internal_assert((int)grad_in_estimates.buffer_estimates.size() == grad_in_dimensions);

            ImageParam d_im(grad_in_type, grad_in_dimensions, grad_in_name);
            for (int d = 0; d < grad_in_dimensions; d++) {
                d_im.parameter().set_min_constraint_estimate(d, grad_in_estimates.buffer_estimates.at(d).min);
                d_im.parameter().set_extent_constraint_estimate(d, grad_in_estimates.buffer_estimates.at(d).extent);
            }
            d_output_imageparams.push_back(d_im);
            gradient_inputs.push_back(to_argument(d_im.parameter()));

            debug(DBG) << "    gradient synthesized input is: " << gradient_inputs.back().name << "\n";
        }
    }

    // Finally: define the output Func(s), one for each unique output/input pair.
    // Note that original_outputs.size() != pi.outputs().size() if any outputs are arrays.
    internal_assert(original_outputs.size() == d_output_imageparams.size()) << "original_outputs.size() " << original_outputs.size() << " d_output_imageparams.size() " << d_output_imageparams.size();
    std::vector<Func> gradient_outputs;
    for (size_t i = 0; i < original_outputs.size(); ++i) {
        const Func &original_output = original_outputs.at(i);
        const ImageParam &d_output = d_output_imageparams.at(i);
        Region bounds;
        for (int i = 0; i < d_output.dimensions(); i++) {
            bounds.emplace_back(d_output.dim(i).min(), d_output.dim(i).extent());
        }
        Func adjoint_func = BoundaryConditions::constant_exterior(d_output, make_zero(d_output.type()));
        Derivative d = propagate_adjoints(original_output, adjoint_func, bounds);

        const std::string &output_name = original_output.name();
        for (const auto &a : arg_infos) {
            if (a.dir != ArgInfoDirection::Input) {
                continue;
            }
            for (const auto &p : input_parameter(a.name)) {
                const std::string &input_name = p.name();

                if (!p.is_buffer()) {
                    // Not sure if skipping scalar inputs is correct, but that's
                    // what the previous version of this code did, so we'll continue for now.
                    debug(DBG) << "    Skipping scalar input " << output_name << " wrt input " << input_name << "\n";
                    continue;
                }

                // Note that Derivative looks up by name; we don't have the original
                // Func, and we can't create a new one with an identical name (since
                // Func's ctor will uniquify the name for us). Let's just look up
                // by the original string instead.
                Func d_f = d(input_name + "_im");

                std::string grad_out_name = replace_all(replace_all(grad_output_pattern, "$OUT$", output_name), "$IN$", input_name);
                if (!d_f.defined()) {
                    grad_out_name = "_dummy" + grad_out_name;
                }

                Func d_out_wrt_in(grad_out_name);
                if (d_f.defined()) {
                    d_out_wrt_in(Halide::_) = d_f(Halide::_);
                } else {
                    debug(DBG) << "    No Derivative found for output " << output_name << " wrt input " << input_name << "\n";
                    // If there was no Derivative found, don't skip the output;
                    // just replace with a dummy Func that is all zeros. This ensures
                    // that the signature of the Pipeline we produce is always predictable.
                    std::vector<Var> vars;
                    for (int i = 0; i < d_output.dimensions(); i++) {
                        vars.push_back(Var::implicit(i));
                    }
                    d_out_wrt_in(vars) = make_zero(d_output.type());
                }

                d_out_wrt_in.set_estimates(p.get_argument_estimates().buffer_estimates);

                // Useful for debugging; ordinarily better to leave out
                // debug(0) << "\n\n"
                //          << "output:\n" << FuncWithDependencies(original_output) << "\n"
                //          << "d_output:\n" << FuncWithDependencies(adjoint_func) << "\n"
                //          << "input:\n" << FuncWithDependencies(f) << "\n"
                //          << "d_out_wrt_in:\n" << FuncWithDependencies(d_out_wrt_in) << "\n";

                gradient_outputs.push_back(d_out_wrt_in);
                debug(DBG) << "    gradient output is: " << d_out_wrt_in.name() << "\n";
            }
        }
    }

    Pipeline grad_pipeline = Pipeline(gradient_outputs);

    AutoSchedulerResults auto_schedule_results;
    const auto context = this->context();
    const auto &asp = context.autoscheduler_params();
    if (!asp.name.empty()) {
        auto_schedule_results = grad_pipeline.apply_autoscheduler(context.target(), asp);
    } else {
        user_warning << "Autoscheduling is not enabled in build_gradient_module(), so the resulting "
                        "gradient module will be unscheduled; this is very unlikely to be what you want.\n";
    }

    Module result = grad_pipeline.compile_to_module(gradient_inputs, function_name, context.target(), linkage_type);
    result.set_auto_scheduler_results(auto_schedule_results);
    return result;
}

Callable AbstractGenerator::compile_to_callable(const JITHandlers *jit_handlers,
                                                const std::map<std::string, JITExtern> *jit_externs) {
    Pipeline pipeline = build_pipeline();

    std::vector<Argument> arguments;
    const auto arg_infos = arginfos();
    for (const auto &a : arg_infos) {
        if (a.dir != ArgInfoDirection::Input) {
            continue;
        }
        for (const auto &p : input_parameter(a.name)) {
            arguments.push_back(to_argument(p));
        }
    }
    if (jit_handlers != nullptr) {
        pipeline.jit_handlers() = *jit_handlers;
    }
    if (jit_externs != nullptr) {
        pipeline.set_jit_externs(*jit_externs);
    }
    return pipeline.compile_to_callable(arguments, context().target());
}

void AbstractGenerator::set_generatorparam_values(const GeneratorParamsMap &m) {
    for (const auto &c : m) {
        user_assert(c.first != "target" && c.first != "auto_scheduler")
            << "The GeneratorParam '" << c.first << "' cannot be specified via string here; use GeneratorContext instead.";
        set_generatorparam_value(c.first, c.second);
    }
}

}  // namespace Internal
}  // namespace Halide
