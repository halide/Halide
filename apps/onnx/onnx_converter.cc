#include "onnx_converter.h"
#include <exception>
#include <unordered_set>

static int DivUp(int num, int denom) {
  return (num + denom - 1) / denom;
}

std::string SanitizeName(const std::string& name) {
  std::string result = name;
  assert(!name.empty());
  // Replace dot with underscores since dots aren't allowed in Halide names.
  std::replace(result.begin(), result.end(), '.', '_');
  return result;
}

Halide::Func FuncForNodeOutput(const onnx::NodeProto& node, int output_id) {
  assert(node.output_size() > output_id);
  return Halide::Func(SanitizeName(node.output(output_id)));
}

Halide::Expr GenerateCastExpr(
    const Halide::Expr& input,
    const onnx::NodeProto& node,
    onnx::TensorProto::DataType* output_type) {
  int tgt_type = onnx::TensorProto_DataType_UNDEFINED;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "to") {
      tgt_type = attr.i();
      break;
    }
  }
  *output_type = static_cast<onnx::TensorProto::DataType>(tgt_type);

  switch (tgt_type) {
    case onnx::TensorProto_DataType_FLOAT:
      return Halide::cast<float>(input);
    case onnx::TensorProto_DataType_DOUBLE:
      return Halide::cast<double>(input);
    case onnx::TensorProto_DataType_INT8:
      return Halide::cast<int8_t>(input);
    case onnx::TensorProto_DataType_INT16:
      return Halide::cast<int16_t>(input);
    case onnx::TensorProto_DataType_INT32:
      return Halide::cast<int32_t>(input);
    case onnx::TensorProto_DataType_INT64:
      return Halide::cast<int64_t>(input);
    case onnx::TensorProto_DataType_UINT8:
      return Halide::cast<uint8_t>(input);
    case onnx::TensorProto_DataType_UINT16:
      return Halide::cast<uint16_t>(input);
    case onnx::TensorProto_DataType_UINT32:
      return Halide::cast<uint32_t>(input);
    case onnx::TensorProto_DataType_UINT64:
      return Halide::cast<uint64_t>(input);
    case onnx::TensorProto_DataType_BOOL:
      return Halide::cast<bool>(input);
  }
  throw std::domain_error(
      "Unsupported or unknown target type for node " + node.name());
}

Halide::Expr GenerateClipExpr(
    const Halide::Expr& input,
    const onnx::NodeProto& node) {
  float mini = -3.4028234663852886e+38;
  float maxi = 3.4028234663852886e+38;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "max") {
      maxi = attr.f();
    } else if (attr.name() == "min") {
      mini = attr.f();
    }
  }
  return Halide::clamp(input, mini, maxi);
}

Halide::Expr GenerateScaleExpr(
    const Halide::Expr& input,
    const onnx::NodeProto& node) {
  float scale = 1.0f;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "scale") {
      scale = attr.f();
    }
  }
  return input * scale;
}

#define BUILD_CONSTANT_EXPR(DataType, FieldName)                    \
  Halide::Buffer<DataType> val(dims);                               \
  val.for_each_element([&](const int* halide_coords) {              \
    int onnx_index = 0;                                             \
    for (int i = 0; i < dims.size(); i++) {                         \
      onnx_index += halide_coords[i] * onnx_strides[i];             \
    }                                                               \
    if (value.FieldName##_data_size() > 0) {                        \
      val(halide_coords) = value.FieldName##_data(onnx_index);      \
    } else {                                                        \
      const char* raw =                                             \
          value.raw_data().data() + sizeof(DataType) * onnx_index;  \
      val(halide_coords) = *reinterpret_cast<const DataType*>(raw); \
    }                                                               \
  });                                                               \
  result.rep(Halide::_) = val(Halide::_);

Tensor BuildFromConstant(const onnx::TensorProto& value) {
  Tensor result;

  std::vector<int> dims;
  for (int64_t dim : value.dims()) {
    result.shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->add_dim()
        ->set_dim_value(dim);
    dims.push_back(dim);
  }
  result.shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      value.data_type());

  int stride = 1;
  std::vector<int> onnx_strides;
  for (int i = 0; i < dims.size(); ++i) {
    onnx_strides.push_back(stride);
    stride *= dims[dims.size() - (i + 1)];
  }
  std::reverse(onnx_strides.begin(), onnx_strides.end());

  switch (value.data_type()) {
    case onnx::TensorProto_DataType_FLOAT: {
      BUILD_CONSTANT_EXPR(float, float) break;
    }
    case onnx::TensorProto_DataType_DOUBLE: {
      BUILD_CONSTANT_EXPR(double, double) break;
    }
    case onnx::TensorProto_DataType_INT32: {
      BUILD_CONSTANT_EXPR(int32_t, int32) break;
    }
    case onnx::TensorProto_DataType_INT64: {
      BUILD_CONSTANT_EXPR(int64_t, int64) break;
    }
    case onnx::TensorProto_DataType_UINT32: {
      BUILD_CONSTANT_EXPR(uint32_t, uint64) break;
    }
    case onnx::TensorProto_DataType_UINT64: {
      BUILD_CONSTANT_EXPR(uint64_t, uint64) break;
    }
    case onnx::TensorProto_DataType_INT8: {
      BUILD_CONSTANT_EXPR(int8_t, int32) break;
    }
    case onnx::TensorProto_DataType_UINT8: {
      BUILD_CONSTANT_EXPR(uint8_t, int32) break;
    }
    case onnx::TensorProto_DataType_INT16: {
      BUILD_CONSTANT_EXPR(int16_t, int32) break;
    }
    case onnx::TensorProto_DataType_UINT16: {
      BUILD_CONSTANT_EXPR(uint16_t, int32) break;
    }
    default:
      throw std::domain_error("Unsupported data type for constant");
  }
  return result;
}
#undef BUILD_CONSTANT_EXPR

Node ConvertNullaryOpNode(const onnx::NodeProto& node) {
  Node result;

  bool found_value = false;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "value") {
      const onnx::TensorProto& value = attr.t();
      result.outputs.resize(1);
      Tensor& out = result.outputs[0];
      out = BuildFromConstant(value);
      found_value = true;
      break;
    }
  }
  if (!found_value) {
    throw std::invalid_argument(
        "Value not specified for constant node " + node.name());
  }

  return result;
}

Node ConvertUnaryOpNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  assert(inputs.size() == 1);

  Node result;
  result.inputs = inputs;
  const Tensor& in = result.inputs[0];

  result.outputs.resize(1);
  Tensor& out = result.outputs[0];
  out.shape = inputs[0].shape;
  out.rep = FuncForNodeOutput(node, 0);

  if (node.op_type() == "Abs") {
    out.rep(Halide::_) = Halide::abs(in.rep(Halide::_));
  } else if (node.op_type() == "Acos") {
    out.rep(Halide::_) = Halide::acos(in.rep(Halide::_));
  } else if (node.op_type() == "Acosh") {
    out.rep(Halide::_) = Halide::acosh(in.rep(Halide::_));
  } else if (node.op_type() == "Asin") {
    out.rep(Halide::_) = Halide::asin(in.rep(Halide::_));
  } else if (node.op_type() == "Asinh") {
    out.rep(Halide::_) = Halide::asinh(in.rep(Halide::_));
  } else if (node.op_type() == "Atan") {
    out.rep(Halide::_) = Halide::atan(in.rep(Halide::_));
  } else if (node.op_type() == "Atanh") {
    out.rep(Halide::_) = Halide::atanh(in.rep(Halide::_));
  } else if (node.op_type() == "Cast") {
    onnx::TensorProto::DataType output_type;
    out.rep(Halide::_) =
        GenerateCastExpr(in.rep(Halide::_), node, &output_type);
    out.shape.mutable_type()->mutable_tensor_type()->set_elem_type(output_type);
  } else if (node.op_type() == "Ceil") {
    out.rep(Halide::_) = Halide::ceil(in.rep(Halide::_));
  } else if (node.op_type() == "Clip") {
    out.rep(Halide::_) = GenerateClipExpr(in.rep(Halide::_), node);
  } else if (node.op_type() == "Cos") {
    out.rep(Halide::_) = Halide::cos(in.rep(Halide::_));
  } else if (node.op_type() == "Cosh") {
    out.rep(Halide::_) = Halide::cosh(in.rep(Halide::_));
  } else if (node.op_type() == "Erf") {
    out.rep(Halide::_) = Halide::erf(in.rep(Halide::_));
  } else if (node.op_type() == "Exp") {
    out.rep(Halide::_) = Halide::exp(in.rep(Halide::_));
  } else if (node.op_type() == "Floor") {
    out.rep(Halide::_) = Halide::floor(in.rep(Halide::_));
  } else if (node.op_type() == "Identity") {
    out.rep(Halide::_) = in.rep(Halide::_);
  } else if (node.op_type() == "IsNaN") {
    out.rep(Halide::_) = Halide::is_nan(in.rep(Halide::_));
    out.shape.mutable_type()->mutable_tensor_type()->set_elem_type(
        onnx::TensorProto_DataType_BOOL);
  } else if (node.op_type() == "Log") {
    out.rep(Halide::_) = Halide::log(in.rep(Halide::_));
  } else if (node.op_type() == "Neg") {
    out.rep(Halide::_) = -in.rep(Halide::_);
  } else if (node.op_type() == "Not") {
    out.rep(Halide::_) = !in.rep(Halide::_);
  } else if (node.op_type() == "Reciprocal") {
    out.rep(Halide::_) = 1 / in.rep(Halide::_);
  } else if (node.op_type() == "Relu") {
    out.rep(Halide::_) = Halide::max(in.rep(Halide::_), 0);
  } else if (node.op_type() == "Scale") {
    out.rep(Halide::_) = GenerateScaleExpr(in.rep(Halide::_), node);
  } else if (node.op_type() == "Sigmoid") {
    out.rep(Halide::_) = 1 / (1 + Halide::exp(-in.rep(Halide::_)));
  } else if (node.op_type() == "Sign") {
    out.rep(Halide::_) = Halide::select(
        in.rep(Halide::_) == 0,
        0,
        in.rep(Halide::_) / Halide::abs(in.rep(Halide::_)));
  } else if (node.op_type() == "Sin") {
    out.rep(Halide::_) = Halide::sin(in.rep(Halide::_));
  } else if (node.op_type() == "Sinh") {
    out.rep(Halide::_) = Halide::sinh(in.rep(Halide::_));
  } else if (node.op_type() == "Softplus") {
    out.rep(Halide::_) = Halide::log(Halide::exp(in.rep(Halide::_)) + 1);
  } else if (node.op_type() == "Softsign") {
    out.rep(Halide::_) =
        in.rep(Halide::_) / (1 + Halide::abs(in.rep(Halide::_)));
  } else if (node.op_type() == "Sqrt") {
    out.rep(Halide::_) = Halide::sqrt(in.rep(Halide::_));
  } else if (node.op_type() == "Tan") {
    out.rep(Halide::_) = Halide::tan(in.rep(Halide::_));
  } else if (node.op_type() == "Tanh") {
    out.rep(Halide::_) = Halide::tanh(in.rep(Halide::_));
  } else if (
      node.op_type() == "Sum" || node.op_type() == "Mean" ||
      node.op_type() == "Min" || node.op_type() == "Max") {
    // These correspond to a degenerate case of a variadic op with a single
    // input, which is literally a no-op.
    out.rep(Halide::_) = in.rep(Halide::_);
  } else {
    throw std::domain_error(
        "Unsupported unary op type " + node.op_type() + " for node " +
        node.name());
  }

  return result;
}

