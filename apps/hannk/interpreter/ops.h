#ifndef HANNK_OPS_H_
#define HANNK_OPS_H_

#include "interpreter/model.h"

namespace hannk {

enum class ActivationFunction {
    None = 0,
    Relu,
    ReluN1To1,
    Relu6,
    Tanh,
    SignBit,
};

enum class Padding {
    Same = 0,
    Valid,
};

// This is an abstract helper op for elementwise operations.
class ElementwiseOp : public Op {
public:
    ElementwiseOp(std::vector<TensorPtr> inputs, std::vector<TensorPtr> outputs)
        : Op(std::move(inputs), std::move(outputs)) {
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;
};

class BinaryOp : public ElementwiseOp {
public:
    enum Operator {
        Add,
        Sub,
        Mul,
        Less,
        LessEqual,
        Equal,
        NotEqual,
    };

    static const char *to_string(Operator op);

private:
    Operator op_;
    ActivationFunction activation_;

public:
    BinaryOp(TensorPtr a, TensorPtr b, TensorPtr output, Operator op, ActivationFunction activation = ActivationFunction::None)
        : ElementwiseOp({a, b}, {output}), op_(op), activation_(activation) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<BinaryOp>(
            apply(map, input(0)), apply(map, input(1)),
            apply(map, output()), op_, activation_);
    }

    void accept(OpVisitor *v);

    void execute();

    void dump(std::ostream &os) const {
        os << "  " << to_string(op_) << " " << output()->name() << std::endl;
    }
};

class ConcatenationOp : public Op {
    int axis_;

public:
    ConcatenationOp(std::vector<TensorPtr> inputs, TensorPtr output, int axis)
        : Op(std::move(inputs), {output}), axis_(axis) {
    }

    int axis() const {
        return axis_;
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        std::vector<TensorPtr> inputs;
        for (int i = 0; i < input_count(); i++) {
            inputs.push_back(apply(map, input(i)));
        }
        return ::hannk::make_unique<ConcatenationOp>(
            std::move(inputs), apply(map, output()), axis_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Concatenation " << output()->name() << std::endl;
    }
};

class Conv2DOp : public Op {
    std::vector<int> stride_;
    std::vector<int> dilation_;
    Padding padding_;
    ActivationFunction activation_;

public:
    Conv2DOp(TensorPtr input, TensorPtr filter, TensorPtr bias, TensorPtr output,
             std::vector<int> stride, std::vector<int> dilation, Padding padding,
             ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          stride_(std::move(stride)),
          dilation_(std::move(dilation)),
          padding_(padding),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<Conv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), stride_, dilation_, padding_, activation_);
    }

    void accept(OpVisitor *v);

    const TensorPtr filter() const {
        return Op::input(1);
    }
    const TensorPtr bias() const {
        return Op::input(2);
    }
    TensorPtr filter() {
        return Op::input(1);
    }
    void set_filter(TensorPtr filter) {
        Op::set_input(1, filter);
    }
    TensorPtr bias() {
        return Op::input(2);
    }

    halide_type_t filter_type() const;
    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Conv2D " << output()->name() << std::endl;
    }
};

class DepthwiseConv2DOp : public Op {
    int depth_multiplier_;
    std::vector<int> stride_;
    std::vector<int> dilation_;
    Padding padding_;
    ActivationFunction activation_;

public:
    DepthwiseConv2DOp(TensorPtr input, TensorPtr filter, TensorPtr bias, TensorPtr output,
                      int depth_multiplier, std::vector<int> stride, std::vector<int> dilation,
                      Padding padding, ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          depth_multiplier_(depth_multiplier),
          stride_(std::move(stride)),
          dilation_(std::move(dilation)),
          padding_(padding),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<DepthwiseConv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), depth_multiplier_, stride_, dilation_,
            padding_, activation_);
    }

    void accept(OpVisitor *v);

    const TensorPtr filter() const {
        return Op::input(1);
    }
    const TensorPtr bias() const {
        return Op::input(2);
    }
    TensorPtr filter() {
        return Op::input(1);
    }
    TensorPtr bias() {
        return Op::input(2);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  DepthwiseConv2D " << output()->name() << std::endl;
    }
};

