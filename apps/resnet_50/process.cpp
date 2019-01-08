#include "halide_benchmark.h"

#include "resnet50block0.h"
#include "resnet50block1.h"
#include "resnet50block2.h"
#include "resnet50block3.h"
#include "resnet50block4.h"
#include "resnet50block5.h"
#include "resnet50block6.h"
#include "resnet50block7.h"
#include "resnet50block8.h"
#include "resnet50block9.h"
#include "resnet50block10.h"
#include "resnet50block11.h"
#include "resnet50block12.h"
#include "resnet50block13.h"
#include "resnet50block14.h"
#include "resnet50block15.h"

#include "HalideBuffer.h"
#include "halide_image_io.h"
#include "halide_malloc_trace.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
#include <math.h>

using namespace Halide::Runtime;
using namespace Halide::Tools;

// to preserve my sanity
#define unroll_array_of_16_buffers(buff_name) \
  buff_name[0], \
  buff_name[1], \
  buff_name[2], \
  buff_name[3], \
  buff_name[4], \
  buff_name[5], \
  buff_name[6], \
  buff_name[7], \
  buff_name[8], \
  buff_name[9], \
  buff_name[10], \
  buff_name[11], \
  buff_name[12], \
  buff_name[13], \
  buff_name[14], \
  buff_name[15]

#define unroll_array_of_4_buffers(buff_name) buff_name[0], \
  buff_name[1], \
  buff_name[2], \
  buff_name[3]

#define unroll_array_of_16_params() \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*

#define unroll_array_of_4_params() \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*, \
  halide_buffer_t*

std::string extension(const std::string &filename) {
    size_t i = filename.rfind('.');
    if (i == std::string::npos) {
        i = 0;
    } else {
        i++;
    }
    return filename.substr(i);
}

std::string leaf(const std::string &filename) {
    size_t i = filename.rfind('/');
    if (i == std::string::npos) {
        i = 0;
    } else {
        i++;
    }
    return filename.substr(i);
}

uint32_t fill_buffer_with_random(Buffer<float> &buf, uint32_t seed) {
    std::mt19937 rng((uint32_t) seed);
    buf.for_each_value([&rng](float &f) {
        f = ((float)rng()) / rng.max() - 0.5f;
    });
    return seed;
}

Buffer<float> rand_buffer(const std::vector<int> &shape) {
    Buffer<float> buf(shape);
    uint32_t seed = time(NULL);
    fill_buffer_with_random(buf, seed);
    return buf;
}

/*** loading from file helpers ***/
void load_shape(const std::string &shapefile, int* dims, int &n, int &num_dims) {
  std::cout << leaf(shapefile) << " num_dims : ";
  int d;
  std::ifstream shape_input(shapefile, std::ios::binary);
  shape_input.read(reinterpret_cast<char*>(&d), sizeof(int));
  assert(!shape_input.fail());
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
        uint32_t seed = (uint32_t) time(NULL);
        std::cerr << "Could not load buffer from file: " << filename << "\n Using random values with seed = " << seed << " instead.\n";
        fill_buffer_with_random(buf, seed);
    }
    return buf;
}

void buffer_to_file(const Buffer<float> &buf, const std::string &filename) {
    std::ofstream o(filename, std::ios_base::trunc | std::ios_base::binary);
    o.write((const char *)(buf.data()), buf.size_in_bytes());
    o.close();
    assert(!o.fail());
}

Buffer<float> load_conv_params(const std::string &shapefile, const std::string &datafile) {
  int dims[4];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert(num_dims == 4);
  Buffer<float> buff = buffer_from_file(datafile, {dims[0], dims[1], dims[2], dims[3]});
  std::cout << "weight shape for " << datafile << std::endl;
  std::cout << buff.dim(0).extent() << " " << buff.dim(1).extent() << " " <<buff.dim(2).extent() << " " << buff.dim(3).extent() << std::endl;
  return buff;
}

