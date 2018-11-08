#include <cstdio>
#include <chrono>
#include "conv_layer.h"
#include "conv_layer_auto_schedule.h"

#include "halide_benchmark.h"
#include "HalideBuffer.h"

using namespace Halide::Tools;
using namespace Halide::Runtime;

// Stuff for the MKL example
#include <iostream>
#include <numeric>
#include <string>
#include <mkldnn.hpp>
using namespace mkldnn;

int main(int argc, char **argv) {

    Buffer<float> input(64, 128+2, 128+2, 4);
    Buffer<float> filter(64, 3, 3, 64);
    Buffer<float> bias(64);

    input.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});
    filter.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});
    bias.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});

    Buffer<float> output(64, 128, 128, 4);

    /*
    Buffer<float> input(128+2, 128+2, 64, 4);
    Buffer<float> filter(3, 3, 64, 64);
    Buffer<float> bias(64);

    input.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});
    filter.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});
    bias.for_each_value([&](float &f) {f = (double)rand()/RAND_MAX;});

    Buffer<float> output(128, 128, 64, 4);
    */

    // conv_layer(input, filter, bias, output);

    // Timing code

    // Manually-tuned version
    double min_t_manual = benchmark(20, 20, [&]() {
        conv_layer(input, filter, bias, output);
    });
    printf("Manually-tuned time: %gms\n", min_t_manual * 1e3);

    // Auto-scheduled version
    double min_t_auto = benchmark(20, 20, [&]() {
        conv_layer_auto_schedule(input, filter, bias, output);
    });
    printf("Auto-scheduled time: %gms\n", min_t_auto * 1e3);

    // MKL (adapted from https://github.com/intel/mkl-dnn/blob/master/examples/simple_net.cpp)
    try {
        auto cpu_engine = engine(engine::cpu, 0);

        /* Create a vector primitive to hold the network. For efficienty purpose,
         * weights are stored in a separate net to perform reordering only once. */
        std::vector<primitive> net;
        std::vector<primitive> net_weights;

        memory::dims conv1_src_tz = { 4, 64, 128+2, 128+2 };
        memory::dims conv1_weights_tz = { 64, 64, 3, 3 };
        memory::dims conv1_bias_tz = { 64 };
        memory::dims conv1_dst_tz = { 4, 64, 128, 128 };
        memory::dims conv1_strides = { 1, 1 };
        auto conv1_padding = { 0, 0 };

        /* create memory for user data */
        auto user_src_memory
            = memory({ { { conv1_src_tz }, memory::data_type::f32, memory::format::nchw }, cpu_engine }, input.data());
        auto user_weights_memory
            = memory({ { { conv1_weights_tz }, memory::data_type::f32, memory::format::oihw }, cpu_engine }, filter.data());
        auto user_bias_memory = memory({ { { conv1_bias_tz }, memory::data_type::f32, memory::format::x }, cpu_engine }, bias.data());

        auto user_dst_memory = memory({ { { conv1_dst_tz }, memory::data_type::f32,
                                                                memory::format::nchw },
                    cpu_engine },
            output.data());

        /* create memory descriptors for convolution data w/ no specified format
         */
        auto conv1_src_md = memory::desc({ conv1_src_tz }, memory::data_type::f32, memory::format::any);
        auto conv1_bias_md = memory::desc({ conv1_bias_tz }, memory::data_type::f32, memory::format::any);
        auto conv1_weights_md = memory::desc({ conv1_weights_tz }, memory::data_type::f32, memory::format::any);
        auto conv1_dst_md = memory::desc({ conv1_dst_tz }, memory::data_type::f32, memory::format::any);

        /* create a convolution */
        auto conv1_desc = convolution_forward::desc(
                                                    prop_kind::forward_inference, convolution_direct, conv1_src_md,
                                                    conv1_weights_md, conv1_bias_md, conv1_dst_md, conv1_strides,
                                                    conv1_padding, conv1_padding, padding_kind::zero);
        auto conv1_prim_desc
            = convolution_forward::primitive_desc(conv1_desc, cpu_engine);

        auto conv1_src_memory = user_src_memory;
        if (memory::primitive_desc(conv1_prim_desc.src_primitive_desc())
            != user_src_memory.get_primitive_desc()) {
            conv1_src_memory = memory(conv1_prim_desc.src_primitive_desc());
            net.push_back(reorder(user_src_memory, conv1_src_memory));
        }

        auto conv1_weights_memory = user_weights_memory;
        if (memory::primitive_desc(conv1_prim_desc.weights_primitive_desc())
            != user_weights_memory.get_primitive_desc()) {
            conv1_weights_memory
                = memory(conv1_prim_desc.weights_primitive_desc());
            net_weights.push_back(
                                  reorder(user_weights_memory, conv1_weights_memory));
        }

        auto conv1_dst_memory = memory(conv1_prim_desc.dst_primitive_desc());

        /* create convolution primitive and add it to net */
        net.push_back(convolution_forward(conv1_prim_desc, conv1_src_memory,
                                          conv1_weights_memory, user_bias_memory,
                                          conv1_dst_memory));

        // A ReLU *should* be free, but in MKL it's apparently
        // not. Switch to 0 below to check performance without the
        // relu
        #if 1
        const float negative1_slope = 1.0f;

        /* create relu primitive and add it to net */
        auto relu1_desc = eltwise_forward::desc(prop_kind::forward_inference,
                                                algorithm::eltwise_relu,
                                                conv1_dst_memory.get_primitive_desc().desc(), negative1_slope);
        auto relu1_prim_desc
            = eltwise_forward::primitive_desc(relu1_desc, cpu_engine);

        net.push_back(eltwise_forward(
                                      relu1_prim_desc, conv1_dst_memory, conv1_dst_memory));

        auto relu1_dst_memory = memory(relu1_prim_desc.dst_primitive_desc());

        if (relu1_dst_memory != user_dst_memory) {
            net.push_back(reorder(relu1_dst_memory, user_dst_memory));
        }
        #endif

        stream(stream::kind::eager).submit(net_weights).wait();
        double mkl_time = benchmark(20, 20, [&]() {
                stream(stream::kind::eager).submit(net).wait();
            });
        printf("MKL time: %gms\n", mkl_time * 1e3);
    } catch (const mkldnn::error &e) {
        std::cerr << e.message << "\n";
    }

    return 0;
}