Node ConvertBinaryOpNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  assert(inputs.size() == 2);

  Node result;
  result.inputs = inputs;
  const Tensor& in1 = result.inputs[0];
  const Tensor& in2 = result.inputs[1];

  const onnx::TensorShapeProto& in1_shape =
      in1.shape.type().tensor_type().shape();
  const int rank_in1 = in1_shape.dim_size();
  std::vector<Halide::Expr> in1_vars;
  in1_vars.resize(rank_in1);
  const onnx::TensorShapeProto& in2_shape =
      in2.shape.type().tensor_type().shape();
  const int rank_in2 = in2_shape.dim_size();
  std::vector<Halide::Expr> in2_vars;
  in2_vars.resize(rank_in2);
  const int rank = std::max(rank_in1, rank_in2);
  std::vector<Halide::Var> out_vars;
  out_vars.resize(rank);

  std::vector<onnx::TensorShapeProto_Dimension> out_shape;
  out_shape.resize(rank);
  for (int i = 1; i <= rank; ++i) {
    onnx::TensorShapeProto_Dimension dim;
    if (i <= rank_in1 && i <= rank_in2 &&
        (!in1_shape.dim(rank_in1 - i).has_dim_value() ||
         !in2_shape.dim(rank_in2 - i).has_dim_value())) {
      // At least one of the 2 dims is purely symbolic, so we can't conclude if
      // there's broadcasting by looking at the dim size. Check that both dims
      // have the same name,
      if (in1_shape.dim(rank_in1 - i).dim_param() !=
          in2_shape.dim(rank_in2 - i).dim_param()) {
        throw std::invalid_argument(
            "Can't determine whether broadcasting takes place on symbolic dimensions");
      }
    }

    if (i <= rank_in1) {
      if (in1_shape.dim(rank_in1 - i).dim_value() != 1) {
        in1_vars[rank_in1 - i] = out_vars[rank - i];
      } else {
        in1_vars[rank_in1 - i] = 0;
      }
      dim = in1_shape.dim(rank_in1 - i);
    }
    if (i <= rank_in2) {
      if (in2_shape.dim(rank_in2 - i).dim_value() != 1) {
        in2_vars[rank_in2 - i] = out_vars[rank - i];
      } else {
        in2_vars[rank_in2 - i] = 0;
      }
      if (!dim.has_dim_value() || dim.dim_value() == 1) {
        dim = in2_shape.dim(rank_in2 - i);
      }
    }
    out_shape[rank - i] = dim;
  }

  result.outputs.resize(1);
  Tensor& out = result.outputs[0];
  out.shape = inputs[0].shape;
  out.shape.mutable_type()->mutable_tensor_type()->mutable_shape()->clear_dim();
  for (int i = 0; i < rank; ++i) {
    *out.shape.mutable_type()
         ->mutable_tensor_type()
         ->mutable_shape()
         ->add_dim() = out_shape[i];
  }

  out.rep = FuncForNodeOutput(node, 0);
  bool boolean_output = false;

  if (node.op_type() == "Add" || node.op_type() == "Sum") {
    out.rep(out_vars) = in1.rep(in1_vars) + in2.rep(in2_vars);
  } else if (node.op_type() == "And") {
    out.rep(out_vars) = in1.rep(in1_vars) & in2.rep(in2_vars);
  } else if (node.op_type() == "Div") {
    out.rep(out_vars) = in1.rep(in1_vars) / in2.rep(in2_vars);
  } else if (node.op_type() == "Equal") {
    out.rep(out_vars) = in1.rep(in1_vars) == in2.rep(in2_vars);
    boolean_output = true;
  } else if (node.op_type() == "Greater") {
    out.rep(out_vars) = in1.rep(in1_vars) > in2.rep(in2_vars);
    boolean_output = true;
  } else if (node.op_type() == "Less") {
    out.rep(out_vars) = in1.rep(in1_vars) < in2.rep(in2_vars);
    boolean_output = true;
  } else if (node.op_type() == "Max") {
    out.rep(out_vars) = Halide::max(in1.rep(in1_vars), in2.rep(in2_vars));
  } else if (node.op_type() == "Mean") {
    out.rep(out_vars) = (in1.rep(in1_vars) + in2.rep(in2_vars)) / 2;
  } else if (node.op_type() == "Min") {
    out.rep(out_vars) = Halide::min(in1.rep(in1_vars), in2.rep(in2_vars));
  } else if (node.op_type() == "Mul") {
    out.rep(out_vars) = in1.rep(in1_vars) * in2.rep(in2_vars);
  } else if (node.op_type() == "Or") {
    out.rep(out_vars) = in1.rep(in1_vars) | in2.rep(in2_vars);
  } else if (node.op_type() == "Pow") {
    out.rep(out_vars) = Halide::pow(in1.rep(in1_vars), in2.rep(in2_vars));
  } else if (node.op_type() == "Sub") {
    out.rep(out_vars) = in1.rep(in1_vars) - in2.rep(in2_vars);
  } else if (node.op_type() == "Xor") {
    out.rep(out_vars) = in1.rep(in1_vars) ^ in2.rep(in2_vars);
  } else {
    throw std::domain_error(
        "Unsupported binary op type " + node.op_type() + " for node " +
        node.name());
  }

  if (boolean_output) {
    out.shape.mutable_type()->mutable_tensor_type()->set_elem_type(
        onnx::TensorProto_DataType_BOOL);
  }

  return result;
}

Node ConvertVariadicOpNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  assert(!inputs.empty());
  Node result;
  result.inputs = inputs;

  result.outputs.resize(1);
  Tensor& out = result.outputs[0];
  out.shape = inputs[0].shape;
  out.rep = FuncForNodeOutput(node, 0);
  out.rep(Halide::_) = inputs[0].rep(Halide::_);

  for (int i = 1; i < inputs.size(); ++i) {
    const Tensor& in = result.inputs[i];

    // We don't support broadcasting yet
    if (in.shape.type().tensor_type().shape().DebugString() !=
        out.shape.type().tensor_type().shape().DebugString()) {
      throw std::invalid_argument("Incompatible shapes");
    }

    if (node.op_type() == "Sum" || node.op_type() == "Mean") {
      out.rep(Halide::_) += in.rep(Halide::_);
    } else if (node.op_type() == "Min") {
      out.rep(Halide::_) = Halide::min(out.rep(Halide::_), in.rep(Halide::_));
    } else if (node.op_type() == "Max") {
      out.rep(Halide::_) = Halide::max(out.rep(Halide::_), in.rep(Halide::_));
    } else {
      throw std::domain_error(
          "Unsupported variadic op type " + node.op_type() + " for node " +
          node.name());
    }
  }

  if (node.op_type() == "Mean") {
    out.rep(Halide::_) /= static_cast<int>(inputs.size());
  }

  return result;
}

Node ConvertMetadataNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one input for node " + node.name());
  }
  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);

  std::vector<int64_t> dims;
  int64_t num_elements = 1;
  const onnx::TensorShapeProto& input_shape =
      inputs[0].shape.type().tensor_type().shape();
  for (int i = 0; i < input_shape.dim_size(); ++i) {
    if (!input_shape.dim(i).has_dim_value()) {
      throw std::invalid_argument(
          "Size of dimension " + std::to_string(i) + " is not known for node " +
          node.name());
    }
    dims.push_back(input_shape.dim(i).dim_value());
    num_elements *= input_shape.dim(i).dim_value();
  }

  if (node.op_type() == "Size") {
    Halide::Buffer<int64_t> shape(1);
    shape(0) = static_cast<int>(num_elements);
    result.outputs[0].rep(Halide::_) = shape(Halide::_);
    result.outputs[0]
        .shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->add_dim()
        ->set_dim_value(1);
  } else {
    Halide::Buffer<int64_t> shape(dims.size());
    shape.for_each_element([&](const int* halide_coords) {
      shape(halide_coords) = static_cast<int>(dims[halide_coords[0]]);
    });
    result.outputs[0].rep(Halide::_) = shape(Halide::_);
    result.outputs[0]
        .shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->add_dim()
        ->set_dim_value(dims.size());
  }
  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      onnx::TensorProto_DataType_INT64);
  return result;
}

