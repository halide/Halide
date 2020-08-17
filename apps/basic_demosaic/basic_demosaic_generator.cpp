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



class BasicDemosaic: public Halide::Generator<BasicDemosaic> {
public:
  Input<Buffer<float>> input{"input", 4};
  /** parameter values for scaling layers **/
  Input<Buffer<float>> g_conv2d_weights{"g_conv2d_weights", 4};
  Input<Buffer<float>> g_1x1_1_weights{"g_1x1_1_weights", 4};
  Input<Buffer<float>> g_1x1_2_weights{"g_1x1_2_weights", 4};
  Input<Buffer<float>> g_filter_weights{"g_filter_weights", 4};
  
  Input<Buffer<float>> chroma_v_weights{"chroma_v_weights", 4};
  Input<Buffer<float>> chroma_h_weights{"chroma_h_weights", 4};
  Input<Buffer<float>> chroma_q_weights{"chroma_q_weights", 4};
  Output<Buffer<float>> output{"output", 4};

  const WeightShape g_conv2d_ws = {16, 5, 5, 2, 1};
  const WeightShape g_1x1_1_ws = {16, 1, 1, 0, 1};
  const WeightShape g_1x1_2_ws = {16, 1, 1, 0, 1};
  const WeightShape g_filter_ws = {16, 5, 5, 2, 1};

  const WeightShape chroma_v_ws = {2, 5, 5, 2, 1};
  const WeightShape chroma_h_ws = {2, 5, 5, 2, 1};
  const WeightShape chroma_q_ws = {2, 5, 5, 2, 1};

