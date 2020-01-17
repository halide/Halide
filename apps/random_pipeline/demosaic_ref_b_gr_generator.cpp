#include "Halide.h"
#include <iostream>
#include <iomanip>
#include <random>
#include <cstdlib>
#include <unordered_map>
#include <limits>

using namespace Halide;
using namespace Halide::Internal;
using std::vector;
using std::unordered_map;

// Convert a vector of Vars to Exprs. Useful for generating references
// to Funcs.
vector<Expr> make_arguments(vector<Var> vars) {
    vector<Expr> result;
    for (Var i : vars) {
        result.push_back(i);
    }
    return result;
}

Expr avg(Expr a, Expr b) {
    Type wider = a.type().with_bits(a.type().bits() * 2);
    return cast(a.type(), (cast(wider, a) + b + 1)/2);
}

// Generator to produce reference demosaic pipeline. 
// Modified from random_pipeline_generator used by autoscheduler to have 
// learnable parameters (currently just the weights used by the conv stages)
template<bool training>
class DemosaicPipeline : public Halide::Generator<DemosaicPipeline<training>> {
public:
    template<typename T> using Input = GeneratorInput<T>;
    template<typename T> using Output = GeneratorOutput<T>;
    using dim_shape = std::tuple<int,int>;
    using Generator<DemosaicPipeline<training>>::auto_schedule;
    using Generator<DemosaicPipeline<training>>::get_pipeline;
    // types for buffers
    using inputT = int16_t;
    Type inputHT = Halide::type_of<inputT>();
    using outputT = int16_t;
    using lossT = float;
    using paramT = float;
    Type paramHT = Halide::type_of<paramT>();

    GeneratorParam<int> num_input_buffers{"num_input_buffers", 4};
    // The size of the input buffers ASSUMING ALL ARE THE SAME SIZE FOR NOW
    GeneratorParam<int> input_w{"input_w", 14};
    GeneratorParam<int> input_h{"input_h", 14};
    GeneratorParam<int> input_c{"input_c", 3};
    GeneratorParam<int> output_w{"output_w", 10};
    GeneratorParam<int> output_h{"output_h", 10};
    GeneratorParam<int> output_c{"output_c", 3};
    GeneratorParam<int> num_output_buffers{"num_output_buffers", 1};
    // how much to shift input image by to avoid boundary issues 
    GeneratorParam<int> shift{"shift", 2}; 
    Input<int> batch_size{ "batch_size", 1 };
    Input<float> learning_rate{ "learning_rate", 1.0f };
    Input<int> timestep{ "timestep", 0 }; // Needed by ADAM

    void set_input_weight_shape(Input<Halide::Buffer<float>>* weight, 
                                dim_shape s0, 
                                dim_shape s1, 
                                dim_shape s2, 
                                dim_shape s3) {
        weight->dim(0).set_bounds(std::get<0>(s0), std::get<1>(s0));
        weight->dim(1).set_bounds(std::get<0>(s1), std::get<1>(s1));
        weight->dim(2).set_bounds(std::get<0>(s2), std::get<1>(s2));
        weight->dim(3).set_bounds(std::get<0>(s3), std::get<1>(s3));
    }

