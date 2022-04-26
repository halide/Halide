#include "onnx_converter.h"
#include <cmath>
#include <random>

#define EXPECT_EQ(a, b) \
    if ((a) != (b)) {   \
        exit(-1);       \
    }
#define EXPECT_NEAR(a, b, c)         \
    if (std::abs((a) - (b)) > (c)) { \
        exit(-1);                    \
    }

static void test_abs() {
    onnx::NodeProto abs_node;
    abs_node.set_name("abs_node");
    abs_node.set_op_type("Abs");
    abs_node.add_input("x");
    abs_node.add_output("y");

    std::vector<Tensor> node_inputs;
    node_inputs.resize(1);
    node_inputs[0].shape = {200};
    Halide::Buffer<float, 1> input(200);
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::mt19937 rnd;
    input.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Var index;
    node_inputs[0].rep(index) = input(index);

    Node converted = convert_node(abs_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 1> output = converted.outputs[0].rep.realize({200});
    for (int i = 0; i < 200; ++i) {
        EXPECT_EQ(output(i), std::abs(input(i)));
    }
}

static void test_activation_function() {
    onnx::NodeProto relu_node;
    relu_node.set_name("relu_node");
    relu_node.set_op_type("Relu");
    relu_node.add_input("x");
    relu_node.add_output("y");

    std::vector<Tensor> node_inputs;
    node_inputs.resize(1);
    node_inputs[0].shape = {200};
    Halide::Buffer<float, 1> input(200);
    std::mt19937 rnd;
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    input.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Var index;
    node_inputs[0].rep(index) = input(index);

    Node converted = convert_node(relu_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 1> output = converted.outputs[0].rep.realize({200});
    for (int i = 0; i < 200; ++i) {
        EXPECT_EQ(output(i), std::max(input(i), 0.0f));
    }
}

static void test_cast() {
    onnx::NodeProto cast_node;
    cast_node.set_name("relu_node");
    cast_node.set_op_type("Cast");
    cast_node.add_input("x");
    cast_node.add_output("y");

    std::vector<Tensor> node_inputs;
    onnx::AttributeProto *attr = cast_node.add_attribute();
    attr->set_name("to");
    attr->set_i(onnx::TensorProto_DataType_FLOAT);
    node_inputs.resize(1);
    node_inputs[0].shape = {200};
    Halide::Buffer<int, 1> input(200);
    std::mt19937 rnd;
    std::uniform_int_distribution<int> dis(-100, 100);
    input.for_each_value([&](int &f) { f = dis(rnd); });
    Halide::Var index;
    node_inputs[0].rep(index) = input(index);

    Node converted = convert_node(cast_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 1> output = converted.outputs[0].rep.realize({200});
    for (int i = 0; i < 200; ++i) {
        EXPECT_EQ(output(i), static_cast<float>(input(i)));
    }
}

static void test_add() {
    onnx::NodeProto add_node;
    add_node.set_name("add_node");
    add_node.set_op_type("Add");
    add_node.add_input("x");
    add_node.add_input("y");
    add_node.add_output("z");

    std::vector<Tensor> node_inputs;
    node_inputs.resize(2);
    node_inputs[0].shape = {200};
    node_inputs[1].shape = node_inputs[0].shape;
    Halide::Buffer<float, 1> in1(200);
    std::mt19937 rnd;
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::uniform_real_distribution<float> dis10(-10.0, 10.0);
    in1.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Buffer<float, 1> in2(200);
    in2.for_each_value([&](float &f) { f = dis10(rnd); });
    Halide::Var index;
    node_inputs[0].rep(index) = in1(index);
    node_inputs[1].rep(index) = in2(index);

    Node converted = convert_node(add_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 1> output = converted.outputs[0].rep.realize({200});
    for (int i = 0; i < 200; ++i) {
        EXPECT_NEAR(output(i), in1(i) + in2(i), 1e-6);
    }
}

static void test_constant() {
    onnx::NodeProto add_node;
    add_node.set_name("constant_node");
    add_node.set_op_type("Constant");
    add_node.add_output("y");
    onnx::AttributeProto *attr = add_node.add_attribute();
    attr->set_name("value");

    onnx::TensorProto &value = *attr->mutable_t();
    value.set_data_type(onnx::TensorProto_DataType_FLOAT);
    value.add_dims(3);
    value.add_dims(7);
    std::mt19937 rnd;
    std::uniform_real_distribution<float> dis(-10.0, 10.0);
    for (int i = 0; i < 3 * 7; ++i) {
        value.add_float_data(dis(rnd));
    }

    Node converted = convert_node(add_node, {});

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 2> output = converted.outputs[0].rep.realize({3, 7});
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 7; ++j) {
            EXPECT_EQ(output(i, j), value.float_data(j + 7 * i));
        }
    }
}

static void test_gemm() {
    onnx::NodeProto add_node;
    add_node.set_name("gemm_node");
    add_node.set_op_type("Gemm");
    add_node.add_input("a");
    add_node.add_input("b");
    add_node.add_input("c");
    add_node.add_output("y");

    std::vector<Tensor> node_inputs;
    node_inputs.resize(3);
    node_inputs[0].shape = {32, 100};
    node_inputs[1].shape = {100, 64};
    node_inputs[2].shape = {32, 64};

    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::uniform_real_distribution<float> dis10(-10.0, 10.0);

    std::mt19937 rnd;
    Halide::Buffer<float, 2> in1(32, 100);
    in1.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Buffer<float, 2> in2(100, 64);
    in2.for_each_value([&](float &f) { f = dis10(rnd); });
    Halide::Buffer<float, 2> in3(32, 64);
    in3.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Var i1, j1;
    node_inputs[0].rep(i1, j1) = in1(i1, j1);
    Halide::Var i2, j2;
    node_inputs[1].rep(i2, j2) = in2(i2, j2);
    Halide::Var i3, j3;
    node_inputs[2].rep(i3, j3) = in3(i3, j3);
    Node converted = convert_node(add_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 2> output = converted.outputs[0].rep.realize({32, 64});

    for (int i = 0; i < 32; ++i) {
        for (int j = 0; j < 64; ++j) {
            float expected = in3(i, j);
            for (int k = 0; k < 100; ++k) {
                expected += in1(i, k) * in2(k, j);
            }
            EXPECT_NEAR(output(i, j), expected, 5e-5f);
        }
    }
}

static void test_conv() {
    onnx::NodeProto add_node;
    add_node.set_name("conv_node");
    add_node.set_op_type("Conv");
    add_node.add_input("x");
    add_node.add_input("w");
    add_node.add_output("y");

    std::vector<Tensor> node_inputs;
    node_inputs.resize(2);
    node_inputs[0].shape = {3, 5, 6, 6};
    node_inputs[1].shape = {7, 5, 3, 3};

    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::uniform_real_distribution<float> dis10(-10.0, 10.0);

    std::mt19937 rnd;
    Halide::Buffer<float, 4> weights(7, 5, 3, 3);
    weights.for_each_value([&](float &f) { f = dis10(rnd); });
    Halide::Var i2, j2, k2, l2;
    node_inputs[1].rep(i2, j2, k2, l2) = weights(i2, j2, k2, l2);

    const std::vector<int> in_shape[2] = {{3, 5, 6, 11}, {3, 5, 10, 14}};
    const std::vector<int> out_shape[2] = {{3, 7, 4, 9}, {3, 7, 8, 12}};

    for (int trial = 0; trial < 2; ++trial) {
        node_inputs[0].shape.resize(4);
        for (int dim = 0; dim < 4; ++dim) {
            node_inputs[0].shape[dim] = in_shape[trial][dim];
        }

        Halide::Buffer<float, 4> in(in_shape[trial]);
        in.for_each_value([&](float &f) { f = dis(rnd); });
        Halide::Var i1, j1, k1, l1;
        node_inputs[0].rep = Halide::Func();
        node_inputs[0].rep(i1, j1, k1, l1) = in(i1, j1, k1, l1);

        Node converted = convert_node(add_node, node_inputs);

        GOOGLE_CHECK_EQ(1, converted.outputs.size());
        Halide::Buffer<float, 4> output =
            converted.outputs[0].rep.realize(out_shape[trial]);

        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 7; ++j) {
                for (int k = 0; k < out_shape[trial][2]; ++k) {
                    for (int l = 0; l < out_shape[trial][3]; ++l) {
                        float expected = 0;
                        for (int c = 0; c < 5; ++c) {
                            for (int w = 0; w < 3; ++w) {
                                for (int h = 0; h < 3; ++h) {
                                    expected += in(i, c, k + w, l + h) * weights(j, c, w, h);
                                }
                            }
                        }
                        EXPECT_NEAR(output(i, j, k, l), expected, 5e-4f);
                    }
                }
            }
        }
    }
}

static void test_sum() {
    onnx::NodeProto sum_node;
    sum_node.set_name("sum_node");
    sum_node.set_op_type("ReduceSum");
    sum_node.add_input("x");
    sum_node.add_output("y");

    onnx::AttributeProto *attr = sum_node.add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    attr->add_ints(2);

    std::vector<Tensor> node_inputs;
    node_inputs.resize(1);
    node_inputs[0].shape = {7, 3, 5, 11};
    Halide::Buffer<float, 4> in1(7, 3, 5, 11);
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::mt19937 rnd;
    in1.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Var i, j, k, l;
    node_inputs[0].rep(i, j, k, l) = in1(i, j, k, l);

    Node converted = convert_node(sum_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 4> output = converted.outputs[0].rep.realize({1, 3, 1, 11});
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 11; ++j) {
            float expected = 0.0f;
            for (int k = 0; k < 7; ++k) {
                for (int l = 0; l < 5; ++l) {
                    expected += in1(k, i, l, j);
                }
            }
            EXPECT_NEAR(expected, output(0, i, 0, j), 1e-5);
        }
    }
}

