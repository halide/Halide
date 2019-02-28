#include <unordered_map>
#include <tuple>
#include "Halide.h"

namespace {

struct Tensor {
    Halide::Func f;
    std::vector<int> shape;
    std::string name;
};

struct WeightShape {
    int c; // output channels
    int w;
    int h;
    int pad;
    int stride;
};

// returns index of found value in array or -1 if not in array
int find_index(int value, std::vector<int> vec) {
  std::vector<int>::iterator it = std::find(vec.begin(), vec.end(), value);
  if (it == vec.end())
    return -1;
  return std::distance(vec.begin(), it);
}


class Resnet50Block: public Halide::Generator<Resnet50Block> {
public:
  GeneratorParam<int> block_id{"block_id", 0}; // 0 through 15 (1 - 16)
  GeneratorParam<bool> classic_auto_schedule_estimates{"classic_auto_schedule_estimates", false};

  Input<Buffer<float>> input{"input", 4};
  /** parameter values for scaling layers **/
  Input<Buffer<float>> conv1_gamma{"conv1_gamma", 1};
  Input<Buffer<float>[4]> br1_gamma{"br1_gamma", 1};
  Input<Buffer<float>[16]> br2a_gamma{"br2a_gamma", 1};
  Input<Buffer<float>[16]> br2b_gamma{"br2b_gamma", 1};
  Input<Buffer<float>[16]> br2c_gamma{"br2c_gamma", 1};

  Input<Buffer<float>> conv1_beta{"conv1_beta", 1};
  Input<Buffer<float>[4]> br1_beta{"br1_beta", 1};
  Input<Buffer<float>[16]> br2a_beta{"br2a_beta", 1};
  Input<Buffer<float>[16]> br2b_beta{"br2b_beta", 1};
  Input<Buffer<float>[16]> br2c_beta{"br2c_beta", 1};

  Input<Buffer<float>> conv1_mu{"conv1_mu", 1};
  Input<Buffer<float>[4]> br1_mu{"br1_mu", 1};
  Input<Buffer<float>[16]> br2a_mu{"br2a_mu", 1};
  Input<Buffer<float>[16]> br2b_mu{"br2b_mu", 1};
  Input<Buffer<float>[16]> br2c_mu{"br2c_mu", 1};

  Input<Buffer<float>> conv1_sig{"conv1_sig", 1};
  Input<Buffer<float>[4]> br1_sig{"br1_sig", 1};
  Input<Buffer<float>[16]> br2a_sig{"br2a_sig", 1};
  Input<Buffer<float>[16]> br2b_sig{"br2b_sig", 1};
  Input<Buffer<float>[16]> br2c_sig{"br2c_sig", 1};

  /** weights and biases for convolutions **/
  Input<Buffer<float>> conv1_weights{"conv1_weights", 4};
  Input<Buffer<float>[4]> br1_conv_weights{"br1_conv_weights", 4};
  Input<Buffer<float>[16]> br2a_conv_weights{"br2a_conv_weights", 4};
  Input<Buffer<float>[16]> br2b_conv_weights{"br2b_conv_weights", 4};
  Input<Buffer<float>[16]> br2c_conv_weights{"br2c_conv_weights", 4};

  Input<Buffer<float>> fc1000_weights{"fc1000_weights", 2};
  Input<Buffer<float>> fc1000_bias{"fc1000_bias", 1};
  Output<Buffer<float>> block_output{"block_output", 4};
  Output<Buffer<float>> final_output{"final_output", 2};

  const std::vector<std::vector<int>> block_dims{
    {256, 56, 56},
    {512, 28, 28},
    {1024, 14, 14},
    {2048, 7, 7}
  };

  /** list out shapes of each layers weights **/
  // weight shapes: out channels, kernel_w, kernel_h, pad, stride. In channels infered by input tensor shape
  const WeightShape conv1_ws = {64, 7, 7, 3, 2};
  const WeightShape pool1_ws = {64, 3, 3, 1, 2};
  const WeightShape pool5_ws = {2048, 7, 7, 0, 1};
  const WeightShape fc1000_ws = {1000, 1, 1, 0, 1}; // 1x1 conv with 2048 input channels and 1000 output channels

  // res2a, res2b, res2c all have shame shapes
  const WeightShape res2x_br2a_ws = {64, 1, 1, 0, 1};
  const WeightShape res2a_br2b_ws = {64, 3, 3, 1, 1};
  const WeightShape res2x_br2b_ws = {64, 3, 3, 1, 1};
  const WeightShape res2x_br2c_ws = {256, 1, 1, 0, 1};
  const WeightShape res2a_br1_ws = {256, 1, 1, 0, 1};

  // res3x is same for most layers
  //WeightShape res3a_br2a_ws = {128, 1, 1, 0, 2};
  const WeightShape res3x_br2a_ws = {128, 1, 1, 0, 1};
  const WeightShape res3a_br2b_ws = {128, 3, 3, 1, 2};
  const WeightShape res3x_br2b_ws = {128, 3, 3, 1, 1};
  const WeightShape res3x_br2c_ws = {512, 1, 1, 0, 1};
  const WeightShape res3a_br1_ws = {512, 1, 1, 0, 2};

