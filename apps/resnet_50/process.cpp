#include "halide_benchmark.h"

#include "resnet50.h"

#include "HalideBuffer.h"
#include "halide_image_io.h"
#include "halide_malloc_trace.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <float.h>
#include <fstream>
#include <iostream>
#include <random>

using namespace Halide::Runtime;
using namespace Halide::Tools;

// to preserve my sanity
#define unroll_array_of_16_buffers(buff_name) \
    buff_name[0],                             \
        buff_name[1],                         \
        buff_name[2],                         \
        buff_name[3],                         \
        buff_name[4],                         \
        buff_name[5],                         \
        buff_name[6],                         \
        buff_name[7],                         \
        buff_name[8],                         \
        buff_name[9],                         \
        buff_name[10],                        \
        buff_name[11],                        \
        buff_name[12],                        \
        buff_name[13],                        \
        buff_name[14],                        \
        buff_name[15]

#define unroll_array_of_4_buffers(buff_name) buff_name[0], \
                                             buff_name[1], \
                                             buff_name[2], \
                                             buff_name[3]

std::vector<int> load_shape(const std::string &shapefile) {
    std::ifstream infile(shapefile, std::ios::binary);
    int num_dims = 0;
    infile.read(reinterpret_cast<char *>(&num_dims), sizeof(int));
    std::vector<int> dims(num_dims);
    infile.read((char *)dims.data(), num_dims * sizeof(int));
    infile.close();
    assert(!infile.fail());
    return dims;
}

void write_buffer_to_file(const Buffer<float, 1> &buf, const std::string &filename) {
    std::ofstream o(filename, std::ios_base::trunc | std::ios_base::binary);
    o.write((const char *)(buf.data()), buf.size_in_bytes());
    o.close();
    assert(!o.fail());
}

// Deliberately unconstrained dims here; caller will
// convert with an implicit runtime check
Buffer<float> load_buffer_from_file(const std::string &filename, std::vector<int> &shape) {
    Buffer<float> buffer(shape);
    std::ifstream infile(filename, std::ios::binary);
    infile.read((char *)buffer.data(), buffer.size_in_bytes());
    infile.close();
    assert(!infile.fail());
    return buffer;
}

Buffer<float, 4> load_conv_params(std::string shapefile, std::string datafile) {
    std::vector<int> shape = load_shape(shapefile);
    assert(shape.size() == 4);
    return load_buffer_from_file(datafile, shape);
}

Buffer<float, 1> load_batch_norm_params(std::string shapefile, std::string datafile) {
    std::vector<int> shape = load_shape(shapefile);
    assert(shape.size());
    return load_buffer_from_file(datafile, shape);
}

Buffer<float, 2> load_fc_weight(std::string shapefile, std::string datafile) {
    std::vector<int> shape = load_shape(shapefile);
    assert(shape.size() == 2);
    return load_buffer_from_file(datafile, shape);
}

