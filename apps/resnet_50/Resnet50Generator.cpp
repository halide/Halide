// We directly include the headers from the Halide source tree to
// avoid a build dependency on Halide.h
#include <unordered_map>
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


class Resnet50 : public Halide::Generator<Resnet50> {
public:
  Input<Buffer<float>> input{"input", 3};
  Output<Buffer<float>> output{"output", 1};

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

  std::vector<int> input_shape = {3, 224, 224};
  
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
  
  void generate() { 
    /** Create architecture DAG here **/
    // handle conv1 section:
    Tensor input_t;
    input_t.f = input;
    input_t.shape = input_shape; 
    
    Tensor conv1 = conv2D(input_t, conv1_ws, conv1_weights);
    Tensor scaled1 = scale_layer(conv1, conv1_gamma, conv1_beta); 
    Tensor relu1 = relu_layer(scaled1);
    Tensor pool1 = max_pool_layer(relu1, pool1_ws);

    /** Declare arrays of other functions **/
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

    for (int i = 0; i < 16; i++) {
        // these values are different depending on the portion of the network
        // and must be conditionally assigned.
        Tensor br2a_input;
        Tensor resunit_sum_input;
        
        if (i == 0) {
            br2a_input = pool1;
        } else {
            br2a_input = resunit_relu[i-1];
        }
        
        // build branch1 if this section has branch1
        int br1_i = find_index(i, branch1_indices);
        if (br1_i >= 0) {
          br1_conv[br1_i] = conv2D(br2a_input, br1_ws[br1_i], br1_conv_weights[br1_i]);
          br1_norm[br1_i] = norm_layer(br1_conv[br1_i], br1_mu[br1_i], br1_sig[br1_i]);
          br1_scale[br1_i] = scale_layer(br1_norm[br1_i], br1_gamma[br1_i], br1_beta[br1_i]);

          resunit_sum_input = br1_scale[br1_i];
        } else {
          resunit_sum_input = resunit_relu[i-1];
        }


        // branch2a
        auto weights = br2a_conv_weights[i];
        br2a_conv[i] = conv2D(br2a_input, br2a_ws[i], weights);
        br2a_norm[i] = norm_layer(br2a_conv[i], br2a_mu[i], br2a_sig[i]);
        br2a_scaled[i] = scale_layer(br2a_norm[i], br2a_gamma[i], br2a_beta[i]);
        br2a_relu[i] = relu_layer(br2a_scaled[i]);

        // branch 2b
        weights = br2b_conv_weights[i];
        br2b_conv[i] = conv2D(br2a_relu[i], br2b_ws[i], weights);
        br2b_norm[i] = norm_layer(br2b_conv[i], br2b_mu[i], br2b_sig[i]);
        br2b_scaled[i] = scale_layer(br2b_norm[i], br2b_gamma[i], br2b_beta[i]);
        br2b_relu[i] = relu_layer(br2b_scaled[i]);

        // branc 2c
        weights = br2c_conv_weights[i];
        br2c_conv[i] = conv2D(br2b_relu[i], br2c_ws[i], weights);
        br2c_norm[i] = norm_layer(br2c_conv[i], br2c_mu[i], br2c_sig[i]);
        br2c_scaled[i] = scale_layer(br2c_norm[i], br2c_gamma[i], br2c_beta[i]);

        // create residual unit
        resunit_sum[i] = sum_layer(resunit_sum_input, br2c_scaled[i]);
        resunit_relu[i] = relu_layer(resunit_sum[i]);

        // create final 3 layers
        if (i == 15) {
            pool5 = avg_pool_layer(resunit_relu[i-1], pool5_ws);
            fc1000 = fc_layer(pool5, fc1000_ws, fc1000_weights, fc1000_bias);
            softmax_layer(fc1000, output, 1000);
        }
      }

      conv1.f.compute_root();
      scaled1.f.compute_root();
      relu1.f.compute_root();
      pool1.f.compute_root();
      
      for (int i = 0; i < 4; i++) {
        br1_conv[i].f.compute_root();
        br1_norm[i].f.compute_root();
        br1_scale[i].f.compute_root();
      }

      for (int i = 0; i < 16; i++) {
        br2a_conv[i].f.compute_root();
        br2a_norm[i].f.compute_root();
        br2a_scaled[i].f.compute_root();
        br2a_relu[i].f.compute_root();

        br2b_conv[i].f.compute_root();
        br2b_norm[i].f.compute_root();
        br2b_scaled[i].f.compute_root();
        br2b_relu[i].f.compute_root();

        br2c_conv[i].f.compute_root();
        br2c_norm[i].f.compute_root();
        br2c_scaled[i].f.compute_root();

        resunit_sum[i].f.compute_root();
        resunit_relu[i].f.compute_root();
      }
      
      pool5.f.compute_root();
      fc1000.f.compute_root();
      softmax.f.compute_root();

    }

    void schedule() {
      
    }

private:
  Var c, i, j;
  
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

HALIDE_REGISTER_GENERATOR(Resnet50, resnet50)
