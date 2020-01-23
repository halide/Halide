#include "onnx_converter.h"
#include <climits>
#include <exception>
#include <math.h>
#include <unordered_set>

static Halide::Expr div_up(Halide::Expr num, int denom) {
    return Halide::Internal::simplify((num + denom - 1) / denom);
}
static Halide::Expr div_up(Halide::Expr num, Halide::Expr denom) {
    return Halide::Internal::simplify((num + denom - 1) / denom);
}
static Halide::Expr div_down(Halide::Expr num, int denom) {
    return Halide::Internal::simplify(num / denom);
}

namespace {
class FuncCallInliner : public Halide::Internal::IRMutator {
    Halide::Expr visit(const Halide::Internal::Call *op) override {
        if (op->call_type != Halide::Internal::Call::Halide) {
            return Halide::Internal::IRMutator::visit(op);
        }

        assert(op->func.defined());

        // Mutate the args
        std::vector<Halide::Expr> args(op->args.size());
        for (size_t i = 0; i < args.size(); i++) {
            args[i] = mutate(op->args[i]);
        }
        // Grab the body
        Halide::Internal::Function func(op->func);
        Halide::Expr body =
            qualify(func.name() + ".", func.values()[op->value_index]);

        // Bind the args using Let nodes
        const std::vector<std::string> func_args = func.args();
        for (size_t i = 0; i < args.size(); i++) {
            if (is_const(args[i]) || args[i].as<Halide::Internal::Variable>()) {
                body = substitute(func.name() + "." + func_args[i], args[i], body);
            } else {
                body = Halide::Internal::Let::make(
                    func.name() + "." + func_args[i], args[i], body);
            }
        }

        return body;
    }
};
}  // namespace

Halide::Expr inline_func_call(Halide::Expr e) {
    FuncCallInliner inliner;

    Halide::Expr r_old = Halide::Internal::simplify(e);
    Halide::Expr r = inliner.mutate(r_old);

    while (!r.same_as(r_old)) {
        r_old = Halide::Internal::simplify(r);
        r = inliner.mutate(r_old);
    }

    return r;
}

std::string sanitize_name(const std::string &name) {
    std::string result = name;
    assert(!name.empty());
    // Replace dot with underscores since dots aren't allowed in Halide names.
    std::replace(result.begin(), result.end(), '.', '_');
    return result;
}

std::string name_for_node(
    const onnx::NodeProto &node,
    const std::string &suffix) {
    if (!node.name().empty()) {
        return sanitize_name(node.name() + suffix);
    }
    if (node.output_size() > 0) {
        return sanitize_name(node.output(0) + suffix);
    }
    return sanitize_name(suffix);
}

Halide::Func func_for_node_output(const onnx::NodeProto &node, int output_id) {
    assert(node.output_size() > output_id);
    return Halide::Func(sanitize_name(node.output(output_id)));
}

static void convert_subgraph(
    const onnx::GraphProto &graph,
    std::unordered_map<std::string, Tensor> &reps,
    std::vector<Halide::Expr> &requirements) {
    // The nodes are always stored in topological order in the ONNX model.
    for (const onnx::NodeProto &node : graph.node()) {
        std::vector<Tensor> inputs;
        for (const std::string &input_name : node.input()) {
            if (input_name.empty()) {
                inputs.push_back(Tensor());
            } else {
                inputs.push_back(reps.at(input_name));
            }
        }
        Node n = convert_node(node, inputs);

        for (int i = 0; i < node.output_size(); ++i) {
            const std::string &output_name = node.output(i);
            if (!output_name.empty()) {
                const Tensor &output_val = n.outputs[i];
                reps[output_name] = output_val;
            }
        }
        for (int i = 0; i < n.requirements.size(); ++i) {
            requirements.push_back(n.requirements[i]);
        }
    }
}

Halide::Expr generate_cast_expr(
    const Halide::Expr &input,
    const onnx::TensorProto::DataType type,
    const std::string &caller_name) {
    switch (type) {
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
    default:
        throw std::domain_error(
            "Unsupported or unknown target type for node " + caller_name);
    }
}

Halide::Expr generate_scale_expr(
    const Halide::Expr &input,
    const onnx::NodeProto &node) {
    float scale = 1.0f;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "scale") {
            scale = attr.f();
        }
    }
    return input * scale;
}

template<typename DataType>
Halide::Func encode_buffer_as_func(
    const Halide::Buffer<DataType> &vals,
    const std::vector<int> &dims,
    const std::string &name) {
    Halide::Func result(name);

    if (dims.size() == 0) {
        result() = Halide::Expr(vals());
    } else if (dims.size() == 1 && dims[0] > 0 && dims[0] <= 10) {
        Halide::Var var;
        Halide::Expr res = Halide::Expr(vals(0));
        for (int i = 1; i < dims[0]; ++i) {
            res = Halide::select(var == i, Halide::Expr(vals(i)), res);
        }
        result(var) = res;
    } else if (
        dims.size() == 2 && dims[0] * dims[1] > 0 && dims[0] * dims[1] <= 16) {
        Halide::Var var1, var2;
        Halide::Expr res = Halide::Expr(vals(0, 0));
        for (int i = 0; i < dims[0]; ++i) {
            for (int j = 0; j < dims[1]; ++j) {
                res = Halide::select(
                    var1 == i && var2 == j, Halide::Expr(vals(i, j)), res);
            }
        }
        result(var1, var2) = res;
    } else {
        result(Halide::_) = vals(Halide::_);
    }
    return result;
}

#define BUILD_CONSTANT_EXPR(DataType, FieldName, NodeName)                 \
    Halide::Buffer<DataType> val(reversed_dims);                           \
    int constant_loc = 0;                                                  \
    val.for_each_value([&, constant_loc](DataType &v) mutable {            \
        if (value.FieldName##_data_size() > 0) {                           \
            v = value.FieldName##_data(constant_loc);                      \
        } else {                                                           \
            const char *raw =                                              \
                value.raw_data().data() + sizeof(DataType) * constant_loc; \
            v = *reinterpret_cast<const DataType *>(raw);                  \
        }                                                                  \
        constant_loc += 1;                                                 \
    });                                                                    \
    val.transpose(dim_order);                                              \
    result.rep = encode_buffer_as_func(val, dims, NodeName);               \
    static_cast<void>(0)

Tensor build_from_constant(
    const onnx::TensorProto &value,
    const std::string &name) {
    Tensor result;

    // The onnx layout is the reverse of the halide layout.
    std::vector<int> dims;
    for (int64_t dim : value.dims()) {
        result.shape.push_back(static_cast<int>(dim));
        dims.push_back(dim);
    }
    std::vector<int> reversed_dims(dims.rbegin(), dims.rend());
    std::vector<int> dim_order(dims.size());
    std::iota(dim_order.rbegin(), dim_order.rend(), 0);
    result.type = static_cast<onnx::TensorProto::DataType>(value.data_type());

    switch (value.data_type()) {
    case onnx::TensorProto_DataType_FLOAT: {
        BUILD_CONSTANT_EXPR(float, float, name);
        break;
    }
    case onnx::TensorProto_DataType_DOUBLE: {
        BUILD_CONSTANT_EXPR(double, double, name);
        break;
    }
    case onnx::TensorProto_DataType_INT32: {
        BUILD_CONSTANT_EXPR(int32_t, int32, name);
        break;
    }
    case onnx::TensorProto_DataType_INT64: {
        BUILD_CONSTANT_EXPR(int64_t, int64, name);
        break;
    }
    case onnx::TensorProto_DataType_UINT32: {
        BUILD_CONSTANT_EXPR(uint32_t, uint64, name);
        break;
    }
    case onnx::TensorProto_DataType_UINT64: {
        BUILD_CONSTANT_EXPR(uint64_t, uint64, name);
        break;
    }
    case onnx::TensorProto_DataType_INT8: {
        BUILD_CONSTANT_EXPR(int8_t, int32, name);
        break;
    }
    case onnx::TensorProto_DataType_UINT8: {
        BUILD_CONSTANT_EXPR(uint8_t, int32, name);
        break;
    }
    case onnx::TensorProto_DataType_INT16: {
        BUILD_CONSTANT_EXPR(int16_t, int32, name);
        break;
    }
    case onnx::TensorProto_DataType_UINT16: {
        BUILD_CONSTANT_EXPR(uint16_t, int32, name);
        break;
    }
    case onnx::TensorProto_DataType_BOOL: {
        BUILD_CONSTANT_EXPR(bool, int32, name);
        break;
    }
    default:
        throw std::domain_error("Unsupported data type for constant");
    }
    return result;
}
#undef BUILD_CONSTANT_EXPR

Node convert_nullary_op_node(const onnx::NodeProto &node) {
    Node result;

    bool found_value = false;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "value") {
            const onnx::TensorProto &value = attr.t();
            result.outputs.resize(1);
            Tensor &out = result.outputs[0];
            out = build_from_constant(value, name_for_node(node, ""));
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

Node convert_unary_op_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    assert(inputs.size() == 1);

    Node result;
    result.inputs = inputs;
    const Tensor &in = result.inputs[0];

    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    out.shape = inputs[0].shape;
    out.type = inputs[0].type;
    out.rep = func_for_node_output(node, 0);

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
        int tgt_type = onnx::TensorProto_DataType_UNDEFINED;
        for (const auto &attr : node.attribute()) {
            if (attr.name() == "to") {
                tgt_type = attr.i();
                break;
            }
        }
        out.type = static_cast<onnx::TensorProto::DataType>(tgt_type);
        out.rep(Halide::_) =
            generate_cast_expr(in.rep(Halide::_), out.type, node.name());
    } else if (node.op_type() == "Ceil") {
        out.rep(Halide::_) = Halide::ceil(in.rep(Halide::_));
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
        out.type = onnx::TensorProto_DataType_BOOL;
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
        out.rep(Halide::_) = generate_scale_expr(in.rep(Halide::_), node);
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

Node convert_binary_op_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    assert(inputs.size() == 2);

    Node result;
    result.inputs = inputs;
    const Tensor &in1 = result.inputs[0];
    const Tensor &in2 = result.inputs[1];

    const std::vector<Halide::Expr> &in1_shape = in1.shape;
    const int rank_in1 = in1_shape.size();
    std::vector<Halide::Expr> in1_vars;
    in1_vars.resize(rank_in1);
    const std::vector<Halide::Expr> &in2_shape = in2.shape;
    const int rank_in2 = in2_shape.size();
    std::vector<Halide::Expr> in2_vars;
    in2_vars.resize(rank_in2);
    const int rank = std::max(rank_in1, rank_in2);
    std::vector<Halide::Var> out_vars;
    out_vars.resize(rank);

    std::vector<Halide::Expr> out_shape;
    out_shape.resize(rank);
    for (int i = 1; i <= rank; ++i) {
        out_shape[rank - i] = 0;
        if (i <= rank_in1) {
            Halide::Expr max_index = Halide::Internal::simplify(
                Halide::cast<int32_t>(in1_shape[rank_in1 - i] - 1));
            in1_vars[rank_in1 - i] = Halide::clamp(out_vars[rank - i], 0, max_index);
            out_shape[rank - i] = Halide::Internal::simplify(
                Halide::max(out_shape[rank - i], in1_shape[rank_in1 - i]));
        }
        if (i <= rank_in2) {
            Halide::Expr max_index = Halide::Internal::simplify(
                Halide::cast<int32_t>(in2_shape[rank_in2 - i] - 1));
            in2_vars[rank_in2 - i] = Halide::clamp(out_vars[rank - i], 0, max_index);
            out_shape[rank - i] = Halide::Internal::simplify(
                Halide::max(out_shape[rank - i], in2_shape[rank_in2 - i]));
        }
    }

    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    out.shape = out_shape;

    out.rep = func_for_node_output(node, 0);
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
    } else if (node.op_type() == "PRelu") {
        // input := in1.rep(in1_vars), alpha := in2.rep(in1_vars)
        out.rep(out_vars) = Halide::select(
            in1.rep(in1_vars) >= 0.0f,
            in1.rep(in1_vars),
            in2.rep(in2_vars) * in1.rep(in1_vars));
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
        out.type = onnx::TensorProto_DataType_BOOL;
    } else {
        out.type = inputs[0].type;
    }

    return result;
}

Node convert_variadic_op_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    assert(!inputs.empty());
    Node result;
    result.inputs = inputs;

    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    out.shape = inputs[0].shape;
    out.type = inputs[0].type;
    out.rep = func_for_node_output(node, 0);
    out.rep(Halide::_) = inputs[0].rep(Halide::_);

    for (int i = 1; i < inputs.size(); ++i) {
        const Tensor &in = result.inputs[i];
        // TODO: we don't support broadcasting yet: if broadcasting is needed
        // halide will generate a cryptic error.
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

Node convert_metadata_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one input for node " + node.name());
    }
    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
    const std::vector<Halide::Expr> &input_shape = inputs[0].shape;

    if (node.op_type() == "Size") {
        Halide::Expr num_elements = 1;
        for (int i = 0; i < input_shape.size(); ++i) {
            num_elements *= input_shape[i];
        }
        result.outputs[0].rep() = Halide::cast<int64_t>(num_elements);
    } else if (node.op_type() == "Shape") {
        Halide::Var var;
        Halide::Expr res = Halide::Expr(static_cast<int64_t>(0));
        for (int i = 0; i < input_shape.size(); ++i) {
            res =
                Halide::select(var == i, Halide::cast<int64_t>(input_shape[i]), res);
        }
        result.outputs[0].rep(var) = res;
        result.outputs[0].shape.push_back(static_cast<int>(input_shape.size()));
    } else {
        throw std::domain_error(
            "Unsupported op type " + node.op_type() + " for node " + node.name());
    }
    result.outputs[0].type = onnx::TensorProto_DataType_INT64;
    return result;
}

Node convert_matmul_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 2) {
        throw std::invalid_argument(
            "Matmul requires 2 inputs, but node " + node.name() + " has " +
            std::to_string(inputs.size()));
    }

    const Tensor &A = inputs[0];
    const Tensor &B = inputs[1];
    const int a_rank = A.shape.size();
    const int b_rank = B.shape.size();
    if (a_rank == 0 || b_rank == 0) {
        throw std::invalid_argument(
            "Matmul doesn't support scalar inputs for node " + node.name());
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    out.type = A.type;
    out.rep = func_for_node_output(node, 0);

    const int k_dim_a = a_rank - 1;
    const int k_dim_b = std::max(b_rank - 2, 0);
    Halide::RDom k(0, A.shape[k_dim_a], name_for_node(node, "_matmul_rdom"));
    result.requirements.push_back(A.shape[k_dim_a] == B.shape[k_dim_b]);

    if (a_rank == 1 && b_rank == 1) {
        out.rep() += A.rep(k) * B.rep(k);
    } else if (a_rank == 1) {
        out.shape.insert(out.shape.begin(), B.shape.begin(), B.shape.end() - 2);
        out.shape.push_back(B.shape.back());
        Halide::Var j("j");
        out.rep(Halide::_, j) += A.rep(k) * B.rep(Halide::_, k, j);
    } else if (b_rank == 1) {
        out.shape.insert(out.shape.begin(), A.shape.begin(), A.shape.end() - 1);
        out.rep(Halide::_) += A.rep(Halide::_, k) * B.rep(k);
    } else {
        const int out_rank = std::max(a_rank, b_rank);
        out.shape.resize(out_rank);
        out.shape[out_rank - 1] = B.shape[k_dim_b + 1];
        out.shape[out_rank - 2] = A.shape[k_dim_a - 1];
        std::vector<Halide::Expr> a_exprs(a_rank);
        std::vector<Halide::Expr> b_exprs(b_rank);
        std::vector<Halide::Var> out_vars(out_rank);
        a_exprs[a_rank - 1] = k;
        a_exprs[a_rank - 2] = out_vars[out_rank - 2];
        b_exprs[b_rank - 1] = out_vars[out_rank - 1];
        b_exprs[b_rank - 2] = k;
        for (int i = 3; i <= out_rank; ++i) {
            if (a_rank - i >= 0 && b_rank - i >= 0) {
                result.requirements.push_back(
                    A.shape[a_rank - i] == B.shape[b_rank - i] ||
                    A.shape[a_rank - i] == 1 || B.shape[b_rank - i] == 1);
                out.shape[out_rank - i] =
                    Halide::max(A.shape[a_rank - i], B.shape[b_rank - i]);
                Halide::Expr max_a = Halide::Internal::simplify(
                    Halide::cast<int32_t>(A.shape[a_rank - i] - 1));
                Halide::Expr max_b = Halide::Internal::simplify(
                    Halide::cast<int32_t>(B.shape[b_rank - i] - 1));
                a_exprs[a_rank - i] = Halide::clamp(out_vars[out_rank - i], 0, max_a);
                b_exprs[b_rank - i] = Halide::clamp(out_vars[out_rank - i], 0, max_b);
            } else if (a_rank - i >= 0) {
                out.shape[out_rank - i] = A.shape[a_rank - i];
                a_exprs[a_rank - i] = out_vars[out_rank - i];
            } else {
                out.shape[out_rank - i] = B.shape[b_rank - i];
                b_exprs[b_rank - i] = out_vars[out_rank - i];
            }
        }
        out.rep(out_vars) += A.rep(a_exprs) * B.rep(b_exprs);
    }
    return result;
}