class FullyConnectedOp : public Op {
    ActivationFunction activation_;

public:
    FullyConnectedOp(TensorPtr input, TensorPtr filter, TensorPtr bias, TensorPtr output,
                     ActivationFunction activation = ActivationFunction::None)
        : Op({input, filter, bias}, {output}), activation_(activation) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<FullyConnectedOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), activation_);
    }

    void accept(OpVisitor *v);

    const TensorPtr filter() const {
        return Op::input(1);
    }
    const TensorPtr bias() const {
        return Op::input(2);
    }
    TensorPtr filter() {
        return Op::input(1);
    }
    TensorPtr bias() {
        return Op::input(2);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  FullyConnected " << output()->name() << std::endl;
    }
};

class L2NormalizationOp : public Op {
public:
    L2NormalizationOp(TensorPtr input, TensorPtr output)
        : Op({input}, {output}) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<L2NormalizationOp>(apply(map, input()), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  L2Normalization " << output()->name() << std::endl;
    }
};

class LstmElementwiseOp : public ElementwiseOp {
public:
    LstmElementwiseOp(TensorPtr activ_temp, TensorPtr prev_state_input, TensorPtr state_output, TensorPtr activ_output)
        : ElementwiseOp({activ_temp, prev_state_input}, {state_output, activ_output}) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<LstmElementwiseOp>(
            apply(map, input(0)), apply(map, input(1)),
            apply(map, output(0)), apply(map, output(1)));
    }

    void accept(OpVisitor *v);

    void execute();

    void dump(std::ostream &os) const {
        os << "  LstmElementwiseOp " << std::endl;
    }
};

class PadOp : public Op {
public:
    PadOp(TensorPtr input, TensorPtr padding, TensorPtr output)
        : Op({input, padding}, {output}) {
        if (input->rank() == 0 || !padding->is_constant()) {
            output->set_dynamic();
        }
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<PadOp>(
            apply(map, input(0)), apply(map, input(1)), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Pad " << output()->name() << std::endl;
    }
};

class PoolOp : public Op {
public:
    enum Operator {
        Average,
        Max,
    };

    static const char *to_string(Operator op);

protected:
    std::vector<int> stride_;
    std::vector<int> filter_size_;
    Padding padding_;
    Operator op_;
    ActivationFunction activation_;

public:
    PoolOp(TensorPtr input, TensorPtr output, std::vector<int> stride,
           std::vector<int> filter_size, Padding padding, Operator op,
           ActivationFunction activation)
        : Op({input}, {output}),
          stride_(std::move(stride)),
          filter_size_(std::move(filter_size)),
          padding_(padding),
          op_(op),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<PoolOp>(
            apply(map, input()), apply(map, output()), stride_, filter_size_, padding_, op_, activation_);
    }

    Operator op() const {
        return op_;
    }
    Padding padding() const {
        return padding_;
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void accept(OpVisitor *v);

    void execute();

    void dump(std::ostream &os) const {
        os << "  " << to_string(op_) << "Pool " << output()->name() << std::endl;
    }
};

class ReductionOp : public Op {
public:
    enum Operator {
        Mean,
    };

    static const char *to_string(Operator op);

protected:
    Operator op_;

    bool reducing(int d) const;

public:
    ReductionOp(TensorPtr input, TensorPtr indices, TensorPtr output, Operator op)
        : Op({input, indices}, {output}), op_(op) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<ReductionOp>(
            apply(map, input()), apply(map, input(1)), apply(map, output()), op_);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void accept(OpVisitor *v);

    void execute();