Buffer<float> load_batch_norm_params(const std::string &shapefile, const std::string &datafile) {
  int dims[1];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert(num_dims == 1);
  return buffer_from_file(datafile, {dims[0]});
}

Buffer<float> load_batch_norm_var(const std::string &shapefile, const std::string &datafile) {
  int dims[1];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert(num_dims == 1);
  Buffer<float> buf = buffer_from_file(datafile, {dims[0]});
  for (int i = 0; i < dims[0]; i++) {
    if (buf(i) == 0.0f) {
      std::cout << "INVALID" << std::endl;
    }
  }
  return buf;
}

Buffer<float> load_fc_weight(const std::string &shapefile, const std::string &datafile) {
  int dims[2];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert(num_dims == 2);
  return buffer_from_file(datafile, {dims[0], dims[1]});
}

Buffer<float> load_fc_bias(const std::string &shapefile, const std::string &datafile) {
  int dims[1];
  int n;
  int num_dims;
  load_shape(shapefile, &dims[0], n, num_dims);
  assert(num_dims == 1);
  return buffer_from_file(datafile, {dims[0]});
}

bool has_branch1(int micro_block) {
  return (micro_block == 3) || (micro_block == 7) || (micro_block == 13);
}

void normalize(Buffer<float> &buf, const std::vector<float> &mean, const std::vector<float> &std) {
  buf.for_each_element([&](int c, int x, int y) {
    buf(c, x, y) = (buf(c, x, y) - mean[c]) / std[c];
  });
}

typedef int (*FnPtr)(
            halide_buffer_t*,
            halide_buffer_t*,
            unroll_array_of_4_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            halide_buffer_t*,
            unroll_array_of_4_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            halide_buffer_t*,
            unroll_array_of_4_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            halide_buffer_t*,
            unroll_array_of_4_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            halide_buffer_t*,
            unroll_array_of_4_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            unroll_array_of_16_params(),
            halide_buffer_t*,
            halide_buffer_t*,
            halide_buffer_t*,
            halide_buffer_t*);

std::vector<FnPtr> blockFns = {resnet50block0,
                              resnet50block1,
                              resnet50block2,
                              resnet50block3,
                              resnet50block4,
                              resnet50block5,
                              resnet50block6,
                              resnet50block7,
                              resnet50block8,
                              resnet50block9,
                              resnet50block10,
                              resnet50block11,
                              resnet50block12,
                              resnet50block13,
                              resnet50block14,
                              resnet50block15};