Buffer<float, 1> load_fc_bias(std::string shapefile, std::string datafile) {
    std::vector<int> shape = load_shape(shapefile);
    assert(shape.size() == 1);
    return load_buffer_from_file(datafile, shape);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: iterations weight_dir seed output_file");
        return -1;
    }
    int iterations = atoi(argv[1]);
    std::string weight_dir = argv[2];
    int seed = atoi(argv[3]);
    std::string output_file = argv[4];

    Buffer<float, 3> input(3, 224, 224);
    Buffer<float, 1> output(1000);

    Buffer<float, 4> conv1_weights;
    Buffer<float, 1> conv1_mu;
    Buffer<float, 1> conv1_sig;
    Buffer<float, 1> conv1_gamma;
    Buffer<float, 1> conv1_beta;

    Buffer<float, 4> br2a_conv_weights[16];
    Buffer<float, 4> br2b_conv_weights[16];
    Buffer<float, 4> br2c_conv_weights[16];
    Buffer<float, 4> br1_conv_weights[4];

    Buffer<float, 1> br2a_gamma[16];
    Buffer<float, 1> br2b_gamma[16];
    Buffer<float, 1> br2c_gamma[16];
    Buffer<float, 1> br1_gamma[4];

    Buffer<float, 1> br2a_beta[16];
    Buffer<float, 1> br2b_beta[16];
    Buffer<float, 1> br2c_beta[16];
    Buffer<float, 1> br1_beta[4];

    Buffer<float, 1> br2a_mu[16];
    Buffer<float, 1> br2b_mu[16];
    Buffer<float, 1> br2c_mu[16];
    Buffer<float, 1> br1_mu[4];

    Buffer<float, 1> br2a_sig[16];
    Buffer<float, 1> br2b_sig[16];
    Buffer<float, 1> br2c_sig[16];
    Buffer<float, 1> br1_sig[4];

    /** load parameters for first section **/
    std::string conv1_w_shapefile = weight_dir + "conv1_weight_shape.data";
    std::string conv1_w_datafile = weight_dir + "conv1_weight.data";
    conv1_weights = load_conv_params(conv1_w_shapefile, conv1_w_datafile);

    std::string conv1_mu_shapefile = weight_dir + "bn1_running_mean_shape.data";
    std::string conv1_mu_datafile = weight_dir + "bn1_running_mean.data";
    conv1_mu = load_batch_norm_params(conv1_mu_shapefile, conv1_mu_datafile);

    std::string conv1_sig_shapefile = weight_dir + "bn1_running_var_shape.data";
    std::string conv1_sig_datafile = weight_dir + "bn1_running_var.data";
    conv1_sig = load_batch_norm_params(conv1_sig_shapefile, conv1_sig_datafile);

    std::string conv1_gamma_shapefile = weight_dir + "bn1_weight_shape.data";
    std::string conv1_gamma_datafile = weight_dir + "bn1_weight.data";
    conv1_gamma = load_batch_norm_params(conv1_gamma_shapefile, conv1_gamma_datafile);

    std::string conv1_beta_shapefile = weight_dir + "bn1_bias_shape.data";
    std::string conv1_beta_datafile = weight_dir + "bn1_bias.data";
    conv1_beta = load_batch_norm_params(conv1_beta_shapefile, conv1_beta_datafile);

    std::string layer_names[16] = {"layer1_0", "layer1_1", "layer1_2",
                                   "layer2_0", "layer2_1", "layer2_2", "layer2_3",
                                   "layer3_0", "layer3_1", "layer3_2", "layer3_3", "layer3_4", "layer3_5",
                                   "layer4_0", "layer4_1", "layer4_2"};

    std::string br1_names[4] = {"layer1_0_downsample", "layer2_0_downsample", "layer3_0_downsample", "layer4_0_downsample"};

    // load branch 1 data
    for (int i = 0; i < 4; i++) {
        std::string conv_shapefile = weight_dir + br1_names[i] + "_0_weight_shape.data";
        std::string conv_datafile = weight_dir + br1_names[i] + "_0_weight.data";

        std::string mu_shapefile = weight_dir + br1_names[i] + "_1_running_mean_shape.data";
        std::string mu_datafile = weight_dir + br1_names[i] + "_1_running_mean.data";

        std::string sig_shapefile = weight_dir + br1_names[i] + "_1_running_var_shape.data";
        std::string sig_datafile = weight_dir + br1_names[i] + "_1_running_var.data";

        std::string gamma_shapefile = weight_dir + br1_names[i] + "_1_weight_shape.data";
        std::string gamma_datafile = weight_dir + br1_names[i] + "_1_weight.data";

        std::string beta_shapefile = weight_dir + br1_names[i] + "_1_bias_shape.data";
        std::string beta_datafile = weight_dir + br1_names[i] + "_1_bias.data";

        br1_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
        br1_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
        br1_sig[i] = load_batch_norm_params(sig_shapefile, sig_datafile);
        br1_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
        br1_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
    }

    // load branch 2 data
    for (int i = 0; i < 16; i++) {
        for (int j = 1; j <= 3; j++) {
            std::string section = std::to_string(j);

            std::string conv_shapefile = weight_dir + layer_names[i] + "_conv" + section + "_weight_shape.data";
            std::string conv_datafile = weight_dir + layer_names[i] + "_conv" + section + "_weight.data";

            std::string mu_shapefile = weight_dir + layer_names[i] + "_bn" + section + "_running_mean_shape.data";
            std::string mu_datafile = weight_dir + layer_names[i] + "_bn" + section + "_running_mean.data";

            std::string sig_shapefile = weight_dir + layer_names[i] + "_bn" + section + "_running_var_shape.data";
            std::string sig_datafile = weight_dir + layer_names[i] + "_bn" + section + "_running_var.data";

            std::string gamma_shapefile = weight_dir + layer_names[i] + "_bn" + section + "_weight_shape.data";
            std::string gamma_datafile = weight_dir + layer_names[i] + "_bn" + section + "_weight.data";

            std::string beta_shapefile = weight_dir + layer_names[i] + "_bn" + section + "_bias_shape.data";
            std::string beta_datafile = weight_dir + layer_names[i] + "_bn" + section + "_bias.data";

            if (j == 1) {
                br2a_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
                br2a_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
                br2a_sig[i] = load_batch_norm_params(sig_shapefile, sig_datafile);
                br2a_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
                br2a_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
            } else if (j == 2) {
                br2b_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
                br2b_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
                br2b_sig[i] = load_batch_norm_params(sig_shapefile, sig_datafile);
                br2b_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
                br2b_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
            } else {
                br2c_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
                br2c_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
                br2c_sig[i] = load_batch_norm_params(sig_shapefile, sig_datafile);
                br2c_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
                br2c_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
            }
        }
    }
    // load fc weights
    std::string weight_shapefile = weight_dir + "fc_weight_shape.data";
    std::string weight_datafile = weight_dir + "fc_weight.data";
    std::string bias_shapefile = weight_dir + "fc_bias_shape.data";
    std::string bias_datafile = weight_dir + "fc_bias.data";

    Buffer<float, 2> fc1000_weights = load_fc_weight(weight_shapefile, weight_datafile);
    Buffer<float> fc1000_bias = load_fc_bias(bias_shapefile, bias_datafile);

    std::mt19937 e2(seed);
    input.for_each_value([&e2](float &v) {
        v = e2() / (float)e2.max();
    });
    printf("Running Resnet50 for %d iterations....\n", iterations);
    double best = benchmark(iterations, 1, [&]() {
        resnet50(input,
                 conv1_gamma,
                 unroll_array_of_4_buffers(br1_gamma),
                 unroll_array_of_16_buffers(br2a_gamma),
                 unroll_array_of_16_buffers(br2b_gamma),
                 unroll_array_of_16_buffers(br2c_gamma),
                 conv1_beta,
                 unroll_array_of_4_buffers(br1_beta),
                 unroll_array_of_16_buffers(br2a_beta),
                 unroll_array_of_16_buffers(br2b_beta),
                 unroll_array_of_16_buffers(br2c_beta),
                 conv1_mu,
                 unroll_array_of_4_buffers(br1_mu),
                 unroll_array_of_16_buffers(br2a_mu),
                 unroll_array_of_16_buffers(br2b_mu),
                 unroll_array_of_16_buffers(br2c_mu),
                 conv1_sig,
                 unroll_array_of_4_buffers(br1_sig),
                 unroll_array_of_16_buffers(br2a_sig),
                 unroll_array_of_16_buffers(br2b_sig),
                 unroll_array_of_16_buffers(br2c_sig),
                 conv1_weights,
                 unroll_array_of_4_buffers(br1_conv_weights),
                 unroll_array_of_16_buffers(br2a_conv_weights),
                 unroll_array_of_16_buffers(br2b_conv_weights),
                 unroll_array_of_16_buffers(br2c_conv_weights),
                 fc1000_weights,
                 fc1000_bias,
                 output);
    });
    printf("*************************** Please note ******************************\n"
           "This code hasn't been scheduled properly yet so this runtime \n"
           "isn't representative of anything and should not be used as a basis\n"
           "for any comparisons.\n");
    printf("Execution time : %gms \n", best * 1e3);
    printf("**********************************************************************\n");

    float max_class_val = -FLT_MIN;
    int max_class = 0;
    for (int i = 0; i < 1000; ++i) {
        if (output(i) > max_class_val) {
            max_class_val = output(i);
            max_class = i;
        }
    }
    printf("Class for random data of seed %d is %d\n", seed, max_class);

    printf("Writing output layer to %s\n", output_file.c_str());
    write_buffer_to_file(output, output_file);
}
