// We directly include the headers from the Halide source tree to
// avoid a build dependency on Halide.h
#include <unordered_map>
#include <tuple>
#include "Halide.h"

using namespace Halide;

namespace {

struct Tensor {
    Func f;
    std::vector<int> shape;
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
  GeneratorParam<int> macro_block_id{"macro_block_id", 0}; // 0 through 3 (2 - 5)
  GeneratorParam<int> block_id{"block_id", 0}; // 0 through 15 (1 - 16)

  Input<Buffer<float>> input{"input", 3};
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
  Input<Buffer<float>> fc1000_bias{"fc1_bias", 1};

  Output<Buffer<float>> block_output{"block_output", 3};
  Output<Buffer<float>> final_output{"final_output", 1};

  std::vector<std::vector<int>> block_dims{
    {256, 56, 56},
    {512, 28, 28},
    {1024, 14, 14},
    {2048, 7, 7}
  };

  /** list out shapes of each layers weights **/
  // weight shapes: out channels, kernel_w, kernel_h, pad, stride. In channels infered by input tensor shape
  WeightShape conv1_ws = {64, 7, 7, 3, 2};
  WeightShape pool1_ws = {64, 3, 3, 1, 2};
  WeightShape pool5_ws = {2048, 7, 7, 0, 1};
  WeightShape fc1000_ws = {1000, 1, 1, 0, 1}; // 1x1 conv with 2048 input channels and 1000 output channels

  // res2a, res2b, res2c all have shame shapes
  WeightShape res2a_br2a_ws = {64, 1, 1, 0, 1};
  WeightShape res2x_br2a_ws = {64, 1, 1, 0, 1};
  WeightShape res2x_br2b_ws = {64, 3, 3, 1, 1};
  WeightShape res2x_br2c_ws = {256, 1, 1, 0, 1};
  WeightShape res2a_br1_ws = {256, 1, 1, 0, 1};

  // res3x is same for most layers
  WeightShape res3a_br2a_ws = {128, 1, 1, 0, 2};
  WeightShape res3x_br2a_ws = {128, 1, 1, 0, 1};
  WeightShape res3x_br2b_ws = {128, 3, 3, 1, 1};
  WeightShape res3x_br2c_ws = {512, 1, 1, 0, 1};
  WeightShape res3a_br1_ws = {512, 1, 1, 0, 2};

  WeightShape res4a_br2a_ws = {256, 1, 1, 0, 2};
  WeightShape res4x_br2a_ws = {256, 1, 1, 0, 1};
  WeightShape res4x_br2b_ws = {256, 3, 3, 1, 1};
  WeightShape res4x_br2c_ws = {1024, 1, 1, 0, 1};
  WeightShape res4a_br1_ws = {1024, 1, 1, 0, 2};

  WeightShape res5a_br2a_ws = {512, 1, 1, 0, 2};
  WeightShape res5x_br2a_ws = {512, 1, 1, 0, 1};
  WeightShape res5x_br2b_ws = {512, 3, 3, 1, 1};
  WeightShape res5x_br2c_ws = {2048, 1, 1, 0, 1};
  WeightShape res5a_br1_ws = {2048, 1, 1, 0, 2};

  WeightShape br1_ws[4] = {res2a_br1_ws, res3a_br1_ws, res4a_br1_ws, res5a_br1_ws};
  WeightShape br2a_ws[16] = {res2a_br2a_ws, res2x_br2a_ws, res2x_br2a_ws,
                             res3a_br2a_ws, res3x_br2a_ws, res3x_br2a_ws, res3x_br2a_ws,
                             res4a_br2a_ws, res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws, res4x_br2a_ws,
                             res5a_br2a_ws, res5x_br2a_ws, res5x_br2a_ws};
  WeightShape br2b_ws[16] = {res2x_br2b_ws, res2x_br2b_ws, res2x_br2b_ws,
                             res3x_br2b_ws, res3x_br2b_ws, res3x_br2b_ws, res3x_br2b_ws,
                             res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws, res4x_br2b_ws,
                             res5x_br2b_ws, res5x_br2b_ws, res5x_br2b_ws};
  WeightShape br2c_ws[16] = {res2x_br2c_ws, res2x_br2c_ws, res2x_br2c_ws,
                             res3x_br2c_ws, res3x_br2c_ws, res3x_br2c_ws, res3x_br2c_ws,
                             res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws, res4x_br2c_ws,
                             res5x_br2c_ws, res5x_br2c_ws, res5x_br2c_ws};