Node convert_gemm_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() < 2 || inputs.size() > 3) {
        throw std::invalid_argument(
            "Gemm requires 2 or 3 inputs, but node " + node.name() + " has " +
            std::to_string(inputs.size()));
    }
    Node result;
    result.inputs = inputs;
    const Tensor &A = result.inputs[0];
    const Tensor &B = result.inputs[1];

    bool transposeA = false;
    bool transposeB = false;
    float alpha = 1.0f;
    float beta = 1.0f;
    for (const auto &attr : node.attribute()) {
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
    Halide::Expr dim_i = transposeA ? A.shape[1] : A.shape[0];
    Halide::Expr dim_j = transposeB ? B.shape[0] : B.shape[1];
    Halide::Expr dim_k_from_a = transposeA ? A.shape[0] : A.shape[1];
    Halide::Expr dim_k_from_b = transposeB ? B.shape[1] : B.shape[0];

    result.requirements.push_back(dim_k_from_a == dim_k_from_b);
    Halide::Expr dim_k = dim_k_from_a;

    Halide::Var i, j;
    Halide::RDom k(0, dim_k, name_for_node(node, "_gemm_rdom"));

    // Check shapes
    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    out.type = A.type;
    out.shape.push_back(dim_i);
    out.shape.push_back(dim_j);
    out.rep = func_for_node_output(node, 0);

    // Add bias if present
    if (inputs.size() == 3) {
        const Tensor &C = result.inputs[2];
        const std::vector<Halide::Expr> &shape_of_c = C.shape;
        switch (shape_of_c.size()) {
        case 0:
            out.rep(i, j) = beta * C.rep();
            break;
        case 1: {
            Halide::Expr max_index = Halide::Internal::simplify(
                Halide::cast<int32_t>(shape_of_c[0] - 1));
            out.rep(i, j) = beta * C.rep(Halide::clamp(j, 0, max_index));
            break;
        }
        case 2: {
            Halide::Expr max_i = Halide::Internal::simplify(
                Halide::cast<int32_t>(shape_of_c[0] - 1));
            Halide::Expr max_j = Halide::Internal::simplify(
                Halide::cast<int32_t>(shape_of_c[1] - 1));
            out.rep(i, j) = beta *
                            C.rep(Halide::clamp(i, 0, max_i), Halide::clamp(j, 0, max_j));
            break;
        }
        default:
            throw std::invalid_argument("invalid rank for bias tensor " + C.name);
        }
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

    return result;
}

enum class PaddingMode { CONSTANT,
                         EDGE,
                         REFLECT };

Halide::Func generate_padding_expr(
    Halide::Func input,
    const std::vector<Halide::Expr> &input_shape,
    Halide::Expr padding_val,
    const std::vector<Halide::Expr> &pads,
    const PaddingMode mode = PaddingMode::CONSTANT) {
    // Number of leading dimensions that are not to be padded.
    const int rank = input_shape.size();
    const int skip_dims = rank - pads.size() / 2;
    assert(skip_dims >= 0);

    // Pad the input with zeros as needed.
    std::vector<std::pair<Halide::Expr, Halide::Expr>> padding_extents;
    bool maybe_has_padding = false;
    for (int i = 0; i < rank - skip_dims; ++i) {
        Halide::Expr pad_before = pads[i];
        Halide::Expr pad_after = input_shape[i + skip_dims] + pad_before - 1;
        padding_extents.emplace_back(pad_before, pad_after);
        const int64_t *p1 = Halide::Internal::as_const_int(pad_before);
        const int64_t *p2 =
            Halide::Internal::as_const_int(pads[rank - skip_dims - i]);
        if (!p1 || *p1 != 0 || !p2 || *p2 != 0) {
            maybe_has_padding = true;
        }
    }

    if (!maybe_has_padding) {
        return input;
    }
    std::vector<Halide::Var> vars(rank);
    std::vector<Halide::Expr> input_vars(rank);
    for (int i = 0; i < skip_dims; ++i) {
        input_vars[i] = vars[i];
    }
    Halide::Expr pad = Halide::cast<bool>(false);
    for (int i = skip_dims; i < rank; ++i) {
        const std::pair<Halide::Expr, Halide::Expr> &paddings =
            padding_extents[i - skip_dims];
        Halide::Expr pad_before = vars[i] < paddings.first;
        assert(pad_before.type().is_bool());
        Halide::Expr pad_after = vars[i] > paddings.second;
        assert(pad_after.type().is_bool());
        pad = pad || pad_before;
        pad = pad || pad_after;
        assert(pad.type().is_bool());
        if (mode == PaddingMode::CONSTANT || mode == PaddingMode::EDGE) {
            input_vars[i] =
                Halide::clamp(vars[i] - paddings.first, 0, Halide::cast<int32_t>(input_shape[i] - 1));
        } else if (mode == PaddingMode::REFLECT) {
            Halide::Expr pad_size = paddings.second - paddings.first + 1;
            Halide::Expr mirror_before = (paddings.first - vars[i]) % pad_size;
            Halide::Expr mirror_after =
                pad_size - ((vars[i] - paddings.second) % pad_size) - 1;
            input_vars[i] = Halide::clamp(
                Halide::select(
                    pad_before,
                    mirror_before,
                    Halide::select(
                        pad_after, mirror_after, Halide::cast<int32_t>(vars[i] - paddings.first))),
                0,
                input_shape[i] - 1);
        }
    }

    Halide::Func padded_input(input.name() + "_padded");
    if (mode == PaddingMode::CONSTANT) {
        padded_input(vars) = Halide::select(pad, padding_val, input(input_vars));
    } else if (mode == PaddingMode::EDGE || mode == PaddingMode::REFLECT) {
        padded_input(vars) = input(input_vars);
    }
    return padded_input;
}

Halide::Func
direct_conv(const Tensor &W, const Halide::Func &input, int rank, int groups) {
    std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
    for (int i = 1; i < rank; ++i) {
        extents.emplace_back(0, W.shape[i]);
    }

    Halide::RDom rdom(extents, input.name() + "_conv_rdom");
    std::vector<Halide::Var> out_vars(rank);
    std::vector<Halide::Expr> x_vars(rank);
    std::vector<Halide::Expr> w_vars(rank);

    if (groups != 1) {
        Halide::Expr group_size = W.shape[0] / groups;
        Halide::Expr group_id = out_vars[1] / group_size;
        Halide::Expr group_size2 = W.shape[1];
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

    Halide::Func direct_conv(input.name() + "_direct_conv");
    direct_conv(out_vars) =
        Halide::sum(input(x_vars) * W.rep(w_vars), input.name() + "_kernel");
    return direct_conv;
}

template<int m, int r>
struct Filters {};

template<>
struct Filters<2, 3> {
    static float *GetBFilter() {
        return const_cast<float *>(&BFilter[0][0]);
    }
    static float *GetGFilter() {
        return const_cast<float *>(&GFilter[0][0]);
    }
    static float *GetAFilter() {
        return const_cast<float *>(&AFilter[0][0]);
    }

private:
    static constexpr float BFilter[4][4] = {{1, 0, -1, 0},
                                            {0, 1, 1, 0},
                                            {0, -1, 1, 0},
                                            {0, 1, 0, -1}};
    static constexpr float GFilter[3][4] = {{1, 0.5, 0.5, 0},
                                            {0, 0.5, -0.5, 0},
                                            {0, 0.5, 0.5, 1}};
    static constexpr float AFilter[2][4] = {{1, 1, 1, 0}, {0, 1, -1, -1}};
};
constexpr float Filters<2, 3>::BFilter[4][4];
constexpr float Filters<2, 3>::GFilter[3][4];
constexpr float Filters<2, 3>::AFilter[2][4];

template<>
struct Filters<4, 3> {
    static float *GetBFilter() {
        return const_cast<float *>(&BFilter[0][0]);
    }
    static float *GetGFilter() {
        return const_cast<float *>(&GFilter[0][0]);
    }
    static float *GetAFilter() {
        return const_cast<float *>(&AFilter[0][0]);
    }

private:
    static constexpr float BFilter[6][6] = {{4, 0, -5, 0, 1, 0},
                                            {0, -4, -4, 1, 1, 0},
                                            {0, 4, -4, -1, 1, 0},
                                            {0, -2, -1, 2, 1, 0},
                                            {0, 2, -1, -2, 1, 0},
                                            {0, 4, 0, -5, 0, 1}};
    static constexpr float GFilter[3][6] = {
        {0.25, -1.0 / 6, -1.0 / 6, 1.0 / 24, 1.0 / 24, 0},
        {0, -1.0 / 6, 1.0 / 6, 1.0 / 12, -1.0 / 12, 0},
        {0, -1.0 / 6, -1.0 / 6, 1.0 / 6, 1.0 / 6, 1}};
    static constexpr float AFilter[4][6] = {{1, 1, 1, 1, 1, 0},
                                            {0, 1, -1, 2, -2, 0},
                                            {0, 1, 1, 4, 4, 0},
                                            {0, 1, -1, 8, -8, 1}};
};
constexpr float Filters<4, 3>::BFilter[6][6];
constexpr float Filters<4, 3>::GFilter[3][6];
constexpr float Filters<4, 3>::AFilter[4][6];

template<int m, int r>
Halide::Func winograd_conv(const Tensor &W, const Halide::Func &input) {
    // We only support the case of a 3x3 convolution at the moment. The notation
    // is derived from the one used in the Winograd paper.
    const Halide::Func B = encode_buffer_as_func(
        Halide::Buffer<float>(Filters<m, r>::GetBFilter(), m + r - 1, m + r - 1),
        {m + r - 1, m + r - 1},
        std::string("winograd_b_filter_") + std::to_string(m) + "_" +
            std::to_string(r));

    const Halide::Func G = encode_buffer_as_func(
        Halide::Buffer<float>(Filters<m, r>::GetGFilter(), m + r - 1, r),
        {m + r - 1, r},
        std::string("winograd_g_filter_") + std::to_string(m) + "_" +
            std::to_string(r));

    const Halide::Func A = encode_buffer_as_func(
        Halide::Buffer<float>(Filters<m, r>::GetAFilter(), m + r - 1, m),
        {m + r - 1, m},
        std::string("winograd_a_filter_") + std::to_string(m) + "_" +
            std::to_string(r));

    Halide::Expr num_channels = W.shape[1];
    Halide::RDom dom1({{0, num_channels}}, input.name() + "_rdom1");
    Halide::RVar c_r = dom1;
    const Halide::RDom dom2({{0, r}, {0, r}}, input.name() + "_rdom2");
    Halide::RVar r1 = dom2[0];
    Halide::RVar r2 = dom2[1];
    const Halide::RDom dom3(
        {{0, m + r - 1}, {0, m + r - 1}}, input.name() + "_rdom3");
    Halide::RVar r3 = dom3[0];
    Halide::RVar r4 = dom3[1];
    const Halide::RDom dom4(
        {{0, m + r - 1}, {0, m + r - 1}}, input.name() + "_rdom4");
    Halide::RVar alpha_r = dom4[0];
    Halide::RVar beta_r = dom4[1];

    Halide::Var k, c, alpha, beta;
    Halide::Func U(input.name() + "_U");
    U(k, c, alpha, beta) = Halide::sum(
        G(alpha, r1) * W.rep(k, c, r1, r2) * G(beta, r2),
        input.name() + "_U_sum");

    Halide::Var b, x, y;
    Halide::Func V(input.name() + "_V");
    V(b, c, x, y, alpha, beta) = Halide::sum(
        B(r3, alpha) * input(b, c, x + r3, y + r4) * B(r4, beta),
        input.name() + "_B_sum");

    Halide::Func M(input.name() + "_M");
    M(b, k, x, y, alpha, beta) = Halide::sum(
        U(k, c_r, alpha, beta) * V(b, c_r, x, y, alpha, beta),
        input.name() + "_M_sum");

    Halide::Func winograd_conv(input.name() + "_winograd");
    winograd_conv(b, k, x, y) = Halide::sum(
        A(alpha_r, x % m) * M(b, k, (x / m) * m, (y / m) * m, alpha_r, beta_r) *
            A(beta_r, y % m),
        input.name() + "_winograd_sum");
    return winograd_conv;
}

Node convert_conv_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() < 2) {
        throw std::invalid_argument(
            "Conv requires 2 or 3 inputs, but node " + node.name() + " has " +
            std::to_string(inputs.size()));
    }
    const Tensor &X = inputs[0];
    const Tensor &W = inputs[1];

    const int rank = X.shape.size();
    if (rank != W.shape.size()) {
        throw std::invalid_argument(
            "Inconsistent ranks for input tensors of Conv node " + node.name() +
            ", input of rank " + std::to_string(rank) + " weights of rank " +
            std::to_string(W.shape.size()));
    }
    if (rank < 3) {
        throw std::invalid_argument(
            "Rank of input tensors for Conv node " + node.name() +
            " should be at least 3");
    }

    std::string padding = "NOTSET";
    int groups = 1;
    std::vector<int> kernel_shape;
    std::vector<int> dilations;
    std::vector<Halide::Expr> pads;
    std::vector<int> strides;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "auto_pad") {
            padding = attr.s();
        } else if (attr.name() == "group") {
            groups = attr.i();
        } else if (attr.name() == "dilations") {
            for (int axis : attr.ints()) {
                dilations.push_back(axis);
            }
        } else if (attr.name() == "kernel_shape") {
            for (int axis : attr.ints()) {
                kernel_shape.push_back(axis);
            }
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
            "Unsupported convolution padding " + padding + " for node " +
            node.name());
    }

    // Determine the shape of the output
    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);

    result.outputs[0].type = inputs[0].type;
    result.outputs[0].shape = inputs[0].shape;
    result.outputs[0].shape[1] = W.shape[0];
    for (int i = 2; i < rank; ++i) {
        Halide::Expr dim = X.shape[i] + pads[i - 2] + pads[rank + i - 4];
        dim -= (W.shape[i] - 1);
        dim = div_up(dim, strides[i - 2]);
        result.outputs[0].shape[i] = Halide::Internal::simplify(dim);
    }

    // Validate the kernel shape if specified
    if (!kernel_shape.empty() && kernel_shape.size() + 2 != rank) {
        throw std::invalid_argument(
            "Invalid kernel shape specified for node" + node.name());
    }
    for (int i = 0; i < kernel_shape.size(); ++i) {
        result.requirements.push_back(W.shape[i + 2] == kernel_shape[i]);
    }

    // Check if winograd can be used
    bool can_use_winograd = false;
    bool needs_extra_padding = false;
    int m[2] = {2, 2};
    if (groups == 1 && rank == 4) {
        bool supported_shape = true;
        for (int i = 2; i < rank; ++i) {
            const Halide::Expr w_shape_expr = Halide::Internal::simplify(W.shape[i]);
            const int64_t *dim = Halide::Internal::as_const_int(w_shape_expr);
            if (!dim || *dim != 3) {
                supported_shape = false;
                break;
            }

            const Halide::Expr out_shape_expr =
                Halide::Internal::simplify(result.outputs[0].shape[i]);
            dim = Halide::Internal::as_const_int(out_shape_expr);

            if (!dim || *dim % 2 != 0) {
                needs_extra_padding = true;
            } else if (dim && *dim % 4 == 0) {
                m[i - 2] = 4;
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
    Halide::Func padded_input = generate_padding_expr(X.rep, X.shape, 0, pads);

    // Convolve the input with the kernel
    Halide::Func basic_conv;
    if (can_use_winograd) {
        if (m[0] == 4 && m[1] == 4) {
            basic_conv = winograd_conv<4, 3>(W, padded_input);
        } else {
            // Double check that we can indeed use the 2,3 winograd transform.
            assert((m[0] == 2 || m[0] == 4) && (m[1] == 2 || m[1] == 4));
            basic_conv = winograd_conv<2, 3>(W, padded_input);
        }
    } else {
        basic_conv = direct_conv(W, padded_input, rank, groups);
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
    Halide::Func conv_no_bias(X.rep.name() + "_strided_conv");
    if (has_strides) {
        conv_no_bias(out_vars) = basic_conv(stride_vars);
    } else {
        conv_no_bias = basic_conv;
    }

    // Return the result after applying the bias if any.
    if (inputs.size() == 3) {
        result.outputs[0].rep = func_for_node_output(node, 0);
        result.outputs[0].rep(out_vars) =
            inputs[2].rep(out_vars[1]) + conv_no_bias(out_vars);
    } else {
        result.outputs[0].rep = conv_no_bias;
    }

    return result;
}

Node convert_reduction_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() < 1) {
        throw std::invalid_argument(
            "Too few inputs for reduction node " + node.name());
    }

    const int rank = inputs[0].shape.size();
    std::set<int> reduction_axes;
    bool keepdims = true;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axes") {
            for (int axis : attr.ints()) {
                if (axis < 0) {
                    axis += rank;
                }
                reduction_axes.insert(axis);
            }
        }
        if (attr.name() == "keepdims" && attr.i() == 0) {
            keepdims = false;
        }
    }

    const std::vector<Halide::Expr> &input_shape = inputs[0].shape;
    if (reduction_axes.empty()) {
        // This is used to specify a full reduction.
        for (int i = 0; i < input_shape.size(); ++i) {
            reduction_axes.insert(i);
        }
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
    std::vector<Halide::Expr> input_vars;
    std::vector<Halide::Expr> output_vars;

    result.outputs[0].type = inputs[0].type;
    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;

    Halide::Expr num_reduced_elems = 1;
    std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
    for (int i = 0; i < input_shape.size(); ++i) {
        if (reduction_axes.find(i) != reduction_axes.end()) {
            Halide::Expr in_dim = inline_func_call(input_shape[i]);
            extents.emplace_back(0, in_dim);
            num_reduced_elems *= in_dim;
        }
    }

    Halide::RDom rdom(extents, name_for_node(node, "_rdom"));
    int current_reduction_dim = 0;
    for (int i = 0; i < input_shape.size(); ++i) {
        if (reduction_axes.find(i) != reduction_axes.end()) {
            input_vars.push_back(rdom[current_reduction_dim++]);
            if (keepdims) {
                // Create a dimension that will be of size 1. Is there a way to let
                // Halide optimize for this ?
                Halide::Var var;
                output_vars.push_back(var);
                output_shape.push_back(1);
            }
        } else {
            Halide::Var var;
            input_vars.push_back(var);
            output_vars.push_back(var);
            output_shape.push_back(input_shape[i]);
        }
    }
    std::string reduction_name = name_for_node(node, "_reduction");
    if (node.op_type() == "ReduceSum") {
        result.outputs[0].rep(output_vars) =
            Halide::sum(result.inputs[0].rep(input_vars), reduction_name);
    } else if (node.op_type() == "ReduceSumSquare") {
        result.outputs[0].rep(output_vars) +=
            result.inputs[0].rep(input_vars) * result.inputs[0].rep(input_vars);
    } else if (node.op_type() == "ReduceLogSum") {
        result.outputs[0].rep(output_vars) = Halide::log(
            Halide::sum(result.inputs[0].rep(input_vars), reduction_name));
    } else if (node.op_type() == "ReduceLogSumExp") {
        result.outputs[0].rep(output_vars) = Halide::log(Halide::sum(
            Halide::exp(result.inputs[0].rep(input_vars)), reduction_name));
    } else if (node.op_type() == "ReduceProd") {
        result.outputs[0].rep(output_vars) =
            Halide::product(result.inputs[0].rep(input_vars), reduction_name);
    } else if (node.op_type() == "ReduceMean") {
        result.outputs[0].rep(output_vars) =
            Halide::sum(result.inputs[0].rep(input_vars), reduction_name) /
            num_reduced_elems;
    } else if (node.op_type() == "ReduceMin") {
        result.outputs[0].rep(output_vars) =
            Halide::minimum(result.inputs[0].rep(input_vars), reduction_name);
    } else if (node.op_type() == "ReduceMax") {
        result.outputs[0].rep(output_vars) =
            Halide::maximum(result.inputs[0].rep(input_vars), reduction_name);
    } else if (node.op_type() == "ReduceL2") {
        result.outputs[0].rep(output_vars) = Halide::sqrt(Halide::sum(
            Halide::pow(result.inputs[0].rep(input_vars), 2), reduction_name));
    } else if (node.op_type() == "ReduceL1") {
        result.outputs[0].rep(output_vars) = Halide::sum(
            Halide::abs(result.inputs[0].rep(input_vars)), reduction_name);
    } else {
        throw std::domain_error(
            "Unsupported reduction type " + node.op_type() + " for node " +
            node.name());
    }

    return result;
}
Node convert_batchnorm_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    bool spatial = true;
    float epsilon = 1e-5f;
    float momentum = 0.9f;
    for (const auto &attr : node.attribute()) {
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
    const Tensor &X = inputs[0];
    const Tensor &scale = inputs[1];
    const Tensor &shift = inputs[2];
    const Tensor &mean = inputs[3];
    const Tensor &variance = inputs[4];

    const int rank = X.shape.size();
    if (rank < 2) {
        throw std::invalid_argument(
            "Input rank less than 2 for BatchNormalization node " + node.name());
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(node.output_size());
    result.outputs[0].shape = inputs[0].shape;
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].rep = func_for_node_output(node, 0);

    std::vector<Halide::Var> vars(rank);
    Halide::Var param_var = vars[1];

    Halide::Func normalized(result.outputs[0].rep.name() + "_normalized");
    normalized(vars) = (X.rep(vars) - mean.rep(param_var)) /
                       Halide::sqrt(variance.rep(param_var) + epsilon);
    result.outputs[0].rep(vars) =
        scale.rep(param_var) * normalized(vars) + shift.rep(param_var);

    return result;
}