  Var c, x, y, n;
  void generate() {

    Tensor input_t;
    std::vector<int> input_shape = {1, 128, 128};
    input_t.f = input;
    input_t.shape = input_shape;

    // green model
    Tensor g_conv2d, g_conv1x1_1, g_conv1x1_2, g_final_weights, g_interpolations, prod, green_pred, green;

    g_conv2d = conv2D(input_t, g_conv2d_ws, g_conv2d_weights, "g_conv2d");
    g_conv1x1_1 = conv2D(g_conv2d, g_1x1_1_ws, g_1x1_1_weights, "g_1x1_1");
    g_conv1x1_2 = conv2D(g_conv1x1_1, g_1x1_2_ws, g_1x1_2_weights, "g_1x1_2");

    g_final_weights = softmax_layer(g_conv1x1_2, 16, "softmax");
    g_interpolations = conv2D(input_t, g_filter_ws, g_filter_weights, "g_filter");
    prod = prod_layer(g_final_weights, g_interpolations, "g_weighted_interpolations");
    green_pred = sumR_layer(prod, "sumR");

    // extract green at red and blue locations and use given green from bayer
    green.shape = input_t.shape;
    green.f(c, x, y, n) = select( (x % 2) == (y % 2), input_t.f(c, x, y, n), green_pred.f(c, x, y, n) );

    // chroma model
    Tensor chroma_minus_g, chroma_v_diff, chroma_h_diff, chroma_q_diff, chroma_v, chroma_h, chroma_q;
    chroma_minus_g.shape = input_t.shape; 
    chroma_minus_g.f(c, x, y, n) = input_t.f(c, x, y, n) - green.f(c, x, y, n);

    chroma_v_diff = conv2D(chroma_minus_g, chroma_v_ws, chroma_v_weights, "chroma_v_diff");
    chroma_h_diff = conv2D(chroma_minus_g, chroma_h_ws, chroma_h_weights, "chroma_h_diff");
    chroma_q_diff = conv2D(chroma_minus_g, chroma_q_ws, chroma_q_weights, "chroma_q_diff");

    std::vector<int> chroma_shape = {2, 128, 128};
    chroma_v.shape = input_t.shape;
    chroma_h.shape = input_t.shape;
    chroma_q.shape = input_t.shape;

    chroma_v.f(c, x, y, n) = chroma_v_diff.f(c, x, y, n) + green.f(c, x, y, n);
    chroma_h.f(c, x, y, n) = chroma_h_diff.f(c, x, y, n) + green.f(c, x, y, n);
    chroma_q.f(c, x, y, n) = chroma_q_diff.f(c, x, y, n) + green.f(c, x, y, n);


    output(c, x, y, n) = select(c == 0, (select( (x % 2) == 0 && (y % 2) == 0, chroma_h.f(0, x, y, n),
    														          select( (x % 2) == 0 && (y % 2) == 1, chroma_q.f(0, x, y, n), 
    															          select( (x % 2) == 1 && (y % 2) == 0, input_t.f(0, x, y, n), 
                                              chroma_v.f(0, x, y, n))))),
                          select(c == 1, green.f(0, x, y, n), 
                            (select( (x % 2) == 0 && (y % 2) == 0, chroma_v.f(0, x, y, n),
														  select( (x % 2) == 0 && (y % 2) == 1, input_t.f(0, x, y, n), 
															  select( (x % 2) == 1 && (y % 2) == 0, chroma_q.f(0, x, y, n), 
																  chroma_h.f(0, x, y, n)))))));

		// estimates
    input.dim(0).set_estimate(0, 1);
    input.dim(1).set_estimate(0, 128);
    input.dim(2).set_estimate(0, 128);
    input.dim(3).set_estimate(0, 32);

    chroma_h_weights.dim(0).set_estimate(0, 2)
      .dim(1).set_estimate(0,5)
      .dim(2).set_estimate(0,5)
      .dim(3).set_estimate(0,1);
    chroma_v_weights.dim(0).set_estimate(0, 2)
      .dim(1).set_estimate(0,5)
      .dim(2).set_estimate(0,5)
      .dim(3).set_estimate(0,1);
    chroma_q_weights.dim(0).set_estimate(0, 2)
      .dim(1).set_estimate(0,5)
      .dim(2).set_estimate(0,5)
      .dim(3).set_estimate(0,1);
    g_conv2d_weights.dim(0).set_estimate(0, 16)
      .dim(1).set_estimate(0,5)
      .dim(2).set_estimate(0,5)
      .dim(3).set_estimate(0,1);
    g_1x1_1_weights.dim(0).set_estimate(0, 16)
      .dim(1).set_estimate(0,1)
      .dim(2).set_estimate(0,1)
      .dim(3).set_estimate(0,16);
    g_1x1_2_weights.dim(0).set_estimate(0, 16)
      .dim(1).set_estimate(0,1)
      .dim(2).set_estimate(0,1)
      .dim(3).set_estimate(0,16);
    g_filter_weights.dim(0).set_estimate(0, 16)
      .dim(1).set_estimate(0,5)
      .dim(2).set_estimate(0,5)
      .dim(3).set_estimate(0,1);

    green.f.bound(green.f.args()[0], 0, 1);
    green.f.bound(green.f.args()[1], 0, 128);
    green.f.bound(green.f.args()[2], 0, 128);
    green.f.bound(green.f.args()[3], 0, 32);

    chroma_minus_g.f.bound(chroma_minus_g.f.args()[0], 0, 1);
    chroma_minus_g.f.bound(chroma_minus_g.f.args()[1], 0, 128);
    chroma_minus_g.f.bound(chroma_minus_g.f.args()[2], 0, 128);
    chroma_minus_g.f.bound(chroma_minus_g.f.args()[3], 0, 32);

    chroma_v_diff.f.bound(chroma_v_diff.f.args()[0], 0, 2);
    chroma_v_diff.f.bound(chroma_v_diff.f.args()[1], 0, 128);
    chroma_v_diff.f.bound(chroma_v_diff.f.args()[2], 0, 128);
    chroma_v_diff.f.bound(chroma_v_diff.f.args()[3], 0, 32);

    chroma_h_diff.f.bound(chroma_h_diff.f.args()[0], 0, 2);
    chroma_h_diff.f.bound(chroma_h_diff.f.args()[1], 0, 128);
    chroma_h_diff.f.bound(chroma_h_diff.f.args()[2], 0, 128);
    chroma_h_diff.f.bound(chroma_h_diff.f.args()[3], 0, 32);

    chroma_q_diff.f.bound(chroma_q_diff.f.args()[0], 0, 2);
    chroma_q_diff.f.bound(chroma_q_diff.f.args()[1], 0, 128);
    chroma_q_diff.f.bound(chroma_q_diff.f.args()[2], 0, 128);
    chroma_q_diff.f.bound(chroma_q_diff.f.args()[3], 0, 32);

    chroma_v.f.bound(chroma_v.f.args()[0], 0, 2);
    chroma_v.f.bound(chroma_v.f.args()[1], 0, 128);
    chroma_v.f.bound(chroma_v.f.args()[2], 0, 128);
    chroma_v.f.bound(chroma_v.f.args()[3], 0, 32);

    chroma_h.f.bound(chroma_h.f.args()[0], 0, 2);
    chroma_h.f.bound(chroma_h.f.args()[1], 0, 128);
    chroma_h.f.bound(chroma_h.f.args()[2], 0, 128);
    chroma_h.f.bound(chroma_h.f.args()[3], 0, 32);

    chroma_q.f.bound(chroma_q.f.args()[0], 0, 2);
    chroma_q.f.bound(chroma_q.f.args()[1], 0, 128);
    chroma_q.f.bound(chroma_q.f.args()[2], 0, 128);
    chroma_q.f.bound(chroma_q.f.args()[3], 0, 32);

    
    output.bound(output.args()[0], 0, 3);
    output.bound(output.args()[1], 0, 128);
    output.bound(output.args()[2], 0, 128);
    output.bound(output.args()[3], 0, 32); // batch size is 32

    if (!auto_schedule) {
      g_conv2d.f.compute_root();
      g_conv1x1_1.f.compute_root();
      g_conv1x1_2.f.compute_root();
    	g_final_weights.f.compute_root();
    	g_interpolations.f.compute_root();
    	prod.f.compute_root();
    	green_pred.f.compute_root();
    	green.f.compute_root();
    	chroma_minus_g.f.compute_root();
    	chroma_v_diff.f.compute_root();
    	chroma_h_diff.f.compute_root();
    	chroma_q_diff.f.compute_root();
    	chroma_v.f.compute_root();
    	chroma_h.f.compute_root();
    	chroma_q.f.compute_root();
    	output.compute_root();
    }
  }