    void set_output_weight_shape(Output<Halide::Buffer<paramT>>* weight,
                                 dim_shape s0,
                                 dim_shape s1,
                                 dim_shape s2,
                                 dim_shape s3) {
        weight->dim(0).set_bounds(std::get<0>(s0), std::get<1>(s0));
        weight->dim(0).set_bounds_estimate(std::get<0>(s0), std::get<1>(s0));
        weight->bound(weight->args()[0], std::get<0>(s0), std::get<1>(s0));
        weight->estimate(weight->args()[0], std::get<0>(s0), std::get<1>(s0));

        weight->dim(1).set_bounds(std::get<0>(s1), std::get<1>(s1));
        weight->dim(1).set_bounds_estimate(std::get<0>(s1), std::get<1>(s1));
        weight->bound(weight->args()[1], std::get<0>(s1), std::get<1>(s1));
        weight->estimate(weight->args()[1], std::get<0>(s1), std::get<1>(s1));

        weight->dim(2).set_bounds(std::get<0>(s2), std::get<1>(s2));
        weight->dim(2).set_bounds_estimate(std::get<0>(s2), std::get<1>(s2));
        weight->bound(weight->args()[2], std::get<0>(s2), std::get<1>(s2));
        weight->estimate(weight->args()[2], std::get<0>(s2), std::get<1>(s2));
        
        weight->dim(3).set_bounds(std::get<0>(s3), std::get<1>(s3));
        weight->dim(3).set_bounds_estimate(std::get<0>(s3), std::get<1>(s3));
        weight->bound(weight->args()[3], std::get<0>(s3), std::get<1>(s3));
        weight->estimate(weight->args()[3], std::get<0>(s3), std::get<1>(s3));

        weight->dim(weight->dimensions()-1).set_bounds(0, 4);
        weight->dim(weight->dimensions()-1).set_bounds_estimate(0, 4);
    }

    void backprop(Halide::ImageParam &weights,
                  Output<Halide::Buffer<paramT>>* grad, 
                  const Derivative &d, 
                  Expr learning_rate, 
                  Expr timestep) {
        std::vector<Expr> args(weights.dimensions()+1);
        for (auto &e : args) e = Var();
        (*grad)(args) = undef<paramT>();
        // We'll report back the new weights and the loss gradients,
        // and update the ADAM state. Depending on the mode the caller
        // is in, it may use the new weights, or it may just send the
        // loss gradients up to an ADAM server.
        args.back() = 0;
        FuncRef new_weight = (*grad)(args);
        args.back() = 1;
        FuncRef smoothed_deriv = (*grad)(args);
        args.back() = 2;
        FuncRef smoothed_second_moment = (*grad)(args);
        args.back() = 3;
        FuncRef loss_gradient = (*grad)(args);

        args.pop_back();
        Expr current_weight = weights(args);

        loss_gradient = d(weights)(args);
        std::cout << "loss gradient: " << loss_gradient << std::endl;
        std::cout << "loss gradient update definitons: " << std::endl;
        for (auto& def : loss_gradient.function().updates()) {
            for (auto& expr : def.values()) {
                std::cout << expr << std::endl;
            }
        }

        // Update the first and second moment estimates
        //smoothed_deriv = 0.9f * smoothed_deriv + 0.1f * loss_gradient;
        std::cout << "\nsmoothed deriv: " << smoothed_deriv << std::endl;
        std::cout << "smoothed deriv update definitons: " << std::endl;
        for (auto& def : smoothed_deriv.function().updates()) {
            for (auto& expr : def.values()) {
                std::cout << expr << std::endl;
            }
        }
        //smoothed_second_moment = 0.999f * smoothed_second_moment + 0.001f * pow(loss_gradient, 2);
      
        // Correction to account for the fact that the smoothed_deriv
        // and smoothed_second_moment start at zero when t == 0
        Expr smoothed_deriv_correction = 1 / (1 - pow(0.9f, timestep + 1));
        Expr smoothed_second_moment_correction = 1 / (1 - pow(0.999f, timestep + 1));

        std::cout << "\nsmoothed deriv expr: " << (Expr)smoothed_deriv << std::endl;
        std::cout << smoothed_deriv.function().name() << std::endl;
        // Update the weights
        //Expr step = learning_rate * smoothed_deriv * smoothed_deriv_correction;
        //step /= sqrt(smoothed_second_moment * smoothed_second_moment_correction) + 1e-5f;
        Expr step = learning_rate * d(weights)(args);

        std::cout << "step: " << step << std::endl;
        new_weight = current_weight - step;
    }

    void do_random_pipeline_schedule(Halide::Pipeline p) {
        // Compute an environment
        std::map<std::string, Function> env;
        for (Func &f : p.outputs()) {
            std::map<std::string, Function> more_funcs = find_transitive_calls(f.function());
            env.insert(more_funcs.begin(), more_funcs.end());
        }

        for (auto &f : env) {
            Func(f.second).compute_root();
        }
        return;
    }

    struct Stage {
        Func func;