Node convert_flatten_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected a single input for Flatten node " + node.name());
    }

    const std::vector<Halide::Expr> &in_shape = inputs[0].shape;
    const int in_rank = in_shape.size();
    int axis = 1;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axis") {
            axis = attr.i();

            if (std::abs(axis) > in_rank) {
                throw std::invalid_argument(
                    "Axis for node " + node.name() + " is " + std::to_string(axis) +
                    "but should be in the range [-" + std::to_string(in_rank) + ", " +
                    std::to_string(in_rank) + "]");
            }
            // Negative axis means we're starting from the end. Convert it to the
            // equivalent positive axis value to make the code below easier to follow.
            if (axis < 0) {
                axis += in_rank;
            }
        }
    }

    const int out_rank = 2;
    std::vector<Halide::Expr> strides(in_rank, 1);
    for (int i = in_rank - 2; i >= 0; --i) {
        strides[i] = inputs[0].shape[i + 1] * strides[i + 1];
    }
    Halide::Expr inner_size = 1;
    Halide::Expr outer_size = 1;
    for (int i = 0; i < in_rank; ++i) {
        const Halide::Expr dim_i = in_shape[i];
        if (i < axis) {
            outer_size *= dim_i;
        } else {
            inner_size *= dim_i;
        }
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(node.output_size());
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].shape.push_back(outer_size);
    result.outputs[0].shape.push_back(inner_size);

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

Node convert_tile_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 2) {
        throw std::invalid_argument(
            "Tile requires 2 inputs, but node " + node.name() + " has " +
            std::to_string(inputs.size()));
    }

    const Tensor &input = inputs[0];
    const Tensor &repeats = inputs[1];

    const int rank = input.shape.size();

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].type = inputs[0].type;
    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;

    // Evaluate repeats if possible to compute output_shape.
    try {
        Halide::Func tiles = repeats.rep;
        Halide::Buffer<int64_t> realized_shape = tiles.realize(rank);
        for (int i = 0; i < rank; ++i) {
            int64_t tiling_factor = realized_shape(i);
            output_shape.push_back(
                input.shape[i] * static_cast<int32_t>(tiling_factor));
        }
    } catch (...) {
        for (int i = 0; i < rank; ++i) {
            output_shape.push_back(input.shape[i] * inline_func_call(repeats.rep(i)));
        }
    }

    std::vector<Halide::Expr> in_vars(rank);
    std::vector<Halide::Var> vars(rank);

    for (int i = 0; i < rank; ++i) {
        Halide::Expr dim_size = input.shape[i];
        in_vars[i] = vars[i] % dim_size;
    }

    result.outputs[0].rep(vars) = input.rep(in_vars);

    return result;
}

Node convert_elu_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
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

    for (const auto &attr : node.attribute()) {
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
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].rep = func_for_node_output(node, 0);
    const Tensor &X = inputs[0];

    if (node.op_type() == "Elu") {
        result.outputs[0].rep(Halide::_) = Halide::select(
            X.rep(Halide::_) > 0.0f,
            X.rep(Halide::_),
            alpha * (Halide::exp(X.rep(Halide::_)) - 1.0f));
    } else if (node.op_type() == "Selu") {
        result.outputs[0].rep(Halide::_) = Halide::select(
            X.rep(Halide::_) > 0.0f,
            gamma * X.rep(Halide::_),
            gamma * (alpha * Halide::exp(X.rep(Halide::_)) - alpha));
    } else if (node.op_type() == "LeakyRelu") {
        result.outputs[0].rep(Halide::_) = Halide::select(
            X.rep(Halide::_) >= 0.0f, X.rep(Halide::_), alpha * X.rep(Halide::_));
    } else if (node.op_type() == "ThresholdedRelu") {
        result.outputs[0].rep(Halide::_) =
            Halide::select(X.rep(Halide::_) > alpha, X.rep(Halide::_), 0);
    } else {
        throw std::domain_error("Invalid elu op " + node.op_type());
    }
    return result;
}

Node convert_dropout_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    float ratio = 0.5f;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "ratio") {
            ratio = attr.f();
        }
    }

    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected a single input for Dropout node " + node.name());
    }

    // Onnx dropout at the moment is always operating as a simple pass through
    ratio = 0.0f;

    Node result;
    result.inputs = inputs;
    result.outputs.resize(node.output_size());

    result.outputs[0].shape = inputs[0].shape;
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].rep = func_for_node_output(node, 0);
    Halide::Func &output = result.outputs[0].rep;

    Halide::Func filter;
    if (node.output_size() == 2) {
        result.outputs[1].shape = inputs[0].shape;
        result.outputs[1].type = inputs[0].type;
        filter = Halide::Func(func_for_node_output(node, 1));
    } else {
        filter = Halide::Func(name_for_node(node, "_filter"));
    }

    if (ratio == 0.0f) {
        // Simple pass through
        output = inputs[0].rep;
        const int rank = inputs[0].shape.size();
        std::vector<Halide::Var> vars(rank);
        filter(vars) = false;
    } else {
        const int rank = inputs[0].shape.size();
        std::vector<Halide::Var> vars(rank);
        Halide::Expr expr = 0;
        Halide::Expr stride = 1;
        for (int i = 0; i < rank; ++i) {
            expr += vars[i] * stride;
            stride *= inputs[0].shape[i];
        }
        filter(vars) = Halide::random_float(expr) >= ratio;
        output(vars) = inputs[0].rep(vars) * filter(vars) / ratio;
    }

    if (node.output_size() == 2) {
        result.outputs[1].rep = filter;
    } else if (node.output_size() != 1) {
        throw std::domain_error(
            "Invalid number of outputs for dropout node " + node.name());
    }
    return result;
}

Node convert_pooling_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (node.output_size() != 1) {
        throw std::domain_error(
            "Can't yet generate indices for pooling node " + node.name());
    }

    std::string padding = "NOTSET";
    std::vector<Halide::Expr> kernel_shape;
    std::vector<int> pads;
    std::vector<int> strides;
    bool count_include_pad = false;  // For avg pool
    int p = 2;                       // For lp pool
    for (const auto &attr : node.attribute()) {
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

    const int rank = inputs[0].shape.size();
    if (node.op_type().find("Global") == 0) {
        // The kernel shape is the whole height/width of the input.
        for (int i = 2; i < rank; ++i) {
            kernel_shape.push_back(inputs[0].shape[i]);
        }
    } else {
        if (kernel_shape.size() + 2 != rank) {
            throw std::invalid_argument(
                "invalid kernel shape for pooling node " + node.name());
        }
    }

    bool extented_count_needed = false;
    if (node.op_type() == "AveragePool" && !count_include_pad) {
        for (int pad : pads) {
            if (pad != 0) {
                extented_count_needed = true;
                break;
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
    std::vector<Halide::Expr> hpads(pads.begin(), pads.end());
    Halide::Func padded_input =
        generate_padding_expr(inputs[0].rep, inputs[0].shape, padding_val, hpads);

    // Pool the input values.
    std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
    for (int i = 0; i < rank - 2; ++i) {
        extents.emplace_back(0, kernel_shape[i]);
    }

    Halide::RDom rdom(extents, name_for_node(node, "_pool_rdom"));
    std::vector<Halide::Var> out_vars(rank);
    std::vector<Halide::Expr> x_vars(rank);
    x_vars[0] = out_vars[0];
    x_vars[1] = out_vars[1];
    for (int i = 2; i < rank; ++i) {
        x_vars[i] = out_vars[i] + rdom[i - 2];
    }

    Halide::Func basic_pool(func_for_node_output(node, 0));
    if (node.op_type() == "MaxPool" || node.op_type() == "GlobalMaxPool") {
        basic_pool(out_vars) =
            Halide::maximum(padded_input(x_vars), name_for_node(node, "_maximum"));
    } else if (
        node.op_type() == "AveragePool" ||
        node.op_type() == "GlobalAveragePool") {
        Halide::Expr num_pooling_vals = 1;
        // If extented_count_needed is set, we need to compute the number of
        // coefficients that reside in the pooling window but aren't padding. If
        // it's not set, we always use the window size regardless of padding.
        for (int i = 0; i < kernel_shape.size(); ++i) {
            Halide::Expr kernel_dim = kernel_shape[i];
            if (extented_count_needed) {
                kernel_dim -= Halide::max(0, pads[i] - out_vars[i + 2]);
                kernel_dim -= Halide::max(
                    0,
                    out_vars[i + 2] + kernel_shape[i] -
                        (inputs[0].shape[i + 2] + pads[i]));
            }
            num_pooling_vals *= kernel_dim;
        }
        basic_pool(out_vars) =
            Halide::sum(padded_input(x_vars), name_for_node(node, "_sum")) /
            num_pooling_vals;
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
        pool = Halide::Func(name_for_node(node, "_strided"));
        pool(out2_vars) = basic_pool(stride_vars);
    } else {
        pool = basic_pool;
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].rep = pool;

    // Determine the shape of the output
    result.outputs[0].shape = inputs[0].shape;
    for (int i = 2; i < rank; ++i) {
        Halide::Expr dim = inputs[0].shape[i] + pads[i - 2] + pads[rank + i - 4];
        dim -= (kernel_shape[i - 2] - 1);
        dim = div_up(dim, strides[i - 2]);
        result.outputs[0].shape[i] = dim;
    }

    return result;
}

Node convert_softmax_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    int axis = 1;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axis") {
            axis = attr.i();
        }
    }

    const std::vector<Halide::Expr> &input_shape = inputs[0].shape;
    const int rank = input_shape.size();
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
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].rep = func_for_node_output(node, 0);

    std::vector<std::pair<Halide::Expr, Halide::Expr>> extents;
    for (int i = axis; i < rank; ++i) {
        extents.emplace_back(0, inputs[0].shape[i]);
    }
    std::vector<Halide::Var> indices(rank);
    Halide::RDom rdom(extents, name_for_node(node, "_softmax_rdom"));
    std::vector<Halide::Expr> denom_vars;
    for (int i = 0; i < axis; ++i) {
        denom_vars.push_back(indices[i]);
    }
    for (int i = axis; i < rank; ++i) {
        denom_vars.push_back(rdom[i - axis]);
    }

    Halide::Func in = inputs[0].rep;
    Halide::Expr max = Halide::maximum(in(denom_vars));
    if (node.op_type() == "LogSoftmax") {
        result.outputs[0].rep(indices) = in(indices)-max -
                                         Halide::log(Halide::sum(Halide::exp(in(denom_vars)-max)));
    } else {
        result.outputs[0].rep(indices) = Halide::exp(in(indices)-max) /
                                         Halide::sum(Halide::exp(in(denom_vars)-max));
    }
    return result;
}

Node convert_concat_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() < 1) {
        throw std::invalid_argument(
            "Too few inputs for concat node " + node.name());
    }
    const int num_dims = inputs[0].shape.size();
    int axis = -1;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axis") {
            axis = attr.i();
            if (axis < 0) {
                axis += num_dims;
            }
        }
    }
    if (axis < 0 || axis >= num_dims) {
        throw std::invalid_argument("Invalid axis for concat node " + node.name());
    }
    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
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
    Halide::Expr concat_offset = 0;
    for (int i = 1; i < inputs.size(); ++i) {
        concat_offset += inline_func_call(inputs[i - 1].shape[axis]);

        src1_indices[axis] = Halide::min(tgt_indices[axis], concat_offset - 1);
        src2_indices[axis] = Halide::max(tgt_indices[axis] - concat_offset, 0);

        concat_funcs[i](tgt_indices) = Halide::select(
            concat_axis < concat_offset,
            concat_funcs[i - 1](src1_indices),
            inputs[i].rep(src2_indices));
    }

    result.outputs[0].rep(tgt_indices) = concat_funcs.back()(tgt_indices);
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].shape = inputs[0].shape;
    const Halide::Expr concatenated_size =
        concat_offset + inputs.back().shape[axis];
    result.outputs[0].shape[axis] = Halide::Internal::simplify(concatenated_size);

    return result;
}

Node convert_split_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Unexpected number of inputs for split node " + node.name());
    }

    const int num_outputs = node.output_size();

    std::vector<int> user_splits;
    int axis = 0;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "split") {
            for (int split_size : attr.ints()) {
                user_splits.push_back(split_size);
            }
            assert(num_outputs == user_splits.size());
        }
        if (attr.name() == "axis") {
            axis = attr.i();
        }
    }

    Node result;
    result.inputs = inputs;

    if (num_outputs == 0) {
        return result;
    }
    result.outputs.resize(num_outputs);

    // Split into equal parts.
    std::vector<Halide::Expr> splits;
    if (axis < 0) {
        axis += inputs[0].shape.size();
    }
    Halide::Expr axis_dim = inputs[0].shape.at(axis);
    const int64_t *axis_dim_size = Halide::Internal::as_const_int(axis_dim);

    if (user_splits.size() == 0) {
        if (axis_dim_size && (*axis_dim_size % num_outputs != 0)) {
            throw std::invalid_argument(
                "Can't equaly split outputs for node " + node.name());
        }
        Halide::Expr size = Halide::Internal::simplify(axis_dim / num_outputs);
        for (int i = 0; i < num_outputs; ++i) {
            splits.push_back(size);
        }
    } else {
        const int total_splits_size =
            std::accumulate(user_splits.begin(), user_splits.end(), 0);
        if (axis_dim_size && (total_splits_size > *axis_dim_size)) {
            throw std::invalid_argument(
                "Inconsistent splits for node " + node.name());
        }
        for (int split : user_splits) {
            splits.push_back(split);
        }
    }

    // Compute offsets.
    std::vector<Halide::Expr> split_offsets(splits.size(), 0);
    split_offsets[0] = Halide::Internal::simplify(Halide::cast<int32_t>(0));
    for (int i = 1; i < splits.size(); ++i) {
        split_offsets[i] = Halide::Internal::simplify(Halide::cast<int32_t>(split_offsets[i - 1] + splits[i - 1]));
    }

    const int rank = inputs[0].shape.size();
    std::vector<Halide::Var> out_vars(rank);
    std::vector<Halide::Expr> in_vars(rank);
    for (int dim = 0; dim < rank; ++dim) {
        if (dim != axis) {
            in_vars[dim] = out_vars[dim];
        }
    }

    for (int i = 0; i < num_outputs; ++i) {
        result.outputs[i].type = inputs[0].type;
        result.outputs[i].shape = inputs[0].shape;
        result.outputs[i].rep = func_for_node_output(node, i);
        result.outputs[i].shape[axis] = splits[i];
        Halide::Expr offset = split_offsets[i];
        in_vars[axis] = out_vars[axis] + offset;
        result.outputs[i].rep(out_vars) = result.inputs[0].rep(in_vars);
    }

    return result;
}