  Func pad(Func f, Expr width, Expr height) {
    Halide::Region bounds(f.dimensions());
    bounds[1].min = 0;
    bounds[1].extent = width;
    bounds[2].min = 0;
    bounds[2].extent = height;
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

		Func w("w");
		Var ci, co;
		w(co, x, y, ci) = weights(co, x, y, ci);

		Func in("in");
		in(c, x, y, n) = padded(c, x, y, n);

		RDom r(0, input.shape[0], 0, weight_shape.w, 0, weight_shape.h);
		Func conv("conv2D");
		conv(c, x, y, n) += w(c, r.y, r.z, r.x) * in(r.x, weight_shape.stride * x + r.y - p, weight_shape.stride * y + r.z - p, n);

		Tensor output;
		output.f = conv;
		output.name = name;
		output.shape = compute_shape(input, weight_shape);
		return output;
	}

  
  Tensor softmax_layer(const Tensor& input, const int classes, const std::string& name) {
      assert(input.shape[0] == classes);
      RDom r(0, classes);
      Func exp_vals("exp_vals");
      exp_vals(c, x, y, n) = fast_exp(input.f(c, x, y, n));
      Func outvals("softmax_vals");
      outvals(c, x, y, n) = exp_vals(c, x, y, n) / sum(exp_vals(r.x, x, y, n));
      Tensor output;
      output.f = outvals;
      output.name = name;
      output.shape = input.shape;
      return output;
  }

  Tensor prod_layer(const Tensor& t1, const Tensor& t2, const std::string& name) {
    assert(t1.shape == t2.shape);
    Func product("product");
    product(c, x, y, n) = t1.f(c, x, y, n) * t2.f(c, x, y, n);
    Tensor output;
    output.f = product;
    output.shape = t1.shape;
    output.name = name;
    return output;
  }

  Tensor sumR_layer(const Tensor& t1, const std::string& name) {
    Func sum_reduction("product");

    RDom r(0, t1.shape[0]);
    sum_reduction(c, x, y, n) += t1.f(r, x, y, n);

    Tensor output;
    output.f = sum_reduction;
    output.shape = t1.shape;
    output.shape[0] = 1;
    output.name = name;
    return output;
  }
};
}

HALIDE_REGISTER_GENERATOR(BasicDemosaic, basic_demosaic)