        // approx width and height and channels. Used to preserve
        // spatial scale when combining stages, and to track the total
        // sizes of things.
        int w, h, c;

        static constexpr int max_size = 10000;
        static constexpr int min_size = 100;
        static constexpr int max_stride = 3; // for convs and pools

        int size() const {
            return w*h*c;
        }

        bool may_increase_size() const {
            return size() < max_size && w <= 8000 && h <= 8000 && c <= 512;
        }

        bool may_reduce_size() const {
            return size() > min_size;
        }
    };

    vector<Stage> stages; // the list of stages we will generate

    // USED BY INTERP 2TAP STAGES
    typedef std::tuple<Stage, vector<Expr>, vector<Expr>, Func> InterpStageAndCoords;

    InterpStageAndCoords interp2Tap_stage(Stage &input_s,
                                          vector<Expr> coords1, 
                                          vector<Expr> coords2) {
        Func interp("interp2Tap");
        std::cout << interp.name() << " is a 2 tap interp on " << input_s.func.name() << std::endl;
        std::cout << "coords1: " << coords1[0] << "," << coords1[1] << std::endl;
        std::cout << "coords2: " << coords2[0] << "," << coords2[1] << std::endl;
        interp(input_s.func.args()) = avg(input_s.func(coords1), input_s.func(coords2));

        Stage interp_s = {interp, input_s.w, input_s.h, input_s.c};
        return std::make_tuple(interp_s, coords1, coords2, input_s.func); 
    }

    bool same_vars(vector<Var> v1, vector<Var> v2) {
        assert(v1.size() == v2.size());
        for (int i = 0; i < (int)v1.size(); i++) {
            if (v1[i].name() != v2[i].name()) return false;
        }
        return true;
    }

    Stage select_interp2Tap_stage(const vector<Stage> &s) {
        Func select_interp("selectInterp2Tap");
        // gr is stage 0 and gb is stage 3

        Stage s1, s2;
        vector<Expr> s1coords1, s1coords2, s2coords1, s2coords2;
        Func s1input, s2input;

        s1 = s[0];
        s2 = s[3];

        s1coords1 = make_arguments(s1.func.args());
        s1coords2 = make_arguments(s1.func.args());
        s2coords1 = make_arguments(s2.func.args());
        s2coords2 = make_arguments(s2.func.args());

        s1coords1[1] += 1;
        s2coords1[0] -= 1;

        std::tie(s1, s1coords1, s1coords2, s1input) = interp2Tap_stage(s1, s1coords1, s1coords2);
        std::tie(s2, s2coords1, s2coords2, s2input) = interp2Tap_stage(s2, s2coords1, s2coords2);
        std::cout << select_interp.name() << " is a 2 tap select interp on " << s1input.name() << " and " << s2input.name() << std::endl;

        assert(s1.w == s2.w && s1.h == s2.h && s1.c == s2.c);

        vector<Expr> s1args = make_arguments(s1input.args());

        Expr diff1 = absd(s1input(s1coords1), s1input(s1coords2));      
        Expr diff2 = absd(s2input(s2coords1), s2input(s2coords2));

        select_interp(s1args) = select(diff1 < diff2, s1.func(s1args), s2.func(s1args));
        return {select_interp, s1.w, s1.h, s1.c};
    }