Node convert_slice_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    bool version_9_op = false;
    if (inputs.size() == 1) {
        version_9_op = true;
    } else if (inputs.size() < 3 || inputs.size() > 5) {
        throw std::invalid_argument(
            "Unexpected number of inputs for slice node " + node.name());
    }
    const Tensor &input = inputs[0];
    const int num_dims = input.shape.size();
    std::vector<Halide::Expr> starts;
    std::vector<Halide::Expr> ends;
    // Version-9 attributes.
    std::unordered_map<int, std::pair<int, int>> extents;
    int num_slice_dims;
    Node result;
    result.inputs = inputs;
    if (version_9_op) {
        std::vector<int> axes;
        std::vector<int> ends;
        std::vector<int> starts;
        for (const auto &attr : node.attribute()) {
            if (attr.name() == "axes") {
                for (int axis : attr.ints()) {
                    if (axis < 0) {
                        axis += num_dims;
                    }
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
            throw std::invalid_argument(
                "Too many ends for slice node " + node.name());
        }
        if (axes.empty()) {
            for (int i = 0; i < starts.size(); ++i) {
                axes.push_back(i);
            }
        } else if (axes.size() != starts.size()) {
            throw std::invalid_argument(
                "Invalid axes/starts for slice node " + node.name());
        }
        for (int i = 0; i < axes.size(); ++i) {
            int axis = axes[i];
            std::pair<int, int> extent = std::make_pair(starts[i], ends[i]);
            extents[axis] = extent;
        }
        num_slice_dims = axes.size();
    } else {
        const Tensor &starts_tensor = inputs[1];
        const Tensor &ends_tensor = inputs[2];
        const Halide::Expr starts_shape_expr =
            Halide::Internal::simplify(starts_tensor.shape[0]);
        const Halide::Expr ends_shape_expr =
            Halide::Internal::simplify(ends_tensor.shape[0]);
        const int64_t *starts_shape_dim_0 =
            Halide::Internal::as_const_int(starts_shape_expr);
        const int64_t *ends_shape_dim_0 =
            Halide::Internal::as_const_int(ends_shape_expr);
        if (starts_shape_dim_0 == nullptr && ends_shape_dim_0 == nullptr) {
            throw std::invalid_argument(
                "Can't statisticaly infer slice dim size for slice node " +
                node.name());
        } else {
            result.requirements.push_back(starts_shape_expr == ends_shape_expr);
        }
        num_slice_dims =
            starts_shape_dim_0 != nullptr ? *starts_shape_dim_0 : *ends_shape_dim_0;
        if (num_slice_dims != *ends_shape_dim_0) {
            throw std::invalid_argument(
                "Starts and ends input tensor must have the same shape for "
                "slice node " +
                node.name());
        }
        for (int i = 0; i < num_slice_dims; ++i) {
            starts.push_back(Halide::cast<int32_t>(inputs[1].rep(i)));
            ends.push_back(Halide::cast<int32_t>(inputs[2].rep(i)));
        }
    }

    std::vector<Halide::Expr> axes;
    std::vector<Halide::Expr> steps(num_slice_dims, 1);

    // Check if axes are explicitly provided.
    if (inputs.size() > 3 && !node.input(3).empty()) {
        const Tensor &axes_tensor = inputs[3];
        const Halide::Expr axes_shape_expr =
            Halide::Internal::simplify(axes_tensor.shape[0]);
        const int64_t *axes_shape_dim_0 =
            Halide::Internal::as_const_int(axes_shape_expr);
        if (axes_shape_dim_0 != nullptr && *axes_shape_dim_0 != num_slice_dims) {
            throw std::invalid_argument(
                "Axes tensor must have the same shape as starts and ends for slice "
                "node " +
                node.name());
        }
        for (int i = 0; i < num_slice_dims; ++i) {
            axes.push_back(Halide::select(
                axes_tensor.rep(i) < 0,
                axes_tensor.rep(i) + num_dims,
                axes_tensor.rep(i)));
        }
    } else {
        for (int i = 0; i < num_slice_dims; ++i) {
            axes.push_back(i);
        }
    }

    // Check if steps are explicitly provided.
    if (inputs.size() > 4 && !node.input(4).empty()) {
        const Tensor &steps_tensor = inputs[4];
        const Halide::Expr steps_shape_expr =
            Halide::Internal::simplify(steps_tensor.shape[0]);
        const int64_t *steps_shape_dim_0 =
            Halide::Internal::as_const_int(steps_shape_expr);
        if (steps_shape_dim_0 != nullptr && *steps_shape_dim_0 != num_slice_dims) {
            throw std::invalid_argument(
                "Steps tensor must have the same shape as starts and ends for slice "
                "node " +
                node.name());
        }
        for (int i = 0; i < num_slice_dims; ++i) {
            steps[i] = Halide::cast<int32_t>(steps_tensor.rep(i));
        }
    }

    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].type = input.type;
    result.outputs[0].shape = input.shape;

    std::vector<Halide::Var> tgt_indices;
    tgt_indices.resize(num_dims);
    std::vector<Halide::Expr> src_indices;

    for (int i = 0; i < num_dims; ++i) {
        if (version_9_op) {
            if (extents.find(i) != extents.end()) {
                int start = extents[i].first;
                int end = extents[i].second;
                Halide::Expr actual_end = end;
                if (end < 0) {
                    actual_end = inputs[0].shape[i] + end;
                }

                Halide::Expr actual_start = Halide::min(start, inputs[0].shape[i]);
                actual_end = Halide::min(actual_end, inputs[0].shape[i]);
                src_indices.push_back(tgt_indices[i] + actual_start);

                result.outputs[0].shape[i] =
                    Halide::Internal::simplify(actual_end - actual_start);
            } else {
                src_indices.push_back(tgt_indices[i]);
            }
        } else {
            Halide::Expr start = 0;
            Halide::Expr end = Halide::cast<int32_t>(input.shape[i] - 1);
            Halide::Expr step = 1;
            // Pick slice boundaries or keep default values.
            Halide::Expr slice_dim = Halide::Internal::const_false();
            for (int j = 0; j < num_slice_dims; ++j) {
                start = select(i == axes[j], starts[j], start);
                end = select(i == axes[j], ends[j], end);
                step = select(i == axes[j], steps[j], step);
                slice_dim = slice_dim || i == axes[j];
            }
            // Negative values are valid, and must be interpreted as and offset from
            // the end
            start = Halide::select(
                start < 0, Halide::cast<int32_t>(input.shape[i] + start), start);
            end = Halide::select(
                end < 0, Halide::cast<int32_t>(input.shape[i] + end), end);
            // INT_MAX is a valid start/end, which must be interpreted as the index of
            // the last element.
            start = Halide::min(start, input.shape[i] - 1);
            end = Halide::min(end, input.shape[i] - 1);
            step = Halide::cast<int32_t>(step);
            slice_dim = Halide::Internal::simplify(slice_dim);
            Halide::Expr max_dim = Halide::cast<int32_t>(input.shape[i] - 1);
            Halide::Expr index = Halide::clamp(
                Halide::Internal::simplify(start + tgt_indices[i] * step),
                0,
                max_dim);
            src_indices.push_back(Halide::select(slice_dim, index, tgt_indices[i]));
            result.outputs[0].shape[i] = Halide::Internal::simplify(
                div_up(Halide::abs(end - start) + 1, Halide::abs(step)));
        }
    }
    result.outputs[0].rep(tgt_indices) = input.rep(src_indices);
    return result;
}

Node convert_pad_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() < 1 || inputs.size() > 3) {
        throw std::invalid_argument(
            "Expected between one and three inputs for pad node " + node.name());
    }
    std::string mode = "constant";
    Halide::Expr value = 0;
    std::vector<Halide::Expr> pads;
    for (const auto &attr : node.attribute()) {
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

    Node result;
    if (inputs.size() >= 2) {
        if (inputs[1].shape.size() != 1) {
            throw std::invalid_argument("Expected 1d tensor for pads");
        }
        const int rank = inputs[0].shape.size();
        result.requirements.push_back(inputs[1].shape[0] == 2 * rank);
        pads.resize(2 * rank);
        for (int i = 0; i < 2 * rank; ++i) {
            pads[i] = Halide::cast<int32_t>(inputs[1].rep(i));
        }
    }
    if (inputs.size() >= 3) {
        if (inputs[2].shape.size() > 0) {
            throw std::invalid_argument("Expected scalar tensor for padding value");
        }
        value = inputs[2].rep();
    }

    const int num_dims = inputs[0].shape.size();
    if (pads.size() != 2 * num_dims) {
        throw std::invalid_argument(
            "Invalid pads specified for node " + node.name());
    }
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep = generate_padding_expr(
        inputs[0].rep, inputs[0].shape, value, pads, padding_mode);

    result.outputs[0].type = inputs[0].type;
    std::vector<Halide::Expr> &shape = result.outputs[0].shape;
    shape = inputs[0].shape;
    const int rank = inputs[0].shape.size();
    for (int i = 0; i < rank; ++i) {
        Halide::Expr padding = pads[i] + pads[i + rank];
        shape[i] = Halide::Internal::simplify(shape[i] + padding);
    }
    return result;
}

Node convert_transpose_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one input for transpose node " + node.name());
    }
    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);

    const Tensor &input = inputs[0];
    const int rank = input.shape.size();
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
    for (const auto &attr : node.attribute()) {
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
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(output_vars) = input.rep(input_vars);

    result.outputs[0].type = input.type;
    result.outputs[0].shape = input.shape;
    const std::vector<Halide::Expr> &input_shape = input.shape;
    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;

    for (int i = 0; i < rank; ++i) {
        output_shape[i] = input_shape[permutation[i]];
    }

    return result;
}

Node convert_unsqueeze_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one input for unsqueeze node " + node.name());
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);

    const std::vector<Halide::Expr> &input_shape = inputs[0].shape;
    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;

    const int in_rank = input_shape.size();
    std::unordered_set<int> dims_to_unsqueeze;

    // axis can be > input rank and we assign this to outermost dimensions.
    int outer_dims = 0;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axes") {
            for (int index : attr.ints()) {
                if (index < 0) {
                    index += in_rank;
                }
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
            output_shape.push_back(1);
            out_vars.push_back(Halide::Var());
        }
        out_vars.push_back(v_i);
        output_shape.push_back(input_shape[i]);
    }

    // axes > in_rank. assign to outer most axis.
    for (int i = 0; i < outer_dims; ++i) {
        out_vars.push_back(Halide::Var());
        output_shape.push_back(1);
    }
    result.outputs[0].type = inputs[0].type;
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(out_vars) = inputs[0].rep(in_vars);

    return result;
}

Node convert_squeeze_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one input for squeeze node " + node.name());
    }

    const Tensor &input = inputs[0];
    const int rank = input.shape.size();

    std::unordered_set<int> dims_to_squeeze;
    bool implicit = true;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axes") {
            for (int index : attr.ints()) {
                if (std::abs(index) >= rank) {
                    throw std::invalid_argument(
                        "invalid axes attribute for node " + node.name());
                }
                // Negative axis means we're starting from the end. Convert it to the
                // equivalent positive axis value to make the code below easier to follow.
                if (index < 0) {
                    index += rank;
                }
                dims_to_squeeze.insert(index);
            }
            implicit = false;
        }
    }
    if (implicit) {
        for (int i = 0; i < rank; ++i) {
            const Halide::Expr dim_expr = Halide::Internal::simplify(input.shape[i]);
            const int64_t *dim = Halide::Internal::as_const_int(dim_expr);
            if (!dim) {
                throw std::invalid_argument(
                    "Unknown dimension for input dim " + std::to_string(i) +
                    " of tensor " + input.name);
            }
            if (*dim == 1) {
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

    const std::vector<Halide::Expr> &input_shape = input.shape;
    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;
    for (int i = 0; i < rank; ++i) {
        if (dims_to_squeeze.find(i) == dims_to_squeeze.end()) {
            output_vars.push_back(Halide::Var());
            input_vars[i] = output_vars.back();
            output_shape.push_back(input_shape[i]);
        } else {
            input_vars[i] = 0;
        }
    }
    result.outputs[0].type = input.type;
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(output_vars) = input.rep(input_vars);

    return result;
}

Node convert_constant_of_shape(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected one input for ConstantOfShape node " + node.name());
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    const Halide::Expr shape_expr =
        Halide::Internal::simplify(inputs[0].shape[0]);
    const int64_t *shape_dim_0 = Halide::Internal::as_const_int(shape_expr);
    if (!shape_dim_0) {
        throw std::invalid_argument(
            "Can't infer rank statically for ConstantOfShape node " + node.name());
    }
    const int out_rank = *shape_dim_0;
    const std::vector<Halide::Var> out_vars(out_rank);
    out.shape.resize(out_rank);
    for (int i = 0; i < out_rank; ++i) {
        out.shape[i] = inline_func_call(inputs[0].rep(i));
    }
    out.rep = func_for_node_output(node, 0);

    bool use_default_value = true;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "value") {
            const onnx::TensorProto &proto_value = attr.t();
            Tensor value = build_from_constant(proto_value, name_for_node(node, ""));
            std::vector<Halide::Expr> vars(value.shape.size(), 0);
            out.rep(out_vars) = Halide::Internal::simplify(value.rep(vars));
            out.type = value.type;
            use_default_value = false;
            break;
        }
    }
    if (use_default_value) {
        out.rep(out_vars) = 0.0f;
        out.type = onnx::TensorProto_DataType_FLOAT;
    }
    return result;
}

Node convert_constant_fill_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 0) {
        throw std::invalid_argument(
            "Expected no inputs for ConstantFill node " + node.name());
    }

    Node result;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);
    int rank = 0;
    Halide::Expr value = 0.0f;
    int dtype = 1;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "shape") {
            rank = attr.ints_size();
            for (int dim : attr.ints()) {
                result.outputs[0].shape.push_back(dim);
            }
        }
        if (attr.name() == "value") {
            value = attr.f();
        }
        if (attr.name() == "dtype") {
            dtype = attr.i();
        }
        if (attr.name() == "extra_shape" || attr.name() == "input_as_shape") {
            throw std::invalid_argument(
                "Attribute " + attr.name() + " Not supported for ConstantFill node " +
                node.name());
        }
    }

    result.outputs[0].type = static_cast<onnx::TensorProto::DataType>(dtype);

    std::vector<Halide::Var> vars(rank);
    switch (dtype) {
    case onnx::TensorProto_DataType_FLOAT:
        result.outputs[0].rep(vars) = value;
        break;
    case onnx::TensorProto_DataType_DOUBLE:
        result.outputs[0].rep(vars) = Halide::cast<double>(value);
        break;
    case onnx::TensorProto_DataType_UINT8:
        result.outputs[0].rep(vars) = Halide::cast<uint8_t>(value);
        break;
    case onnx::TensorProto_DataType_INT8:
        result.outputs[0].rep(vars) = Halide::cast<int8_t>(value);
        break;
    case onnx::TensorProto_DataType_UINT16:
        result.outputs[0].rep(vars) = Halide::cast<uint16_t>(value);
        break;
    case onnx::TensorProto_DataType_INT16:
        result.outputs[0].rep(vars) = Halide::cast<int16_t>(value);
        break;
    case onnx::TensorProto_DataType_UINT32:
        result.outputs[0].rep(vars) = Halide::cast<uint32_t>(value);
        break;
    case onnx::TensorProto_DataType_INT32:
        result.outputs[0].rep(vars) = Halide::cast<int32_t>(value);
        break;
    case onnx::TensorProto_DataType_UINT64:
        result.outputs[0].rep(vars) = Halide::cast<uint64_t>(value);
        break;
    case onnx::TensorProto_DataType_INT64:
        result.outputs[0].rep(vars) = Halide::cast<int64_t>(value);
        break;
    case onnx::TensorProto_DataType_BOOL:
        result.outputs[0].rep(vars) = Halide::cast<bool>(value);
        break;
    default:
        throw std::invalid_argument(
            "Unsupported argument dtype = " + std::to_string(dtype) +
            " for node " + node.name());
    }

    return result;
}

Node convert_where_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 3) {
        throw std::invalid_argument(
            "Expected exactly three inputs for where node " + node.name());
    }
    const Tensor &cond = inputs[0];
    const Tensor &input_1 = inputs[1];
    const Tensor &input_2 = inputs[2];
    if (input_1.type != input_2.type) {
        throw std::invalid_argument(
            "Expected inputs to have the same type for where node " + node.name());
    }

    const int rank = std::max(
        std::max(input_1.shape.size(), input_2.shape.size()), cond.shape.size());

    std::vector<Halide::Expr> out_shape(rank);
    for (int i = 1; i <= rank; ++i) {
        out_shape[rank - i] = 1;
        if (i <= cond.shape.size()) {
            out_shape[rank - i] =
                Halide::max(out_shape[rank - i], cond.shape[cond.shape.size() - i]);
        }
        if (i <= input_1.shape.size()) {
            out_shape[rank - i] = Halide::max(
                out_shape[rank - i], input_1.shape[input_1.shape.size() - i]);
        }
        if (i <= input_2.shape.size()) {
            out_shape[rank - i] = Halide::max(
                out_shape[rank - i], input_2.shape[input_2.shape.size() - i]);
        }
    }

    std::vector<Halide::Var> out_vars(rank);

    // Broadcasting is right -> left.
    auto broadcast_vars = [rank](
                              const std::vector<Halide::Var> &out_vars,
                              const std::vector<Halide::Expr> &input_shape) {
        const int input_rank = input_shape.size();
        std::vector<Halide::Expr> in_expr(input_rank);
        for (int i = 1; i <= input_rank; ++i) {
            Halide::Expr max_index = Halide::Internal::simplify(
                Halide::cast<int32_t>(input_shape[input_rank - i] - 1));
            in_expr[input_rank - i] = Halide::clamp(out_vars[rank - i], 0, max_index);
        }
        return in_expr;
    };
    const std::vector<Halide::Expr> cond_expr =
        broadcast_vars(out_vars, cond.shape);
    const std::vector<Halide::Expr> input_1_expr =
        broadcast_vars(out_vars, input_1.shape);
    const std::vector<Halide::Expr> input_2_expr =
        broadcast_vars(out_vars, input_2.shape);

    Node result;
    result.outputs.resize(1);
    result.outputs[0].shape = out_shape;
    result.outputs[0].type = input_1.type;
    result.outputs[0].rep = func_for_node_output(node, 0);

    result.outputs[0].rep(out_vars) = Halide::select(
        cond.rep(cond_expr) != 0,
        input_1.rep(input_1_expr),
        input_2.rep(input_2_expr));
    return result;
}

