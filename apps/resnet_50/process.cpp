#include "halide_benchmark.h"

#include "resnet50block_manual0.h"
#include "resnet50block_manual1.h"
#include "resnet50block_manual2.h"
#include "resnet50block_manual3.h"
#include "resnet50block_manual4.h"
#include "resnet50block_manual5.h"
#include "resnet50block_manual6.h"
#include "resnet50block_manual7.h"
#include "resnet50block_manual8.h"
#include "resnet50block_manual9.h"
#include "resnet50block_manual10.h"
#include "resnet50block_manual11.h"
#include "resnet50block_manual12.h"
#include "resnet50block_manual13.h"
#include "resnet50block_manual14.h"
#include "resnet50block_manual15.h"

#include "resnet50block_classic_auto_schedule0.h"
#include "resnet50block_classic_auto_schedule1.h"
#include "resnet50block_classic_auto_schedule2.h"
#include "resnet50block_classic_auto_schedule3.h"
#include "resnet50block_classic_auto_schedule4.h"
#include "resnet50block_classic_auto_schedule5.h"
#include "resnet50block_classic_auto_schedule6.h"
#include "resnet50block_classic_auto_schedule7.h"
#include "resnet50block_classic_auto_schedule8.h"
#include "resnet50block_classic_auto_schedule9.h"
#include "resnet50block_classic_auto_schedule10.h"
#include "resnet50block_classic_auto_schedule11.h"
#include "resnet50block_classic_auto_schedule12.h"
#include "resnet50block_classic_auto_schedule13.h"
#include "resnet50block_classic_auto_schedule14.h"
#include "resnet50block_classic_auto_schedule15.h"

#include "resnet50block_auto_schedule0.h"
#include "resnet50block_auto_schedule1.h"
#include "resnet50block_auto_schedule2.h"
#include "resnet50block_auto_schedule3.h"
#include "resnet50block_auto_schedule4.h"
#include "resnet50block_auto_schedule5.h"
#include "resnet50block_auto_schedule6.h"
#include "resnet50block_auto_schedule7.h"
#include "resnet50block_auto_schedule8.h"
#include "resnet50block_auto_schedule9.h"
#include "resnet50block_auto_schedule10.h"
#include "resnet50block_auto_schedule11.h"
#include "resnet50block_auto_schedule12.h"
#include "resnet50block_auto_schedule13.h"
#include "resnet50block_auto_schedule14.h"
#include "resnet50block_auto_schedule15.h"

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

