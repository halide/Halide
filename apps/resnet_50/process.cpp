#include "halide_benchmark.h"

#include "resnet50block.h"

#include "HalideBuffer.h"
#include "halide_image_io.h"
#include "halide_malloc_trace.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <fstream>
#include <iostream>
#include <tuple>

using namespace Halide::Runtime;
using namespace Halide::Tools;

// to preserve my sanity
#define unroll_array_of_16_buffers(buff_name) buff_name[0], buff_name[1], buff_name[2], buff_name[3], buff_name[4], \
buff_name[5], buff_name[6], buff_name[7], buff_name[8], buff_name[9], buff_name[10], buff_name[11], buff_name[12], \
buff_name[13], buff_name[14], buff_name[15]

#define unroll_array_of_4_buffers(buff_name) buff_name[0], buff_name[1], buff_name[2], buff_name[3]


/*** loading from file helpers ***/
void load_shape(std::string shapefile, int* dims, int &n, int &num_dims) {
  std::cout << shapefile << " num_dims : ";
  int d;
  std::ifstream shape_input(shapefile, std::ios::binary);
  shape_input.read(reinterpret_cast<char*>(&d), sizeof(int));
  num_dims = d;
  
  std::cout << num_dims << " shape: ";
  n = 1;
  for (int i = 0; i < num_dims; i++) {
    shape_input.read(reinterpret_cast<char*>(&dims[i]), sizeof(int));
    n *= dims[i];
    std::cout << dims[i] << " ";
  } 
  std::cout << std::endl;
  shape_input.close();

}

Buffer<float> buffer_from_file(const std::string &filename, const std::vector<int> &shape) {
    Buffer<float> buf(shape);
    std::ifstream i(filename, std::ios_base::binary);
    i.read((char *)(buf.data()), buf.size_in_bytes());
    i.close();
    if (i.fail()) {
        auto seed = time(NULL);
        std::mt19937 rng((uint32_t) seed);
        std::cerr << "Could not load buffer from file: " << filename << "\n Using random values with seed = " << seed << " instead.\n";
        buf.for_each_value([&rng](float &f) {
                f = ((float)rng()) / rng.max() - 0.5f;
            });
    }

    return buf;
}

Buffer<float> load_conv_params(std::string shapefile, std::string datafile) {
  int dims[4];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert (num_dims == 4);
  return buffer_from_file(datafile, {dims[0], dims[1], dims[2], dims[3]});
}

Buffer<float> load_batch_norm_params(std::string shapefile, std::string datafile) {
  int dims[1];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert (num_dims == 1);
  return buffer_from_file(datafile, {dims[0]});
}

Buffer<float> load_fc_weight(std::string shapefile, std::string datafile) {
  int dims[2];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert (num_dims == 2);
  return buffer_from_file(datafile, {dims[0], dims[1]});
}

Buffer<float> load_fc_bias(std::string shapefile, std::string datafile) {
  int dims[1];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert (num_dims == 1);
  return buffer_from_file(datafile, {dims[0]});
}

bool has_branch1(int micro_block) {
  return (micro_block == 3) || (micro_block == 7) || (micro_block == 13);
}

Buffer<float> rand_buffer(const std::vector<int> &shape) {
    Buffer<float> buf(shape);
    auto seed = time(NULL);
    std::mt19937 rng((uint32_t) seed);
    buf.for_each_value([&rng](float &f) {
            f = ((float)rng()) / rng.max() - 0.5f;
        });
    return buf;
}

int main(int argc, char **argv) {
  int timing_iterations = atoi(argv[1]);
  int macro_block_id = atoi(argv[2]);
  int micro_block_id = atoi(argv[3]);

  typedef std::vector<int> tensor_dim; 
  std::vector<tensor_dim> block_dims{
    {256, 56, 56},
    {512, 28, 28},
    {1024, 14, 14},
    {2048, 7, 7}
  };
  printf("macro block %d\n", macro_block_id);
  printf("micro block %d\n", micro_block_id);

  tensor_dim output_dim = block_dims[macro_block_id];

  std::vector<int> input_shape;
  if (micro_block_id == 0) {
    input_shape = {3, 224, 224};
  } else if (has_branch1(micro_block_id)) {
    input_shape = block_dims[macro_block_id-1];
  } else {
    input_shape = block_dims[macro_block_id];
  }

  Buffer<float> input = rand_buffer(input_shape);
  std::cout << input(10,5,5) << std::endl;
  //Buffer<float> input(image, input_shape[0], input_shape[1], input_shape[2]);
  Buffer<float> block_output(output_dim[0], output_dim[1], output_dim[2]);
  Buffer<float> final_output(1000);

  std::string weight_dir = "./weights/";

  int num_sections = 16;

  Buffer<float> conv1_weights;
  Buffer<float> conv1_mu;
  Buffer<float> conv1_sig;
  Buffer<float> conv1_gamma;
  Buffer<float> conv1_beta;

  Buffer<float> br2a_conv_weights[16];
  Buffer<float> br2b_conv_weights[16];
  Buffer<float> br2c_conv_weights[16];
  Buffer<float> br1_conv_weights[4];

  Buffer<float> br2a_gamma[16];
  Buffer<float> br2b_gamma[16];
  Buffer<float> br2c_gamma[16];
  Buffer<float> br1_gamma[4];

  Buffer<float> br2a_beta[16];
  Buffer<float> br2b_beta[16];
  Buffer<float> br2c_beta[16];
  Buffer<float> br1_beta[4];

  Buffer<float> br2a_mu[16];
  Buffer<float> br2b_mu[16];
  Buffer<float> br2c_mu[16];
  Buffer<float> br1_mu[4];

  Buffer<float> br2a_sig[16];
  Buffer<float> br2b_sig[16];
  Buffer<float> br2c_sig[16];
  Buffer<float> br1_sig[4];

  /** load parameters for first section **/
  std::string shapefile, datafile;
  shapefile = weight_dir + "conv1_weight_shape.data";
  datafile = weight_dir + "conv1_weight.data";
  conv1_weights = load_conv_params(shapefile, datafile);
 
  shapefile = weight_dir + "bn1_running_mean_shape.data";
  datafile = weight_dir + "bn1_running_mean.data";
  conv1_mu = load_batch_norm_params(shapefile, datafile);
  
  shapefile = weight_dir + "bn1_running_var_shape.data";
  datafile = weight_dir + "bn1_running_var.data";
  conv1_sig = load_batch_norm_params(shapefile, datafile);

  shapefile = weight_dir + "bn1_weight_shape.data";
  datafile = weight_dir + "bn1_weight.data";
  conv1_gamma = load_batch_norm_params(shapefile, datafile);
  
  shapefile = weight_dir + "bn1_bias_shape.data";
  datafile = weight_dir + "bn1_bias.data";
  conv1_beta = load_batch_norm_params(shapefile, datafile);
  
  std::string layer_names[num_sections] = {"layer1_0", "layer1_1", "layer1_2",
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
      }
      else if (j == 2) {
        br2b_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
        br2b_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
        br2b_sig[i] = load_batch_norm_params(sig_shapefile, sig_datafile);
        br2b_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
        br2b_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
      }
      else {
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

  Buffer<float> fc1000_weights = load_fc_weight(weight_shapefile, weight_datafile);
  Buffer<float> fc1000_bias = load_fc_bias(bias_shapefile, bias_datafile);

  double best = benchmark(timing_iterations, 1, [&]() {
    resnet50block(
            input,
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
            block_output,
            final_output
    );
  });
  std::cout << block_output(5,5,5) << std::endl;

  printf("Manually tuned time: %gms\n", best * 1e3);

}