Node convert_gather_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 2) {
        throw std::invalid_argument(
            "Expected exactly two arguments for gather node " + node.name());
    }

    const Tensor &input = inputs[0];
    const Tensor &indices = inputs[1];

    const int in_rank = inputs[0].shape.size();
    const int indices_rank = inputs[1].shape.size();
    const int out_rank = in_rank + indices_rank - 1;

    int axis = 0;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axis") {
            axis = attr.i();
        }
    }

    if (axis >= in_rank || axis < -in_rank) {
        throw std::invalid_argument(
            "Expected axis to in range of the input rank r, [-r, r-1]");
    }

    if (axis < 0) {
        axis += in_rank;
    }

    // This node acts like numpy.like, e.g flat indexing from the python docs:
    // Ni, Nk = input.shape[:axis], input.shape[axis+1:]
    // Nj = indices.shape
    // for ii in ndindex(Ni):
    //   for jj in ndindex(Nj):
    //     for kk in ndindex(Nk):
    //       out[ii + jj + kk] = input[ii + (indices[jj],) + kk]
    //
    std::vector<Halide::Expr> output_shape(out_rank);
    std::vector<Halide::Var> output_vars(out_rank);
    std::vector<Halide::Expr> input_vars(in_rank);
    std::vector<Halide::Expr> indices_vars(indices_rank);
    for (int i = 0; i < in_rank; ++i) {
        if (i < axis) {
            output_shape[i] = input.shape[i];
            input_vars[i] = output_vars[i];
        } else if (i == axis) {
            for (int j = 0; j < indices_rank; ++j) {
                output_shape[j + i] = indices.shape[j];
                indices_vars[j] = output_vars[j + i];
            }
            // Buffers are 32-bit indexed.
            // Negative indices means counting from the end of the dimension
            Halide::Expr index = inline_func_call(indices.rep(indices_vars));
            Halide::Expr valid =
                Halide::select(index < 0, index + input.shape[axis], index);
            Halide::Expr simplified =
                Halide::Internal::simplify(Halide::cast<int32_t>(valid));
            input_vars[axis] = clamp(simplified, 0, input.shape[axis] - 1);
        } else {
            output_shape[i + indices_rank - 1] = input.shape[i];
            input_vars[i] = output_vars[i + indices_rank - 1];
        }
    }
    Node result;
    result.outputs.resize(1);
    result.outputs[0].type = input.type;
    result.outputs[0].shape = output_shape;
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(output_vars) = input.rep(input_vars);
    result.requirements.push_back(indices.rep(indices_vars) < INT_MAX);
    return result;
}

Node convert_expand_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 2) {
        throw std::invalid_argument(
            "Expected exactly two inputs for expand node " + node.name());
    }
    if (node.output_size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one output for expand node " + node.name());
    }
    const Tensor &input = inputs[0];
    const Tensor &expand_shape = inputs[1];
    const int in_rank = input.shape.size();
    const Halide::Expr shape_expr =
        Halide::Internal::simplify(expand_shape.shape[0]);
    const int64_t *shape_dim_0 = Halide::Internal::as_const_int(shape_expr);
    if (!shape_dim_0) {
        throw std::invalid_argument(
            "Can't infer rank statically for expand node " + node.name());
    }
    const int shape_rank = *shape_dim_0;
    const int rank = std::max(in_rank, shape_rank);

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);

    std::vector<Halide::Var> out_vars(rank);
    std::vector<Halide::Expr> in_exprs(in_rank);
    std::vector<Halide::Expr> output_shape(rank);

    // Broadcasting rule.
    for (int i = 1; i <= rank; ++i) {
        if (in_rank - i >= 0) {
            if (shape_rank - i >= 0) {
                Halide::Expr max_index = Halide::Internal::simplify(
                    Halide::cast<int32_t>(input.shape[in_rank - i] - 1));
                in_exprs[in_rank - i] = Halide::clamp(out_vars[rank - i], 0, max_index);
                Halide::Expr bcast_dim =
                    inline_func_call(expand_shape.rep(shape_rank - i));
                result.requirements.push_back(
                    input.shape[in_rank - i] == bcast_dim ||
                    input.shape[in_rank - i] == 1 || bcast_dim == 1 || bcast_dim == -1);
                output_shape[rank - i] =
                    Halide::max(input.shape[in_rank - i], bcast_dim);
            } else {
                in_exprs[in_rank - i] = out_vars[rank - i];
                output_shape[rank - i] = input.shape[in_rank - i];
            }
        } else {
            Halide::Expr bcast_dim =
                inline_func_call(expand_shape.rep(shape_rank - i));
            output_shape[rank - i] = bcast_dim;
        }
    }

    result.outputs[0].type = inputs[0].type;
    result.outputs[0].shape = output_shape;
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(out_vars) = input.rep(in_exprs);
    return result;
}

Node convert_random_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() > 0) {
        throw std::invalid_argument(
            "Expected no inputs for " + node.op_type() + " node " + node.name());
    }

    int dtype = 1;
    float high = 1;
    float low = 0;
    int seed = 123;
    float mean = 0.0f;
    float scale = 1.0f;
    bool use_seed = false;
    std::vector<Halide::Expr> shape;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "dtype") {
            dtype = attr.i();
        } else if (attr.name() == "high") {
            high = attr.f();
        } else if (attr.name() == "low") {
            low = attr.f();
        } else if (attr.name() == "seed") {
            use_seed = true;
            union {
                float f;
                int32_t i;
            } u;
            u.f = attr.f();
            seed = u.i;
        } else if (attr.name() == "mean") {
            mean = attr.f();
        } else if (attr.name() == "scale") {
            scale = attr.f();
        } else if (attr.name() == "shape") {
            for (const auto dim : attr.ints()) {
                shape.emplace_back(dim);
            }
        }
    }

    if (!shape.size()) {
        throw std::invalid_argument(
            "Attribute shape is rquired for node " + node.name());
    }

    std::vector<Halide::Var> out_vars(shape.size());

    Node result;
    result.outputs.resize(1);
    result.outputs[0].shape = shape;
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].type = static_cast<onnx::TensorProto::DataType>(dtype);

    if (result.outputs[0].type != onnx::TensorProto_DataType_FLOAT &&
        result.outputs[0].type != onnx::TensorProto_DataType_DOUBLE &&
        result.outputs[0].type != onnx::TensorProto_DataType_FLOAT16) {
        throw std::invalid_argument(
            "Unsuported dtype attribute for node " + node.name());
    }

    // Box-Muller Transformation sampler.
    // http://mathworld.wolfram.com/Box-MullerTransformation.html
    auto sample_gaussian = [&](float mu, float sig) {
        Halide::Expr u1 =
            use_seed ? Halide::random_float(seed) : Halide::random_float();
        Halide::Expr u2 =
            use_seed ? Halide::random_float(seed) : Halide::random_float();
        Halide::Expr two_pi = 2.0f * static_cast<float>(M_PI);
        Halide::Expr z =
            Halide::sqrt(-2.0f * Halide::log(u1)) * Halide::cos(two_pi * u2);
        return z * sig + mu;
    };

    Halide::Expr sample;

    if (node.op_type() == "RandomUniform") {
        sample = (high - low) * Halide::random_float(seed) + low;
    } else if (node.op_type() == "RandomNormal") {
        sample = sample_gaussian(mean, scale);
    } else {
        throw std::invalid_argument(
            "Unsupported sampling method for node type : " + node.op_type());
    }

    result.outputs[0].rep(out_vars) =
        generate_cast_expr(sample, result.outputs[0].type, node.name());
    return result;
}

Node convert_shrink_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected one input for shrink node " + node.name());
    }

    const Tensor &input = inputs[0];
    float bias = 0.0f;
    float lambd = 0.5f;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "bias") {
            bias = attr.f();
        } else if (attr.name() == "lambd") {
            lambd = attr.f();
        }
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].shape = input.shape;
    result.outputs[0].type = input.type;
    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(Halide::_) = select(
        input.rep(Halide::_) < -lambd,
        input.rep(Halide::_) + bias,
        select(input.rep(Halide::_) > lambd, input.rep(Halide::_) - bias, 0));
    return result;
}

Node convert_lrn_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one input for lrn node " + node.name());
    }
    float alpha = 0.0001f;
    float beta = 0.75f;
    float bias = 1.0f;
    bool found_size = false;
    int size = 0;
    for (const auto attr : node.attribute()) {
        if (attr.name() == "alpha") {
            alpha = attr.f();
        } else if (attr.name() == "beta") {
            beta = attr.f();
        } else if (attr.name() == "bias") {
            bias = attr.f();
        } else if (attr.name() == "size") {
            size = attr.i();
            if (size <= 0) {
                throw std::invalid_argument(
                    "Attribute size should be > 0 but its " + std::to_string(size));
            }
            found_size = true;
        }
    }

    if (!found_size) {
        throw std::invalid_argument(
            "Attribute size is required for lrn node " + node.name());
    }

    const Tensor &input = inputs[0];
    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].shape = input.shape;
    result.outputs[0].type = input.type;
    result.outputs[0].rep = func_for_node_output(node, 0);

    Halide::Var n("n"), c("c");
    Halide::Func sum_squares(node.name() + "_sum_squares");

    Halide::RDom r(-div_down(size - 1, 2), size);

    if (input.shape.size() < 2) {
        throw std::invalid_argument(
            "Input rank must be at least 2 but its " +
            std::to_string(input.shape.size()));
    }

    Halide::Expr c_size = input.shape[1];

    sum_squares(n, c, Halide::_) = Halide::sum(Halide::select(
        c + r < 0 || c + r >= c_size,
        0,
        input.rep(n, Halide::clamp((c + r), 0, c_size - 1), Halide::_) *
            input.rep(n, Halide::clamp((c + r), 0, c_size - 1), Halide::_)));

    result.outputs[0].rep(n, c, Halide::_) = input.rep(n, c, Halide::_) /
                                             (Halide::pow(bias + (alpha / size) * sum_squares(n, c, Halide::_), beta));

    return result;
}

Node convert_isinf_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 1) {
        throw std::invalid_argument(
            "Expected exactly one input for isinf node " + node.name());
    }

    bool detect_negative = true;
    bool detect_positive = true;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "detect_negative") {
            detect_negative = attr.i();
        } else if (attr.name() == "detect_positive") {
            detect_positive = attr.i();
        }
    }

    const Tensor &input = inputs[0];
    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].shape = input.shape;
    result.outputs[0].type = onnx::TensorProto_DataType_BOOL;
    result.outputs[0].rep = func_for_node_output(node, 0);
    if (input.type == onnx::TensorProto_DataType_FLOAT ||
        input.type == onnx::TensorProto_DataType_DOUBLE) {
        Halide::Expr inf_value = input.type == onnx::TensorProto_DataType_FLOAT ? Halide::Expr(std::numeric_limits<float>::infinity()) : Halide::Expr(std::numeric_limits<double>::infinity());

        Halide::Expr pos_inf = detect_positive && input.rep(Halide::_) == inf_value;
        Halide::Expr neg_inf =
            detect_negative && input.rep(Halide::_) == -inf_value;
        // TODO(ataei): Fix Halide's mapping of const nanf into inf_f32.
        result.outputs[0].rep(Halide::_) =
            (pos_inf || neg_inf) && !Halide::is_nan(input.rep(Halide::_));
    } else {
        result.outputs[0].rep(Halide::_) = false;
    }
    return result;
}

Node convert_clip_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() < 1 || inputs.size() > 3) {
        throw std::invalid_argument(
            "Expected one to three inputs for clip node " + node.name());
    }

    float mini = std::numeric_limits<float>::lowest();
    float maxi = std::numeric_limits<float>::max();
    bool has_min = false;
    bool has_max = false;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "max") {
            has_max = true;
            maxi = attr.f();
        } else if (attr.name() == "min") {
            has_min = true;
            mini = attr.f();
        }
    }

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    Tensor &out = result.outputs[0];
    out.shape = inputs[0].shape;
    out.type = inputs[0].type;
    out.rep = func_for_node_output(node, 0);

    const Tensor &in = inputs[0];

    if (inputs.size() >= 2 && !node.input(1).empty()) {
        if (inputs[1].shape.size() > 0) {
            throw std::invalid_argument("The minimum value should be a scalar");
        }
        out.rep(Halide::_) = Halide::max(in.rep(Halide::_), inputs[1].rep());
    } else if (has_min) {
        out.rep(Halide::_) = Halide::max(in.rep(Halide::_), mini);
    } else {
        out.rep = in.rep;
    }
    if (inputs.size() >= 3 && !node.input(2).empty()) {
        if (inputs[2].shape.size() > 0) {
            throw std::invalid_argument("The maximum value should be a scalar");
        }
        out.rep(Halide::_) = Halide::min(out.rep(Halide::_), inputs[2].rep());
    } else if (has_max) {
        out.rep(Halide::_) = Halide::min(out.rep(Halide::_), maxi);
    }

    return result;
}

Node convert_reshape_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 2) {
        throw std::invalid_argument(
            "Expected exactly two inputs for reshape node " + node.name());
    }

    const Tensor &input = inputs[0];
    const Tensor &new_shape = inputs[1];

    Halide::Expr num_elems = 1;
    for (const Halide::Expr &dim : input.shape) {
        num_elems *= dim;
    }

    if (new_shape.shape.size() != 1) {
        throw std::invalid_argument("invalid shape");
    }
    const Halide::Expr shape_expr =
        Halide::Internal::simplify(new_shape.shape[0]);
    const int64_t *num_dims = Halide::Internal::as_const_int(shape_expr);
    if (!num_dims) {
        throw std::domain_error(
            "Couldn't statically infer the rank of the output of " + node.name());
    }
    const int output_rank = *num_dims;

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].type = inputs[0].type;
    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;

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
                output_shape.push_back(Halide::Expr());
            } else {
                output_shape.push_back(
                    Halide::Internal::simplify(Halide::cast<int32_t>(dim)));
                known_size *= dim;
            }
        }
        if (unknown_dim >= 0) {
            Halide::Expr dim = num_elems / static_cast<int>(known_size);
            output_shape[unknown_dim] =
                Halide::Internal::simplify(Halide::cast<int32_t>(dim));
        }
        new_shape_known = true;
    } catch (...) {
        if (output_rank == 1) {
            // Infer the dim from the number of elements in the input.
            output_shape.push_back(
                Halide::Internal::simplify(Halide::cast<int32_t>(num_elems)));
            new_shape_known = true;
        }
    }

    if (!new_shape_known) {
        output_shape.resize(output_rank);
        Halide::Expr known_size = 1;
        for (int i = 0; i < output_rank; ++i) {
            known_size *= Halide::Internal::simplify(
                Halide::cast<int32_t>(inline_func_call(new_shape.rep(i))));
        }
        Halide::Expr unknown_dim_if_any =
            Halide::cast<int32_t>(num_elems / Halide::abs(known_size));

        for (int i = 0; i < output_rank; ++i) {
            Halide::Expr shp =
                Halide::cast<int32_t>(inline_func_call(new_shape.rep(i)));
            output_shape[i] = Halide::Internal::simplify(Halide::select(
                shp == -1, unknown_dim_if_any, Halide::cast<int32_t>(shp)));
        }
    }

    std::vector<Halide::Expr> output_strides(output_rank);
    output_strides[output_rank - 1] = 1;
    for (int i = output_rank - 2; i >= 0; --i) {
        output_strides[i] = output_strides[i + 1] * output_shape[i + 1];
    }

    std::vector<Halide::Var> output_coordinates(output_rank);
    Halide::Expr coeff_index = 0;
    for (int i = 0; i < output_rank; ++i) {
        coeff_index += output_coordinates[i] * output_strides[i];
    }
    const std::vector<Halide::Expr> &input_shape = inputs[0].shape;
    const int input_rank = input_shape.size();
    std::vector<Halide::Expr> input_coordinates(input_rank);
    for (int i = input_rank - 1; i >= 0; --i) {
        Halide::Expr coord = Halide::Internal::simplify(
            Halide::cast<int32_t>(coeff_index % input_shape[i]));
        input_coordinates[i] = coord;
        coeff_index = (coeff_index - coord) / input_shape[i];
    }

    result.outputs[0].rep = func_for_node_output(node, 0);
    result.outputs[0].rep(output_coordinates) = input.rep(input_coordinates);

    return result;
}

Node convert_one_hot_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    if (inputs.size() != 3) {
        throw std::invalid_argument(
            "Expected exactly three inputs for OneHot node " + node.name());
    }

    const int rank = inputs[0].shape.size();
    int axis = rank;
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "axis") {
            axis = attr.i();
            if (axis < 0) {
                axis = rank;
            }
        }
    }

    const Tensor &indices = inputs[0];
    const Tensor &depth = inputs[1];
    const Tensor &values = inputs[2];

    Node result;
    result.inputs = inputs;
    result.outputs.resize(1);
    result.outputs[0].rep = func_for_node_output(node, 0);

    std::vector<Halide::Var> out_vars(rank + 1);
    std::vector<Halide::Var> in_vars(rank);
    for (int i = 0; i < std::min(rank, axis); ++i) {
        in_vars[i] = out_vars[i];
    }
    for (int i = axis; i < rank; ++i) {
        in_vars[i] = out_vars[i + 1];
    }
    Halide::Var selected = out_vars[axis];
    Halide::Expr off_value = values.rep(0);
    Halide::Expr on_value = values.rep(1);

    result.outputs[0].rep(out_vars) =
        Halide::select(indices.rep(in_vars) == selected, on_value, off_value);
    result.outputs[0].type = values.type;

    std::vector<Halide::Expr> &output_shape = result.outputs[0].shape;
    output_shape = inputs[0].shape;
    output_shape.resize(rank + 1);
    for (int i = rank; i > axis; --i) {
        output_shape[i] = output_shape[i - 1];
    }
    output_shape[axis] = Halide::Internal::simplify(depth.rep(0));

    return result;
}

