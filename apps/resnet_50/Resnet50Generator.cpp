// We directly include the headers from the Halide source tree to
// avoid a build dependency on Halide.h
#include <unordered_map>
#include "Halide.h"

using namespace Halide;

namespace {
struct TensorShape {
    int c;
    int w;
    int h;
};

struct WeightShape {
    int c; // output channels
    int w;
    int h;
    int pad;
    int stride;
};

Func pad(Func f, Expr width, Expr height) {
    std::vector<std::pair<Expr, Expr>> bounds(f.dimensions());
    bounds[1].first = 0;
    bounds[1].second = width;
    bounds[2].first = 0;
    bounds[2].second = height;
    return BoundaryConditions::constant_exterior(f, 0.0f, bounds);
}


// returns output tensor shape when a layer with the given weight 
// params is applied to the input tensor shape
TensorShape compute_shape(TensorShape in_shape, WeightShape params) {
    int w = (1.0/params.stride) * (params.pad * 2 + in_shape.w - params.w + 1 + params.stride - 1);
    int h = (1.0/params.stride) * (params.pad * 2 + in_shape.h - params.h + 1 + params.stride - 1);
    int c = params.c;

    TensorShape output_shape = {c, h, w};
    return output_shape;
}

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

  Input<Buffer<float>> conv1_mu{"conv1_mu", 4};
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

  TensorShape img_shape = {3, 224, 224};
  TensorShape resunit_relu_shapes[16];

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
    TensorShape conv1_shape = compute_shape(img_shape, conv1_ws);
    Func conv1 = conv2D(input, img_shape, conv1_ws, conv1_weights);

    TensorShape scaled1_shape = conv1_shape;
    Func scaled1 = scale_layer(conv1, conv1_gamma, conv1_beta);

    TensorShape relu1_shape = scaled1_shape;
    Func relu1 = relu_layer(scaled1);

    TensorShape pool1_shape = compute_shape(relu1_shape, pool1_ws);
    Func pool1 = max_pool_layer(relu1, relu1_shape, pool1_ws);

    /** Declare arrays of other functions **/
    Func br1_conv[4];
    Func br1_norm[4];
    Func br1_scale[4];

    Func br2a_conv[16];
    Func br2a_norm[16];
    Func br2a_scaled[16];
    Func br2a_relu[16];

    Func br2b_conv[16];
    Func br2b_norm[16];
    Func br2b_scaled[16];
    Func br2b_relu[16];

    Func br2c_conv[16];
    Func br2c_norm[16];
    Func br2c_scaled[16];

    Func resunit_sum[16];
    Func resunit_relu[16];

    Func pool5;
    Func fc1000;
    Func softmax;
    
    TensorShape resunit_relu_shapes[16];
    std::vector<int> branch1_indices{0,3,7,13};