int main(int argc, char **argv) {
  if (argc != 5) {
      printf("Usage: %s iterations pytorch_weights_dir inputfile outputfile\n", argv[0]);
      return 1;
  }

  const int NUMBLOCKS = 16;
  const int timing_iterations = atoi(argv[1]);
  std::string weight_dir = argv[2];
  std::string input_file = argv[3];
  std::string output_file = argv[4];

  if (weight_dir.back() != '/') weight_dir += '/';

  std::vector<std::vector<int>> block_dims{
    {256, 56, 56},
    {512, 28, 28},
    {1024, 14, 14},
    {2048, 7, 7}
  };

  const std::vector<float> ImageNet_mean = {0.485, 0.456, 0.406};
  const std::vector<float> ImageNet_std = {0.229, 0.224, 0.225};

  Buffer<float> image;
  if (extension(input_file) == "bin") {
    // load image that is prenormalized and transposed
    //  (TODO: image size is hardcoded for panda)
    image = buffer_from_file(input_file, {3, 224, 224});
    std::cout << "Loading prenormalized, transposed image from " << input_file << std::endl;
  } else {
    // TODO(srj): this path doesn't work; in theory this is the process used to
    // get cropped_panda_preprocessed but in practice the results differ.
    // Leave in for now, pending investigation.
    assert(0);

    Buffer<float> im_in = load_and_convert_image(input_file);
    std::cout << "Loading image from " << input_file << std::endl;
    assert(im_in.dimensions() == 3);
    // Reorder so that that channels is the first dimension
    image = Buffer<float>(im_in.extent(2), im_in.extent(0), im_in.extent(1));
    image.copy_from(im_in.transposed({2, 0, 1}));
    normalize(image, ImageNet_mean, ImageNet_std);
  }

  /** setup inputs and outputs for each block **/
  std::vector<int> image_shape = {image.extent(0), image.extent(1), image.extent(2)};
  std::vector<std::vector<int>> input_shapes;
  std::vector<std::vector<int>> output_shapes;
  int macro_block = 0;
  for (int micro_block = 0; micro_block < NUMBLOCKS; micro_block++) {
    std::vector<int> input_shape;
    if (micro_block == 0) {
      input_shape = {3, 224, 224};
    } else if (has_branch1(micro_block)) {
      macro_block++;
      input_shape = block_dims[macro_block-1];
    } else {
      input_shape = block_dims[macro_block];
    }
    input_shapes.push_back(input_shape);
    output_shapes.push_back(block_dims[macro_block]);
  }

  std::vector<Buffer<float>> block_outputs(NUMBLOCKS);
  for (int i = 0; i < NUMBLOCKS; i++) {
    block_outputs[i] = Buffer<float>(output_shapes[i]);
  }
  Buffer<float> final_output(1000);

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
  conv1_sig = load_batch_norm_var(shapefile, datafile);

  shapefile = weight_dir + "bn1_weight_shape.data";
  datafile = weight_dir + "bn1_weight.data";
  conv1_gamma = load_batch_norm_params(shapefile, datafile);

  shapefile = weight_dir + "bn1_bias_shape.data";
  datafile = weight_dir + "bn1_bias.data";
  conv1_beta = load_batch_norm_params(shapefile, datafile);

  std::string layer_names[NUMBLOCKS] = {"layer1_0", "layer1_1", "layer1_2",
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
    br1_sig[i] = load_batch_norm_var(sig_shapefile, sig_datafile);
    br1_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
    br1_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
  }

  // load branch 2 data
  for (int i = 0; i < NUMBLOCKS; i++) { // 2:a,b,c -- 3:a,b,c,d -- 4:a,b,c,d,e,f --5:a,b,c
    for (int j = 1; j <= 3; j++) { // conv 1, 2, 3 per block
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
        br2a_sig[i] = load_batch_norm_var(sig_shapefile, sig_datafile);
        br2a_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
        br2a_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
      }
      else if (j == 2) {
        br2b_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
        br2b_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
        br2b_sig[i] = load_batch_norm_var(sig_shapefile, sig_datafile);
        br2b_gamma[i] = load_batch_norm_params(gamma_shapefile, gamma_datafile);
        br2b_beta[i] = load_batch_norm_params(beta_shapefile, beta_datafile);
      }
      else {
        br2c_conv_weights[i] = load_conv_params(conv_shapefile, conv_datafile);
        br2c_mu[i] = load_batch_norm_params(mu_shapefile, mu_datafile);
        br2c_sig[i] = load_batch_norm_var(sig_shapefile, sig_datafile);
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
  Buffer<float> input;

  /** DONE LOADING WEIGHTS **/
  double best = benchmark(timing_iterations, 1, [&]() {
    input = image;

    for (int block_id = 0; block_id < 16; block_id++) {
      std::vector<int> blockin_dim;
      if (block_id == 0) {
        blockin_dim = {64, 56, 56};
      }
      else {
        blockin_dim = input_shapes[block_id];
      }

      FnPtr blockfn = blockFns[block_id];

      blockfn(input,
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
            block_outputs[block_id],
            final_output);
        input = block_outputs[block_id];
    }
  });

  printf("Manually tuned time: %gms\n", best * 1e3);
  buffer_to_file(final_output, output_file);

  // check final output
  int max_class = -1;
  float max_val = -100;
  for (int i = 0; i < 1000; i++) {
    if (final_output(i) > max_val) {
      max_val = final_output(i);
      max_class = i;
    }
  }
  std::cout << " class: " << max_class << std::endl;

}