Node convert_gru_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    int hidden_size = 1;
    int linear_before_reset = 0;
    bool input_forget = false;
    std::string direction = "forward";
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "hidden_size") {
            hidden_size = attr.i();
        } else if (attr.name() == "input_forget") {
            input_forget = static_cast<bool>(attr.i());
        } else if (attr.name() == "direction") {
            direction = attr.s();
        } else if (attr.name() == "linear_before_reset") {
            linear_before_reset = attr.i();
        } else if (
            attr.name() == "clip" || attr.name() == "activation_alpha" ||
            attr.name() == "activation_beta" || attr.name() == "activations") {
            throw std::domain_error(attr.name() + " not supported yet");
        }
    }

    // TBD: handle these cases
    if (direction != "forward") {
        throw std::domain_error("Unsupported direction");
    }
    if (input_forget) {
        throw std::domain_error("input_forget not supported yet");
    }
    if (linear_before_reset != 0) {
        throw std::domain_error("linear_before_reset not supported yet");
    }

    const int rank = inputs[0].shape.size();
    if (rank != 3) {
        throw std::domain_error("Invalid rank");
    }

    const Halide::Expr dim_expr = Halide::Internal::simplify(inputs[0].shape[0]);
    const int64_t *dim = Halide::Internal::as_const_int(dim_expr);
    if (!dim) {
        throw std::domain_error("Unknown number of timesteps");
    }
    const int num_time_steps = *dim;
    if (num_time_steps < 1) {
        throw std::domain_error("At least one timestep is required");
    }
    // Build an onnx graph encoding the GRU computations
    onnx::GraphProto gru_graph;
    // TODO: generate unique prefixes in case there is more than 1 unnamed GRU
    // node
    const std::string prefix =
        node.name().empty() ? std::string("gru") : node.name();
    // Split input into timesteps
    onnx::NodeProto *split_node = gru_graph.add_node();
    split_node->set_name(prefix + "_split");
    split_node->set_op_type("Split");
    onnx::AttributeProto *attr = split_node->add_attribute();
    attr->set_name("axis");
    attr->set_i(0);
    *split_node->add_input() = node.input(0);
    for (int i = 0; i < num_time_steps; ++i) {
        *split_node->add_output() = prefix + "_t" + std::to_string(i);
    }

    // Squeeze the first dim output the Xi, W and R tensors since we're only
    // supporting unidirectional LSTM for now
    onnx::NodeProto *W = gru_graph.add_node();
    W->set_name(node.input(1) + "_squeezed");
    W->set_op_type("Squeeze");
    attr = W->add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    *W->add_input() = node.input(1);
    *W->add_output() = W->name();

    onnx::NodeProto *R = gru_graph.add_node();
    R->set_name(node.input(2) + "_squeezed");
    R->set_op_type("Squeeze");
    attr = R->add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    *R->add_input() = node.input(2);
    *R->add_output() = R->name();

    // Rz, Rr, Rh
    onnx::NodeProto *Rs = gru_graph.add_node();
    Rs->set_name(R->name() + "_split");
    Rs->set_op_type("Split");
    attr = Rs->add_attribute();
    attr->set_name("axis");
    attr->set_i(0);
    *Rs->add_input() = R->name();
    *Rs->add_output() = Rs->name() + "_z";
    *Rs->add_output() = Rs->name() + "_r";
    *Rs->add_output() = Rs->name() + "_h";
    // Bias B, if any
    onnx::NodeProto *B = nullptr;
    if (inputs.size() >= 4 && !node.input(3).empty()) {
        // Preprocess the bias tensor, support only unidirectional for now
        onnx::NodeProto *Bs = gru_graph.add_node();
        Bs->set_name(node.input(3) + "_split");
        Bs->set_op_type("Split");
        attr = Bs->add_attribute();
        attr->set_name("axis");
        attr->set_i(1);
        Bs->add_input(node.input(3));
        Bs->add_output(Bs->name() + "_Wb");
        Bs->add_output(Bs->name() + "_Rb");

        B = gru_graph.add_node();
        B->set_name(node.input(3) + "_sum");
        B->set_op_type("Add");
        B->add_input(Bs->output(0));
        B->add_input(Bs->output(1));
        B->add_output(B->name());

    } else {
        B = gru_graph.add_node();
        B->set_name(prefix + "_zero");
        B->set_op_type("ConstantFill");
        attr = B->add_attribute();
        attr->set_name("shape");
        attr->add_ints(1);
        B->add_output(B->name());
    }

    // seq_len
    if (inputs.size() >= 5 && !node.input(4).empty()) {
        throw std::domain_error("Unsupported prespecified seq_len");
    }

    // Initial state if any, unidirection for now
    onnx::NodeProto *H_t = nullptr;
    if (inputs.size() >= 6 && !node.input(5).empty()) {
        H_t = gru_graph.add_node();
        H_t->set_name(node.input(5) + "_squeezed");
        H_t->set_op_type("Squeeze");
        attr = H_t->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *H_t->add_input() = node.input(5);
        *H_t->add_output() = H_t->name();
    }

    std::vector<onnx::NodeProto *> Xt;
    for (int i = 0; i < num_time_steps; ++i) {
        onnx::NodeProto *Xi = gru_graph.add_node();
        Xi->set_name(split_node->output(i) + "_squeezed");
        Xi->set_op_type("Squeeze");
        attr = Xi->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Xi->add_input() = split_node->output(i);
        *Xi->add_output() = Xi->name();
        Xt.push_back(Xi);
    }

    // Process each timestep
    std::vector<onnx::NodeProto *> Hs;
    for (int i = 0; i < num_time_steps; ++i) {
        onnx::NodeProto *Xi = Xt[i];
        onnx::NodeProto *Gi = gru_graph.add_node();
        // Gi = dot(x, transpose(w)) + (B)
        Gi->set_name(Xi->name() + "_gemm1_" + std::to_string(i));
        Gi->set_op_type("Gemm");
        attr = Gi->add_attribute();
        attr->set_name("transB");
        attr->set_i(1);
        *Gi->add_input() = Xi->name();
        *Gi->add_input() = W->name();
        *Gi->add_input() = B->name();
        *Gi->add_output() = Gi->name();

        // S splits Gi into three input components for z, r, h gates
        onnx::NodeProto *S = gru_graph.add_node();
        S->set_name(Gi->name() + "_split");
        S->set_op_type("Split");
        attr = S->add_attribute();
        attr->set_name("axis");
        attr->set_i(1);
        *S->add_input() = Gi->output(0);
        *S->add_output() = S->name() + "_z";
        *S->add_output() = S->name() + "_r";
        *S->add_output() = S->name() + "_h";

        onnx::NodeProto *z_t = nullptr;
        onnx::NodeProto *r_t = nullptr;
        onnx::NodeProto *h_t = nullptr;
        onnx::NodeProto *H = nullptr;

        onnx::NodeProto *One = gru_graph.add_node();
        One->set_name(prefix + "_one");
        One->set_op_type("ConstantFill");  // Constant of shape
        attr = One->add_attribute();
        attr->set_name("shape");
        attr->add_ints(1);
        attr = One->add_attribute();
        attr->set_name("dtype");
        attr->set_i(1);
        attr = One->add_attribute();
        attr->set_name("value");
        attr->set_f(1.0f);
        One->add_output(One->name());

        if (H_t) {
            onnx::NodeProto *G_z = gru_graph.add_node();
            G_z->set_name(prefix + "_G_z_" + std::to_string(i));
            G_z->set_op_type("Gemm");
            attr = G_z->add_attribute();
            attr->set_name("transB");
            attr->set_i(1);
            *G_z->add_input() = H_t->output(0);  // H_(t - 1)
            *G_z->add_input() = Rs->output(0);   // Rz
            *G_z->add_input() = S->output(0);    // Sz
            *G_z->add_output() = G_z->name();

            // z_t = f(.), f defaults Sigmoid
            z_t = gru_graph.add_node();
            z_t->set_name(prefix + "_zt_" + std::to_string(i));
            z_t->set_op_type("Sigmoid");
            *z_t->add_input() = G_z->output(0);
            *z_t->add_output() = z_t->name();

            onnx::NodeProto *NZ = gru_graph.add_node();
            NZ->set_name(prefix + "_N_Z_" + std::to_string(i));
            NZ->set_op_type("Sub");
            *NZ->add_input() = One->output(0);
            *NZ->add_input() = z_t->output(0);
            *NZ->add_output() = NZ->name();

            onnx::NodeProto *ZH = gru_graph.add_node();
            ZH->set_name(prefix + "_Z_H_" + std::to_string(i));
            ZH->set_op_type("Mul");
            *ZH->add_input() = z_t->output(0);
            *ZH->add_input() = H_t->output(0);
            *ZH->add_output() = ZH->name();

            onnx::NodeProto *G_r = gru_graph.add_node();
            G_r->set_name(prefix + "_G_r_" + std::to_string(i));
            G_r->set_op_type("Gemm");
            attr = G_r->add_attribute();
            attr->set_name("transB");
            attr->set_i(1);
            *G_r->add_input() = H_t->output(0);  // H_(t - 1)
            *G_r->add_input() = Rs->output(1);   // Rz
            *G_r->add_input() = S->output(1);    // Sz
            *G_r->add_output() = G_r->name();

            // r_t = f(.), f defaults Sigmoid
            r_t = gru_graph.add_node();
            r_t->set_name(prefix + "_rt_" + std::to_string(i));
            r_t->set_op_type("Sigmoid");
            *r_t->add_input() = G_r->output(0);
            *r_t->add_output() = r_t->name();

            onnx::NodeProto *RH = gru_graph.add_node();
            RH->set_name(prefix + "_RH_" + std::to_string(i));
            RH->set_op_type("Mul");
            *RH->add_input() = r_t->output(0);
            *RH->add_input() = H_t->output(0);
            *RH->add_output() = RH->name();

            onnx::NodeProto *G_h = gru_graph.add_node();
            G_h->set_name(prefix + "_G_h_" + std::to_string(i));
            G_h->set_op_type("Gemm");
            attr = G_h->add_attribute();
            attr->set_name("transB");
            attr->set_i(1);
            *G_h->add_input() = RH->name();     // H_(t - 1)
            *G_h->add_input() = Rs->output(2);  // Rz
            *G_h->add_input() = S->output(2);   // Sz
            *G_h->add_output() = G_h->name();

            // h_t = g(.), g defaults Tanh(.)
            h_t = gru_graph.add_node();
            h_t->set_name(prefix + "_ht_" + std::to_string(i));
            h_t->set_op_type("Tanh");
            *h_t->add_input() = G_h->output(0);
            *h_t->add_output() = h_t->name();

            onnx::NodeProto *NZH = gru_graph.add_node();
            NZH->set_name(prefix + "_N_Z_H_" + std::to_string(i));
            NZH->set_op_type("Mul");
            *NZH->add_input() = NZ->output(0);
            *NZH->add_input() = h_t->output(0);
            *NZH->add_output() = NZH->name();

            // H= (1-z) * H_t + z * H_(t-1)
            H = gru_graph.add_node();
            H->set_name(prefix + "_H_" + std::to_string(i));
            H->set_op_type("Add");
            *H->add_input() = NZH->output(0);
            *H->add_input() = ZH->output(0);
            *H->add_output() = H->name();
        } else {
            // z_t = f(.), f defaults Sigmoid
            z_t = gru_graph.add_node();
            z_t->set_name(prefix + "_zt_" + std::to_string(i));
            z_t->set_op_type("Sigmoid");
            *z_t->add_input() = S->output(0);
            *z_t->add_output() = z_t->name();

            onnx::NodeProto *NZ = gru_graph.add_node();
            NZ->set_name(prefix + "_N_Z_" + std::to_string(i));
            NZ->set_op_type("Sub");
            *NZ->add_input() = One->output(0);
            *NZ->add_input() = z_t->output(0);
            *NZ->add_output() = NZ->name();

            // r_t = f(.), f defaults Sigmoid
            r_t = gru_graph.add_node();
            r_t->set_name(prefix + "_rt_" + std::to_string(i));
            r_t->set_op_type("Sigmoid");
            *r_t->add_input() = S->output(1);
            *r_t->add_output() = r_t->name();

            // h_t = g(.), g defaults Tanh(.)
            h_t = gru_graph.add_node();
            h_t->set_name(prefix + "_rt_" + std::to_string(i));
            h_t->set_op_type("Tanh");
            *h_t->add_input() = S->output(2);
            *h_t->add_output() = h_t->name();

            // H = (1-z) * H_t
            onnx::NodeProto *NZH = gru_graph.add_node();
            NZH->set_name(prefix + "_N_Z_H_" + std::to_string(i));
            NZH->set_op_type("Mul");
            *NZH->add_input() = NZ->output(0);
            *NZH->add_input() = h_t->output(0);
            *NZH->add_output() = NZH->name();
            H = NZH;
        }

        H_t = H;

        onnx::NodeProto *Hu = gru_graph.add_node();
        Hu->set_name(prefix + "_H_unsqueeze_" + std::to_string(i));
        Hu->set_op_type("Unsqueeze");
        attr = Hu->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Hu->add_input() = H->output(0);
        *Hu->add_output() = Hu->name();
        Hs.push_back(Hu);
    }

    // Y: A tensor concating all the intermediate output values of the hidden.
    // It has shape `[seq_length, num_directions, batch_size, hidden_size]`.
    if (node.output_size() >= 1 && !node.output(0).empty()) {
        // Hconcat: concat + unsqueeze
        onnx::NodeProto *Hconcat = gru_graph.add_node();
        Hconcat->set_name(node.output(0) + "_Concat");
        Hconcat->set_op_type("Concat");
        attr = Hconcat->add_attribute();
        attr->set_name("axis");
        attr->set_i(0);
        for (const onnx::NodeProto *input : Hs) {
            Hconcat->add_input(input->name());
        }
        Hconcat->add_output(Hconcat->name());
        onnx::NodeProto *H = gru_graph.add_node();
        H->set_name(node.output(1));
        H->set_op_type("Unsqueeze");
        attr = H->add_attribute();
        attr->set_name("axes");
        attr->add_ints(1);
        H->add_input(Hconcat->output(0));
        H->add_output(H->name());
    }

    // Y_h: The last output value of the hidden.
    // It has shape `[num_directions, batch_size, hidden_size]`.
    if (node.output_size() >= 2 && !node.output(1).empty()) {
        onnx::NodeProto *Y_h = Hs.back();
        Y_h->set_name(node.output(1));
        Y_h->set_output(0, node.output(1));
    }

    // populate rep with inputs;
    assert(node.input_size() == inputs.size());
    std::unordered_map<std::string, Tensor> reps;
    for (int i = 0; i < node.input_size(); ++i) {
        const std::string &input_name = node.input(i);
        const Tensor &t = inputs[i];
        reps[input_name] = t;
    }

    Node result;
    convert_subgraph(gru_graph, reps, result.requirements);

    // extract outputs from reps;
    result.inputs = inputs;
    result.outputs.resize(node.output_size());
    for (int i = 0; i < node.output_size(); ++i) {
        if (!node.output(i).empty()) {
            const Tensor &t = reps.at(node.output(i));
            result.outputs[i] = t;
        }
    }
    return result;
}

Node convert_rnn_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    int hidden_size = 8;
    bool input_forget = false;
    std::string direction = "forward";
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "hidden_size") {
            hidden_size = attr.i();
        } else if (attr.name() == "input_forget") {
            input_forget = static_cast<bool>(attr.i());
        } else if (attr.name() == "direction") {
            direction = attr.s();
        } else if (
            attr.name() == "clip" || attr.name() == "activation_alpha" ||
            attr.name() == "activation_beta" || attr.name() == "activations") {
            throw std::domain_error(attr.name() + " not supported yet");
        }
    }

    // TBD: handle these cases
    if (direction != "forward") {
        throw std::domain_error("Unsupported direction");
    }
    if (input_forget) {
        throw std::domain_error("input_forget not supported yet");
    }

    const int rank = inputs[0].shape.size();
    if (rank != 3) {
        throw std::domain_error("Invalid rank");
    }

    const Halide::Expr dim_expr = Halide::Internal::simplify(inputs[0].shape[0]);
    const int64_t *dim = Halide::Internal::as_const_int(dim_expr);
    if (!dim) {
        throw std::domain_error("Unknown number of timesteps");
    }
    const int num_time_steps = *dim;
    if (num_time_steps < 1) {
        throw std::domain_error("At least one timestep is required");
    }

    // Build an onnx graph encoding the RNN computations
    onnx::GraphProto rnn_graph;
    // TODO: generate unique prefixes in case there is more than 1 unnamed RNN
    // node
    const std::string prefix =
        node.name().empty() ? std::string("rnn") : node.name();
    // Split input into timesteps
    onnx::NodeProto *split_node = rnn_graph.add_node();
    split_node->set_name(prefix + "_split");
    split_node->set_op_type("Split");
    onnx::AttributeProto *attr = split_node->add_attribute();
    attr->set_name("axis");
    attr->set_i(0);
    *split_node->add_input() = node.input(0);
    for (int i = 0; i < num_time_steps; ++i) {
        *split_node->add_output() = prefix + "_t" + std::to_string(i);
    }

    // Squeeze the first dim output the Xi, W and R tensors since we're only
    // supporting unidirectional RNN for now
    onnx::NodeProto *W = rnn_graph.add_node();
    W->set_name(node.input(1) + "_squeezed");
    W->set_op_type("Squeeze");
    attr = W->add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    *W->add_input() = node.input(1);
    *W->add_output() = W->name();

    onnx::NodeProto *R = rnn_graph.add_node();
    R->set_name(node.input(2) + "_squeezed");
    R->set_op_type("Squeeze");
    attr = R->add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    *R->add_input() = node.input(2);
    *R->add_output() = R->name();

    // Bias B, if any
    onnx::NodeProto *B = nullptr;
    if (inputs.size() >= 4 && !node.input(3).empty()) {
        // Preprocess the bias tensor
        onnx::NodeProto *Bs = rnn_graph.add_node();
        Bs->set_name(node.input(3) + "_split");
        Bs->set_op_type("Split");
        attr = Bs->add_attribute();
        attr->set_name("axis");
        attr->set_i(1);
        Bs->add_input(node.input(3));
        Bs->add_output(Bs->name() + "_0");
        Bs->add_output(Bs->name() + "_1");

        B = rnn_graph.add_node();
        B->set_name(node.input(3) + "_sum");
        B->set_op_type("Add");
        B->add_input(Bs->output(0));
        B->add_input(Bs->output(1));
        B->add_output(B->name());
    } else {
        B = rnn_graph.add_node();
        B->set_name(prefix + "_zero");
        B->set_op_type("ConstantFill");
        attr = B->add_attribute();
        attr->set_name("shape");
        attr->add_ints(1);
        B->add_output(B->name());
    }

    if (inputs.size() >= 5 && !node.input(4).empty()) {
        throw std::domain_error("Unsupported prespecified seq_len");
    }
    // Initial state if any
    onnx::NodeProto *H_t = nullptr;
    if (inputs.size() >= 6 && !node.input(5).empty()) {
        H_t = rnn_graph.add_node();
        H_t->set_name(node.input(5) + "_squeezed");
        H_t->set_op_type("Squeeze");
        attr = H_t->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *H_t->add_input() = node.input(5);
        *H_t->add_output() = H_t->name();
    }
    //  Ht = f(Xt*(Wi^T) + Ht-1*(Ri^T) + Wbi + Rbi); f Tanh default
    std::vector<onnx::NodeProto *> Xt;
    for (int i = 0; i < num_time_steps; ++i) {
        onnx::NodeProto *Xi = rnn_graph.add_node();
        Xi->set_name(split_node->output(i) + "_squeezed");
        Xi->set_op_type("Squeeze");
        attr = Xi->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Xi->add_input() = split_node->output(i);
        *Xi->add_output() = Xi->name();
        Xt.push_back(Xi);
    }

    // Process each timestep
    std::vector<onnx::NodeProto *> Hs;
    for (int i = 0; i < num_time_steps; ++i) {
        onnx::NodeProto *Xi = Xt[i];
        onnx::NodeProto *Gi = rnn_graph.add_node();
        // Gi = dot(x, transpose(w)) + bias
        Gi->set_name(Xi->name() + "_gemm1_" + std::to_string(i));
        Gi->set_op_type("Gemm");
        attr = Gi->add_attribute();
        attr->set_name("transB");
        attr->set_i(1);
        *Gi->add_input() = Xi->name();
        *Gi->add_input() = W->name();
        *Gi->add_input() = B->name();
        *Gi->add_output() = Gi->name();

        onnx::NodeProto *Gii = Gi;
        if (H_t) {
            // Gii = dot(H_t, transpose(R)) + Gi;
            Gii = rnn_graph.add_node();
            Gii->set_name(Xi->name() + "_gemm2_" + std::to_string(i));
            Gii->set_op_type("Gemm");
            attr = Gii->add_attribute();
            attr->set_name("transB");
            attr->set_i(1);
            *Gii->add_input() = H_t->name();
            *Gii->add_input() = R->name();
            *Gii->add_input() = Gi->name();
            *Gii->add_output() = Gii->name();
        }

        onnx::NodeProto *H = rnn_graph.add_node();
        // activation of H, currently default: Tanh
        H->set_name(prefix + "_H_" + std::to_string(i));
        H->set_op_type("Tanh");
        *H->add_input() = Gii->output(0);
        *H->add_output() = H->name();

        H_t = H;

        onnx::NodeProto *Hu = rnn_graph.add_node();
        Hu->set_name(prefix + "_H_unsqueeze_" + std::to_string(i));
        Hu->set_op_type("Unsqueeze");
        attr = Hu->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Hu->add_input() = H->output(0);
        *Hu->add_output() = Hu->name();
        Hs.push_back(Hu);
    }

    // Output
    // concat + unsqueeze
    if (node.output_size() >= 1 && !node.output(0).empty()) {
        onnx::NodeProto *Hconcat = rnn_graph.add_node();
        Hconcat->set_name(node.output(0) + "_concat");
        Hconcat->set_op_type("Concat");
        attr = Hconcat->add_attribute();
        attr->set_name("axis");
        attr->set_i(0);
        for (const onnx::NodeProto *input : Hs) {
            Hconcat->add_input(input->name());
        }
        Hconcat->add_output(Hconcat->name());
        onnx::NodeProto *H = rnn_graph.add_node();
        H->set_name(node.output(0));
        H->set_op_type("Unsqueeze");
        attr = H->add_attribute();
        attr->set_name("axes");
        attr->add_ints(1);
        H->add_input(Hconcat->output(0));
        H->add_output(H->name());
    }

    if (node.output_size() >= 2 && !node.output(1).empty()) {
        onnx::NodeProto *Y_h = Hs.back();
        Y_h->set_name(node.output(1));
        Y_h->set_output(0, node.output(1));
    }

    // populate rep with inputs;
    assert(node.input_size() == inputs.size());
    std::unordered_map<std::string, Tensor> reps;
    for (int i = 0; i < node.input_size(); ++i) {
        const std::string &input_name = node.input(i);
        const Tensor &t = inputs[i];
        reps[input_name] = t;
    }
    Node result;
    convert_subgraph(rnn_graph, reps, result.requirements);

    // extract outputs from reps;
    result.inputs = inputs;
    result.outputs.resize(node.output_size());
    for (int i = 0; i < node.output_size(); ++i) {
        if (!node.output(i).empty()) {
            const Tensor &t = reps.at(node.output(i));
            result.outputs[i] = t;
        }
    }
    return result;
}