static void test_where_broadcast() {
    onnx::NodeProto where_node;
    where_node.set_name("where_node");
    where_node.set_op_type("Where");
    where_node.add_input("c");
    where_node.add_input("x");
    where_node.add_input("y");
    where_node.add_output("z");

    std::vector<Tensor> node_inputs;
    node_inputs.resize(3);
    node_inputs[0].shape = {2, 2, 2};
    node_inputs[1].shape = {2};
    node_inputs[2].shape = {2, 2};
    Halide::Buffer<bool, 3> in_c(2, 2, 2);
    in_c.for_each_element(
        [&](int x, int y, int z) { in_c(x, y, z) = (x == y && x == z); });
    Halide::Buffer<float, 1> in_x(2);
    Halide::Buffer<float, 2> in_y(2, 2);
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::mt19937 rnd;
    in_x.for_each_value([&](float &f) { f = dis(rnd); });
    in_y.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Var i("i"), j("j"), k("k");
    node_inputs[0].rep(i, j, k) = in_c(i, j, k);
    node_inputs[1].rep(i) = in_x(i);
    node_inputs[2].rep(i, j) = in_y(i, j);

    Node converted = convert_node(where_node, node_inputs);
    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 3> output = converted.outputs[0].rep.realize({2, 2, 2});

    for (int i = 0; i < 2; ++i) {
        for (int j = 0; j < 2; ++j) {
            for (int k = 0; k < 2; ++k) {
                if (in_c(i, j, k)) {
                    EXPECT_EQ(output(i, j, k), in_x(k));
                } else {
                    EXPECT_EQ(output(i, j, k), in_y(j, k));
                }
            }
        }
    }
}