    for (int i = 0; i < 16; i++) {
        // these values are different depending on the portion of the network
        // and must be conditionally assigned.
        Func br2a_input;
        Func resunit_sum_input;
        
        TensorShape br2a_input_shape;
        WeightShape br2a_conv_ws;
        WeightShape br2b_conv_ws;
        WeightShape br2c_conv_ws;

        if (i == 0) {
            br2a_input = pool1;
            br2a_input_shape = pool1_shape;
        } else {
            br2a_input = resunit_relu[i-1];
            br2a_input_shape = resunit_relu_shapes[i-1];
        }
        
        // build branch1 if this section has branch1
        int br1_i = find_index(i, branch1_indices);
        if (br1_i >= 0) {
          TensorShape br1_conv_shape = compute_shape(br2a_input_shape, br1_ws[br1_i]);
          br1_conv[br1_i] = conv2D(br2a_input, br2a_input_shape, br1_ws[br1_i], br1_conv_weights[br1_i]);

          TensorShape br1_norm_shape = br1_conv_shape;
          br1_norm[br1_i] = norm_layer(br1_conv[br1_i], br1_mu[br1_i], br1_sig[br1_i]);;

          TensorShape br1_scale_shape = br1_norm_shape;
          br1_scale[br1_i] = scale_layer(br1_norm[br1_i], br1_gamma[br1_i], br1_beta[br1_i]);

          resunit_sum_input = br1_scale[br1_i];
        } else {
          resunit_sum_input = resunit_relu[i-1];
        }

        br2a_conv_ws = br2a_ws[i];
        br2b_conv_ws = br2b_ws[i];
        br2c_conv_ws = br2c_ws[i];

        // branch2a
        auto weights = br2a_conv_weights[i];
        TensorShape br2a_conv_shape = compute_shape(br2a_input_shape, br2a_conv_ws);
        br2a_conv[i] = conv2D(br2a_input, br2a_input_shape, br2a_conv_ws, weights);

        TensorShape br2a_normed_shape = br2a_conv_shape;
        br2a_norm[i] = norm_layer(br2a_conv[i], br2a_mu[i], br2a_sig[i]);
        
        TensorShape br2a_scaled_shape = br2a_normed_shape;
        br2a_scaled[i] = scale_layer(br2a_norm[i], br2a_gamma[i], br2a_beta[i]);

        TensorShape br2a_relu_shape = br2a_scaled_shape;
        br2a_relu[i] = relu_layer(br2a_scaled[i]);

        // branch 2b
        weights = br2b_conv_weights[i];
        TensorShape br2b_conv_shape = compute_shape(br2a_relu_shape, br2b_conv_ws);
        br2b_conv[i] = conv2D(br2a_relu[i], br2a_relu_shape, br2b_conv_ws, weights);
        
        TensorShape br2b_normed_shape = br2b_conv_shape;
        br2b_norm[i] = norm_layer(br2b_conv[i], br2b_mu[i], br2b_sig[i]);
        
        TensorShape br2b_scaled_shape = br2b_normed_shape;
        br2b_scaled[i] = scale_layer(br2b_norm[i], br2b_gamma[i], br2b_beta[i]);
        
        TensorShape br2b_relu_shape = br2b_scaled_shape;
        br2b_relu[i] = relu_layer(br2b_scaled[i]);

        // branc 2c
        weights = br2c_conv_weights[i];
        TensorShape br2c_conv_shape = compute_shape(br2b_relu_shape, br2c_conv_ws);
        br2c_conv[i] = conv2D(br2b_relu[i], br2b_relu_shape, br2c_conv_ws, weights);

        TensorShape br2c_normed_shape = br2c_conv_shape;
        br2c_norm[i] = norm_layer(br2c_conv[i], br2c_mu[1], br2c_sig[1]);

        TensorShape br2c_scaled_shape = br2c_normed_shape;
        br2c_scaled[i] = scale_layer(br2c_norm[i], br2c_gamma[i], br2c_beta[i]);

        // create residual unit
        TensorShape resunit_sum_shape = br2c_scaled_shape;
        resunit_sum[i] = sum_layer(resunit_sum_input, br2c_scaled[i]);
        TensorShape resunit_relu_shape = resunit_sum_shape;
        resunit_relu[i] = relu_layer(resunit_sum[i]);
        resunit_relu_shapes[i] = resunit_relu_shape;

        // create final 3 layers
        if (i == 15) {
            TensorShape pool5_shape = compute_shape(resunit_relu_shape, pool5_ws);
            pool5 = avg_pool_layer(resunit_relu[i-1], resunit_relu_shape, pool5_ws);
            TensorShape fc1000_shape = compute_shape(pool5_shape, fc1000_ws);
            fc1000 = fc_layer(pool5, pool5_shape, fc1000_weights, fc1000_bias);
            softmax_layer(fc1000, output, 1000);
        }
      }

      conv1.compute_root();
      scaled1.compute_root();
      relu1.compute_root();
      pool1.compute_root();
      
      for (int i = 0; i < 4; i++) {
        br1_conv[i].compute_root();
        br1_norm[i].compute_root();
        br1_scale[i].compute_root();
      }