    InterpStageAndCoords correct_interp2Tap_stage(const vector<Stage> &s) {
        Func correctInterp("correctInterp2Tap");
        Stage input_s, ref_s, interp_s;
        int input_id, ref_id, interp_id;
        input_id = 2;
        ref_id = 0;
        interp_id = s.size() - 1;

        input_s = s[input_id];
        ref_s = s[ref_id];
        interp_s = s[interp_id];

        Func input_f  = input_s.func;
        Func ref_f    = ref_s.func;
        Func interp_f = interp_s.func;
        
        std::cout << correctInterp.name() << " is Corrected Interp 2 Tap on: " << input_f.name() << " with correction funcs: " << ref_f.name() << " and " << interp_f.name() << std::endl;

        // generate random coordinates to use 
        vector<Expr> coords1 = make_arguments(input_f.args());
        vector<Expr> coords2 = make_arguments(input_f.args());

        coords2[1] -= 1;

        std::cout << "coords1: " << coords1[0] << "," << coords1[1] << std::endl;
        std::cout << "coords2: " << coords2[0] << "," << coords2[1] << std::endl;

        vector<Expr> coords = make_arguments(input_f.args()); 
        Expr correction = ref_f(coords) - avg(interp_f(coords1), interp_f(coords2));
        Expr value = correction + avg(input_f(coords1), input_f(coords2));

        correctInterp(coords) = value;

        Stage correct_interp_s = {correctInterp, input_s.w, input_s.h, input_s.c};  
        
        std::cout << correctInterp(coords) << " = ";
        std::cout << value << std::endl;

        return std::make_tuple(correct_interp_s, coords1, coords2, input_s.func);
    }

    // build pipeline and define all required inputs and outputs for the generated program
    void configure() {
        // create input and output buffers
        for (int i = 0; i < num_input_buffers; i++) {
            Input<Buffer<inputT>>* input_buff = 
                Halide::Internal::GeneratorBase::add_input<Buffer<outputT>>("input_" + std::to_string(i), 3);
            input_buffs.push_back(input_buff);
        }
        for (int i = 0; i < num_output_buffers; i++) {
            Output<Buffer<outputT>>* output_buff = 
                Halide::Internal::GeneratorBase::add_output<Buffer<outputT>>("output_" + std::to_string(i), 3);
            output_buffs.push_back(output_buff);
            Input<Buffer<outputT>>* correct_output_buff = 
                Halide::Internal::GeneratorBase::add_input<Buffer<outputT>>("correct_output_" + std::to_string(i), 3);
            correct_outputs.push_back(correct_output_buff);
        }

        Var x("x"), y("y"), c("c");

        // create dummy image params for each input buffer so that we can access them in configure()
        // Zero pad all inputs and add them as stages to be used by the generated stages
        // Assuming all inputs are same size for now
        for (int i = 0; i < num_input_buffers; i++) { 
            input_buff_dummies.emplace_back(Halide::ImageParam(inputHT, 3, "input_" + std::to_string(i)));
            std::vector<std::pair<Expr, Expr>> bounds(3); 
            bounds.at(0).first = 0;
            bounds.at(0).second = input_w;
            bounds.at(1).first = 0;
            bounds.at(1).second = input_h;
            bounds.at(2).first = 0;
            bounds.at(2).second = input_c;
            Func padded_input = Halide::BoundaryConditions::constant_exterior(input_buff_dummies[i], cast(inputHT, 0), bounds);
            Func input_func, shifted_input;
            // shift the input so that we don't have to worry about boundary conditions
            input_func(x, y, c) = padded_input(x, y, c);
            shifted_input(x, y, c) = input_func(x + (int)shift, y + (int)shift, c);
            stages.emplace_back(Stage{shifted_input, output_w, output_h, output_c});  
        } 
        std::cout << "finished adding input buffers to stages " << std::endl;
        Stage select_interp = select_interp2Tap_stage(stages);
        stages.push_back(select_interp);

        Stage correct_interp;
        vector<Expr> coords1, coords2;
        Func input_func;

        std::tie(correct_interp, coords1, coords2, input_func) = correct_interp2Tap_stage(stages);
        stages.push_back(correct_interp);

        std::cout << "Approx size: " << stages.back().w << ", " << stages.back().h << ", " << stages.back().c << "\n";
       
        std::cout << "finished adding stages" << std::endl;
    }