static void test_concat() {
    onnx::NodeProto concat_node;
    concat_node.set_name("concat_node");
    concat_node.set_op_type("Concat");
    concat_node.add_input("x");
    concat_node.add_output("y");

    onnx::AttributeProto *attr = concat_node.add_attribute();
    attr->set_name("axis");
    attr->add_ints(0);

    std::vector<Tensor> node_inputs;
    node_inputs.resize(2);
    node_inputs[0].shape = {7, 3};
    Halide::Buffer<float, 2> in1(7, 3);
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::mt19937 rnd;
    in1.for_each_value([&](float &f) { f = dis(rnd); });
    Halide::Var i, j;
    node_inputs[0].rep(i, j) = in1(i, j);

    node_inputs[1].shape = {5, 3};
    Halide::Buffer<float, 2> in2(5, 3);
    in2.for_each_value([&](float &f) { f = dis(rnd); });
    node_inputs[1].rep(i, j) = in2(i, j);

    Node converted = convert_node(concat_node, node_inputs);

    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<float, 2> output = converted.outputs[0].rep.realize({7 + 5, 3});
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 7; ++j) {
            EXPECT_EQ(in1(j, i), output(j, i));
        }
        for (int j = 0; j < 5; ++j) {
            EXPECT_EQ(in2(j, i), output(j + 7, i));
        }
    }
}