Node ConvertGemmNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs,
    const std::string& device) {
  if (inputs.size() != 3) {
    throw std::invalid_argument(
        "Gemm requires 3 inputs, but node " + node.name() + " has " +
        std::to_string(inputs.size()));
  }
  Node result;
  result.inputs = inputs;
  const Tensor& A = result.inputs[0];
  const Tensor& B = result.inputs[1];

  bool transposeA = false;
  bool transposeB = false;
  float alpha = 1.0f;
  float beta = 1.0f;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "transA" && attr.i()) {
      transposeA = true;
    }
    if (attr.name() == "transB" && attr.i()) {
      transposeB = true;
    }
    if (attr.name() == "alpha") {
      alpha = attr.f();
    }
    if (attr.name() == "beta") {
      beta = attr.f();
    }
  }
  onnx::TensorShapeProto::Dimension dim_i = transposeA
      ? A.shape.type().tensor_type().shape().dim(1)
      : A.shape.type().tensor_type().shape().dim(0);
  onnx::TensorShapeProto::Dimension dim_j = transposeB
      ? B.shape.type().tensor_type().shape().dim(0)
      : B.shape.type().tensor_type().shape().dim(1);
  onnx::TensorShapeProto::Dimension dim_k_from_a = transposeA
      ? A.shape.type().tensor_type().shape().dim(0)
      : A.shape.type().tensor_type().shape().dim(1);
  onnx::TensorShapeProto::Dimension dim_k_from_b = transposeB
      ? B.shape.type().tensor_type().shape().dim(1)
      : B.shape.type().tensor_type().shape().dim(0);
  onnx::TensorShapeProto::Dimension dim_k;
  if (dim_k_from_a.has_dim_value()) {
    dim_k = dim_k_from_a;
  } else if (dim_k_from_b.has_dim_value()) {
    dim_k = dim_k_from_b;
  } else {
    throw std::domain_error("Unknown k dimension for node " + node.name());
  }
  Halide::Var i, j;
  Halide::RDom k(0, static_cast<int>(dim_k.dim_value()));

  // Check shapes
  result.outputs.resize(1);
  Tensor& out = result.outputs[0];
  out.shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      A.shape.type().tensor_type().elem_type());
  *out.shape.mutable_type()->mutable_tensor_type()->mutable_shape()->add_dim() =
      dim_i;
  *out.shape.mutable_type()->mutable_tensor_type()->mutable_shape()->add_dim() =
      dim_j;
  out.rep = FuncForNodeOutput(node, 0);

  // To do: check that C != 0
  const Tensor& C = result.inputs[2];
  const onnx::TensorShapeProto& shape_of_c =
      C.shape.type().tensor_type().shape();
  switch (shape_of_c.dim_size()) {
    case 0:
      out.rep(i, j) = beta * C.rep();
      break;
    case 1:
      if (shape_of_c.dim(0).dim_value() == 1) {
        out.rep(i, j) = beta * C.rep(0);
      } else {
        out.rep(i, j) = beta * C.rep(j);
      }
      break;
    case 2:
      if (shape_of_c.dim(0).dim_value() == 1 &&
          shape_of_c.dim(1).dim_value() == 1) {
        out.rep(i, j) = beta * C.rep(0, 0);
      } else if (shape_of_c.dim(0).dim_value() == 1) {
        out.rep(i, j) = beta * C.rep(0, j);
      } else if (shape_of_c.dim(1).dim_value() == 1) {
        out.rep(i, j) = beta * C.rep(i, 0);
      } else {
        out.rep(i, j) = beta * C.rep(i, j);
      }
      break;
    default:
      throw std::invalid_argument(
          "invalid rank for bias tensor " + C.shape.name());
  }

  if (transposeA && transposeB) {
    out.rep(i, j) += alpha * A.rep(k, i) * B.rep(j, k);
  } else if (transposeA) {
    out.rep(i, j) += alpha * A.rep(k, i) * B.rep(k, j);
  } else if (transposeB) {
    out.rep(i, j) += alpha * A.rep(i, k) * B.rep(j, k);
  } else {
    out.rep(i, j) += alpha * A.rep(i, k) * B.rep(k, j);
  }

  /* TBD: find good default schedule
    if (device.find("CPU") != std::string::npos) {
      // Basic schedule
      Halide::Var xi, yi, yii, xy;
      out.rep.tile(i, j, xi, yi, 24, 32).fuse(i, j, xy).parallel(xy)
      out.rep.bound(i, 0, static_cast<int>(dim_i.dim_value()));
      out.rep.bound(j, 0, static_cast<int>(dim_j.dim_value()));
    }
    */
  return result;
}

enum class PaddingMode { CONSTANT, EDGE, REFLECT };

Halide::Func GeneratePaddingExpr(
    Halide::Func input,
    const onnx::TensorShapeProto& input_shape,
    float padding_val,
    const std::vector<int>& pads,
    const PaddingMode mode = PaddingMode::CONSTANT) {
  // Number of leading dimensions that are not to be padded.
  const int rank = input_shape.dim_size();
  const int skip_dims = rank - pads.size() / 2;
  assert(skip_dims >= 0);

  // Pad the input with zeros as needed.
  std::vector<std::pair<int, int>> padding_extents;
  bool has_padding = false;
  for (int i = 0; i < rank - skip_dims; ++i) {
    int pad_before = pads[i];
    int pad_after = input_shape.dim(i + skip_dims).dim_value() + pad_before - 1;
    padding_extents.emplace_back(pad_before, pad_after);
    if (pad_before != 0 || pads[rank - skip_dims - i] != 0) {
      has_padding = true;
    }
  }

  if (!has_padding) {
    return input;
  }
  std::vector<Halide::Var> vars(rank);
  std::vector<Halide::Expr> input_vars(rank);
  for (int i = 0; i < skip_dims; ++i) {
    input_vars[i] = vars[i];
  }
  Halide::Expr pad = Halide::cast<bool>(false);
  for (int i = skip_dims; i < rank; ++i) {
    const std::pair<int, int>& pads = padding_extents[i - skip_dims];
    Halide::Expr pad_before = vars[i] < pads.first;
    assert(pad_before.type().is_bool());
    Halide::Expr pad_after = vars[i] > pads.second;
    assert(pad_after.type().is_bool());
    pad = pad || pad_before;
    pad = pad || pad_after;
    assert(pad.type().is_bool());
    if (mode == PaddingMode::CONSTANT || mode == PaddingMode::EDGE) {
      input_vars[i] = Halide::clamp(
          vars[i] - pads.first,
          0,
          static_cast<int>(input_shape.dim(i).dim_value() - 1));
    } else if (mode == PaddingMode::REFLECT) {
      Halide::Expr pad_size = pads.second - pads.first + 1;
      Halide::Expr mirror_before = (pads.first - vars[i]) % pad_size;
      Halide::Expr mirror_after =
          pad_size - ((vars[i] - pads.second) % pad_size) - 1;
      input_vars[i] = Halide::clamp(
          Halide::select(
              pad_before,
              mirror_before,
              Halide::select(pad_after, mirror_after, vars[i] - pads.first)),
          0,
          static_cast<int>(input_shape.dim(i).dim_value() - 1));
    }
  }

  Halide::Func padded_input;
  if (mode == PaddingMode::CONSTANT) {
    padded_input(vars) = Halide::select(pad, padding_val, input(input_vars));
  } else if (mode == PaddingMode::EDGE || mode == PaddingMode::REFLECT) {
    padded_input(vars) = input(input_vars);
  }
  return padded_input;
}

Halide::Func DirectConv(
    const Tensor& W,
    const Halide::Func& padded_input,
    int rank,
    int groups) {
  std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
  for (int i = 1; i < rank; ++i) {
    extents.emplace_back(
        0,
        static_cast<int>(
            W.shape.type().tensor_type().shape().dim(i).dim_value()));
  }

  Halide::RDom rdom(extents);
  std::vector<Halide::Var> out_vars(rank);
  std::vector<Halide::Expr> x_vars(rank);
  std::vector<Halide::Expr> w_vars(rank);

  if (groups != 1) {
    Halide::Expr group_size =
        static_cast<int>(
            W.shape.type().tensor_type().shape().dim(0).dim_value()) /
        groups;
    Halide::Expr group_id = out_vars[1] / group_size;
    Halide::Expr group_size2 = static_cast<int>(
        W.shape.type().tensor_type().shape().dim(1).dim_value());
    x_vars[1] = rdom[0] + group_id * group_size2;
  } else {
    x_vars[1] = rdom[0];
  }
  x_vars[0] = out_vars[0];
  for (int i = 2; i < rank; ++i) {
    x_vars[i] = out_vars[i] + rdom[i - 1];
  }
  w_vars[0] = out_vars[1];
  for (int i = 1; i < rank; ++i) {
    w_vars[i] = rdom[i - 1];
  }

  Halide::Func direct_conv;
  direct_conv(out_vars) = Halide::sum(padded_input(x_vars) * W.rep(w_vars));
  return direct_conv;
}

Halide::Func WinogradConv(const Tensor& W, const Halide::Func& padded_input) {
  // We only support the case of a 3x3 convolution at the moment. The notation
  // is derived from the one used in the Winograd paper.
  static float BFilter[4][4] = {
      {1, 0, -1, 0}, {0, 1, 1, 0}, {0, -1, 1, 0}, {0, 1, 0, -1}};
  const Halide::Buffer<float> B(&BFilter[0][0], 4, 4);

  static float GFilter[3][4] = {
      {1, 0.5, 0.5, 0}, {0, 0.5, -0.5, 0}, {0, 0.5, 0.5, 1}};
  const Halide::Buffer<float> G{&GFilter[0][0], 4, 3};

  static float AFilter[2][4] = {{1, 1, 1, 0}, {0, 1, -1, -1}};
  const Halide::Buffer<float> A{&AFilter[0][0], 4, 2};

  int num_channels = W.shape.type().tensor_type().shape().dim(1).dim_value();
  Halide::RDom dom1(0, num_channels);
  Halide::RVar c_r = dom1;
  const Halide::RDom dom2({{0, 3}, {0, 3}});
  Halide::RVar r1 = dom2[0];
  Halide::RVar r2 = dom2[1];
  const Halide::RDom dom3({{0, 4}, {0, 4}});
  Halide::RVar r3 = dom3[0];
  Halide::RVar r4 = dom3[1];
  const Halide::RDom dom4({{0, 4}, {0, 4}});
  Halide::RVar alpha_r = dom4[0];
  Halide::RVar beta_r = dom4[1];

  Halide::Var k, c, alpha, beta;
  Halide::Func U;
  U(k, c, alpha, beta) =
      Halide::sum(G(alpha, r1) * W.rep(k, c, r1, r2) * G(beta, r2));

  Halide::Var b, x, y;
  Halide::Func V;
  V(b, c, x, y, alpha, beta) = Halide::sum(
      B(r3, alpha) * padded_input(b, c, x + r3, y + r4) * B(r4, beta));

  Halide::Func M;
  M(b, k, x, y, alpha, beta) =
      Halide::sum(U(k, c_r, alpha, beta) * V(b, c_r, x, y, alpha, beta));

  Halide::Func winograd_conv;
  winograd_conv(b, k, x, y) = Halide::sum(
      A(alpha_r, x % 2) * M(b, k, (x / 2) * 2, (y / 2) * 2, alpha_r, beta_r) *
      A(beta_r, y % 2));
  return winograd_conv;
}

