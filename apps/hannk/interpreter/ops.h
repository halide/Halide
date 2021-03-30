#ifndef OPS_H_
#define OPS_H_

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
    explicit ElementwiseOp(std::vector<Tensor *> inputs, Tensor *output)
        : Op(std::move(inputs), {output}) {
    }

    Bounds infer_bounds(const Box &crop) const;
};

// This is an abstract helper op for pooling operations.
class PoolOp : public Op {
protected:
    std::vector<int> stride_;
    std::vector<int> filter_size_;
    Padding padding_;
    ActivationFunction activation_;

public:
    PoolOp(Tensor *input, Tensor *output, std::vector<int> stride,
           std::vector<int> filter_size, Padding padding,
           ActivationFunction activation)
        : Op({input}, {output}),
          stride_(std::move(stride)),
          filter_size_(std::move(filter_size)),
          padding_(padding),
          activation_(activation) {
    }

    Bounds infer_bounds(const Box &crop) const;
    std::vector<SplitInfo> get_split_info() const;
};

class AddOp : public ElementwiseOp {
    int input2_sign_;
    ActivationFunction activation_;

public:
    explicit AddOp(Tensor *input1, Tensor *input2, Tensor *output, int input2_sign,
                   ActivationFunction activation)
        : ElementwiseOp({input1, input2}, output), input2_sign_(input2_sign), activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<AddOp>(
            apply(map, input(0)), apply(map, input(1)),
            apply(map, output()), input2_sign_, activation_);
    }

    void accept(OpVisitor *v);

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        const char *name;
        if (input(1) == nullptr) {
            name = "Quantize";
        } else if (input2_sign_ > 0) {
            name = "Add";
        } else {
            name = "Sub";
        }
        os << "  " << name << " " << output()->name() << std::endl;
    }
};

class AveragePoolOp : public PoolOp {
public:
    AveragePoolOp(Tensor *input, Tensor *output, std::vector<int> stride,
                  std::vector<int> filter_size, Padding padding,
                  ActivationFunction activation)
        : PoolOp(input, output, std::move(stride),
                 std::move(filter_size), padding, activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<AveragePoolOp>(
            apply(map, input()), apply(map, output()), stride_,
            filter_size_, padding_, activation_);
    }

    void accept(OpVisitor *v);

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  AveragePool " << output()->name() << std::endl;
    }
};

class ConcatenationOp : public Op {
    int axis_;
    ActivationFunction activation_;

public:
    ConcatenationOp(std::vector<Tensor *> inputs, Tensor *output,
                    int axis, ActivationFunction activation)
        : Op(std::move(inputs), {output}), axis_(axis), activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        std::vector<Tensor *> inputs;
        for (int i = 0; i < input_count(); i++) {
            inputs.push_back(apply(map, input(i)));
        }
        return ::hannk::make_unique<ConcatenationOp>(
            inputs, apply(map, output()), axis_, activation_);
    }

    void accept(OpVisitor *v);

    Bounds infer_bounds(const Box &crop) const;
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
    Box filter_required() const;
    Box input_required(const Box &crop) const;
    Bounds infer_bounds(const Box &crop) const;
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

    int depth_multiplier() const;

public:
    DepthwiseConv2DOp(Tensor *input, Tensor *filter, Tensor *bias, Tensor *output,
                      std::vector<int> stride, std::vector<int> dilation, Padding padding,
                      ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          stride_(std::move(stride)),
          dilation_(std::move(dilation)),
          padding_(padding),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<DepthwiseConv2DOp>(
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
    Tensor *bias() {
        return Op::input(2);
    }

    Box input_required(const Box &crop) const;
    Bounds infer_bounds(const Box &crop) const;
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

    Bounds infer_bounds(const Box &crop) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  FullyConnected " << output()->name() << std::endl;
    }
};

class MaxPoolOp : public PoolOp {
public:
    MaxPoolOp(Tensor *input, Tensor *output, std::vector<int> stride,
              std::vector<int> filter_size, Padding padding,
              ActivationFunction activation)
        : PoolOp(input, output, std::move(stride),
                 std::move(filter_size), padding, activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<MaxPoolOp>(
            apply(map, input()), apply(map, output()), stride_, filter_size_, padding_, activation_);
    }

    void accept(OpVisitor *v);

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  MaxPool " << output()->name() << std::endl;
    }
};

class PadOp : public Op {
public:
    explicit PadOp(Tensor *input, Tensor *padding, Tensor *output)
        : Op({input, padding}, {output}) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return ::hannk::make_unique<PadOp>(
            apply(map, input(0)), apply(map, input(1)), apply(map, output()));
    }

    void accept(OpVisitor *v);

    Bounds infer_bounds(const Box &crop) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  Pad " << output()->name() << std::endl;
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

    Bounds infer_bounds(const Box &crop) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  Reshape " << output()->name() << std::endl;
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

    Bounds infer_bounds(const Box &crop) const;
    std::vector<SplitInfo> get_split_info() const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  TileConvFilterOp " << output()->name() << std::endl;
    }
};

class OpVisitor {
public:
    virtual void visit(AddOp *op) {}
    virtual void visit(AveragePoolOp *op) {}
    virtual void visit(ConcatenationOp *op) {}
    virtual void visit(Conv2DOp *op) {}
    virtual void visit(DepthwiseConv2DOp *op) {}
    virtual void visit(FullyConnectedOp *op) {}
    virtual void visit(MaxPoolOp *op) {}
    virtual void visit(PadOp *op) {}
    virtual void visit(ReshapeOp *op) {}
    virtual void visit(TileConvFilterOp *op) {}
};

}  // namespace hannk

#endif  // OPS_H_