Node convert_lstm_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    int hidden_size = 1;
    bool input_forget = false;
    std::string direction = "forward";
    for (const auto &attr : node.attribute()) {
        if (attr.name() == "hidden_size") {
            hidden_size = attr.i();
        } else if (attr.name() == "input_forget") {
            input_forget = static_cast<bool>(attr.i());
        } else if (attr.name() == "direction") {
            direction = attr.s();
        } else if (
            attr.name() == "clip" || attr.name() == "activation_alpha" ||
            attr.name() == "activation_beta" || attr.name() == "activations") {
            throw std::domain_error(attr.name() + " not supported yet");
        }
    }

    // TBD: handle these cases
    if (direction != "forward") {
        throw std::domain_error("Unsupported direction");
    }
    if (input_forget) {
        throw std::domain_error("input_forget not supported yet");
    }

    const int rank = inputs[0].shape.size();
    if (rank != 3) {
        throw std::domain_error("Invalid rank");
    }
    const Halide::Expr dim_expr = Halide::Internal::simplify(inputs[0].shape[0]);
    const int64_t *dim = Halide::Internal::as_const_int(dim_expr);
    if (!dim) {
        throw std::domain_error("Unknown number of timesteps");
    }
    const int num_time_steps = *dim;
    if (num_time_steps < 1) {
        throw std::domain_error("At least one timestep is required");
    }
    // Build an onnx graph encoding the LSTM computations
    onnx::GraphProto lstm_graph;

    // TODO: generate unique prefixes in case there is more than 1 unnamed lstm
    // node
    const std::string prefix =
        node.name().empty() ? std::string("lstm") : node.name();

    // Split input into timesteps
    onnx::NodeProto *split_node = lstm_graph.add_node();
    split_node->set_name(prefix + "_split");
    split_node->set_op_type("Split");
    onnx::AttributeProto *attr = split_node->add_attribute();
    attr->set_name("axis");
    attr->set_i(0);
    *split_node->add_input() = node.input(0);
    for (int i = 0; i < num_time_steps; ++i) {
        *split_node->add_output() = prefix + "_t" + std::to_string(i);
    }

    // Squeeze the first dim output the Xi, W and R tensors since we're only
    // supporting unidirectional LSTM for now
    onnx::NodeProto *W = lstm_graph.add_node();
    W->set_name(node.input(1) + "_squeezed");
    W->set_op_type("Squeeze");
    attr = W->add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    *W->add_input() = node.input(1);
    *W->add_output() = W->name();

    onnx::NodeProto *R = lstm_graph.add_node();
    R->set_name(node.input(2) + "_squeezed");
    R->set_op_type("Squeeze");
    attr = R->add_attribute();
    attr->set_name("axes");
    attr->add_ints(0);
    *R->add_input() = node.input(2);
    *R->add_output() = R->name();

    onnx::NodeProto *B = nullptr;
    if (inputs.size() >= 4 && !node.input(3).empty()) {
        // Preprocess the bias tensor
        onnx::NodeProto *Bs = lstm_graph.add_node();
        Bs->set_name(node.input(3) + "_split");
        Bs->set_op_type("Split");
        attr = Bs->add_attribute();
        attr->set_name("axis");
        attr->set_i(1);
        Bs->add_input(node.input(3));
        Bs->add_output(Bs->name() + "_0");
        Bs->add_output(Bs->name() + "_1");

        B = lstm_graph.add_node();
        B->set_name(node.input(3) + "_sum");
        B->set_op_type("Add");
        B->add_input(Bs->output(0));
        B->add_input(Bs->output(1));
        B->add_output(B->name());
    }

    // Initial state if any
    onnx::NodeProto *H_t = nullptr;
    if (inputs.size() >= 6 && !node.input(5).empty()) {
        H_t = lstm_graph.add_node();
        H_t->set_name(node.input(5) + "_squeezed");
        H_t->set_op_type("Squeeze");
        attr = H_t->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *H_t->add_input() = node.input(5);
        *H_t->add_output() = H_t->name();
    }
    onnx::NodeProto *C_t = nullptr;
    if (inputs.size() >= 7 && !node.input(6).empty()) {
        C_t = lstm_graph.add_node();
        C_t->set_name(node.input(6) + "_squeezed");
        C_t->set_op_type("Squeeze");
        attr = C_t->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *C_t->add_input() = node.input(6);
        *C_t->add_output() = C_t->name();
    }

    // Optional peephole inputs
    onnx::NodeProto *P = nullptr;
    if (inputs.size() >= 8 && !node.input(7).empty()) {
        onnx::NodeProto *Ps = lstm_graph.add_node();
        Ps->set_name(node.input(7) + "_squeezed");
        Ps->set_op_type("Squeeze");
        attr = Ps->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Ps->add_input() = node.input(7);
        *Ps->add_output() = Ps->name();

        P = lstm_graph.add_node();
        P->set_name(node.input(7) + "_split");
        P->set_op_type("Split");
        *P->add_input() = Ps->output(0);
        *P->add_output() = P->name() + "_0";
        *P->add_output() = P->name() + "_1";
        *P->add_output() = P->name() + "_2";
    }

    std::vector<onnx::NodeProto *> Xt;
    for (int i = 0; i < num_time_steps; ++i) {
        onnx::NodeProto *Xi = lstm_graph.add_node();
        Xi->set_name(split_node->output(i) + "_squeezed");
        Xi->set_op_type("Squeeze");
        attr = Xi->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Xi->add_input() = split_node->output(i);
        *Xi->add_output() = Xi->name();
        Xt.push_back(Xi);
    }

    // Process each timestep
    std::vector<onnx::NodeProto *> Hs;
    for (int i = 0; i < num_time_steps; ++i) {
        onnx::NodeProto *Xi = Xt[i];
        onnx::NodeProto *Gi = lstm_graph.add_node();
        // Gi = dot(x, transpose(w)) + bias
        Gi->set_name(Xi->name() + "_gemm1_" + std::to_string(i));
        Gi->set_op_type("Gemm");
        attr = Gi->add_attribute();
        attr->set_name("transB");
        attr->set_i(1);
        *Gi->add_input() = Xi->name();
        *Gi->add_input() = W->name();
        if (B) {
            *Gi->add_input() = B->name();
        }
        *Gi->add_output() = Gi->name();

        onnx::NodeProto *Gii = Gi;
        if (H_t) {
            // Gii = Gi + dot(H_t, transpose(R));
            Gii = lstm_graph.add_node();
            Gii->set_name(Xi->name() + "_gemm2_" + std::to_string(i));
            Gii->set_op_type("Gemm");
            attr = Gii->add_attribute();
            attr->set_name("transB");
            attr->set_i(1);
            *Gii->add_input() = H_t->name();
            *Gii->add_input() = R->name();
            *Gii->add_input() = Gi->name();
            *Gii->add_output() = Gii->name();
        }
        // i, o, f, c = split(Gii, 4, -1)
        onnx::NodeProto *split_node = lstm_graph.add_node();
        split_node->set_name(prefix + "_split_" + std::to_string(i));
        split_node->set_op_type("Split");
        attr = split_node->add_attribute();
        attr->set_name("axis");
        attr->set_i(-1);
        *split_node->add_input() = Gii->output(0);
        for (int j = 0; j < 4; ++j) {
            *split_node->add_output() = split_node->name() + "_" + std::to_string(j);
        }
        // i = sigmoid(i + p_i * C_t)
        onnx::NodeProto *add = nullptr;
        if (P && C_t) {
            onnx::NodeProto *pict = lstm_graph.add_node();
            pict->set_name(prefix + "_pi_ct_" + std::to_string(i));
            pict->set_op_type("Mul");
            *pict->add_input() = P->output(0);
            *pict->add_input() = C_t->output(0);
            *pict->add_output() = pict->name();

            add = lstm_graph.add_node();
            add->set_name(prefix + "i_pi_ct_" + std::to_string(i));
            add->set_op_type("Add");
            *add->add_input() = split_node->output(0);
            *add->add_input() = pict->output(0);
            *add->add_output() = add->name();
        }

        onnx::NodeProto *node_i = lstm_graph.add_node();
        node_i->set_name(prefix + "_i_" + std::to_string(i));
        node_i->set_op_type("Sigmoid");
        if (add) {
            *node_i->add_input() = add->output(0);
        } else {
            *node_i->add_input() = split_node->output(0);
        }
        *node_i->add_output() = node_i->name();

        // f = sigmoid(f + p_f * C_t)
        add = nullptr;
        if (P && C_t) {
            onnx::NodeProto *pfct = lstm_graph.add_node();
            pfct->set_name(prefix + "_pf_ct_" + std::to_string(i));
            pfct->set_op_type("Mul");
            *pfct->add_input() = P->output(2);
            *pfct->add_input() = C_t->output(0);
            *pfct->add_output() = pfct->name();

            add = lstm_graph.add_node();
            add->set_name(prefix + "f_pf_ct_" + std::to_string(i));
            add->set_op_type("Add");
            *add->add_input() = split_node->output(2);
            *add->add_input() = pfct->output(0);
            *add->add_output() = add->name();
        }

        onnx::NodeProto *node_f = lstm_graph.add_node();
        node_f->set_name(prefix + "_f_" + std::to_string(i));
        node_f->set_op_type("Sigmoid");
        if (add) {
            *node_f->add_input() = add->output(0);
        } else {
            *node_f->add_input() = split_node->output(2);
        }
        *node_f->add_output() = node_f->name();

        // c = tanh(c)
        onnx::NodeProto *node_c = lstm_graph.add_node();
        node_c->set_name(prefix + "_c_" + std::to_string(i));
        node_c->set_op_type("Tanh");
        *node_c->add_input() = split_node->output(3);
        *node_c->add_output() = node_c->name();

        // C = f * C_t + i*c
        onnx::NodeProto *ic = lstm_graph.add_node();
        ic->set_name(prefix + "_ic_" + std::to_string(i));
        ic->set_op_type("Mul");
        *ic->add_input() = node_i->output(0);
        *ic->add_input() = node_c->output(0);
        *ic->add_output() = ic->name();
        onnx::NodeProto *C = ic;

        if (C_t) {
            // add f*C_t to ic
            onnx::NodeProto *f_ct = lstm_graph.add_node();
            f_ct->set_name(prefix + "_f_ct_" + std::to_string(i));
            f_ct->set_op_type("Mul");
            *f_ct->add_input() = node_f->output(0);
            *f_ct->add_input() = C_t->output(0);
            *f_ct->add_output() = f_ct->name();

            add = lstm_graph.add_node();
            add->set_name(prefix + "f_ct_ic_" + std::to_string(i));
            add->set_op_type("Add");
            *add->add_input() = f_ct->output(0);
            *add->add_input() = ic->output(0);
            *add->add_output() = add->name();
            C = add;
        }

        // o = sigmoid(o + p_o * C)
        add = nullptr;
        if (P) {
            onnx::NodeProto *po_c = lstm_graph.add_node();
            po_c->set_name(prefix + "_po_c_" + std::to_string(i));
            po_c->set_op_type("Mul");
            *po_c->add_input() = C->output(0);
            *po_c->add_input() = P->output(1);
            *po_c->add_output() = po_c->name();

            add = lstm_graph.add_node();
            add->set_name(prefix + "o_po_c_" + std::to_string(i));
            add->set_op_type("Add");
            *add->add_input() = split_node->output(1);
            *add->add_input() = po_c->output(0);
            *add->add_output() = add->name();
        }
        onnx::NodeProto *node_o = lstm_graph.add_node();
        node_o->set_name(prefix + "_o_" + std::to_string(i));
        node_o->set_op_type("Sigmoid");
        if (add) {
            *node_o->add_input() = add->output(0);
        } else {
            *node_o->add_input() = split_node->output(1);
        }
        *node_o->add_output() = node_o->name();

        // H = o * tanh(C)
        onnx::NodeProto *hC = lstm_graph.add_node();
        hC->set_name(prefix + "_hC_" + std::to_string(i));
        hC->set_op_type("Tanh");
        *hC->add_input() = C->output(0);
        *hC->add_output() = hC->name();

        onnx::NodeProto *H = lstm_graph.add_node();
        H->set_name(prefix + "_H_" + std::to_string(i));
        H->set_op_type("Mul");
        *H->add_input() = node_o->output(0);
        *H->add_input() = hC->output(0);
        *H->add_output() = H->name();

        H_t = H;
        C_t = C;

        onnx::NodeProto *Hu = lstm_graph.add_node();
        Hu->set_name(prefix + "_H_unsqueeze_" + std::to_string(i));
        Hu->set_op_type("Unsqueeze");
        attr = Hu->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        *Hu->add_input() = H->output(0);
        *Hu->add_output() = Hu->name();
        Hs.push_back(Hu);
    }

    if (node.output_size() >= 2 && !node.output(1).empty()) {
        onnx::NodeProto *Y_h = Hs.back();
        Y_h->set_name(node.output(1));
        Y_h->set_output(0, node.output(1));
    }

    if (node.output_size() >= 3 && !node.output(2).empty()) {
        onnx::NodeProto *Y_h = lstm_graph.add_node();
        Y_h->set_name(node.output(2));
        Y_h->set_op_type("Unsqueeze");
        attr = Y_h->add_attribute();
        attr->set_name("axes");
        attr->add_ints(0);
        Y_h->add_input(H_t->output(0));
        Y_h->add_output(Y_h->name());
    }

    if (node.output_size() >= 1 && !node.output(0).empty()) {
        onnx::NodeProto *Hconcat = lstm_graph.add_node();
        Hconcat->set_name(node.output(0) + "_concat");
        Hconcat->set_op_type("Concat");
        attr = Hconcat->add_attribute();
        attr->set_name("axis");
        attr->set_i(0);
        for (const onnx::NodeProto *input : Hs) {
            Hconcat->add_input(input->name());
        }
        Hconcat->add_output(Hconcat->name());
        onnx::NodeProto *H = lstm_graph.add_node();
        H->set_name(node.output(0));
        H->set_op_type("Unsqueeze");
        attr = H->add_attribute();
        attr->set_name("axes");
        attr->add_ints(1);
        H->add_input(Hconcat->output(0));
        H->add_output(H->name());
    }

    // populate rep with inputs;
    assert(node.input_size() == inputs.size());
    std::unordered_map<std::string, Tensor> reps;
    for (int i = 0; i < node.input_size(); ++i) {
        const std::string &input_name = node.input(i);
        const Tensor &t = inputs[i];
        reps[input_name] = t;
    }

    Node result;
    convert_subgraph(lstm_graph, reps, result.requirements);

    // extract outputs from reps;
    result.inputs = inputs;
    result.outputs.resize(node.output_size());
    for (int i = 0; i < node.output_size(); ++i) {
        if (!node.output(i).empty()) {
            const Tensor &t = reps.at(node.output(i));
            result.outputs[i] = t;
        }
    }
    return result;
}