Node ConvertConvNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs,
    const std::string& device) {
  if (inputs.size() < 2) {
    throw std::invalid_argument(
        "Conv requires 2 or 3 inputs, but node " + node.name() + " has " +
        std::to_string(inputs.size()));
  }
  const Tensor& X = inputs[0];
  const Tensor& W = inputs[1];

  const int rank = X.shape.type().tensor_type().shape().dim_size();
  if (rank != W.shape.type().tensor_type().shape().dim_size()) {
    throw std::invalid_argument(
        "Inconsistent ranks for input tensors of Conv node " + node.name());
  }
  if (rank < 3) {
    throw std::invalid_argument(
        "Rank of input tensors for Conv node " + node.name() +
        " should be at least 3");
  }

  std::string padding = "NOTSET";
  int groups = 1;
  std::vector<int> dilations;
  // std::vector<int> kernel_shape;
  std::vector<int> pads;
  std::vector<int> strides;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "auto_pad") {
      padding = attr.s();
    } else if (attr.name() == "group") {
      groups = attr.i();
    } else if (attr.name() == "dilations") {
      for (int axis : attr.ints()) {
        dilations.push_back(axis);
      }
      // TODO: check that the kernel shape is compatible with the shape of W
      /*} else if (attr.name() == "kernel_shape") {
        for (int axis : attr.ints()) {
          kernel_shape.push_back(axis);
        }*/
    } else if (attr.name() == "pads") {
      for (int axis : attr.ints()) {
        pads.push_back(axis);
      }
    } else if (attr.name() == "strides") {
      for (int axis : attr.ints()) {
        strides.push_back(axis);
      }
    }
  }

  pads.resize(2 * rank - 4, 0);
  dilations.resize(rank - 2, 1);
  strides.resize(rank - 2, 1);

  for (int i : dilations) {
    if (i != 1) {
      throw std::domain_error(
          "Dilated convolution not supported for node " + node.name());
    }
  }

  if (padding != "NOTSET") {
    throw std::domain_error(
        "Unsupported type of convolution for node " + node.name());
  }

  // Determine the shape of the output
  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);

  result.outputs[0].shape = inputs[0].shape;
  *result.outputs[0]
       .shape.mutable_type()
       ->mutable_tensor_type()
       ->mutable_shape()
       ->mutable_dim(1) = W.shape.type().tensor_type().shape().dim(0);
  for (int i = 2; i < rank; ++i) {
    int dim = X.shape.type().tensor_type().shape().dim(i).dim_value() +
        pads[i - 2] + pads[rank + i - 4];
    dim -= (W.shape.type().tensor_type().shape().dim(i).dim_value() - 1);
    dim = DivUp(dim, strides[i - 2]);

    result.outputs[0]
        .shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->mutable_dim(i)
        ->clear_dim_param();
    result.outputs[0]
        .shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->mutable_dim(i)
        ->set_dim_value(dim);
  }

  // Check if winograd can be used
  bool can_use_winograd = false;
  bool needs_extra_padding = false;
  if (groups == 1 && rank == 4) {
    bool supported_shape = true;
    for (int i = 2; i < rank; ++i) {
      if (W.shape.type().tensor_type().shape().dim(i).dim_value() != 3) {
        supported_shape = false;
        break;
      }
      if (result.outputs[0]
                  .shape.type()
                  .tensor_type()
                  .shape()
                  .dim(i)
                  .dim_value() %
              2 !=
          0) {
        needs_extra_padding = true;
      }
      if (strides[i - 2] != 1) {
        supported_shape = false;
        break;
      }
    }
    can_use_winograd = supported_shape;
  }

  if (can_use_winograd && needs_extra_padding) {
    pads[2] += 1;
    pads[3] += 1;
  }

  // Pad the input with zeros as needed.
  Halide::Func padded_input =
      GeneratePaddingExpr(X.rep, X.shape.type().tensor_type().shape(), 0, pads);

  // Convolve the input with the kernel
  Halide::Func basic_conv;
  if (can_use_winograd) {
    basic_conv = WinogradConv(W, padded_input);
  } else {
    basic_conv = DirectConv(W, padded_input, rank, groups);
  }

  // Apply the strides if needed
  std::vector<Halide::Var> out_vars(rank);
  std::vector<Halide::Expr> stride_vars(rank);
  stride_vars[0] = out_vars[0];
  stride_vars[1] = out_vars[1];

  bool has_strides = false;
  for (int i = 0; i < rank - 2; ++i) {
    if (strides[i] != 1) {
      stride_vars[i + 2] = strides[i] * out_vars[i + 2];
      has_strides = true;
    } else {
      stride_vars[i + 2] = out_vars[i + 2];
    }
  }
  Halide::Func conv_no_bias;
  if (has_strides) {
    conv_no_bias(out_vars) = basic_conv(stride_vars);
  } else {
    conv_no_bias = basic_conv;
  }

  result.outputs[0].rep = FuncForNodeOutput(node, 0);

  // Return the result after applying the bias if any.
  if (inputs.size() == 3) {
    result.outputs[0].rep(out_vars) =
        inputs[2].rep(out_vars[1]) + conv_no_bias(out_vars);
  } else {
    result.outputs[0].rep(out_vars) = conv_no_bias(out_vars);
  }

  /* TBD: find good schedule
    if (device.find("CPU") != std::string::npos) {
      Halide::Var n = out_vars[0];
      Halide::Var c = out_vars[1];
      Halide::Var x = out_vars[2];
      Halide::Var y = out_vars[3];
      Halide::RDom r = rdom;

      // FIXME: support for 1 and 3d
      result.outputs[0].rep
                  .vectorize(c, 8).unroll(c).unroll(x).unroll(y)
                  .update().reorder(c, x, y, r.x, r.y, r.z, n)
                  .vectorize(c, 8).unroll(c).unroll(x).unroll(y).unroll(r.x, 2);
    }
  */

  return result;
}

Node ConvertReductionNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  std::set<int> reduction_axes;
  bool keepdims = true;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axes") {
      for (int axis : attr.ints()) {
        reduction_axes.insert(axis);
      }
    }
    if (attr.name() == "keepdims" && attr.i() == 0) {
      keepdims = false;
    }
  }

  const onnx::TensorShapeProto& input_shape =
      inputs[0].shape.type().tensor_type().shape();
  if (reduction_axes.empty()) {
    // This is used to specify a full reduction.
    for (int i = 0; i < input_shape.dim_size(); ++i) {
      reduction_axes.insert(i);
    }
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  std::vector<Halide::Expr> input_vars;
  std::vector<Halide::Expr> output_vars;

  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      inputs[0].shape.type().tensor_type().elem_type());
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();

  int num_reduced_elems = 1;
  std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
  for (int i = 0; i < input_shape.dim_size(); ++i) {
    if (reduction_axes.find(i) != reduction_axes.end()) {
      if (!input_shape.dim(i).has_dim_value()) {
        throw std::invalid_argument(
            "Size of reduced dimension " + std::to_string(i) +
            " is not known for node " + node.name());
      }
      extents.emplace_back(0, static_cast<int>(input_shape.dim(i).dim_value()));
      num_reduced_elems *= input_shape.dim(i).dim_value();
    }
  }

  Halide::RDom rdom(extents);
  int current_reduction_dim = 0;
  for (int i = 0; i < input_shape.dim_size(); ++i) {
    if (reduction_axes.find(i) != reduction_axes.end()) {
      input_vars.push_back(rdom[current_reduction_dim++]);
      if (keepdims) {
        // Create a dimension that will be of size 1. Is there a way to let
        // Halide optimize for this ?
        Halide::Var var;
        output_vars.push_back(var);
        output_shape->add_dim()->set_dim_value(1);
      }
    } else {
      Halide::Var var;
      input_vars.push_back(var);
      output_vars.push_back(var);
      *output_shape->add_dim() = input_shape.dim(i);
    }
  }
  if (node.op_type() == "ReduceSum") {
    result.outputs[0].rep(output_vars) =
        Halide::sum(result.inputs[0].rep(input_vars));
  } else if (node.op_type() == "ReduceSumSquare") {
    result.outputs[0].rep(output_vars) +=
        result.inputs[0].rep(input_vars) * result.inputs[0].rep(input_vars);
  } else if (node.op_type() == "ReduceLogSum") {
    result.outputs[0].rep(output_vars) =
        Halide::log(Halide::sum(result.inputs[0].rep(input_vars)));
  } else if (node.op_type() == "ReduceLogSumExp") {
    result.outputs[0].rep(output_vars) =
        Halide::log(Halide::sum(Halide::exp(result.inputs[0].rep(input_vars))));
  } else if (node.op_type() == "ReduceProd") {
    result.outputs[0].rep(output_vars) =
        Halide::product(result.inputs[0].rep(input_vars));
  } else if (node.op_type() == "ReduceMean") {
    result.outputs[0].rep(output_vars) =
        Halide::sum(result.inputs[0].rep(input_vars)) / num_reduced_elems;
  } else if (node.op_type() == "ReduceMin") {
    result.outputs[0].rep(output_vars) =
        Halide::minimum(result.inputs[0].rep(input_vars));
  } else if (node.op_type() == "ReduceMax") {
    result.outputs[0].rep(output_vars) =
        Halide::maximum(result.inputs[0].rep(input_vars));
  } else if (node.op_type() == "ReduceL2") {
    result.outputs[0].rep(output_vars) = Halide::sqrt(
        Halide::sum(Halide::pow(result.inputs[0].rep(input_vars), 2)));
  } else if (node.op_type() == "ReduceL1") {
    result.outputs[0].rep(output_vars) =
        Halide::sum(Halide::abs(result.inputs[0].rep(input_vars)));
  } else {
    throw std::domain_error(
        "Unsupported reduction type " + node.op_type() + " for node " +
        node.name());
  }

  return result;
}

Node ConvertBatchNormNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  bool spatial = true;
  float epsilon = 1e-5f;
  float momentum = 0.9f;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "spatial") {
      spatial = static_cast<bool>(attr.i());
    }
    if (attr.name() == "epsilon") {
      epsilon = attr.f();
    }
    if (attr.name() == "momentum") {
      momentum = attr.f();
    }
  }

  if (!spatial) {
    throw std::domain_error(
        "This type of batch normalization is not supported yet");
  }

  if (node.output_size() != 1) {
    throw std::domain_error("Only test mode supported yet");
  }

  if (inputs.size() != 5) {
    throw std::invalid_argument(
        "Expected 5 inputs for BatchNormalization node " + node.name());
  }
  const Tensor& X = inputs[0];
  const Tensor& scale = inputs[1];
  const Tensor& shift = inputs[2];
  const Tensor& mean = inputs[3];
  const Tensor& variance = inputs[4];

  const onnx::TensorShapeProto& input_shape =
      inputs[0].shape.type().tensor_type().shape();
  const int rank = input_shape.dim_size();
  if (rank < 2) {
    throw std::invalid_argument(
        "Input rank less than 2 for BatchNormalization node " + node.name());
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(node.output_size());
  result.outputs[0].shape = inputs[0].shape;
  result.outputs[0].rep = FuncForNodeOutput(node, 0);

  std::vector<Halide::Var> vars(rank);
  Halide::Var param_var = vars[1];

  Halide::Func normalized;
  normalized(vars) = (X.rep(vars) - mean.rep(param_var)) /
      Halide::sqrt(variance.rep(param_var) + epsilon);
  result.outputs[0].rep(vars) =
      scale.rep(param_var) * normalized(vars) + shift.rep(param_var);

  return result;
}