  //WeightShape res4a_br2a_ws = {256, 1, 1, 0, 2};
  const WeightShape res4x_br2a_ws = {256, 1, 1, 0, 1};
  const WeightShape res4a_br2b_ws = {256, 3, 3, 1, 2};
  const WeightShape res4x_br2b_ws = {256, 3, 3, 1, 1};
  const WeightShape res4x_br2c_ws = {1024, 1, 1, 0, 1};
  const WeightShape res4a_br1_ws = {1024, 1, 1, 0, 2};

  //WeightShape res5a_br2a_ws = {512, 1, 1, 0, 2};
  const WeightShape res5x_br2a_ws = {512, 1, 1, 0, 1};
  const WeightShape res5a_br2b_ws = {512, 3, 3, 1, 2};
  const WeightShape res5x_br2b_ws = {512, 3, 3, 1, 1};
  const WeightShape res5x_br2c_ws = {2048, 1, 1, 0, 1};
  const WeightShape res5a_br1_ws = {2048, 1, 1, 0, 2};

  const WeightShape br1_ws[4] = {res2a_br1_ws, res3a_br1_ws, res4a_br1_ws, res5a_br1_ws};
  const WeightShape br2a_ws[16] = {res2x_br2a_ws, res2x_br2a_ws, res2x_br2a_ws,
                                   res3x_br2a_ws, res3x_br2a_ws, res3x_br2a_ws, res3x_br2a_ws,
                                   res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws,
                                   res5x_br2a_ws, res5x_br2a_ws, res5x_br2a_ws};
  const WeightShape br2b_ws[16] = {res2a_br2b_ws, res2x_br2b_ws, res2x_br2b_ws,
                                   res3a_br2b_ws, res3x_br2b_ws, res3x_br2b_ws, res3x_br2b_ws,
                                   res4a_br2b_ws, res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws,
                                   res5a_br2b_ws, res5x_br2b_ws, res5x_br2b_ws};
  const WeightShape br2c_ws[16] = {res2x_br2c_ws, res2x_br2c_ws, res2x_br2c_ws,
                                   res3x_br2c_ws, res3x_br2c_ws, res3x_br2c_ws, res3x_br2c_ws,
                                   res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws,
                                   res5x_br2c_ws, res5x_br2c_ws, res5x_br2c_ws};

    Var c, i, j, n;

  void generate() {

    // Algorithm

    /** Declare arrays of other functions and build the requested block **/
    Tensor br1_conv[4];
    Tensor br1_norm[4];
    Tensor br1_scale[4];

    Tensor br2a_conv[16];
    Tensor br2a_norm[16];
    Tensor br2a_scaled[16];
    Tensor br2a_relu[16];

    Tensor br2b_conv[16];
    Tensor br2b_norm[16];
    Tensor br2b_scaled[16];
    Tensor br2b_relu[16];

    Tensor br2c_conv[16];
    Tensor br2c_norm[16];
    Tensor br2c_scaled[16];

    Tensor resunit_sum[16];
    Tensor resunit_relu[16];

    Tensor pool5;
    Tensor fc1000;
    Tensor softmax;

    int macro_block_id_table[16] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3, 3, 3};
    int macro_block_id = macro_block_id_table[block_id];

    std::vector<int> branch1_indices{0,3,7,13};

    // these tensors are different depending on the block and must be conditionally assigned.
    Tensor input_t;
    std::vector<int> input_shape;
    Tensor br2a_input;
    Tensor resunit_sum_input;

    // used only for block_id == 0
    Tensor conv1, norm1, scaled1, relu1, pool1;

    /** if block_id is 0 build the (stem) conv1 section **/
    if (block_id == 0) {
      input_shape = {3, 224, 224};
      input_t.f = input;
      input_t.shape = input_shape;

      conv1 = conv2D(input_t, conv1_ws, conv1_weights, "conv1");
      norm1 = norm_layer(conv1, conv1_mu, conv1_sig, "norm1");
      scaled1 = scale_layer(norm1, conv1_gamma, conv1_beta, "scale1");
      relu1 = relu_layer(scaled1, "relu1");
      pool1 = max_pool_layer(relu1, pool1_ws, "pool1");

      br2a_input = pool1;
    } else {
      int br1_i = find_index(block_id, branch1_indices);

      if (br1_i >= 0) {
        input_shape = block_dims[macro_block_id-1];
      } else {
        input_shape = block_dims[macro_block_id];
      }
      input_t.f = input;
      input_t.shape = input_shape;

      br2a_input = input_t;
    }

    // build branch1 if this section has branch1
    int br1_i = find_index(block_id, branch1_indices);
    if (br1_i >= 0) {
      br1_conv[br1_i] = conv2D(br2a_input, br1_ws[br1_i], br1_conv_weights[br1_i], "br1_conv");
      br1_norm[br1_i] = norm_layer(br1_conv[br1_i], br1_mu[br1_i], br1_sig[br1_i], "br1_norm");
      br1_scale[br1_i] = scale_layer(br1_norm[br1_i], br1_gamma[br1_i], br1_beta[br1_i], "br1_scale");
      resunit_sum_input = br1_scale[br1_i];
    } else {
      resunit_sum_input = input_t;
    }