    // Select which funcs to map to the output buffers 
    // Compute the loss and call backprop if we are in training mode
    void generate() {
        Var x("x"), y("y"), c("c");

        std::vector<Func> last_funcs; // need these for backrop if training
        last_funcs.push_back(stages[stages.size()-1].func);

        (*output_buffs[0])(x, y, c) = stages[stages.size()-1].func(x, y, c);

        Derivative d_loss_d;
        Func err;

        // need to compute total loss over all outputs
        RDom r(0, output_w, 
               0, output_h,
               0, output_c);
        Expr loss = Expr(0.0f);
        for (int i = 0; i < num_output_buffers; i++) {
            Expr diff = cast<double>((*correct_outputs[i])(x, y, c) - last_funcs[i](x, y, c));
            err(x, y, c) = (diff*diff);
            loss += sum(err(r.x, r.y, r.z)/((int)(output_w) * (int)(output_h)));
        }

        loss_output() = cast<lossT>(loss);

        // Compute derivatives of the loss, and backprop them to the parameters.
        if (training) {
            d_loss_d = propagate_adjoints(loss_output);
            
            // iterate over the generated params and backprop
            for (auto &output_w : output_params) {
                auto& input_w = input_param_dummies[output_w.first];
                backprop(input_w, output_w.second, d_loss_d, learning_rate, timestep);
            }
        }
        // set param_shapes for input and output weights
        if (training) {
            for (auto &output_w : output_params) {
                auto &shape = param_shapes[output_w.first];
                auto input_w = input_params[output_w.first];
                set_input_weight_shape(input_w, std::get<0>(shape), std::get<1>(shape), std::get<2>(shape), std::get<3>(shape));
                set_output_weight_shape(output_w.second, std::get<0>(shape), std::get<1>(shape), std::get<2>(shape), std::get<3>(shape));
            }      
        } else {
            for (auto &input_w : input_params) {
                auto &shape = param_shapes[input_w.first];
                set_input_weight_shape(input_w.second, std::get<0>(shape), std::get<1>(shape), std::get<2>(shape), std::get<3>(shape));
            }      
        }
        learning_rate.set_estimate(0.001f);
        timestep.set_estimate(37);
        batch_size.set_estimate(1);

        // SCHEDULING
        if (!auto_schedule and !training) {
            do_random_pipeline_schedule(get_pipeline());
        } 
        if (!auto_schedule and training) {
            do_random_pipeline_schedule(get_pipeline());
        }

        // bound all inputs and outputs
        for (int i = 0; i < num_input_buffers; i++) {
            (*input_buffs[i]).dim(0).set_bounds_estimate(0, input_w)
                .dim(1).set_bounds_estimate(0, input_h)
                .dim(2).set_bounds_estimate(0, input_c);
        }
        for (int i = 0; i < num_output_buffers; i++) {
            (*correct_outputs[i]).dim(0).set_bounds_estimate(0, output_w)
                .dim(1).set_bounds_estimate(0, output_h)
                .dim(2).set_bounds_estimate(0, output_c);

            (*output_buffs[i]).dim(0).set_bounds_estimate(0, output_w)
                .dim(1).set_bounds_estimate(0, output_h)
                .dim(2).set_bounds_estimate(0, output_c);
        }
    }

    void set_inputs(const std::vector<Buffer<inputT>> &inputs) {
        for (size_t i = 0; i < inputs.size(); i++) input_buff_dummies[i].set(inputs[i]);
    }

private:
    std::vector<Halide::ImageParam> input_buff_dummies;
    std::vector<Input<Buffer<inputT>> *>   input_buffs;
    std::vector<Input<Buffer<outputT>> *>  correct_outputs;
    std::vector<Output<Buffer<outputT>> *> output_buffs;

    std::unordered_map<std::string, Halide::ImageParam> input_param_dummies;
    std::unordered_map<std::string, Input<Halide::Buffer<paramT>> *> input_params;
    std::unordered_map<std::string, Output<Halide::Buffer<paramT>> *> output_params;
    // param_shapes of parameter buffers
    std::unordered_map<std::string, std::tuple<dim_shape, dim_shape, dim_shape, dim_shape>> param_shapes;

    Output<Buffer<lossT>> loss_output { "loss_output", 0 };
};

using DemosaicPipelineInference = DemosaicPipeline<false>;
using DemosaicPipelineTraining = DemosaicPipeline<true>;


HALIDE_REGISTER_GENERATOR(DemosaicPipelineInference, demosaic_pipeline_inference)
HALIDE_REGISTER_GENERATOR(DemosaicPipelineTraining, demosaic_pipeline_training)