Node ConvertFlattenNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected a single input for Flatten node " + node.name());
  }

  int in_rank = inputs[0].shape.type().tensor_type().shape().dim_size();
  int axis = 1;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axis") {
      axis = attr.i();
      if (axis > in_rank) {
        throw std::invalid_argument(
            "Axis for node " + node.name() + " is " + std::to_string(axis) +
            "but should be less than input rank " + std::to_string(in_rank));
      }
    }
  }

  const int out_rank = 2;
  std::vector<int> strides(in_rank, 1);
  std::vector<int> in_shape(in_rank);
  for (int i = in_rank - 2; i >= 0; --i) {
    if (!inputs[0]
             .shape.type()
             .tensor_type()
             .shape()
             .dim(i + 1)
             .has_dim_value()) {
      throw std::invalid_argument(
          "Input Tenosr axis " + std::to_string(i) + " for node " +
          node.name() + " should have dim_value");
    }
    const int dim_i =
        inputs[0].shape.type().tensor_type().shape().dim(i + 1).dim_value();
    strides[i] = dim_i * strides[i + 1];
  }
  int inner_size = 1, outer_size = 1;
  for (int i = 0; i < in_rank; ++i) {
    const int dim_i =
        inputs[0].shape.type().tensor_type().shape().dim(i).dim_value();
    in_shape[i] = dim_i;
    if (i < axis) {
      outer_size *= dim_i;
    } else {
      inner_size *= dim_i;
    }
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(node.output_size());
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      inputs[0].shape.type().tensor_type().elem_type());
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();

  output_shape->add_dim()->set_dim_value(outer_size);
  output_shape->add_dim()->set_dim_value(inner_size);

  std::vector<Halide::Expr> in_vars(in_rank);
  std::vector<Halide::Var> out_vars(out_rank);

  Halide::Expr flat_index = out_vars[1] + out_vars[0] * inner_size;

  for (int i = 0; i < in_rank; ++i) {
    if (i == 0) {
      in_vars[i] = flat_index / strides[i];
    } else if (i == in_rank - 1) {
      in_vars[i] = flat_index % in_shape[i];
    } else {
      in_vars[i] = (flat_index / strides[i]) % in_shape[i];
    }
  }

  result.outputs[0].rep(out_vars) = result.inputs[0].rep(in_vars);

  return result;
}

Node ConvertEluNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  auto default_alpha = [&node]() -> float {
    if (node.op_type() == "Selu") {
      return 1.67326319217681884765625f;
    } else if (node.op_type() == "LeakyRelu") {
      return 0.01;
    }
    return 1.0f;
  };

  auto default_gamma = [&node]() -> float {
    if (node.op_type() == "Selu") {
      return 1.05070102214813232421875;
    }
    return 1.0f;
  };

  float alpha = default_alpha();
  float gamma = default_gamma();

  for (const auto& attr : node.attribute()) {
    if (attr.name() == "alpha") {
      alpha = attr.f();
    }
    if (attr.name() == "gamma") {
      gamma = attr.f();
    }
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(node.output_size());
  result.outputs[0].shape = inputs[0].shape;
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  const Tensor& X = inputs[0];

  // Elu expression def.
  Halide::Func elu;
  elu(Halide::_) = select(
      X.rep(Halide::_) > 0.0f,
      X.rep(Halide::_),
      alpha * (exp(X.rep(Halide::_)) - 1.0f));

  // Selu expression def.
  Halide::Func selu;
  selu(Halide::_) = select(
      X.rep(Halide::_) > 0.0f,
      gamma * X.rep(Halide::_),
      gamma * (alpha * exp(X.rep(Halide::_)) - alpha));

  // LeakyRelu expression def.
  Halide::Func leaky_relu;
  leaky_relu(Halide::_) = select(
      X.rep(Halide::_) >= 0.0f, X.rep(Halide::_), alpha * X.rep(Halide::_));

  if (node.op_type() == "Elu") {
    result.outputs[0].rep(Halide::_) = elu(Halide::_);
  } else if (node.op_type() == "Selu") {
    result.outputs[0].rep(Halide::_) = selu(Halide::_);
  } else if (node.op_type() == "LeakyRelu") {
    result.outputs[0].rep(Halide::_) = leaky_relu(Halide::_);
  } else {
    throw std::domain_error("Invalid elu op " + node.op_type());
  }
  return result;
}

Node ConvertDropoutNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  float ratio = 0.5f;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "ratio") {
      ratio = attr.f();
    }
  }

  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected a single input for Dropout node " + node.name());
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(node.output_size());
  if (node.output_size() == 1) {
    // Simple pass through
    result.outputs[0].shape = inputs[0].shape;
    result.outputs[0].rep = inputs[0].rep;
  } else if (node.output_size() == 2) {
    int rank = inputs[0].shape.type().tensor_type().shape().dim_size();
    std::vector<Halide::Var> vars(rank);
    Halide::Expr expr = 0;
    int stride = 1;
    for (int i = 0; i < rank; ++i) {
      expr += vars[i] * stride;
      stride *= inputs[0].shape.type().tensor_type().shape().dim(i).dim_value();
    }
    Halide::Func filter;
    filter(vars) = Halide::random_float(expr) > ratio;

    result.outputs[0].shape = inputs[0].shape;
    result.outputs[0].rep = FuncForNodeOutput(node, 0);
    result.outputs[0].rep(vars) = inputs[0].rep(vars) * filter(vars) / ratio;

    result.outputs[1].shape = inputs[0].shape;
    result.outputs[1].rep = FuncForNodeOutput(node, 1);
    result.outputs[1].rep(vars) = filter(vars);
  } else {
    throw std::domain_error(
        "Invalid number of outputs for dropout node " + node.name());
  }
  return result;
}

Node ConvertPoolingNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (node.output_size() != 1) {
    throw std::domain_error(
        "Can't yet generate indices for pooling node " + node.name());
  }

  std::string padding = "NOTSET";
  std::vector<int> kernel_shape;
  std::vector<int> pads;
  std::vector<int> strides;
  bool count_include_pad = false; // For avg pool
  int p = 2; // For lp pool
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "auto_pad") {
      padding = attr.s();
    }
    if (attr.name() == "count_include_pad") {
      count_include_pad = static_cast<bool>(attr.i());
    }
    if (attr.name() == "p") {
      p = attr.i();
    }
    if (attr.name() == "kernel_shape") {
      for (int dim : attr.ints()) {
        kernel_shape.push_back(dim);
      }
    }
    if (attr.name() == "pads") {
      for (int pad : attr.ints()) {
        pads.push_back(pad);
      }
    }
    if (attr.name() == "strides") {
      for (int stride : attr.ints()) {
        strides.push_back(stride);
      }
    }
  }

  const int rank = inputs[0].shape.type().tensor_type().shape().dim_size();
  if (node.op_type().find("Global") == 0) {
    // The kernel shape is the whole height/width of the input.
    for (int i = 2; i < rank; ++i) {
      kernel_shape.push_back(
          inputs[0].shape.type().tensor_type().shape().dim(i).dim_value());
    }
  } else {
    if (kernel_shape.size() + 2 != rank) {
      throw std::invalid_argument(
          "invalid kernel shape for pooling node " + node.name());
    }
  }

  if (node.op_type() == "AveragePool" && !count_include_pad) {
    for (int pad : pads) {
      if (pad != 0) {
        throw std::domain_error(
            "Unsupported type of padding for average pooling node " +
            node.name());
      }
    }
  }

  pads.resize(2 * rank - 4, 0);
  strides.resize(rank - 2, 1);

  if (padding != "NOTSET") {
    throw std::domain_error(
        "Unsupported type of padding for pooling node " + node.name());
  }

  // Pad the input with zeros as needed

  float padding_val = 0.0f;
  if (node.op_type() == "MaxPool" || node.op_type() == "GlobalMaxPool") {
    padding_val = -std::numeric_limits<float>::max();
  }
  Halide::Func padded_input = GeneratePaddingExpr(
      inputs[0].rep,
      inputs[0].shape.type().tensor_type().shape(),
      padding_val,
      pads);

  // Pool the input values.
  std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
  for (int i = 0; i < rank - 2; ++i) {
    extents.emplace_back(0, kernel_shape[i]);
  }

  Halide::RDom rdom(extents);
  std::vector<Halide::Var> out_vars(rank);
  std::vector<Halide::Expr> x_vars(rank);
  x_vars[0] = out_vars[0];
  x_vars[1] = out_vars[1];
  for (int i = 2; i < rank; ++i) {
    x_vars[i] = out_vars[i] + rdom[i - 2];
  }

  Halide::Func basic_pool;
  if (node.op_type() == "MaxPool" || node.op_type() == "GlobalMaxPool") {
    basic_pool(out_vars) = Halide::maximum(padded_input(x_vars));
  } else if (
      node.op_type() == "AveragePool" ||
      node.op_type() == "GlobalAveragePool") {
    int num_pooling_vals = 1;
    for (int kernel_dim : kernel_shape) {
      num_pooling_vals *= kernel_dim;
    }
    basic_pool(out_vars) = Halide::sum(padded_input(x_vars)) / num_pooling_vals;

  } else {
    throw std::domain_error(
        "Unsupported type of pooling " + node.op_type() + " for node " +
        node.name());
  }

  // Apply the strides if needed
  std::vector<Halide::Expr> stride_vars(rank);
  std::vector<Halide::Var> out2_vars(rank);
  stride_vars[0] = out2_vars[0];
  stride_vars[1] = out2_vars[1];

  bool has_strides = false;
  for (int i = 0; i < rank - 2; ++i) {
    if (strides[i] != 1) {
      stride_vars[i + 2] = strides[i] * out2_vars[i + 2];
      has_strides = true;
    } else {
      stride_vars[i + 2] = out2_vars[i + 2];
    }
  }
  Halide::Func pool;
  if (has_strides) {
    pool(out2_vars) = basic_pool(stride_vars);
  } else {
    pool = basic_pool;
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].rep = pool;

  // Determine the shape of the output
  result.outputs[0].shape = inputs[0].shape;
  for (int i = 2; i < rank; ++i) {
    int dim = inputs[0].shape.type().tensor_type().shape().dim(i).dim_value() +
        pads[i - 2] + pads[rank + i - 4];
    dim -= (kernel_shape[i - 2] - 1);
    dim = DivUp(dim, strides[i - 2]);
    result.outputs[0]
        .shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->mutable_dim(i)
        ->clear_dim_param();
    result.outputs[0]
        .shape.mutable_type()
        ->mutable_tensor_type()
        ->mutable_shape()
        ->mutable_dim(i)
        ->set_dim_value(dim);
  }

  return result;
}