    void dump(std::ostream &os) const {
        os << "  " << to_string(op_) << " " << output()->name() << std::endl;
    }
};

class ReshapeOp : public Op {
    std::vector<int> shape_array_;

    std::vector<int> calc_new_shape() const;

public:
    ReshapeOp(TensorPtr input, TensorPtr shape_tensor, TensorPtr output, std::vector<int> shape_array)
        : Op({input, shape_tensor}, {output}), shape_array_(std::move(shape_array)) {
        if (shape_tensor && !shape_tensor->is_constant()) {
            output->set_dynamic();
        }
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<ReshapeOp>(apply(map, input()), apply(map, input(1)),
                                               apply(map, output()), shape_array_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Reshape " << output()->name() << std::endl;
    }
};

class ShapeOp : public Op {
public:
    ShapeOp(TensorPtr input, TensorPtr output)
        : Op({input}, {output}) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<ShapeOp>(apply(map, input()), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Shape " << output()->name() << std::endl;
    }
};

class SoftmaxOp : public Op {
    float beta_;

public:
    SoftmaxOp(TensorPtr input, TensorPtr output, float beta)
        : Op({input}, {output}), beta_(beta) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<SoftmaxOp>(apply(map, input()), apply(map, output()), beta_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Softmax " << output()->name() << std::endl;
    }
};

class SpaceDepthOp : public Op {
    int block_size_;

public:
    SpaceDepthOp(TensorPtr input, TensorPtr output, float block_size)
        : Op({input}, {output}), block_size_(block_size) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<SpaceDepthOp>(apply(map, input()), apply(map, output()), block_size_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        const char *name = block_size_ > 0 ? "SpaceToDepth" : "DepthToSpace";
        os << "  " << name << " " << output()->name() << std::endl;
    }
};

class TileConvFilterOp : public Op {
public:
    TileConvFilterOp(TensorPtr input, TensorPtr output)
        : Op({input}, {output}) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<TileConvFilterOp>(apply(map, input()), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  TileConvFilterOp " << output()->name() << std::endl;
    }
};

class UnaryOp : public ElementwiseOp {
public:
    enum Operator {
        Logistic,
        Negate,
        Relu,
        Relu6,
        ReluN1To1,
        Square,
        Tanh,
    };

    static const char *to_string(Operator op);

private:
    Operator op_;

public:
    UnaryOp(TensorPtr input, TensorPtr output, Operator op)
        : ElementwiseOp({input}, {output}), op_(op) {
    }

    std::unique_ptr<Op> clone(TensorMap &map) const {
        return ::hannk::make_unique<UnaryOp>(
            apply(map, input()), apply(map, output()), op_);
    }

    void accept(OpVisitor *v);

    void execute();

    void dump(std::ostream &os) const {
        os << "  " << to_string(op_) << " " << output()->name() << std::endl;
    }
};

class OpVisitor {
public:
    virtual ~OpVisitor() = default;

    virtual void visit(BinaryOp *op) {
    }
    virtual void visit(ConcatenationOp *op) {
    }
    virtual void visit(Conv2DOp *op) {
    }
    virtual void visit(DepthwiseConv2DOp *op) {
    }
    virtual void visit(FullyConnectedOp *op) {
    }
    virtual void visit(L2NormalizationOp *op) {
    }
    virtual void visit(LstmElementwiseOp *op) {
    }
    virtual void visit(PadOp *op) {
    }
    virtual void visit(PoolOp *op) {
    }
    virtual void visit(ReductionOp *op) {
    }
    virtual void visit(ReshapeOp *op) {
    }
    virtual void visit(ShapeOp *op) {
    }
    virtual void visit(SoftmaxOp *op) {
    }
    virtual void visit(SpaceDepthOp *op) {
    }
    virtual void visit(TileConvFilterOp *op) {
    }
    virtual void visit(UnaryOp *op) {
    }
    virtual void visit(OpGroup *op) {
    }
};

}  // namespace hannk

#endif  // HANNK_OPS_H_