    // branch2a
    auto weights = br2a_conv_weights[block_id];

    br2a_conv[block_id] = conv2D(br2a_input, br2a_ws[block_id], weights, "block" + std::to_string(block_id) + "_2a_conv");
    br2a_norm[block_id] = norm_layer(br2a_conv[block_id], br2a_mu[block_id], br2a_sig[block_id], "block" + std::to_string(block_id) + "_2a_norm");
    br2a_scaled[block_id] = scale_layer(br2a_norm[block_id], br2a_gamma[block_id], br2a_beta[block_id], "block" + std::to_string(block_id) + "_2a_scale");
    br2a_relu[block_id] = relu_layer(br2a_scaled[block_id], "2a_relu");

    // branch 2b
    weights = br2b_conv_weights[block_id];
    br2b_conv[block_id] = conv2D(br2a_relu[block_id], br2b_ws[block_id], weights, "block" + std::to_string(block_id) + "_2b_conv");
    br2b_norm[block_id] = norm_layer(br2b_conv[block_id], br2b_mu[block_id], br2b_sig[block_id], "block" + std::to_string(block_id) + "_2b_norm");
    br2b_scaled[block_id] = scale_layer(br2b_norm[block_id], br2b_gamma[block_id], br2b_beta[block_id], "block" + std::to_string(block_id) + "_2b_scale");
    br2b_relu[block_id] = relu_layer(br2b_scaled[block_id], "2b_relu");

    // branch 2c
    weights = br2c_conv_weights[block_id];
    br2c_conv[block_id] = conv2D(br2b_relu[block_id], br2c_ws[block_id], weights, "block" + std::to_string(block_id) + "_2c_conv");
    br2c_norm[block_id] = norm_layer(br2c_conv[block_id], br2c_mu[block_id], br2c_sig[block_id], "block" + std::to_string(block_id) + "_2c_norm");
    br2c_scaled[block_id] = scale_layer(br2c_norm[block_id], br2c_gamma[block_id], br2c_beta[block_id], "block" + std::to_string(block_id) + "_2c_scale");

    // create residual unit
    resunit_sum[block_id] = sum_layer(resunit_sum_input, br2c_scaled[block_id], "block" + std::to_string(block_id) + "_res_sum");
    resunit_relu[block_id] = relu_layer(resunit_sum[block_id], "block" + std::to_string(block_id) + "_res_relu");

    // create final 3 layers
    if (block_id == 15) {
        block_output(c, i, j, n) = Halide::undef<float>();
        pool5 = avg_pool_layer(resunit_relu[block_id], pool5_ws, "pool5");
        fc1000 = fc_layer(pool5, fc1000_ws, fc1000_weights, fc1000_bias, "fc");
        final_output = softmax_layer(fc1000, 1000, "softmax");
    } else {
        // output of each block is the residual unit
        block_output(c, i, j, n) = resunit_relu[block_id].f(c, i, j, n);
        final_output(c, n) = Halide::undef<float>();
    }

    // Estimates
    const std::vector<int> output_dim = block_dims[macro_block_id];
    final_output.bound(final_output.args()[0], 0, 1000);
    final_output.bound(final_output.args()[1], 0, 1); // compile for statically-known batch-size 1 for now

    block_output.bound(block_output.args()[0], 0, output_dim[0]);
    block_output.bound(block_output.args()[1], 0, output_dim[1]);
    block_output.bound(block_output.args()[2], 0, output_dim[2]);
    block_output.bound(block_output.args()[3], 0, 1); // compile for batch-size 1

    if (classic_auto_schedule_estimates) {
      // classic auto-scheduler requires explicit estimates for everything,
      // whether or not they can be inferred
      do_class_auto_schedule_estimate();
    }

