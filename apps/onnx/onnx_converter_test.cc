#include <gtest/gtest.h>
#include <cmath>
#include <random>
#include "onnx_converter.h"

TEST(ConverterTest, testAbs) {
  onnx::NodeProto abs_node;
  abs_node.set_name("abs_node");
  abs_node.set_op_type("Abs");
  abs_node.add_input("x");
  abs_node.add_output("y");

  std::vector<Tensor> node_inputs;
  node_inputs.resize(1);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim();
  Halide::Buffer<float> input(200);
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  std::mt19937 rnd;
  input.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Var index;
  node_inputs[0].rep(index) = input(index);

  Node converted = ConvertNode(abs_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(200);
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(output(i), std::abs(input(i)));
  }
}

TEST(ConverterTest, testActivationFunction) {
  onnx::NodeProto relu_node;
  relu_node.set_name("relu_node");
  relu_node.set_op_type("Relu");
  relu_node.add_input("x");
  relu_node.add_output("y");

  std::vector<Tensor> node_inputs;
  node_inputs.resize(1);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim();
  Halide::Buffer<float> input(200);
  std::mt19937 rnd;
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  input.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Var index;
  node_inputs[0].rep(index) = input(index);

  Node converted = ConvertNode(relu_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(200);
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(output(i), std::max(input(i), 0.0f));
  }
}

TEST(ConverterTest, testCast) {
  onnx::NodeProto cast_node;
  cast_node.set_name("relu_node");
  cast_node.set_op_type("Cast");
  cast_node.add_input("x");
  cast_node.add_output("y");

  std::vector<Tensor> node_inputs;
  onnx::AttributeProto* attr = cast_node.add_attribute();
  attr->set_name("to");
  attr->set_i(onnx::TensorProto_DataType_FLOAT);
  node_inputs.resize(1);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim();
  Halide::Buffer<int> input(200);
  std::mt19937 rnd;
  std::uniform_int_distribution<int> dis(-100, 100);
  input.for_each_value([&](int& f) { f = dis(rnd); });
  Halide::Var index;
  node_inputs[0].rep(index) = input(index);

  Node converted = ConvertNode(cast_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(200);
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(output(i), static_cast<float>(input(i)));
  }
}

TEST(ConverterTest, testAdd) {
  onnx::NodeProto add_node;
  add_node.set_name("add_node");
  add_node.set_op_type("Add");
  add_node.add_input("x");
  add_node.add_input("y");
  add_node.add_output("z");

  std::vector<Tensor> node_inputs;
  node_inputs.resize(2);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim();
  node_inputs[1].shape = node_inputs[0].shape;
  Halide::Buffer<float> in1(200);
  std::mt19937 rnd;
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  std::uniform_real_distribution<float> dis10(-10.0, 10.0);
  in1.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Buffer<float> in2(200);
  in2.for_each_value(
      [&](float& f) { f = dis10(rnd); });
  Halide::Var index;
  node_inputs[0].rep(index) = in1(index);
  node_inputs[1].rep(index) = in2(index);

  Node converted = ConvertNode(add_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(200);
  for (int i = 0; i < 200; ++i) {
    EXPECT_EQ(output(i), in1(i) + in2(i));
  }
}

TEST(ConverterTest, testConstant) {
  onnx::NodeProto add_node;
  add_node.set_name("constant_node");
  add_node.set_op_type("Constant");
  add_node.add_output("y");
  onnx::AttributeProto* attr = add_node.add_attribute();
  attr->set_name("value");

  onnx::TensorProto& value = *attr->mutable_t();
  value.set_data_type(onnx::TensorProto_DataType_FLOAT);
  value.add_dims(3);
  value.add_dims(7);
  std::mt19937 rnd;
  std::uniform_real_distribution<float> dis(-10.0, 10.0);
  for (int i = 0; i < 3 * 7; ++i) {
    value.add_float_data(dis(rnd));
  }

  Node converted = ConvertNode(add_node, {}, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize({3, 7});
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 7; ++j) {
      EXPECT_EQ(output(i, j), value.float_data(j + 7 * i));
    }
  }
}