Node ConvertSoftmaxNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  int axis = 1;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axis") {
      axis = attr.i();
    }
  }

  const onnx::TensorShapeProto& input_shape =
      inputs[0].shape.type().tensor_type().shape();
  const int rank = input_shape.dim_size();
  if (rank < 2) {
    throw std::invalid_argument(
        "Input rank less than 2 for softmax node " + node.name());
  }
  if (axis < 0) {
    axis += rank;
  }
  if (axis < 0 || axis >= rank) {
    throw std::invalid_argument(
        "Invalid axis specified for softmax node " + node.name());
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].shape = inputs[0].shape;
  result.outputs[0].rep = FuncForNodeOutput(node, 0);

  std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
  for (int i = axis; i < rank; ++i) {
    if (!inputs[0].shape.type().tensor_type().shape().dim(i).has_dim_value()) {
      throw std::domain_error(
          "Unknown dim not supported for softmax node " + node.name());
    }
    const int extent =
        inputs[0].shape.type().tensor_type().shape().dim(i).dim_value();
    extents.emplace_back(0, extent);
  }
  std::vector<Halide::Var> indices(input_shape.dim_size());
  Halide::RDom rdom(extents);
  std::vector<Halide::Expr> denom_vars;
  for (int i = 0; i < axis; ++i) {
    denom_vars.push_back(indices[i]);
  }
  for (int i = axis; i < rank; ++i) {
    denom_vars.push_back(rdom[i - axis]);
  }

  if (node.op_type() == "LogSoftmax") {
    Halide::Func in = inputs[0].rep;
    result.outputs[0].rep(indices) =
        in(indices)-Halide::log(Halide::sum(Halide::exp(in(denom_vars))));
  } else {
    Halide::Func in = inputs[0].rep;
    result.outputs[0].rep(indices) =
        Halide::exp(in(indices)) / Halide::sum(Halide::exp(in(denom_vars)));
  }
  return result;
}

Node ConvertConcatNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() < 1) {
    throw std::invalid_argument(
        "Too few inputs for concat node " + node.name());
  }
  int axis = -1;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axis") {
      axis = attr.i();
    }
  }
  const int num_dims = inputs[0].shape.type().tensor_type().shape().dim_size();
  if (axis < 0 || axis >= num_dims) {
    throw std::invalid_argument("Invalid axis for concat node " + node.name());
  }
  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  std::vector<Halide::Var> tgt_indices;
  tgt_indices.resize(num_dims);
  std::vector<Halide::Expr> src1_indices;
  std::vector<Halide::Expr> src2_indices;
  for (int i = 0; i < num_dims; ++i) {
    src1_indices.push_back(tgt_indices[i]);
    src2_indices.push_back(tgt_indices[i]);
  }

  Halide::Var concat_axis = tgt_indices[axis];
  std::vector<Halide::Func> concat_funcs;
  concat_funcs.resize(inputs.size());
  concat_funcs[0](tgt_indices) = inputs[0].rep(tgt_indices);
  int concat_offset = 0;
  for (int i = 1; i < inputs.size(); ++i) {
    if (!inputs[i - 1]
             .shape.type()
             .tensor_type()
             .shape()
             .dim(axis)
             .has_dim_value()) {
      throw std::invalid_argument(
          "Unknown size for dim " + std::to_string(axis) + " of node " +
          node.name());
    }
    concat_offset +=
        inputs[i - 1].shape.type().tensor_type().shape().dim(axis).dim_value();

    src1_indices[axis] = Halide::min(tgt_indices[axis], concat_offset - 1);
    src2_indices[axis] = Halide::max(tgt_indices[axis] - concat_offset, 0);

    concat_funcs[i](tgt_indices) = Halide::select(
        concat_axis < concat_offset,
        concat_funcs[i - 1](src1_indices),
        inputs[i].rep(src2_indices));
  }

  result.outputs[0].rep = concat_funcs.back();

  result.outputs[0].shape = inputs[0].shape;
  const int concatenated_size = concat_offset +
      inputs.back().shape.type().tensor_type().shape().dim(axis).dim_value();
  result.outputs[0]
      .shape.mutable_type()
      ->mutable_tensor_type()
      ->mutable_shape()
      ->mutable_dim(axis)
      ->set_dim_value(concatenated_size);
  return result;
}

Node ConvertSplitNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Unexpected number of inputs for split node " + node.name());
  }

  const int in_rank = inputs[0].shape.type().tensor_type().shape().dim_size();
  std::vector<int> splits;
  int axis = 0;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "split") {
      for (int split_size : attr.ints()) {
        splits.push_back(split_size);
      }
    }
    if (attr.name() == "axis") {
      axis = attr.i();
    }
  }

  const int axis_dim =
      inputs[0].shape.type().tensor_type().shape().dim(axis).dim_value();
  const int num_outputs = node.output_size();

  Node result;
  result.inputs = inputs;

  if(num_outputs == 0) {
    return result;
  }
  result.outputs.resize(num_outputs);

  // Split into equal parts.
  if (splits.size() == 0) {
    if (axis_dim % num_outputs != 0) {
      throw std::invalid_argument(
          "Can't equaly split outputs for node " + node.name());
    }
    const int size = axis_dim / num_outputs;
    for (int i = 0; i < num_outputs; ++i) {
      splits.push_back(size);
    }
  } else {
    const int total_splits_size =
        std::accumulate(splits.begin(), splits.end(), 0);
    if (total_splits_size > axis_dim) {
      throw std::invalid_argument(
          "Inconsistent splits for node " + node.name());
    }
  }

  // Compute offsets.
  std::vector<int> split_offsets(splits.size(), 0);
  for (int i = 1; i < splits.size(); ++i) {
    split_offsets[i] = split_offsets[i - 1] + splits[i - 1];
  }

  const int out_rank = in_rank;
  for (int i = 0; i < num_outputs; ++i) {
    result.outputs[i].shape = inputs[0].shape;
    std::vector<Halide::Var> out_vars(out_rank);
    std::vector<Halide::Expr> in_vars(in_rank);
    result.outputs[i].rep = FuncForNodeOutput(node, i);
    for (int dim = 0; dim < in_rank; ++dim) {
      if (dim == axis) {
        result.outputs[i]
            .shape.mutable_type()
            ->mutable_tensor_type()
            ->mutable_shape()
            ->mutable_dim(dim)
            ->set_dim_value(splits[i]);
        in_vars[dim] = out_vars[dim] + split_offsets[i];
      } else {
        in_vars[dim] = out_vars[dim];
      }
    }
    result.outputs[i].rep(out_vars) = result.inputs[0].rep(in_vars);
  }
  return result;
}

Node ConvertSliceNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Unexpected number of inputs for slice node " + node.name());
  }
  const int num_dims = inputs[0].shape.type().tensor_type().shape().dim_size();

  std::vector<int> axes;
  std::vector<int> ends;
  std::vector<int> starts;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axes") {
      for (int axis : attr.ints()) {
        if (axis < 0 || axis >= num_dims) {
          throw std::invalid_argument(
              "Invalid axis for slice node " + node.name());
        }
        axes.push_back(axis);
      }
    }
    if (attr.name() == "ends") {
      for (int index : attr.ints()) {
        ends.push_back(index);
      }
    }
    if (attr.name() == "starts") {
      for (int index : attr.ints()) {
        starts.push_back(index);
      }
    }
  }

  if (ends.size() != starts.size()) {
    throw std::invalid_argument(
        "Inconsistent starts/ends for slice node " + node.name());
  }
  if (ends.size() > num_dims) {
    throw std::invalid_argument("Too many ends for slice node " + node.name());
  }
  if (axes.empty()) {
    for (int i = 0; i < starts.size(); ++i) {
      axes.push_back(i);
    }
  } else if (axes.size() != starts.size()) {
    throw std::invalid_argument(
        "Invalid axes/starts for slice node " + node.name());
  }

  std::unordered_map<int, std::pair<int, int>> extents;
  for (int i = 0; i < axes.size(); ++i) {
    int axis = axes[i];
    std::pair<int, int> extent = std::make_pair(starts[i], ends[i]);
    extents[axis] = extent;
  }

  Node result;

  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  std::vector<Halide::Var> tgt_indices;
  tgt_indices.resize(num_dims);

  std::vector<Halide::Expr> src_indices;
  result.outputs[0].shape = inputs[0].shape;

  for (int i = 0; i < num_dims; ++i) {
    if (extents.find(i) != extents.end()) {
      int start = extents[i].first;
      int end = extents[i].second;
      if (end < 0) {
        if (inputs[0]
                .shape.type()
                .tensor_type()
                .shape()
                .dim(i)
                .has_dim_value()) {
          end =
              inputs[0].shape.type().tensor_type().shape().dim(i).dim_value() +
              end;
        } else {
          throw std::invalid_argument(
              "negative end for unknown dimension of slice node " +
              node.name());
        }
      }

      if (inputs[0].shape.type().tensor_type().shape().dim(i).has_dim_value()) {
        if (start >=
            inputs[0].shape.type().tensor_type().shape().dim(i).dim_value()) {
          start =
              inputs[0].shape.type().tensor_type().shape().dim(i).dim_value();
        }
        if (end >=
            inputs[0].shape.type().tensor_type().shape().dim(i).dim_value()) {
          end = inputs[0].shape.type().tensor_type().shape().dim(i).dim_value();
        }
      }

      src_indices.push_back(tgt_indices[i] + start);

      result.outputs[0]
          .shape.mutable_type()
          ->mutable_tensor_type()
          ->mutable_shape()
          ->mutable_dim(i)
          ->set_dim_value(end - start);
    } else {
      src_indices.push_back(tgt_indices[i]);
    }
  }
  result.outputs[0].rep(tgt_indices) = inputs[0].rep(src_indices);

  return result;
}

Node ConvertPadNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one input for pad node " + node.name());
  }
  std::string mode = "constant";
  float value = 0.0f;
  std::vector<int> pads;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "mode") {
      mode = attr.s();
    }
    if (attr.name() == "value") {
      value = attr.f();
    }
    if (attr.name() == "pads") {
      for (int pad : attr.ints()) {
        pads.push_back(pad);
      }
    }
  }

  PaddingMode padding_mode = PaddingMode::CONSTANT;
  if (mode == "edge") {
    padding_mode = PaddingMode::EDGE;
  } else if (mode == "reflect") {
    padding_mode = PaddingMode::REFLECT;
  } else if (mode != "constant") {
    throw std::domain_error(
        "Unsupported " + mode + " padding type of node " + node.name());
  }

  const int num_dims = inputs[0].shape.type().tensor_type().shape().dim_size();
  if (pads.size() != 2 * num_dims) {
    throw std::invalid_argument(
        "Invalid pads specified for node " + node.name());
  }
  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].rep = GeneratePaddingExpr(
      inputs[0].rep,
      inputs[0].shape.type().tensor_type().shape(),
      value,
      pads,
      padding_mode);

  result.outputs[0].shape = inputs[0].shape;
  const int rank = inputs[0].shape.type().tensor_type().shape().dim_size();
  onnx::ValueInfoProto& shape = result.outputs[0].shape;
  for (int i = 0; i < rank; ++i) {
    int padding = pads[i] + pads[i + rank];
    if (padding != 0) {
      shape.mutable_type()
          ->mutable_tensor_type()
          ->mutable_shape()
          ->mutable_dim(i)
          ->clear_dim_param();
      if (shape.type().tensor_type().shape().dim(i).has_dim_value()) {
        shape.mutable_type()
            ->mutable_tensor_type()
            ->mutable_shape()
            ->mutable_dim(i)
            ->set_dim_value(
                shape.type().tensor_type().shape().dim(i).dim_value() +
                padding);
      }
    }
  }
  return result;
}