  Var c, i, j;

  void generate() {

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

    std::vector<int> branch1_indices{0,3,7,13};

    // these tensors are different depending on the block and must be conditionally assigned.
    Tensor input_t;
    std::vector<int> input_shape;
    Tensor br2a_input;
    Tensor resunit_sum_input;

    /** if block_id is 0 build the (stem) conv1 section **/
    if (block_id == 0) {
      input_shape = {3, 224, 224};
      input_t.f = input;
      input_t.shape = input_shape;

      Tensor conv1 = conv2D(input_t, conv1_ws, conv1_weights);
      Tensor scaled1 = scale_layer(conv1, conv1_gamma, conv1_beta);
      Tensor relu1 = relu_layer(scaled1);
      Tensor pool1 = max_pool_layer(relu1, pool1_ws);

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
      br1_conv[br1_i] = conv2D(br2a_input, br1_ws[br1_i], br1_conv_weights[br1_i]);
      br1_norm[br1_i] = norm_layer(br1_conv[br1_i], br1_mu[br1_i], br1_sig[br1_i]);
      br1_scale[br1_i] = scale_layer(br1_norm[br1_i], br1_gamma[br1_i], br1_beta[br1_i]);
      resunit_sum_input = br1_scale[br1_i];
    } else {
      resunit_sum_input = input_t;
    }

    // branch2a
    auto weights = br2a_conv_weights[block_id];
    br2a_conv[block_id] = conv2D(br2a_input, br2a_ws[block_id], weights);
    br2a_norm[block_id] = norm_layer(br2a_conv[block_id], br2a_mu[block_id], br2a_sig[block_id]);
    br2a_scaled[block_id] = scale_layer(br2a_norm[block_id], br2a_gamma[block_id], br2a_beta[block_id]);
    br2a_relu[block_id] = relu_layer(br2a_scaled[block_id]);

    // branch 2b
    weights = br2b_conv_weights[block_id];
    br2b_conv[block_id] = conv2D(br2a_relu[block_id], br2b_ws[block_id], weights);
    br2b_norm[block_id] = norm_layer(br2b_conv[block_id], br2b_mu[block_id], br2b_sig[block_id]);
    br2b_scaled[block_id] = scale_layer(br2b_norm[block_id], br2b_gamma[block_id], br2b_beta[block_id]);
    br2b_relu[block_id] = relu_layer(br2b_scaled[block_id]);

    // branch 2c
    weights = br2c_conv_weights[block_id];
    br2c_conv[block_id] = conv2D(br2b_relu[block_id], br2c_ws[block_id], weights);
    br2c_norm[block_id] = norm_layer(br2c_conv[block_id], br2c_mu[block_id], br2c_sig[block_id]);
    br2c_scaled[block_id] = scale_layer(br2c_norm[block_id], br2c_gamma[block_id], br2c_beta[block_id]);

    // create residual unit
    resunit_sum[block_id] = sum_layer(resunit_sum_input, br2c_scaled[block_id]);
    resunit_relu[block_id] = relu_layer(resunit_sum[block_id]);
    // output of each block is the residual unit
    block_output(c, i, j) = resunit_relu[block_id].f(c, i, j);

    // create final 3 layers
    if (block_id == 15) {
        pool5 = avg_pool_layer(resunit_relu[block_id], pool5_ws);
        fc1000 = fc_layer(pool5, fc1000_ws, fc1000_weights, fc1000_bias);
        softmax_layer(fc1000, final_output, 1000);
    }

    // provide bounds estimates on outputs
    final_output(c) = undef<float>();
    std::vector<int> output_dim = block_dims[macro_block_id];
    final_output.estimate(final_output.args()[0], 0, 1000);
    block_output.estimate(block_output.args()[0], 0, output_dim[0]);
    block_output.estimate(block_output.args()[1], 0, output_dim[1]);
    block_output.estimate(block_output.args()[2], 0, output_dim[2]);
  }

private:
  Func pad(Func f, Expr width, Expr height) {
      std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
      bounds[1].first = 0;
      bounds[1].second = width;
      bounds[2].first = 0;
      bounds[2].second = height;
      return BoundaryConditions::constant_exterior(f, 0.0f, bounds);
  }