static void test_constant_fill() {
    constexpr float const_value = 2.0f;
    onnx::NodeProto concat_node;
    concat_node.set_name("constant_fill_node");
    concat_node.set_op_type("ConstantFill");
    concat_node.add_output("y");
    onnx::AttributeProto *shape_attr = concat_node.add_attribute();
    shape_attr->set_name("shape");
    shape_attr->add_ints(3);
    shape_attr->add_ints(4);
    onnx::AttributeProto *val_attr = concat_node.add_attribute();
    val_attr->set_name("value");
    val_attr->set_f(const_value);
    onnx::AttributeProto *dtype_attr = concat_node.add_attribute();
    dtype_attr->set_name("dtype");
    dtype_attr->set_i(4);

    Node converted = convert_node(concat_node, {});
    GOOGLE_CHECK_EQ(1, converted.outputs.size());
    Halide::Buffer<uint16_t, 2> output = converted.outputs[0].rep.realize({3, 4});
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            EXPECT_EQ(2u, output(i, j));
        }
    }
}

static void test_model() {
    onnx::ModelProto model;
    onnx::ValueInfoProto *input_def = model.mutable_graph()->add_input();
    input_def->set_name("model_input");
    input_def->mutable_type()->mutable_tensor_type()->set_elem_type(
        onnx::TensorProto_DataType_FLOAT);
    input_def->mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->add_dim()
        ->set_dim_value(3);
    input_def->mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->add_dim()
        ->set_dim_value(7);

    model.mutable_graph()->add_output()->set_name("model_output");
    model.mutable_graph()->add_output()->set_name("output_shape");
    model.mutable_graph()->add_output()->set_name("output_size");

    onnx::NodeProto *first_node = model.mutable_graph()->add_node();
    first_node->set_name("exp_of_input");
    first_node->set_op_type("Exp");
    first_node->add_input("model_input");
    first_node->add_output("input_exp");

    onnx::NodeProto *second_node = model.mutable_graph()->add_node();
    second_node->set_name("log_of_exp");
    second_node->set_op_type("Log");
    second_node->add_input("input_exp");
    second_node->add_output("log_exp");

    onnx::NodeProto *third_node = model.mutable_graph()->add_node();
    third_node->set_name("sum");
    third_node->set_op_type("Add");
    third_node->add_input("input_exp");
    third_node->add_input("log_exp");
    third_node->add_output("model_output");

    onnx::NodeProto *fourth_node = model.mutable_graph()->add_node();
    fourth_node->set_name("shape");
    fourth_node->set_op_type("Shape");
    fourth_node->add_input("model_output");
    fourth_node->add_output("output_shape");

    onnx::NodeProto *fifth_node = model.mutable_graph()->add_node();
    fifth_node->set_name("size");
    fifth_node->set_op_type("Size");
    fifth_node->add_input("model_output");
    fifth_node->add_output("output_size");

    std::unordered_map<std::string, int> dummy;
    Model converted = convert_model(model, dummy, IOLayout::Native);

    Halide::Buffer<float, 2> input_values(3, 7);
    std::uniform_real_distribution<float> dis(-1.0, 1.0);
    std::mt19937 rnd;
    input_values.for_each_value([&](float &f) { f = dis(rnd); });

    Halide::ImageParam &input = converted.inputs.at("model_input");
    input.set(input_values);
    Tensor node = converted.outputs.at("model_output");
    Halide::Buffer<float, 2> output_values = node.rep.realize({3, 7});

    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 7; ++j) {
            float expected =
                std::exp(input_values(i, j)) + std::log(std::exp(input_values(i, j)));
            float actual = output_values(i, j);
            EXPECT_NEAR(actual, expected, 1e-6f);
        }
    }

    Tensor size = converted.outputs.at("output_size");
    Halide::Buffer<int64_t, 0> output_size = size.rep.realize();
    EXPECT_EQ(21, output_size());

    Tensor shape = converted.outputs.at("output_shape");
    Halide::Buffer<int64_t, 1> output_shape = shape.rep.realize({2});
    EXPECT_EQ(3, output_shape(0));
    EXPECT_EQ(7, output_shape(1));
}

int main() {
    test_abs();
    test_activation_function();
    test_cast();
    test_add();
    test_constant();
    test_gemm();
    test_conv();
    test_sum();
    test_where_broadcast();
    test_concat();
    test_constant_fill();
    test_model();
    printf("Success!\n");
    return 0;
}