Node ConvertTransposeNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one input for transpose node " + node.name());
  }
  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);

  const Tensor& input = inputs[0];
  int rank = input.shape.type().tensor_type().shape().dim_size();
  if (rank <= 1) {
    // Nothing to do.
    result.outputs[0] = input;
    return result;
  }

  // Unless specified otherwise, reverse the dimensions.
  std::vector<int> permutation;
  for (int i = rank - 1; i >= 0; --i) {
    permutation.push_back(i);
  }
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "perm") {
      permutation.clear();
      for (int index : attr.ints()) {
        if (index >= rank) {
          throw std::invalid_argument(
              "invalid perm attribute for node " + node.name());
        }
        permutation.push_back(index);
      }
    }
  }

  if (permutation.size() != rank) {
    throw std::invalid_argument(
        "invalid permutation for transpose node " + node.name());
  }
  std::vector<Halide::Var> input_vars;
  input_vars.resize(rank);
  std::vector<Halide::Var> output_vars;
  for (int i = 0; i < rank; ++i) {
    output_vars.push_back(input_vars[permutation[i]]);
  }
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].rep(output_vars) = input.rep(input_vars);

  result.outputs[0].shape = input.shape;
  const onnx::TensorShapeProto& input_shape =
      input.shape.type().tensor_type().shape();
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();
  for (int i = 0; i < rank; ++i) {
    *output_shape->mutable_dim(i) = input_shape.dim(permutation[i]);
  }

  return result;
}

Node ConvertUnsqueezeNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one input for unsqueeze node " + node.name());
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);

  const onnx::TensorShapeProto& input_shape =
      inputs[0].shape.type().tensor_type().shape();
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();

  const int in_rank = input_shape.dim_size();
  std::unordered_set<int> dims_to_unsqueeze;

  // axis can be > input rank and we assign this to outermost dimensions.
  int outer_dims = 0;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axes") {
      for (int index : attr.ints()) {
        dims_to_unsqueeze.insert(index);
        if (index >= in_rank) {
          outer_dims++;
        }
      }
    }
  }
  if (dims_to_unsqueeze.empty()) {
    // No op.
    result.outputs[0] = inputs[0];
    return result;
  }

  std::vector<Halide::Expr> in_vars;
  std::vector<Halide::Var> out_vars;

  // axes < in_rank.
  for (int i = 0; i < in_rank; ++i) {
    Halide::Var v_i;
    in_vars.push_back(v_i);
    if (dims_to_unsqueeze.find(i) != dims_to_unsqueeze.end()) {
      output_shape->add_dim()->set_dim_value(1);
      out_vars.push_back(Halide::Var());
    }
    out_vars.push_back(v_i);
    *output_shape->add_dim() = input_shape.dim(i);
  }

  // axes > in_rank. assign to outer most axis.
  for (int i = 0; i < outer_dims; ++i) {
    out_vars.push_back(Halide::Var());
    output_shape->add_dim()->set_dim_value(1);
  }
  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      inputs[0].shape.type().tensor_type().elem_type());
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].rep(out_vars) = inputs[0].rep(in_vars);
  return result;
}

Node ConvertSqueezeNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 1) {
    throw std::invalid_argument(
        "Expected exactly one input for squeeze node " + node.name());
  }

  const Tensor& input = inputs[0];
  const int rank = input.shape.type().tensor_type().shape().dim_size();

  std::unordered_set<int> dims_to_squeeze;
  bool implicit = true;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axes") {
      for (int index : attr.ints()) {
        if (index >= rank) {
          throw std::invalid_argument(
              "invalid axes attribute for node " + node.name());
        }
        dims_to_squeeze.insert(index);
      }
      implicit = false;
    }
  }
  if (implicit) {
    for (int i = 0; i < rank; ++i) {
      const onnx::TensorShapeProto_Dimension& dim =
          input.shape.type().tensor_type().shape().dim(i);
      if (!dim.has_dim_value()) {
        throw std::invalid_argument(
            "Unknown dimension for input dim " + std::to_string(i) +
            " of tensor " + input.shape.name());
      }
      if (dim.dim_value() == 1) {
        dims_to_squeeze.insert(i);
      }
    }
  }

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  if (dims_to_squeeze.empty()) {
    // No op.
    result.outputs[0] = input;
    return result;
  }

  std::vector<Halide::Expr> input_vars;
  input_vars.resize(rank);
  std::vector<Halide::Var> output_vars;

  const onnx::TensorShapeProto& input_shape =
      input.shape.type().tensor_type().shape();
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();
  for (int i = 0; i < rank; ++i) {
    if (dims_to_squeeze.find(i) == dims_to_squeeze.end()) {
      output_vars.push_back(Halide::Var());
      input_vars[i] = output_vars.back();
      *output_shape->add_dim() = input_shape.dim(i);
    } else {
      input_vars[i] = 0;
    }
  }
  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      input.shape.type().tensor_type().elem_type());
  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].rep(output_vars) = input.rep(input_vars);

  return result;
}

Node ConvertReshapeNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 2) {
    throw std::invalid_argument(
        "Expected exactly two inputs for reshape node " + node.name());
  }

  const Tensor& input = inputs[0];
  const Tensor& new_shape = inputs[1];

  const onnx::TensorShapeProto& input_shape =
      input.shape.type().tensor_type().shape();
  const int input_rank = input_shape.dim_size();
  int64_t num_elems = 1;
  bool num_elems_known = false;
  for (int i = 0; i < input_rank; ++i) {
    if (input_shape.dim(i).has_dim_value()) {
      num_elems *= input_shape.dim(i).dim_value();
    } else {
      num_elems_known = false;
    }
  }

  if (new_shape.shape.type().tensor_type().shape().dim_size() != 1) {
    throw std::invalid_argument("invalid shape");
  }
  const int output_rank =
      new_shape.shape.type().tensor_type().shape().dim(0).dim_value();

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      inputs[0].shape.type().tensor_type().elem_type());
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();

  // The new_shape tensor is often a constant, so we can use it to determine
  // the actual shape of the output.
  bool new_shape_known = false;
  try {
    Halide::Func mutable_func = new_shape.rep;
    Halide::Buffer<int64_t> realized_shape = mutable_func.realize(output_rank);
    int unknown_dim = -1;
    int64_t known_size = 1;
    for (int i = 0; i < output_rank; ++i) {
      int dim = realized_shape(i);
      if (dim == -1) {
        unknown_dim = i;
        output_shape->add_dim();
      } else {
        output_shape->add_dim()->set_dim_value(dim);
        known_size *= dim;
      }
    }
    if (unknown_dim >= 0) {
      if (num_elems_known) {
        int64_t dim = num_elems / known_size;
        output_shape->mutable_dim(unknown_dim)->set_dim_value(dim);
        new_shape_known = true;
      }
    } else {
      new_shape_known = true;
    }
  } catch (...) {
    if (output_rank == 1 && num_elems_known) {
      // Infer the dim from the number of elements in the input.
      output_shape->add_dim()->set_dim_value(num_elems);
    } else {
      // Use symbolic shapes
      for (int i = 0; i < output_rank; ++i) {
        output_shape->add_dim()->set_dim_param("d" + std::to_string(i));
      }
    }
  }

  std::vector<Halide::Expr> output_strides(output_rank);
  output_strides[output_rank - 1] = 1;
  for (int i = output_rank - 2; i >= 0; --i) {
    Halide::Expr new_dim;
    if (new_shape_known) {
      new_dim = static_cast<int>(output_shape->dim(i + 1).dim_value());
    } else {
      new_dim = Halide::require(
          new_shape.rep(i + 1) > 0,
          new_shape.rep(i + 1),
          "-1 not supported here yet");
    }
    output_strides[i] = output_strides[i + 1] * Halide::cast<int>(new_dim);
  }

  std::vector<Halide::Var> output_coordinates(output_rank);
  Halide::Expr coeff_index = 0;
  for (int i = 0; i < output_rank; ++i) {
    coeff_index += output_coordinates[i] * output_strides[i];
  }
  std::vector<Halide::Expr> input_coordinates(input_rank);
  for (int i = input_rank - 1; i >= 0; --i) {
    Halide::Expr coord =
        coeff_index % static_cast<int>(input_shape.dim(i).dim_value());
    input_coordinates[i] = coord;
    coeff_index = (coeff_index - coord) /
        static_cast<int>(input_shape.dim(i).dim_value());
  }

  result.outputs[0].rep = FuncForNodeOutput(node, 0);
  result.outputs[0].rep(output_coordinates) = input.rep(input_coordinates);

  return result;
}

Node ConvertOneHotNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs) {
  if (inputs.size() != 3) {
    throw std::invalid_argument(
        "Expected exactly three inputs for OneHot node " + node.name());
  }

  const int rank = inputs[0].shape.type().tensor_type().shape().dim_size();
  int axis = rank;
  for (const auto& attr : node.attribute()) {
    if (attr.name() == "axis") {
      axis = attr.i();
      if (axis < 0) {
        axis = rank;
      }
    }
  }

  const Tensor& indices = inputs[0];
  const Tensor& depth = inputs[1];
  const Tensor& values = inputs[2];

  Node result;
  result.inputs = inputs;
  result.outputs.resize(1);
  result.outputs[0].rep = FuncForNodeOutput(node, 0);

  std::vector<Halide::Var> out_vars(rank + 1);
  std::vector<Halide::Var> in_vars(rank);
  for (int i = 0; i < std::min(rank, axis); ++i) {
    in_vars[i] = out_vars[i];
  }
  for (int i = axis; i < rank; ++i) {
    in_vars[i] = out_vars[i + 1];
  }
  Halide::Var selected = out_vars[axis];
  Halide::Expr d = depth.rep(0);
  Halide::Expr off_value = values.rep(0);
  Halide::Expr on_value = values.rep(1);

  result.outputs[0].rep(out_vars) =
      Halide::select(indices.rep(in_vars) == selected, on_value, off_value);

  result.outputs[0].shape = inputs[0].shape;
  result.outputs[0].shape.mutable_type()->mutable_tensor_type()->set_elem_type(
      values.shape.type().tensor_type().elem_type());
  onnx::TensorShapeProto* output_shape = result.outputs[0]
                                             .shape.mutable_type()
                                             ->mutable_tensor_type()
                                             ->mutable_shape();

  output_shape->add_dim();
  for (int i = rank; i > axis; --i) {
    *output_shape->mutable_dim(i) = output_shape->dim(i - 1);
  }
  output_shape->mutable_dim(axis)->clear_dim_param();
  // The depth tensor is often a constant, so let's try to get its value from
  // Halide.
  try {
    Halide::Func func = depth.rep;
    Halide::Buffer<int64_t> realized_depth = func.realize(1);
    output_shape->mutable_dim(axis)->set_dim_value(realized_depth(0));
  } catch (...) {
    output_shape->mutable_dim(axis)->clear_dim_value();
  }

  return result;
}