namespace {

constexpr bool verbose = false;

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

/*** loading from file helpers ***/
void load_shape(const std::string &shapefile, int* dims, int &n, int &num_dims) {
  if (verbose) {
    std::cout << leaf(shapefile) << " num_dims : ";
  }
  int d;
  std::ifstream shape_input(shapefile, std::ios::binary);
  shape_input.read(reinterpret_cast<char*>(&d), sizeof(int));
  assert(!shape_input.fail());
  num_dims = d;

  if (verbose) {
    std::cout << num_dims << " shape: ";
  }
  n = 1;
  for (int i = 0; i < num_dims; i++) {
    shape_input.read(reinterpret_cast<char*>(&dims[i]), sizeof(int));
    n *= dims[i];
    if (verbose) {
      std::cout << dims[i] << " ";
    }
  }
  if (verbose) {
    std::cout << std::endl;
  }
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
  if (verbose) {
    std::cout << "weight shape for " << datafile << std::endl;
    std::cout << buff.dim(0).extent() << " " << buff.dim(1).extent() << " " <<buff.dim(2).extent() << " " << buff.dim(3).extent() << std::endl;
  }
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
      std::cerr << "INVALID" << std::endl;
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

const FnPtr blockFns[3][16] = {
  {
    resnet50block_manual0,
    resnet50block_manual1,
    resnet50block_manual2,
    resnet50block_manual3,
    resnet50block_manual4,
    resnet50block_manual5,
    resnet50block_manual6,
    resnet50block_manual7,
    resnet50block_manual8,
    resnet50block_manual9,
    resnet50block_manual10,
    resnet50block_manual11,
    resnet50block_manual12,
    resnet50block_manual13,
    resnet50block_manual14,
    resnet50block_manual15
  },
  {
    resnet50block_classic_auto_schedule0,
    resnet50block_classic_auto_schedule1,
    resnet50block_classic_auto_schedule2,
    resnet50block_classic_auto_schedule3,
    resnet50block_classic_auto_schedule4,
    resnet50block_classic_auto_schedule5,
    resnet50block_classic_auto_schedule6,
    resnet50block_classic_auto_schedule7,
    resnet50block_classic_auto_schedule8,
    resnet50block_classic_auto_schedule9,
    resnet50block_classic_auto_schedule10,
    resnet50block_classic_auto_schedule11,
    resnet50block_classic_auto_schedule12,
    resnet50block_classic_auto_schedule13,
    resnet50block_classic_auto_schedule14,
    resnet50block_classic_auto_schedule15
  },
  {
    resnet50block_auto_schedule0,
    resnet50block_auto_schedule1,
    resnet50block_auto_schedule2,
    resnet50block_auto_schedule3,
    resnet50block_auto_schedule4,
    resnet50block_auto_schedule5,
    resnet50block_auto_schedule6,
    resnet50block_auto_schedule7,
    resnet50block_auto_schedule8,
    resnet50block_auto_schedule9,
    resnet50block_auto_schedule10,
    resnet50block_auto_schedule11,
    resnet50block_auto_schedule12,
    resnet50block_auto_schedule13,
    resnet50block_auto_schedule14,
    resnet50block_auto_schedule15
  },
};

}  // namespace

int main(int argc, char **argv) {
  if (argc != 6) {
      std::cout << "Usage: process iterations scheduletype pytorch_weights_dir inputfile outputfile\n";
      return 1;
  }

  const int NUMBLOCKS = 16;
  const int timing_iterations = atoi(argv[1]);
  std::string schedule_type_name = argv[2];
  std::string weight_dir = argv[3];
  std::string input_file = argv[4];
  std::string output_file = argv[5];

  std::map<std::string, int> schedule_type_map = {
    {"manual", 0},
    {"classic_auto_schedule", 1},
    {"auto_schedule", 2},
  };

  const auto it = schedule_type_map.find(schedule_type_name);
  if (it == schedule_type_map.end()) {
    std::cout << "scheduletype must be one of manual, auto_schedule, classic_auto_schedule\n";
    return 1;
  }
  const int schedule_type = it->second;

  if (weight_dir.back() != '/') weight_dir += '/';

  std::vector<std::vector<int>> block_dims{
    {256, 56, 56},
    {512, 28, 28},
    {1024, 14, 14},
    {2048, 7, 7}
  };

  /** setup inputs and outputs for each block **/
  constexpr int kImageWidth = 224;
  constexpr int kImageHeight = 224;
  constexpr int kImageChannels = 3;
  std::vector<int> image_shape = {kImageChannels, kImageHeight, kImageWidth};
  std::vector<std::vector<int>> input_shapes;
  std::vector<std::vector<int>> output_shapes;
  int macro_block = 0;
  for (int micro_block = 0; micro_block < NUMBLOCKS; micro_block++) {
    std::vector<int> input_shape;
    if (micro_block == 0) {
      input_shape = image_shape;
    } else if (has_branch1(micro_block)) {
      macro_block++;
      input_shape = block_dims[macro_block-1];
    } else {
      input_shape = block_dims[macro_block];
    }
    input_shapes.push_back(input_shape);
    output_shapes.push_back(block_dims[macro_block]);
  }

  const std::vector<float> ImageNet_mean = {0.485, 0.456, 0.406};
  const std::vector<float> ImageNet_std = {0.229, 0.224, 0.225};

  Buffer<float> image;
  if (extension(input_file) == "bin") {
    // load image that is prenormalized and transposed
    // (we'll just assume it matches canonical size and dimensions)
    image = buffer_from_file(input_file, image_shape);
    std::cout << "Loading prenormalized, transposed image from " << input_file << std::endl;
  } else {
    Buffer<float> im_in = load_and_convert_image(input_file);
    std::cout << "Loading image from " << input_file << std::endl;
    if (im_in.dimensions() != 3 || im_in.width() != kImageWidth || im_in.height() != kImageHeight || im_in.channels() != kImageChannels) {
      std::cerr << "ResNet requires an image of exactly " << kImageWidth << "x" << kImageHeight << "x" << kImageChannels;
      exit(1);
    }
    // Reorder dimensions
    image = Buffer<float>(kImageChannels, kImageHeight, kImageWidth);
    image.copy_from(im_in.transposed({2, 1, 0}));
    normalize(image, ImageNet_mean, ImageNet_std);
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

      FnPtr blockfn = blockFns[schedule_type][block_id];

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
    final_output.device_sync();
  });

  final_output.copy_to_host();
  std::cout << "Manually tuned time: " << best * 1e3 << "ms for schedule_type=" << schedule_type_name << "\n";
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