TEST(ConverterTest, testGemm) {
  onnx::NodeProto add_node;
  add_node.set_name("gemm_node");
  add_node.set_op_type("Gemm");
  add_node.add_input("a");
  add_node.add_input("b");
  add_node.add_input("c");
  add_node.add_output("y");

  std::vector<Tensor> node_inputs;
  node_inputs.resize(3);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(32);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(100);
  node_inputs[1]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(100);
  node_inputs[1]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(64);
  node_inputs[2]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(32);
  node_inputs[2]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(64);
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  std::uniform_real_distribution<float> dis10(-10.0, 10.0);

  std::mt19937 rnd;
  Halide::Buffer<float> in1(32, 100);
  in1.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Buffer<float> in2(100, 64);
  in2.for_each_value(
      [&](float& f) { f = dis10(rnd); });
  Halide::Buffer<float> in3(32, 64);
  in3.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Var i1, j1;
  node_inputs[0].rep(i1, j1) = in1(i1, j1);
  Halide::Var i2, j2;
  node_inputs[1].rep(i2, j2) = in2(i2, j2);
  Halide::Var i3, j3;
  node_inputs[2].rep(i3, j3) = in3(i3, j3);
  Node converted = ConvertNode(add_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(32, 64);

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

TEST(ConverterTest, testSum) {
  onnx::NodeProto sum_node;
  sum_node.set_name("sum_node");
  sum_node.set_op_type("ReduceSum");
  sum_node.add_input("x");
  sum_node.add_output("y");

  onnx::AttributeProto* attr = sum_node.add_attribute();
  attr->set_name("axes");
  attr->add_ints(0);
  attr->add_ints(2);

  std::vector<Tensor> node_inputs;
  node_inputs.resize(1);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(7);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(3);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(5);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(11);
  Halide::Buffer<float> in1(7, 3, 5, 11);
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  std::mt19937 rnd;
  in1.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Var i, j, k, l;
  node_inputs[0].rep(i, j, k, l) = in1(i, j, k, l);

  Node converted = ConvertNode(sum_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(1, 3, 1, 11);
  float expected = 0.0f;
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

TEST(ConverterTest, testConcat) {
  onnx::NodeProto concat_node;
  concat_node.set_name("concat_node");
  concat_node.set_op_type("Concat");
  concat_node.add_input("x");
  concat_node.add_output("y");

  onnx::AttributeProto* attr = concat_node.add_attribute();
  attr->set_name("axis");
  attr->add_ints(0);

  std::vector<Tensor> node_inputs;
  node_inputs.resize(2);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(7);
  node_inputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(3);
  Halide::Buffer<float> in1(7, 3);
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  std::mt19937 rnd;
  in1.for_each_value(
      [&](float& f) { f = dis(rnd); });
  Halide::Var i, j;
  node_inputs[0].rep(i, j) = in1(i, j);

  node_inputs[1]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(5);
  node_inputs[1]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->add_dim()
      ->set_dim_value(3);
  Halide::Buffer<float> in2(5, 3);
  in2.for_each_value(
      [&](float& f) { f = dis(rnd); });
  node_inputs[1].rep(i, j) = in2(i, j);

  Node converted = ConvertNode(concat_node, node_inputs, "");

  GOOGLE_CHECK_EQ(1, converted.outputs.size());
  Halide::Buffer<float> output = converted.outputs[0].rep.realize(7 + 5, 3);
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 7; ++j) {
      EXPECT_EQ(in1(j, i), output(j, i));
    }
    for (int j = 0; j < 5; ++j) {
      EXPECT_EQ(in2(j, i), output(j + 7, i));
    }
  }
}

TEST(ConverterTest, testModel) {
  onnx::ModelProto model;
  onnx::ValueInfoProto* input_def = model.mutable_graph()->add_input();
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

  onnx::NodeProto* first_node = model.mutable_graph()->add_node();
  first_node->set_name("exp_of_input");
  first_node->set_op_type("Exp");
  first_node->add_input("model_input");
  first_node->add_output("input_exp");

  onnx::NodeProto* second_node = model.mutable_graph()->add_node();
  second_node->set_name("log_of_exp");
  second_node->set_op_type("Log");
  second_node->add_input("input_exp");
  second_node->add_output("log_exp");

  onnx::NodeProto* third_node = model.mutable_graph()->add_node();
  third_node->set_name("sum");
  third_node->set_op_type("Add");
  third_node->add_input("input_exp");
  third_node->add_input("log_exp");
  third_node->add_output("model_output");

  onnx::NodeProto* fourth_node = model.mutable_graph()->add_node();
  fourth_node->set_name("shape");
  fourth_node->set_op_type("Shape");
  fourth_node->add_input("model_output");
  fourth_node->add_output("output_shape");

  onnx::NodeProto* fifth_node = model.mutable_graph()->add_node();
  fifth_node->set_name("size");
  fifth_node->set_op_type("Size");
  fifth_node->add_input("model_output");
  fifth_node->add_output("output_size");

  Model converted = ConvertModel(model, "");

  Halide::Buffer<float> input_values({3, 7});
  std::uniform_real_distribution<float> dis(-1.0, 1.0);
  std::mt19937 rnd;
  input_values.for_each_value(
      [&](float& f) { f = dis(rnd); });

  Halide::ImageParam& input = converted.inputs.at("model_input");
  input.set(input_values);
  Tensor node = converted.outputs.at("model_output");
  Halide::Buffer<float> output_values = node.rep.realize({3, 7});

  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 7; ++j) {
      float expected =
          std::exp(input_values(i, j)) + std::log(std::exp(input_values(i, j)));
      float actual = output_values(i, j);
      EXPECT_NEAR(actual, expected, 1e-6f);
    }
  }

  Tensor size = converted.outputs.at("output_size");
  Halide::Buffer<int64_t> output_size = size.rep.realize(1);
  EXPECT_EQ(21, output_size(0));

  Tensor shape = converted.outputs.at("output_shape");
  Halide::Buffer<int64_t> output_shape = shape.rep.realize(2);
  EXPECT_EQ(3, output_shape(0));
  EXPECT_EQ(7, output_shape(1));
}