  void compute_shape(Tensor& in, Tensor& out, WeightShape& params) {
      assert(out.shape.empty());
      int w = (1.0/params.stride) * (params.pad * 2 + in.shape[1] - params.w + 1 + params.stride - 1);
      int h = (1.0/params.stride) * (params.pad * 2 + in.shape[2] - params.h + 1 + params.stride - 1);
      int c = params.c;

      int new_shape[3] = {c, w, h};
      out.shape.insert(out.shape.end(), new_shape, new_shape+3);
  }

  Tensor conv2D(Tensor& input, WeightShape& weight_shape, Func weights) {
      int p = weight_shape.pad;
      Func padded;
      // pad input
      if (p) {
          padded = pad(input.f, input.shape[1], input.shape[2]);
      } else {
          padded = input.f;
      }
      RDom r(0, input.shape[0], -p, -p + weight_shape.w, -p, -p + weight_shape.h);
      Func conv;
      conv(c, i, j) += weights(r.x, r.y, r.z, c) * padded(r.x, weight_shape.stride * i + r.y, weight_shape.stride * j + r.z);

      Tensor output;
      output.f = conv;
      compute_shape(input, output, weight_shape);
      return output;
  }

  // assumes input is 3D (c, w, h) where w and h = 1
  Tensor fc_layer(Tensor& input, WeightShape& weight_shape, Func weights, Func bias) {
      RDom r(0, input.shape[0]);
      Func fc;
      fc(c) = bias(c);
      fc(c) += weights(r.x, c) * input.f(r.x, 0, 0);

      Tensor output;
      output.f = fc;
      compute_shape(input, output, weight_shape);
      return output;
  }


  Tensor relu_layer(Tensor& input) {
      Func relu;
      relu(c, i, j) = max(0, input.f(c, i, j));
      Tensor output;
      output.f = relu;
      output.shape = input.shape;
      return output;
  }

  Tensor max_pool_layer(Tensor& input, WeightShape& weight_shape) {
      int p = weight_shape.pad;
      Func padded;
      if (p) {
          padded = pad(input.f, input.shape[1], input.shape[2]);
      } else {
          padded = input.f;
      }
      RDom r(-p, -p + weight_shape.w, -p, -p + weight_shape.h);
      Func pool;
      pool(c, i, j) = maximum(padded(c, i + r.x, j + r.y));
      Tensor output;
      output.f = pool;
      compute_shape(input, output, weight_shape);
      return output;
  }

  Tensor avg_pool_layer(Tensor& input, WeightShape& weight_shape) {
      int p = weight_shape.pad;
      Func padded;
      if (p) {
          padded = pad(input.f, input.shape[1], input.shape[2]);
      } else {
          padded = input.f;
      }
      RDom r(-p, -p + weight_shape.w, -p, -p + weight_shape.h);
      float scale = weight_shape.w * weight_shape.h;
      Func pool;
      float n = 1.0f/scale;
      pool(c, i, j) += n * padded(c, weight_shape.stride * i + r.x, weight_shape.stride * j + r.y);
      Tensor output;
      output.f = pool;
      compute_shape(input, output, weight_shape);
      return output;
  }

  Tensor norm_layer(Tensor& input, Func mu, Func sigma) {
      Func normed;
      normed(c, i, j)  = (input.f(c, i, j) - mu(c)) / (1e-12f + sigma(c));
      Tensor output;
      output.f = normed;
      output.shape = input.shape;
      return output;
  }

  Tensor scale_layer(Tensor& input, Func gamma, Func beta) {
      Func scaled;
      scaled(c, i, j) = input.f(c, i, j) * gamma(c) + beta(c);
      Tensor output;
      output.f = scaled;
      output.shape = input.shape;
      return output;
  }

  Tensor sum_layer(Tensor& t1, Tensor& t2) {
      assert(t1.shape == t2.shape);
      Func summed;
      summed(c, i, j) = t1.f(c, i, j) + t2.f(c, i, j);
      Tensor output;
      output.f = summed;
      output.shape = t1.shape;
      return output;
  }

  void softmax_layer(Tensor& input, Func output, int classes) {
      assert(input.shape[0] == classes);
      RDom r(0, classes);
      output(c) = exp(input.f(c)) / sum(exp(input.f(r.x)));
  }


}; // end of class definition
} //namespace

HALIDE_REGISTER_GENERATOR(Resnet50Block, resnet50block)