Node convert_node(
    const onnx::NodeProto &node,
    const std::vector<Tensor> &inputs) {
    // Handle ATen ops whenever possible by converting them to native ONNX ops.
    if (node.op_type() == "ATen") {
        std::string actual_op;
        for (const auto &attr : node.attribute()) {
            if (attr.name() == "operator") {
                actual_op = attr.s();
                break;
            }
        }
        onnx::NodeProto actual_node = node;
        if (actual_op == "ceil") {
            actual_node.set_op_type("Ceil");
        } else if (actual_op == "floor") {
            actual_node.set_op_type("Floor");
        } else if (actual_op == "where") {
            actual_node.set_op_type("Where");
        } else {
            throw std::domain_error(
                "Unsupported ATen op type " + actual_op + " for node " + node.name());
        }
        return convert_node(actual_node, inputs);
    }
    // Handle meta ops
    if (node.op_type() == "RNN") {
        return convert_rnn_node(node, inputs);
    }
    if (node.op_type() == "LSTM") {
        return convert_lstm_node(node, inputs);
    }
    if (node.op_type() == "GRU") {
        return convert_gru_node(node, inputs);
    }
    // Handle metadata operations
    if (node.op_type() == "Shape" || node.op_type() == "Size") {
        return convert_metadata_node(node, inputs);
    }
    // Start with nodes that require special handling.
    if (node.op_type() == "Gemm") {
        return convert_gemm_node(node, inputs);
    }
    if (node.op_type() == "MatMul") {
        return convert_matmul_node(node, inputs);
    }
    if (node.op_type() == "Conv") {
        return convert_conv_node(node, inputs);
    }
    if (node.op_type().find("Reduce") == 0) {
        return convert_reduction_node(node, inputs);
    }
    if (node.op_type() == "BatchNormalization") {
        return convert_batchnorm_node(node, inputs);
    }
    if (node.op_type() == "Dropout") {
        return convert_dropout_node(node, inputs);
    }
    if (node.op_type().length() >= 6 &&
        node.op_type().find("Pool") == node.op_type().length() - 4) {
        return convert_pooling_node(node, inputs);
    }
    if (node.op_type() == "Softmax" || node.op_type() == "LogSoftmax") {
        return convert_softmax_node(node, inputs);
    }
    if (node.op_type() == "Concat") {
        return convert_concat_node(node, inputs);
    }
    if (node.op_type() == "Slice") {
        return convert_slice_node(node, inputs);
    }
    if (node.op_type() == "Split") {
        return convert_split_node(node, inputs);
    }
    if (node.op_type() == "Pad") {
        return convert_pad_node(node, inputs);
    }
    if (node.op_type() == "Transpose") {
        return convert_transpose_node(node, inputs);
    }
    if (node.op_type() == "Squeeze") {
        return convert_squeeze_node(node, inputs);
    }
    if (node.op_type() == "Unsqueeze") {
        return convert_unsqueeze_node(node, inputs);
    }
    if (node.op_type() == "Reshape") {
        return convert_reshape_node(node, inputs);
    }
    if (node.op_type() == "OneHot") {
        return convert_one_hot_node(node, inputs);
    }
    if (node.op_type() == "Flatten") {
        return convert_flatten_node(node, inputs);
    }
    if (node.op_type() == "Tile") {
        return convert_tile_node(node, inputs);
    }
    if (node.op_type() == "ConstantFill") {
        return convert_constant_fill_node(node, inputs);
    }
    if (node.op_type() == "ConstantOfShape") {
        return convert_constant_of_shape(node, inputs);
    }
    if (node.op_type() == "Where") {
        return convert_where_node(node, inputs);
    }
    if (node.op_type() == "Gather") {
        return convert_gather_node(node, inputs);
    }
    if (node.op_type() == "Expand") {
        return convert_expand_node(node, inputs);
    }
    if (node.op_type() == "RandomUniform" || node.op_type() == "RandomNormal") {
        return convert_random_node(node, inputs);
    }
    if (node.op_type() == "Shrink") {
        return convert_shrink_node(node, inputs);
    }
    if (node.op_type() == "LRN") {
        return convert_lrn_node(node, inputs);
    }
    if (node.op_type() == "IsInf") {
        return convert_isinf_node(node, inputs);
    }
    if (node.op_type() == "Clip") {
        return convert_clip_node(node, inputs);
    }
    // Handle exponential linear units.
    if (node.op_type() == "Elu" || node.op_type() == "Selu" ||
        node.op_type() == "LeakyRelu" || node.op_type() == "ThresholdedRelu") {
        return convert_elu_node(node, inputs);
    }
    // Handle coefficient-wise operators.
    if (node.input_size() == 0) {
        return convert_nullary_op_node(node);
    } else if (node.input_size() == 1 && node.output_size() == 1) {
        return convert_unary_op_node(node, inputs);
    } else if (node.input_size() == 2 && node.output_size() == 1) {
        return convert_binary_op_node(node, inputs);
    } else if (node.input_size() > 2 && node.output_size() == 1) {
        return convert_variadic_op_node(node, inputs);
    }

    throw std::domain_error("Unsupported op type " + node.op_type());
}

Halide::ImageParam encode_as_image_param(
    const onnx::ValueInfoProto &input,
    const std::unordered_map<std::string, int> &expected_dim_sizes,
    const IOLayout layout,
    std::unordered_map<std::string, Halide::Internal::Dimension> *symbolic_dims,
    std::vector<Halide::Expr> *shape,
    std::vector<Halide::Expr> *requirements) {
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
    Halide::ImageParam result(t, num_dims, sanitize_name(input.name()));

    // Encode the input shape as bounds on the dimensions for the autoscheduler.
    const onnx::TensorShapeProto &dims = input.type().tensor_type().shape();
    for (int i = 0; i < num_dims; ++i) {
        const onnx::TensorShapeProto::Dimension &dim = dims.dim(i);
        if (dim.has_dim_value()) {
            int dim_val = dim.dim_value();
            if (dim_val <= 0) {
                throw std::invalid_argument("Invalid shape for input " + input.name());
            }
            result.dim(i).set_bounds(0, dim_val);
            result.dim(i).set_estimate(0, dim_val);
            shape->push_back(static_cast<int>(dim_val));
        } else {
            assert(dim.has_dim_param());
            if (symbolic_dims->find(dim.dim_param()) != symbolic_dims->end()) {
                Halide::Internal::Dimension new_dim =
                    symbolic_dims->at(dim.dim_param());
                shape->push_back(new_dim.extent());
                result.dim(i).set_bounds(0, shape->back());
            } else {
                Halide::Internal::Dimension new_dim = result.dim(i);
                new_dim.set_min(0);
                shape->push_back(new_dim.extent());
                symbolic_dims->emplace(dim.dim_param(), new_dim);
                // The dimension should be at least of size 1.
                requirements->push_back(shape->back() > 0);
            }

            // Dimension is unknown, just make a guess.
            if (expected_dim_sizes.find(dim.dim_param()) !=
                expected_dim_sizes.end()) {
                int expected_size = expected_dim_sizes.at(dim.dim_param());
                result.dim(i).set_estimate(0, expected_size);
            } else {
                // Use an estimate that's in the ballpark of what we can expect for
                // typical neural network inputs.
                result.dim(i).set_estimate(0, 128);
            }
        }
    }

    switch (layout) {
    case Native: {
        Halide::Expr stride = 1;
        for (int i = 0; i < num_dims; ++i) {
            result.dim(i).set_stride(stride);
            stride = stride * shape->at(i);
        }
        break;
    }
    case NumPy: {
        Halide::Expr stride = 1;
        for (int i = num_dims - 1; i >= 0; --i) {
            result.dim(i).set_stride(stride);
            stride = stride * shape->at(i);
        }
        break;
    }
    }

    return result;
}

std::vector<Halide::Expr> finalize_type_info(
    const onnx::TypeProto &tp,
    const Tensor &t,
    const IOLayout &layout,
    const std::unordered_map<std::string, Halide::Internal::Dimension> &
        symbolic_dims,
    const std::string &name,
    std::vector<Halide::Expr> &requirements) {
    std::vector<Halide::Expr> result = t.shape;
    if (tp.has_tensor_type()) {
        if (t.type != tp.tensor_type().elem_type()) {
            throw std::invalid_argument(
                "Inconsistent data types detected for tensor " + name +
                ", expected " + std::to_string(tp.tensor_type().elem_type()) +
                " instead got " + std::to_string(t.type));
        }

        if (tp.tensor_type().has_shape()) {
            const onnx::TensorShapeProto &tp_shape = tp.tensor_type().shape();
            if (result.size() != tp_shape.dim_size()) {
                throw std::invalid_argument(
                    "Inconsistent ranks detected for tensor " + name + ", expected " +
                    std::to_string(tp_shape.dim_size()) + " instead got " +
                    std::to_string(result.size()));
            }
            for (int i = 0; i < tp_shape.dim_size(); ++i) {
                if (tp_shape.dim(i).has_dim_value()) {
                    int dim_value = static_cast<int>(tp_shape.dim(i).dim_value());
                    requirements.push_back(t.shape[i] == dim_value);
                    result[i] = dim_value;
                } else if (tp_shape.dim(i).has_dim_param()) {
                    auto it = symbolic_dims.find(tp_shape.dim(i).dim_param());
                    if (it != symbolic_dims.end()) {
                        Halide::Expr dim = it->second.extent();
                        requirements.push_back(t.shape[i] == dim);
                        result[i] = dim;
                    }
                }
            }
        }
    }

    for (int i = 0; i < result.size(); ++i) {
        result[i] = Halide::Internal::simplify(result[i]);
        t.rep.output_buffer().dim(i).set_bounds(0, result[i]);
    }

    switch (layout) {
    case Native: {
        Halide::Expr stride = 1;
        for (int i = 0; i < result.size(); ++i) {
            t.rep.output_buffer().dim(i).set_stride(stride);
            stride = stride * result[i];
        }
        break;
    }
    case NumPy: {
        Halide::Expr stride = 1;
        for (int i = result.size() - 1; i >= 0; --i) {
            t.rep.output_buffer().dim(i).set_stride(stride);
            stride = stride * result[i];
        }
        break;
    }
    }

    return result;
}

Model convert_model(
    const onnx::ModelProto &model,
    const std::unordered_map<std::string, int> &expected_dim_sizes,
    IOLayout layout) {
    Model result;
    std::unordered_map<std::string, Tensor> &reps = result.tensors;
    std::unordered_map<std::string, Halide::Internal::Dimension> symbolic_dims;

    // Encode the constants inputs.
    for (const auto &constant : model.graph().initializer()) {
        Tensor t = build_from_constant(constant, sanitize_name(constant.name()));
        reps[constant.name()] = t;
    }

    // Encode the variable inputs as Halide ImageParam. Note that constant inputs
    // can be listed here as well, so we need to filter them out.
    for (const auto &input : model.graph().input()) {
        if (reps.find(input.name()) != reps.end()) {
            continue;
        }
        std::vector<Halide::Expr> shape;
        Halide::ImageParam p(encode_as_image_param(
            input,
            expected_dim_sizes,
            layout,
            &symbolic_dims,
            &shape,
            &result.requirements));
        result.inputs[input.name()] = p;
        reps[input.name()] = Tensor{input.name(),
                                    static_cast<onnx::TensorProto::DataType>(
                                        input.type().tensor_type().elem_type()),
                                    shape,
                                    p};
    }

    convert_subgraph(model.graph(), reps, result.requirements);

    // Check if output tensors are also used as inputs to other nodes.
    std::unordered_map<std::string, bool> output_types;
    for (const auto &output : model.graph().output()) {
        output_types.emplace(output.name(), false);
    }
    for (const auto &node : model.graph().node()) {
        for (const auto &input_name : node.input()) {
            if (output_types.find(input_name) != output_types.end()) {
                output_types[input_name] = true;
            }
        }
    }

    // Last but not least, extract the model outputs.
    for (const auto &output : model.graph().output()) {
        if (reps.find(output.name()) == reps.end()) {
            throw std::invalid_argument(
                "Output " + output.name() +
                " isn't generated by any node from the graph");
        }
        Tensor &t = reps.at(output.name());

        Tensor t_out = t;
        if (output_types[output.name()]) {
            // The scheduler doesn't support outputs that are also used by other
            // funcs. Make a copy of the output function to avoid this corner case.
            t_out.rep = Halide::Func(t.rep.name() + "_output");
            t_out.rep(Halide::_) = t.rep(Halide::_);
        }

        // Merge type info.
        t_out.shape = finalize_type_info(
            output.type(),
            t_out,
            layout,
            symbolic_dims,
            output.name(),
            result.requirements);

        // Encode the output shape as bounds on the value of the args to help the
        // the autoscheduler.
        Halide::Func &f = t_out.rep;
        const std::vector<Halide::Var> &args = f.args();
        const std::vector<Halide::Expr> &dims = t.shape;

        if (args.size() != dims.size()) {
            throw std::domain_error("Invalid dimensions for output " + output.name());
        }
        for (int i = 0; i < args.size(); ++i) {
            const int64_t *dim_value = Halide::Internal::as_const_int(dims[i]);
            if (dim_value) {
                int dim = static_cast<int>(*dim_value);
                f.set_estimate(args[i], 0, dim);
            } else {
                // Dimension is unknown, make a guess
                f.set_estimate(args[i], 0, 1000);
            }
        }
        result.outputs[output.name()] = t_out;
    }

    return result;
}

Halide::Type get_halide_type(const Tensor &tensor) {
    switch (tensor.type) {
    case onnx::TensorProto_DataType_FLOAT:
        return Halide::Float(32);
    case onnx::TensorProto_DataType_DOUBLE:
        return Halide::Float(64);
    case onnx::TensorProto_DataType_INT8:
        return Halide::Int(8);
    case onnx::TensorProto_DataType_INT16:
        return Halide::Int(16);
    case onnx::TensorProto_DataType_INT32:
        return Halide::Int(32);
    case onnx::TensorProto_DataType_INT64:
        return Halide::Int(64);
    case onnx::TensorProto_DataType_UINT8:
        return Halide::UInt(8);
    case onnx::TensorProto_DataType_UINT16:
        return Halide::UInt(16);
    case onnx::TensorProto_DataType_UINT32:
        return Halide::UInt(32);
    case onnx::TensorProto_DataType_UINT64:
        return Halide::UInt(64);
    case onnx::TensorProto_DataType_BOOL:
        return Halide::Bool();
    default:
        throw std::domain_error("Unsupported or unknown target type");
    }
    throw std::domain_error("Unsupported or unknown target type");
}

static int64_t infer_dim_from_inputs(
    Halide::Expr dim,
    const std::vector<std::pair<Halide::Expr, Halide::Expr>> &replacements,
    const std::string &name) {
    Halide::Expr result = dim;
    for (const auto &replacement : replacements) {
        result = Halide::Internal::substitute(
            replacement.first, replacement.second, result);
    }
    result = Halide::Internal::simplify(result);
    const int64_t *actual_dim = Halide::Internal::as_const_int(result);
    if (!actual_dim) {
        throw std::invalid_argument(
            "Couldn't statically infer one of the dimensions of output " + name);
    }
    return *actual_dim;
}

void compute_output_shapes(
    const Model &model,
    const std::map<std::string, std::vector<int>> &input_shapes,
    std::map<std::string, std::vector<int>> *output_shapes) {
    std::unordered_map<std::string, Halide::ImageParam> inputs;
    std::vector<std::pair<Halide::Expr, Halide::Expr>> replacements;
    for (auto it = model.inputs.begin(); it != model.inputs.end(); ++it) {
        const std::string &input_name = it->first;
        const Halide::ImageParam &input = it->second;
        const std::vector<int> &input_shape = input_shapes.at(input_name);

        for (int i = 0; i < input.dimensions(); ++i) {
            const Halide::Internal::Dimension dim = input.dim(i);
            const Halide::Expr extent = dim.extent();
            if (extent.node_type() != Halide::Internal::IRNodeType::IntImm) {
                replacements.emplace_back(extent, Halide::Expr(input_shape[i]));
            }
        }
    }

    for (auto it = model.outputs.begin(); it != model.outputs.end(); ++it) {
        const std::string &name = it->first;
        const Tensor &t = it->second;

        std::vector<int> &output_shape = (*output_shapes)[name];
        const int rank = t.shape.size();
        for (int i = 0; i < rank; ++i) {
            const int64_t *dim = Halide::Internal::as_const_int(t.shape[i]);
            if (!dim) {
                output_shape.push_back(
                    infer_dim_from_inputs(t.shape[i], replacements, name));
            } else {
                output_shape.push_back(*dim);
            }
        }
    }
}

void extract_expected_input_shapes(
    const Model &model,
    std::map<std::string, std::vector<int>> *expected_input_shapes) {
    for (const auto &input : model.inputs) {
        const std::string &input_name = input.first;
        const Halide::ImageParam &in = input.second;

        const Tensor &t = model.tensors.at(input_name);
        std::vector<int> input_shape;
        for (int i = 0; i < t.shape.size(); ++i) {
            const int64_t *dim = Halide::Internal::as_const_int(t.shape[i]);
            if (!dim) {
                // The dimension isn't fixed: use the estimated typical value instead if
                // one was provided.
                Halide::Expr d = in.dim(i).extent_estimate();
                dim = Halide::Internal::as_const_int(d);
            }
            if (!dim) {
                throw std::invalid_argument(
                    "Unknown dim " + std::to_string(i) + " for input " + input_name);
            }
            input_shape.push_back(*dim);
        }

        expected_input_shapes->emplace(input_name, input_shape);
    }
}

void compute_expected_output_shapes(
    const Model &model,
    std::map<std::string, std::vector<int>> *output_shapes) {
    std::map<std::string, std::vector<int>> expected_input_shapes;
    extract_expected_input_shapes(model, &expected_input_shapes);
    compute_output_shapes(model, expected_input_shapes, output_shapes);
}
