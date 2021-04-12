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
    ElementwiseOp(std::vector<Tensor *> inputs, Tensor *output)
        : Op(std::move(inputs), {output}) {
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;
};

class BinaryOp : public ElementwiseOp {
public:
    enum Operator {
        Add,
        Sub,
    };

    static const char *to_string(Operator op);

private:
    Operator op_;
    ActivationFunction activation_;

public:
    BinaryOp(Tensor *a, Tensor *b, Tensor *output, Operator op, ActivationFunction activation)
        : ElementwiseOp({a, b}, output), op_(op), activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<BinaryOp>(
            apply(map, input(0)), apply(map, input(1)),
            apply(map, output()), op_, activation_);
    }

    void accept(OpVisitor *v);

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  " << to_string(op_) << " " << output()->name() << std::endl;
    }
};

class ConcatenationOp : public Op {
    int axis_;

public:
    ConcatenationOp(std::vector<Tensor *> inputs, Tensor *output, int axis)
        : Op(std::move(inputs), {output}), axis_(axis) {
    }

    int axis() const {
        return axis_;
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        std::vector<Tensor *> inputs;
        for (int i = 0; i < input_count(); i++) {
            inputs.push_back(apply(map, input(i)));
        }
        return ::hannk::make_unique<ConcatenationOp>(
            std::move(inputs), apply(map, output()), axis_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

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
    Conv2DOp(Tensor *input, Tensor *filter, Tensor *bias, Tensor *output,
             std::vector<int> stride, std::vector<int> dilation, Padding padding,
             ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          stride_(std::move(stride)),
          dilation_(std::move(dilation)),
          padding_(padding),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<Conv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), stride_, dilation_, padding_, activation_);
    }

    void accept(OpVisitor *v);

    const Tensor *filter() const {
        return Op::input(1);
    }
    const Tensor *bias() const {
        return Op::input(2);
    }
    Tensor *filter() {
        return Op::input(1);
    }
    void set_filter(Tensor *filter) {
        Op::set_input(1, filter);
    }
    Tensor *bias() {
        return Op::input(2);
    }

    halide_type_t filter_type() const;
    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

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
    DepthwiseConv2DOp(Tensor *input, Tensor *filter, Tensor *bias, Tensor *output,
                      int depth_multiplier, std::vector<int> stride, std::vector<int> dilation,
                      Padding padding, ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          depth_multiplier_(depth_multiplier),
          stride_(std::move(stride)),
          dilation_(std::move(dilation)),
          padding_(padding),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<DepthwiseConv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), depth_multiplier_, stride_, dilation_,
            padding_, activation_);
    }

    void accept(OpVisitor *v);

    const Tensor *filter() const {
        return Op::input(1);
    }
    const Tensor *bias() const {
        return Op::input(2);
    }
    Tensor *filter() {
        return Op::input(1);
    }
    Tensor *bias() {
        return Op::input(2);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  DepthwiseConv2D " << output()->name() << std::endl;
    }
};

class FullyConnectedOp : public Op {
    ActivationFunction activation_;

public:
    FullyConnectedOp(Tensor *input, Tensor *filter, Tensor *bias, Tensor *output, ActivationFunction activation)
        : Op({input, filter, bias}, {output}), activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<FullyConnectedOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), activation_);
    }

    void accept(OpVisitor *v);

    const Tensor *filter() const {
        return Op::input(1);
    }
    const Tensor *bias() const {
        return Op::input(2);
    }
    Tensor *filter() {
        return Op::input(1);
    }
    Tensor *bias() {
        return Op::input(2);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  FullyConnected " << output()->name() << std::endl;
    }
};

class L2NormalizationOp : public Op {
public:
    L2NormalizationOp(Tensor *input, Tensor *output)
        : Op({input}, {output}) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<L2NormalizationOp>(apply(map, input()), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  L2Normalization " << output()->name() << std::endl;
    }
};

class PadOp : public Op {
public:
    PadOp(Tensor *input, Tensor *padding, Tensor *output)
        : Op({input, padding}, {output}) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<PadOp>(
            apply(map, input(0)), apply(map, input(1)), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

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
    PoolOp(Tensor *input, Tensor *output, std::vector<int> stride,
           std::vector<int> filter_size, Padding padding, Operator op,
           ActivationFunction activation)
        : Op({input}, {output}),
          stride_(std::move(stride)),
          filter_size_(std::move(filter_size)),
          padding_(padding),
          op_(op),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
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
    std::vector<SplitInfo> get_split_info() const;

    void accept(OpVisitor *v);

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  " << to_string(op_) << "Pool " << output()->name() << std::endl;
    }
};

class ReshapeOp : public Op {
    std::vector<int> new_shape_;

public:
    ReshapeOp(Tensor *input, Tensor *output, std::vector<int> new_shape)
        : Op({input}, {output}), new_shape_(std::move(new_shape)) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<ReshapeOp>(apply(map, input()), apply(map, output()), new_shape_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  Reshape " << output()->name() << std::endl;
    }
};

class SoftmaxOp : public Op {
    float beta_;

public:
    SoftmaxOp(Tensor *input, Tensor *output, float beta)
        : Op({input}, {output}), beta_(beta) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<SoftmaxOp>(apply(map, input()), apply(map, output()), beta_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  Softmax " << output()->name() << std::endl;
    }
};

class TileConvFilterOp : public Op {
public:
    TileConvFilterOp(Tensor *input, Tensor *output)
        : Op({input}, {output}) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<TileConvFilterOp>(apply(map, input()), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  TileConvFilterOp " << output()->name() << std::endl;
    }
};

class UnaryOp : public ElementwiseOp {
public:
    enum Operator {
        Logistic,
        Tanh,
    };

    static const char *to_string(Operator op);

private:
    Operator op_;

public:
    UnaryOp(Tensor *input, Tensor *output, Operator op)
        : ElementwiseOp({input}, output), op_(op) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<UnaryOp>(
            apply(map, input()), apply(map, output()), op_);
    }

    void accept(OpVisitor *v);

    void execute(const Box &crop);

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
    virtual void visit(PadOp *op) {
    }
    virtual void visit(PoolOp *op) {
    }
    virtual void visit(ReshapeOp *op) {
    }
    virtual void visit(SoftmaxOp *op) {
    }
    virtual void visit(TileConvFilterOp *op) {
    }
    virtual void visit(UnaryOp *op) {
    }
};

}  // namespace hannk

#endif  // HANNK_OPS_H_