    // Schedule
    if (!auto_schedule) {
      // Really dumb compute-root-everything schedule
      if (block_id == 0) {
        relu1.f.compute_root();
        pool1.f.compute_root();
      }
      for (int i = 0; i < 4; i++) {
        br1_scale[i].f.compute_root();
      }
      for (int i = 0; i < 16; i++) {
        br2a_relu[i].f.compute_root();
        br2b_relu[i].f.compute_root();
        resunit_relu[i].f.compute_root();
      }
      pool5.f.compute_root();
      fc1000.f.compute_root();
      softmax.f.compute_root();
    }

  }

private:
    // Estimates for the master autoscheduler. Not required for our
    // new algorithm. Derived by running manual pipeline in debug mode
    // and just copying the values actually passed in for block 0.
  void do_class_auto_schedule_estimate() {
      input.dim(0).set_bounds_estimate(0, 3)
        .dim(1).set_bounds_estimate(0, 224)
        .dim(2).set_bounds_estimate(0, 224)
        .dim(3).set_bounds_estimate(0, 1);
      conv1_gamma.dim(0).set_bounds_estimate(0, 64);
      br1_gamma[0].dim(0).set_bounds_estimate(0, 256);
      br1_gamma[1].dim(0).set_bounds_estimate(0, 512);
      br1_gamma[2].dim(0).set_bounds_estimate(0, 1024);
      br1_gamma[3].dim(0).set_bounds_estimate(0, 2048);
      br2a_gamma[0].dim(0).set_bounds_estimate(0, 64);
      br2a_gamma[1].dim(0).set_bounds_estimate(0, 64);
      br2a_gamma[2].dim(0).set_bounds_estimate(0, 64);
      br2a_gamma[3].dim(0).set_bounds_estimate(0, 128);
      br2a_gamma[4].dim(0).set_bounds_estimate(0, 128);
      br2a_gamma[5].dim(0).set_bounds_estimate(0, 128);
      br2a_gamma[6].dim(0).set_bounds_estimate(0, 128);
      br2a_gamma[7].dim(0).set_bounds_estimate(0, 256);
      br2a_gamma[8].dim(0).set_bounds_estimate(0, 256);
      br2a_gamma[9].dim(0).set_bounds_estimate(0, 256);
      br2a_gamma[10].dim(0).set_bounds_estimate(0, 256);
      br2a_gamma[11].dim(0).set_bounds_estimate(0, 256);
      br2a_gamma[12].dim(0).set_bounds_estimate(0, 256);
      br2a_gamma[13].dim(0).set_bounds_estimate(0, 512);
      br2a_gamma[14].dim(0).set_bounds_estimate(0, 512);
      br2a_gamma[15].dim(0).set_bounds_estimate(0, 512);
      br2b_gamma[0].dim(0).set_bounds_estimate(0, 64);
      br2b_gamma[1].dim(0).set_bounds_estimate(0, 64);
      br2b_gamma[2].dim(0).set_bounds_estimate(0, 64);
      br2b_gamma[3].dim(0).set_bounds_estimate(0, 128);
      br2b_gamma[4].dim(0).set_bounds_estimate(0, 128);
      br2b_gamma[5].dim(0).set_bounds_estimate(0, 128);
      br2b_gamma[6].dim(0).set_bounds_estimate(0, 128);
      br2b_gamma[7].dim(0).set_bounds_estimate(0, 256);
      br2b_gamma[8].dim(0).set_bounds_estimate(0, 256);
      br2b_gamma[9].dim(0).set_bounds_estimate(0, 256);
      br2b_gamma[10].dim(0).set_bounds_estimate(0, 256);
      br2b_gamma[11].dim(0).set_bounds_estimate(0, 256);
      br2b_gamma[12].dim(0).set_bounds_estimate(0, 256);
      br2b_gamma[13].dim(0).set_bounds_estimate(0, 512);
      br2b_gamma[14].dim(0).set_bounds_estimate(0, 512);
      br2b_gamma[15].dim(0).set_bounds_estimate(0, 512);
      br2c_gamma[0].dim(0).set_bounds_estimate(0, 256);
      br2c_gamma[1].dim(0).set_bounds_estimate(0, 256);
      br2c_gamma[2].dim(0).set_bounds_estimate(0, 256);
      br2c_gamma[3].dim(0).set_bounds_estimate(0, 512);
      br2c_gamma[4].dim(0).set_bounds_estimate(0, 512);
      br2c_gamma[5].dim(0).set_bounds_estimate(0, 512);
      br2c_gamma[6].dim(0).set_bounds_estimate(0, 512);
      br2c_gamma[7].dim(0).set_bounds_estimate(0, 1024);
      br2c_gamma[8].dim(0).set_bounds_estimate(0, 1024);
      br2c_gamma[9].dim(0).set_bounds_estimate(0, 1024);
      br2c_gamma[10].dim(0).set_bounds_estimate(0, 1024);
      br2c_gamma[11].dim(0).set_bounds_estimate(0, 1024);
      br2c_gamma[12].dim(0).set_bounds_estimate(0, 1024);
      br2c_gamma[13].dim(0).set_bounds_estimate(0, 2048);
      br2c_gamma[14].dim(0).set_bounds_estimate(0, 2048);
      br2c_gamma[15].dim(0).set_bounds_estimate(0, 2048);
      conv1_beta.dim(0).set_bounds_estimate(0, 64);
      br1_beta[0].dim(0).set_bounds_estimate(0, 256);
      br1_beta[1].dim(0).set_bounds_estimate(0, 512);
      br1_beta[2].dim(0).set_bounds_estimate(0, 1024);
      br1_beta[3].dim(0).set_bounds_estimate(0, 2048);
      br2a_beta[0].dim(0).set_bounds_estimate(0, 64);
      br2a_beta[1].dim(0).set_bounds_estimate(0, 64);
      br2a_beta[2].dim(0).set_bounds_estimate(0, 64);
      br2a_beta[3].dim(0).set_bounds_estimate(0, 128);
      br2a_beta[4].dim(0).set_bounds_estimate(0, 128);
      br2a_beta[5].dim(0).set_bounds_estimate(0, 128);
      br2a_beta[6].dim(0).set_bounds_estimate(0, 128);
      br2a_beta[7].dim(0).set_bounds_estimate(0, 256);
      br2a_beta[8].dim(0).set_bounds_estimate(0, 256);
      br2a_beta[9].dim(0).set_bounds_estimate(0, 256);
      br2a_beta[10].dim(0).set_bounds_estimate(0, 256);
      br2a_beta[11].dim(0).set_bounds_estimate(0, 256);
      br2a_beta[12].dim(0).set_bounds_estimate(0, 256);
      br2a_beta[13].dim(0).set_bounds_estimate(0, 512);
      br2a_beta[14].dim(0).set_bounds_estimate(0, 512);
      br2a_beta[15].dim(0).set_bounds_estimate(0, 512);
      br2b_beta[0].dim(0).set_bounds_estimate(0, 64);
      br2b_beta[1].dim(0).set_bounds_estimate(0, 64);
      br2b_beta[2].dim(0).set_bounds_estimate(0, 64);
      br2b_beta[3].dim(0).set_bounds_estimate(0, 128);
      br2b_beta[4].dim(0).set_bounds_estimate(0, 128);
      br2b_beta[5].dim(0).set_bounds_estimate(0, 128);
      br2b_beta[6].dim(0).set_bounds_estimate(0, 128);
      br2b_beta[7].dim(0).set_bounds_estimate(0, 256);
      br2b_beta[8].dim(0).set_bounds_estimate(0, 256);
      br2b_beta[9].dim(0).set_bounds_estimate(0, 256);
      br2b_beta[10].dim(0).set_bounds_estimate(0, 256);
      br2b_beta[11].dim(0).set_bounds_estimate(0, 256);
      br2b_beta[12].dim(0).set_bounds_estimate(0, 256);
      br2b_beta[13].dim(0).set_bounds_estimate(0, 512);
      br2b_beta[14].dim(0).set_bounds_estimate(0, 512);
      br2b_beta[15].dim(0).set_bounds_estimate(0, 512);
      br2c_beta[0].dim(0).set_bounds_estimate(0, 256);
      br2c_beta[1].dim(0).set_bounds_estimate(0, 256);
      br2c_beta[2].dim(0).set_bounds_estimate(0, 256);
      br2c_beta[3].dim(0).set_bounds_estimate(0, 512);
      br2c_beta[4].dim(0).set_bounds_estimate(0, 512);
      br2c_beta[5].dim(0).set_bounds_estimate(0, 512);
      br2c_beta[6].dim(0).set_bounds_estimate(0, 512);
      br2c_beta[7].dim(0).set_bounds_estimate(0, 1024);
      br2c_beta[8].dim(0).set_bounds_estimate(0, 1024);
      br2c_beta[9].dim(0).set_bounds_estimate(0, 1024);
      br2c_beta[10].dim(0).set_bounds_estimate(0, 1024);
      br2c_beta[11].dim(0).set_bounds_estimate(0, 1024);
      br2c_beta[12].dim(0).set_bounds_estimate(0, 1024);
      br2c_beta[13].dim(0).set_bounds_estimate(0, 2048);
      br2c_beta[14].dim(0).set_bounds_estimate(0, 2048);
      br2c_beta[15].dim(0).set_bounds_estimate(0, 2048);
      conv1_mu.dim(0).set_bounds_estimate(0, 64);
      br1_mu[0].dim(0).set_bounds_estimate(0, 256);
      br1_mu[1].dim(0).set_bounds_estimate(0, 512);
      br1_mu[2].dim(0).set_bounds_estimate(0, 1024);
      br1_mu[3].dim(0).set_bounds_estimate(0, 2048);
      br2a_mu[0].dim(0).set_bounds_estimate(0, 64);
      br2a_mu[1].dim(0).set_bounds_estimate(0, 64);
      br2a_mu[2].dim(0).set_bounds_estimate(0, 64);
      br2a_mu[3].dim(0).set_bounds_estimate(0, 128);
      br2a_mu[4].dim(0).set_bounds_estimate(0, 128);
      br2a_mu[5].dim(0).set_bounds_estimate(0, 128);
      br2a_mu[6].dim(0).set_bounds_estimate(0, 128);
      br2a_mu[7].dim(0).set_bounds_estimate(0, 256);
      br2a_mu[8].dim(0).set_bounds_estimate(0, 256);
      br2a_mu[9].dim(0).set_bounds_estimate(0, 256);
      br2a_mu[10].dim(0).set_bounds_estimate(0, 256);
      br2a_mu[11].dim(0).set_bounds_estimate(0, 256);
      br2a_mu[12].dim(0).set_bounds_estimate(0, 256);
      br2a_mu[13].dim(0).set_bounds_estimate(0, 512);
      br2a_mu[14].dim(0).set_bounds_estimate(0, 512);
      br2a_mu[15].dim(0).set_bounds_estimate(0, 512);
      br2b_mu[0].dim(0).set_bounds_estimate(0, 64);
      br2b_mu[1].dim(0).set_bounds_estimate(0, 64);
      br2b_mu[2].dim(0).set_bounds_estimate(0, 64);
      br2b_mu[3].dim(0).set_bounds_estimate(0, 128);
      br2b_mu[4].dim(0).set_bounds_estimate(0, 128);
      br2b_mu[5].dim(0).set_bounds_estimate(0, 128);
      br2b_mu[6].dim(0).set_bounds_estimate(0, 128);
      br2b_mu[7].dim(0).set_bounds_estimate(0, 256);
      br2b_mu[8].dim(0).set_bounds_estimate(0, 256);
      br2b_mu[9].dim(0).set_bounds_estimate(0, 256);
      br2b_mu[10].dim(0).set_bounds_estimate(0, 256);
      br2b_mu[11].dim(0).set_bounds_estimate(0, 256);
      br2b_mu[12].dim(0).set_bounds_estimate(0, 256);
      br2b_mu[13].dim(0).set_bounds_estimate(0, 512);
      br2b_mu[14].dim(0).set_bounds_estimate(0, 512);
      br2b_mu[15].dim(0).set_bounds_estimate(0, 512);
      br2c_mu[0].dim(0).set_bounds_estimate(0, 256);
      br2c_mu[1].dim(0).set_bounds_estimate(0, 256);
      br2c_mu[2].dim(0).set_bounds_estimate(0, 256);
      br2c_mu[3].dim(0).set_bounds_estimate(0, 512);
      br2c_mu[4].dim(0).set_bounds_estimate(0, 512);
      br2c_mu[5].dim(0).set_bounds_estimate(0, 512);
      br2c_mu[6].dim(0).set_bounds_estimate(0, 512);
      br2c_mu[7].dim(0).set_bounds_estimate(0, 1024);
      br2c_mu[8].dim(0).set_bounds_estimate(0, 1024);
      br2c_mu[9].dim(0).set_bounds_estimate(0, 1024);
      br2c_mu[10].dim(0).set_bounds_estimate(0, 1024);
      br2c_mu[11].dim(0).set_bounds_estimate(0, 1024);
      br2c_mu[12].dim(0).set_bounds_estimate(0, 1024);
      br2c_mu[13].dim(0).set_bounds_estimate(0, 2048);
      br2c_mu[14].dim(0).set_bounds_estimate(0, 2048);
      br2c_mu[15].dim(0).set_bounds_estimate(0, 2048);
      conv1_sig.dim(0).set_bounds_estimate(0, 64);
      br1_sig[0].dim(0).set_bounds_estimate(0, 256);
      br1_sig[1].dim(0).set_bounds_estimate(0, 512);
      br1_sig[2].dim(0).set_bounds_estimate(0, 1024);
      br1_sig[3].dim(0).set_bounds_estimate(0, 2048);
      br2a_sig[0].dim(0).set_bounds_estimate(0, 64);
      br2a_sig[1].dim(0).set_bounds_estimate(0, 64);
      br2a_sig[2].dim(0).set_bounds_estimate(0, 64);
      br2a_sig[3].dim(0).set_bounds_estimate(0, 128);
      br2a_sig[4].dim(0).set_bounds_estimate(0, 128);
      br2a_sig[5].dim(0).set_bounds_estimate(0, 128);
      br2a_sig[6].dim(0).set_bounds_estimate(0, 128);
      br2a_sig[7].dim(0).set_bounds_estimate(0, 256);
      br2a_sig[8].dim(0).set_bounds_estimate(0, 256);
      br2a_sig[9].dim(0).set_bounds_estimate(0, 256);
      br2a_sig[10].dim(0).set_bounds_estimate(0, 256);
      br2a_sig[11].dim(0).set_bounds_estimate(0, 256);
      br2a_sig[12].dim(0).set_bounds_estimate(0, 256);
      br2a_sig[13].dim(0).set_bounds_estimate(0, 512);
      br2a_sig[14].dim(0).set_bounds_estimate(0, 512);
      br2a_sig[15].dim(0).set_bounds_estimate(0, 512);
      br2b_sig[0].dim(0).set_bounds_estimate(0, 64);
      br2b_sig[1].dim(0).set_bounds_estimate(0, 64);
      br2b_sig[2].dim(0).set_bounds_estimate(0, 64);
      br2b_sig[3].dim(0).set_bounds_estimate(0, 128);
      br2b_sig[4].dim(0).set_bounds_estimate(0, 128);
      br2b_sig[5].dim(0).set_bounds_estimate(0, 128);
      br2b_sig[6].dim(0).set_bounds_estimate(0, 128);
      br2b_sig[7].dim(0).set_bounds_estimate(0, 256);
      br2b_sig[8].dim(0).set_bounds_estimate(0, 256);
      br2b_sig[9].dim(0).set_bounds_estimate(0, 256);
      br2b_sig[10].dim(0).set_bounds_estimate(0, 256);
      br2b_sig[11].dim(0).set_bounds_estimate(0, 256);
      br2b_sig[12].dim(0).set_bounds_estimate(0, 256);
      br2b_sig[13].dim(0).set_bounds_estimate(0, 512);
      br2b_sig[14].dim(0).set_bounds_estimate(0, 512);
      br2b_sig[15].dim(0).set_bounds_estimate(0, 512);
      br2c_sig[0].dim(0).set_bounds_estimate(0, 256);
      br2c_sig[1].dim(0).set_bounds_estimate(0, 256);
      br2c_sig[2].dim(0).set_bounds_estimate(0, 256);
      br2c_sig[3].dim(0).set_bounds_estimate(0, 512);
      br2c_sig[4].dim(0).set_bounds_estimate(0, 512);
      br2c_sig[5].dim(0).set_bounds_estimate(0, 512);
      br2c_sig[6].dim(0).set_bounds_estimate(0, 512);
      br2c_sig[7].dim(0).set_bounds_estimate(0, 1024);
      br2c_sig[8].dim(0).set_bounds_estimate(0, 1024);
      br2c_sig[9].dim(0).set_bounds_estimate(0, 1024);
      br2c_sig[10].dim(0).set_bounds_estimate(0, 1024);
      br2c_sig[11].dim(0).set_bounds_estimate(0, 1024);
      br2c_sig[12].dim(0).set_bounds_estimate(0, 1024);
      br2c_sig[13].dim(0).set_bounds_estimate(0, 2048);
      br2c_sig[14].dim(0).set_bounds_estimate(0, 2048);
      br2c_sig[15].dim(0).set_bounds_estimate(0, 2048);
      conv1_weights.dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 7)
        .dim(2).set_bounds_estimate(0, 7)
        .dim(3).set_bounds_estimate(0, 3);
      br1_conv_weights[0].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 64);
      br1_conv_weights[1].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br1_conv_weights[2].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br1_conv_weights[3].dim(0).set_bounds_estimate(0, 2048)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[0].dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 64);
      br2a_conv_weights[1].dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2a_conv_weights[2].dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2a_conv_weights[3].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2a_conv_weights[4].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br2a_conv_weights[5].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br2a_conv_weights[6].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br2a_conv_weights[7].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br2a_conv_weights[8].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[9].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[10].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[11].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[12].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[13].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 1024);
      br2a_conv_weights[14].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 2048);
      br2a_conv_weights[15].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 2048);
      br2b_conv_weights[0].dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 64);
      br2b_conv_weights[1].dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 64);
      br2b_conv_weights[2].dim(0).set_bounds_estimate(0, 64)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 64);
      br2b_conv_weights[3].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 128);
      br2b_conv_weights[4].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 128);
      br2b_conv_weights[5].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 128);
      br2b_conv_weights[6].dim(0).set_bounds_estimate(0, 128)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 128);
      br2b_conv_weights[7].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 256);
      br2b_conv_weights[8].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 256);
      br2b_conv_weights[9].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 256);
      br2b_conv_weights[10].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 256);
      br2b_conv_weights[11].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 256);
      br2b_conv_weights[12].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 256);
      br2b_conv_weights[13].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 512);
      br2b_conv_weights[14].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 512);
      br2b_conv_weights[15].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 3)
        .dim(2).set_bounds_estimate(0, 3)
        .dim(3).set_bounds_estimate(0, 512);
      br2c_conv_weights[0].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 64);
      br2c_conv_weights[1].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 64);
      br2c_conv_weights[2].dim(0).set_bounds_estimate(0, 256)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 64);
      br2c_conv_weights[3].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 128);
      br2c_conv_weights[4].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 128);
      br2c_conv_weights[5].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 128);
      br2c_conv_weights[6].dim(0).set_bounds_estimate(0, 512)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 128);
      br2c_conv_weights[7].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2c_conv_weights[8].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2c_conv_weights[9].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2c_conv_weights[10].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2c_conv_weights[11].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2c_conv_weights[12].dim(0).set_bounds_estimate(0, 1024)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 256);
      br2c_conv_weights[13].dim(0).set_bounds_estimate(0, 2048)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br2c_conv_weights[14].dim(0).set_bounds_estimate(0, 2048)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      br2c_conv_weights[15].dim(0).set_bounds_estimate(0, 2048)
        .dim(1).set_bounds_estimate(0, 1)
        .dim(2).set_bounds_estimate(0, 1)
        .dim(3).set_bounds_estimate(0, 512);
      fc1000_weights.dim(0).set_bounds_estimate(0, 1000)
        .dim(1).set_bounds_estimate(0, 2048);
      fc1000_bias.dim(0).set_bounds_estimate(0, 1000);
  }

  Func pad(Func f, Expr width, Expr height) {
      std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
      bounds[1].first = 0;
      bounds[1].second = width;
      bounds[2].first = 0;
      bounds[2].second = height;
      return Halide::BoundaryConditions::constant_exterior(f, 0.0f, bounds);
  }

  std::vector<int> compute_shape(const Tensor& in, const WeightShape& params) {
      int w = (1.0/params.stride) * (params.pad * 2 + in.shape[1] - params.w + 1 + params.stride - 1);
      int h = (1.0/params.stride) * (params.pad * 2 + in.shape[2] - params.h + 1 + params.stride - 1);
      int c = params.c;

      return {c, w, h};
  }

  Tensor conv2D(const Tensor& input, const WeightShape& weight_shape, const Func& weights, const std::string& name) {
      int p = weight_shape.pad;
      Func padded;
      // pad input
      if (p) {
          padded = pad(input.f, input.shape[1], input.shape[2]);
      } else {
          padded = input.f;
      }
      Func w;
      Var ci, co;
      w(co, i, j, ci) = weights(co, i, j, ci);

      Func in;
      in(c, i, j, n) = padded(c, i, j, n);

      RDom r(0, input.shape[0], 0, weight_shape.w, 0, weight_shape.h);
      Func conv;
      conv(c, i, j, n) += w(c, r.y, r.z, r.x) * in(r.x, weight_shape.stride * i + r.y - p, weight_shape.stride * j + r.z - p, n);

      Tensor output;
      output.f = conv;
      output.name = name;
      output.shape = compute_shape(input, weight_shape);
      return output;
  }

  // assumes input is 3D (c, w, h) where w and h = 1
  Tensor fc_layer(const Tensor& input, const WeightShape& weight_shape, const Func& weights, const Func& bias, const std::string& name) {
      RDom r(0, input.shape[0]);
      Func fc;
      fc(c, n) = bias(c);
      fc(c, n) += weights(c, r.x) * input.f(r.x, 0, 0, n);

      Tensor output;
      output.f = fc;
      output.name = name;
      output.shape = compute_shape(input, weight_shape);

      return output;
  }

  Tensor relu_layer(const Tensor& input, const std::string& name) {
      Func relu;
      relu(c, i, j, n) = max(0.0f, input.f(c, i, j, n));
      Tensor output;
      output.f = relu;
      output.shape = input.shape;
      output.name = name;
      return output;
  }

  Tensor max_pool_layer(const Tensor& input, const WeightShape& weight_shape, const std::string& name) {
      int p = weight_shape.pad;
      Func padded;
      if (p) {
          padded = pad(input.f, input.shape[1], input.shape[2]);
      } else {
          padded = input.f;
      }
      RDom r(0, weight_shape.w, 0, weight_shape.h);
      Func pool;
      pool(c, i, j, n) = maximum(padded(c, weight_shape.stride * i + r.x - p, weight_shape.stride * j + r.y - p, n));
      Tensor output;
      output.f = pool;
      output.name = name;
      output.shape = compute_shape(input, weight_shape);

      return output;
  }

  Tensor avg_pool_layer(const Tensor& input, const WeightShape& weight_shape, const std::string& name) {
      int p = weight_shape.pad;
      Func padded;
      if (p) {
          padded = pad(input.f, input.shape[1], input.shape[2]);
      } else {
          padded = input.f;
      }
      RDom r(0, weight_shape.w, 0, weight_shape.h);
      float scale = weight_shape.w * weight_shape.h;
      Func pool;
      float norm = 1.0f/scale;
      pool(c, i, j, n) += norm * padded(c, weight_shape.stride * i + r.x - p, weight_shape.stride * j + r.y - p, n);

      Tensor output;
      output.f = pool;
      output.name = name;
      output.shape = compute_shape(input, weight_shape);

      return output;
  }

  Tensor norm_layer(const Tensor& input, const Func& mu, const Func& sigma, const std::string& name) {
      Func normed;
      Expr e = input.f(c, i, j, n);
      normed(c, i, j, n) = (input.f(c, i, j, n) - mu(c)) / (sqrt(sigma(c) + 1e-5f));
      Tensor output;
      output.f = normed;
      output.shape = input.shape;
      output.name = name;
      return output;
  }

  Tensor scale_layer(const Tensor& input, const Func& gamma, const Func& beta, const std::string& name) {
      Func scaled;
      scaled(c, i, j, n) = input.f(c, i, j, n) * gamma(c) + beta(c);
      Tensor output;
      output.f = scaled;
      output.shape = input.shape;
      output.name = name;
      return output;
  }

  Tensor sum_layer(const Tensor& t1, const Tensor& t2, const std::string& name) {
      assert(t1.shape == t2.shape);
      Func summed;
      summed(c, i, j, n) = t1.f(c, i, j, n) + t2.f(c, i, j, n);
      Tensor output;
      output.f = summed;
      output.shape = t1.shape;
      output.name = name;
      return output;
  }

  Func softmax_layer(const Tensor& input, const int classes, const std::string& name) {
      assert(input.shape[0] == classes);
      RDom r(0, classes);
      Func exp_vals;
      exp_vals(c, n) = exp(input.f(c, n));
      Func output("output");
      output(c, n) = exp_vals(c, n) / sum(exp_vals(r.x, n));
      return output;
  }


}; // end of class definition
} //namespace

HALIDE_REGISTER_GENERATOR(Resnet50Block, resnet50block)