      for (int i = 0; i < 16; i++) {
        br2a_conv[i].compute_root();
        br2a_norm[i].compute_root();
        br2a_scaled[i].compute_root();
        br2a_relu[i].compute_root();

        br2b_conv[i].compute_root();
        br2b_norm[i].compute_root();
        br2b_scaled[i].compute_root();
        br2b_relu[i].compute_root();

        br2c_conv[i].compute_root();
        br2c_norm[i].compute_root();
        br2c_scaled[i].compute_root();

        resunit_sum[i].compute_root();
        resunit_relu[i].compute_root();
      }
      
      pool5.compute_root();
      fc1000.compute_root();
      softmax.compute_root();

    }

    void schedule() {
      
    }

private:
  Var c, i, j;
  Func conv2D(Func input, TensorShape input_shape, WeightShape weight_shape, 
              Func weights) {
      int p = weight_shape.pad;
      Func padded;
      // pad input
      if (p) {
          padded = pad(input, input_shape.w, input_shape.h);
      } else {
          padded = input;
      }
      RDom r(0, input_shape.c, -p, -p + weight_shape.w, -p, -p + weight_shape.h);
      Func conv;
      //Expr zero = cast<float>(0.0);
      //conv(c, i, j) = zero;
      conv(c, i, j) += weights(r.x, r.y, r.z, c) * padded(r.x, weight_shape.stride * i + r.y, weight_shape.stride * j + r.z);
      return conv;
  }

  // assumes input is 3D (c, w, h) where w and h = 1
  Func fc_layer(Func input, TensorShape input_shape, Func weights, Func bias) {
      RDom r(0, input_shape.c);
      Func fc;
      fc(c) = bias(c);
      fc(c) += weights(r.x, c) * input(r.x, 0, 0);
      return fc;
  }

  Func relu_layer(Func input) {
    Func relu;
    relu(c, i, j) = max(0, input(c, i, j));
    return relu;
  }

  Func max_pool_layer(Func input, TensorShape input_shape, WeightShape weight_shape) {
      int p = weight_shape.pad;
      Func padded;
      if (p) {
          padded = pad(input, input_shape.w, input_shape.h);
      } else {
          padded = input;
      }
      RDom r(-p, -p + weight_shape.w, -p, -p+ weight_shape.h);
      Func pool;
      pool(c, i, j) = maximum(padded(c, i + r.x, j + r.y));
      return pool;
  }

  Func avg_pool_layer(Func input, TensorShape input_shape, WeightShape weight_shape) {
      int p = weight_shape.pad;
      Func padded;
      if (p) {
          padded = pad(input, input_shape.w, input_shape.h);
      } else {
          padded = input;
      }
      RDom r(-p, -p + weight_shape.w, -p, -p + weight_shape.h);
      float scale = weight_shape.w * weight_shape.h;
      Func pool;
      //Expr zero = cast<float>(0.0);
      //pool(c, i, j) = zero;
      float n = 1.0f/scale;
      pool(c, i, j) += n * padded(c, weight_shape.stride * i + r.x, weight_shape.stride * j + r.y);
      return pool;
  }

  Func norm_layer(Func f, Func mu, Func sigma) {
    Func normed;
    normed(c, i, j)  = (f(c, i, j) - mu(c)) / (0.0000000000001f+ sigma(c));
    return normed;
  }

  Func scale_layer(Func f, Func gamma, Func beta) {
      Func scaled;
      scaled(c, i, j) = f(c, i, j) * gamma(c) + beta(c);
      return scaled;
  }

  Func sum_layer(Func f1, Func f2) {
      Func summed;
      summed(c, i, j) = f1(c, i, j) + f2(c, i, j);
      return summed;
  }

   void softmax_layer(Func input, Func output, int classes) {
      RDom r(0, classes);
      output(c) = exp(input(c)) / sum(exp(input(r.x)));
  }


}; // end of class definition
} //namespace

HALIDE_REGISTER_GENERATOR(Resnet50, resnet50)