Node ConvertNode(
    const onnx::NodeProto& node,
    const std::vector<Tensor>& inputs,
    const std::string& device) {
  // Handle metadata operations
  if (node.op_type() == "Shape" || node.op_type() == "Size") {
    return ConvertMetadataNode(node, inputs);
  }
  // Start with nodes that require special handling.
  if (node.op_type() == "Gemm") {
    return ConvertGemmNode(node, inputs, device);
  }
  if (node.op_type() == "Conv") {
    return ConvertConvNode(node, inputs, device);
  }
  if (node.op_type().find("Reduce") == 0) {
    return ConvertReductionNode(node, inputs);
  }
  if (node.op_type() == "BatchNormalization") {
    return ConvertBatchNormNode(node, inputs);
  }
  if (node.op_type() == "Dropout") {
    return ConvertDropoutNode(node, inputs);
  }
  if (node.op_type().length() >= 6 &&
      node.op_type().find("Pool") == node.op_type().length() - 4) {
    return ConvertPoolingNode(node, inputs);
  }
  if (node.op_type() == "Softmax" || node.op_type() == "LogSoftmax") {
    return ConvertSoftmaxNode(node, inputs);
  }
  if (node.op_type() == "Concat") {
    return ConvertConcatNode(node, inputs);
  }
  if (node.op_type() == "Slice") {
    return ConvertSliceNode(node, inputs);
  }
  if(node.op_type() == "Split") {
    return ConvertSplitNode(node, inputs);
  }
  if (node.op_type() == "Pad") {
    return ConvertPadNode(node, inputs);
  }
  if (node.op_type() == "Transpose") {
    return ConvertTransposeNode(node, inputs);
  }
  if (node.op_type() == "Squeeze") {
    return ConvertSqueezeNode(node, inputs);
  }
  if (node.op_type() == "Unsqueeze") {
    return ConvertUnsqueezeNode(node, inputs);
  }
  if (node.op_type() == "Reshape") {
    return ConvertReshapeNode(node, inputs);
  }
  if (node.op_type() == "OneHot") {
    return ConvertOneHotNode(node, inputs);
  }
  if (node.op_type() == "Flatten") {
    return ConvertFlattenNode(node, inputs);
  }

  // Handle exponential linear units.
  if (node.op_type() == "Elu" || node.op_type() == "Selu" ||
      node.op_type() == "LeakyRelu") {
    return ConvertEluNode(node, inputs);
  }
  // Handle coefficient-wise operators.
  if (node.input_size() == 0) {
    return ConvertNullaryOpNode(node);
  } else if (node.input_size() == 1 && node.output_size() == 1) {
    return ConvertUnaryOpNode(node, inputs);
  } else if (node.input_size() == 2 && node.output_size() == 1) {
    return ConvertBinaryOpNode(node, inputs);
  } else if (node.input_size() > 2 && node.output_size() == 1) {
    return ConvertVariadicOpNode(node, inputs);
  }
  throw std::domain_error(node.op_type());
}

Halide::ImageParam EncodeAsImageParam(const onnx::ValueInfoProto& input) {
  Halide::Type t;
  switch (input.type().tensor_type().elem_type()) {
    case onnx::TensorProto_DataType_FLOAT:
      t = Halide::type_of<float>();
      break;
    case onnx::TensorProto_DataType_UINT8:
      t = Halide::type_of<uint8_t>();
      break;
    case onnx::TensorProto_DataType_INT8:
      t = Halide::type_of<int8_t>();
      break;
    case onnx::TensorProto_DataType_UINT16:
      t = Halide::type_of<uint16_t>();
      break;
    case onnx::TensorProto_DataType_INT16:
      t = Halide::type_of<int16_t>();
      break;
    case onnx::TensorProto_DataType_INT32:
      t = Halide::type_of<int32_t>();
      break;
    case onnx::TensorProto_DataType_INT64:
      t = Halide::type_of<int64_t>();
      break;
    case onnx::TensorProto_DataType_BOOL:
      t = Halide::type_of<bool>();
      break;
    case onnx::TensorProto_DataType_DOUBLE:
      t = Halide::type_of<double>();
      break;
    case onnx::TensorProto_DataType_UINT32:
      t = Halide::type_of<uint32_t>();
      break;
    case onnx::TensorProto_DataType_UINT64:
      t = Halide::type_of<uint64_t>();
      break;
    case onnx::TensorProto_DataType_STRING:
      throw std::domain_error("string can't be used as model input type");
    case onnx::TensorProto_DataType_FLOAT16:
      throw std::domain_error("float16 aren't supported as model input type");
    case onnx::TensorProto_DataType_BFLOAT16:
      throw std::domain_error("bfloat16 aren't supported as model input type");
    default:
      throw std::domain_error("unexpected model input type");
  }
  int num_dims = input.type().tensor_type().shape().dim_size();
  Halide::ImageParam result(t, num_dims, SanitizeName(input.name()));

  // Encode the input shape as bounds on the dimensions for the autoscheduler.
  const onnx::TensorShapeProto& dims = input.type().tensor_type().shape();
  for (int i = 0; i < num_dims; ++i) {
    if (dims.dim(i).has_dim_value()) {
      int dim = dims.dim(i).dim_value();
      if (dim <= 0) {
        throw std::invalid_argument("Invalid shape for input " + input.name());
      }
      result.dim(i).set_bounds_estimate(0, dim);
    } else {
      // Dimension is unknown, just make a guess.
      result.dim(i).set_bounds_estimate(0, 1000);
    }
  }

  return result;
}

onnx::TypeProto MergeTypeInfo(
    const onnx::TypeProto& v1,
    const onnx::TypeProto& v2,
    const std::string& name) {
  onnx::TypeProto result = v1.has_tensor_type() ? v1 : v2;
  if (v1.has_tensor_type() && v2.has_tensor_type()) {
    if (v1.tensor_type().elem_type() != v2.tensor_type().elem_type()) {
      throw std::invalid_argument(
          "Inconsistent data types detected for tensor " + name);
    }
    if (!v1.tensor_type().has_shape()) {
      *result.mutable_tensor_type()->mutable_shape() = v2.tensor_type().shape();
    } else if (!v2.tensor_type().has_shape()) {
      *result.mutable_tensor_type()->mutable_shape() = v1.tensor_type().shape();
    } else {
      const onnx::TensorShapeProto& v1_shape = v1.tensor_type().shape();
      const onnx::TensorShapeProto& v2_shape = v2.tensor_type().shape();
      if (v1_shape.dim_size() != v2_shape.dim_size()) {
        throw std::invalid_argument(
            "Inconsistent ranks detected for tensor " + name);
      }
      onnx::TensorShapeProto* result_shape =
          result.mutable_tensor_type()->mutable_shape();
      for (int i = 0; i < v1_shape.dim_size(); ++i) {
        if (v1_shape.dim(i).has_dim_value() &&
            v2_shape.dim(i).has_dim_value() &&
            (v1_shape.dim(i).dim_value() != v2_shape.dim(i).dim_value())) {
          throw std::invalid_argument(
              "Inconsistent shapes detected for tensor " + name + " " +
              v1.DebugString() + " vs " + v2.DebugString());
        }
        if (!result_shape->dim(i).has_dim_value()) {
          if (v1_shape.dim(i).has_dim_value()) {
            result_shape->mutable_dim(i)->set_dim_value(
                v1_shape.dim(i).dim_value());
          } else if (v2_shape.dim(i).has_dim_value()) {
            result_shape->mutable_dim(i)->set_dim_value(
                v2_shape.dim(i).dim_value());
          }
        }
      }
    }
  }

  return result;
}

Model ConvertModel(const onnx::ModelProto& model, const std::string& device) {
  Model result;
  std::unordered_map<std::string, Tensor>& reps = result.tensors;

  // Encode the inputs as Halide ImageParam.
  for (const auto& input : model.graph().input()) {
    Halide::ImageParam p(EncodeAsImageParam(input));
    result.inputs[input.name()] = p;
    reps[input.name()] = Tensor{input, p};
  }

  // Now convert the model nodes. The nodes are always stored in topological
  // order in the ONNX model.
  for (const onnx::NodeProto& node : model.graph().node()) {
    std::vector<Tensor> inputs;
    for (const std::string& input_name : node.input()) {
      if (input_name.empty()) {
        inputs.push_back(Tensor());
      } else {
        inputs.push_back(reps.at(input_name));
      }
    }
    Node n = ConvertNode(node, inputs, device);

    for (int i = 0; i < node.output_size(); ++i) {
      const std::string& output_name = node.output(i);
      const Tensor& output_val = n.outputs[i];
      reps[output_name] = Tensor{output_val.shape, output_val.rep};
    }
  }

  // Process the default values if any.
  for (const onnx::TensorProto& constant : model.graph().initializer()) {
    result.default_values[constant.name()] = BuildFromConstant(constant);
  }

  // Last but not least, extract the model outputs.
  for (const auto& output : model.graph().output()) {
    if (reps.find(output.name()) == reps.end()) {
      throw std::invalid_argument(
          "Output " + output.name() +
          " isn't generated by any node from the graph");
    }
    Tensor& t = reps.at(output.name());

    // Merge type info.
    *t.shape.mutable_type() =
        MergeTypeInfo(output.type(), t.shape.type(), output.name());

    // Encode the output shape as bounds on the value of the args to help the
    // the autoscheduler.
    Halide::Func& f = t.rep;
    std::vector<Halide::Var> args = f.args();
    const onnx::TensorShapeProto& dims = t.shape.type().tensor_type().shape();

    if (args.size() > dims.dim_size()) {
      throw std::domain_error("Invalid dimensions for output " + output.name());
    }
    for (int i = 0; i < args.size(); ++i) {
      if (dims.dim(i).has_dim_value()) {
        int dim = dims.dim(i).dim_value();
        f.estimate(args[i], 0, dim);
      } else {
        // Dimension is unknown, make a guess
        f.estimate(args[i], 0, 1000);
      }
    }
    result.outputs[output.name()] = t;
  }

  return result;
}
