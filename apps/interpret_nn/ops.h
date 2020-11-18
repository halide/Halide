#ifndef OPS_H_
#define OPS_H_

#include "model.h"

namespace interpret_nn {

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
    std::vector<Box> split(const Box &crop) const;
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
    std::vector<Box> split(const Box &crop) const;
};

class AddOp : public ElementwiseOp {
    ActivationFunction activation_;

public:
    explicit AddOp(Tensor *input1, Tensor *input2, Tensor *output,
                   ActivationFunction activation)
        : ElementwiseOp({input1, input2}, output), activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return make_unique<AddOp>(
            apply(map, input(0)), apply(map, input(1)),
            apply(map, output()), activation_);
    }

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  Add " << output()->name() << std::endl;
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
        return make_unique<AveragePoolOp>(
            apply(map, input()), apply(map, output()), stride_,
            filter_size_, padding_, activation_);
    }

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
        : Op(std::move(inputs), {output}), axis_(axis), activation_(activation) {}

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        std::vector<Tensor *> inputs;
        for (int i = 0; i < input_count(); i++) {
            inputs.push_back(apply(map, input(i)));
        }
        return make_unique<ConcatenationOp>(
            inputs, apply(map, output()), axis_, activation_);
    }

    Bounds infer_bounds(const Box &crop) const;
    std::vector<Box> split(const Box &crop) const;

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
        return make_unique<Conv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), stride_, dilation_, padding_, activation_);
    }

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
    std::vector<Box> split(const Box &crop) const;

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
                      int depth_multiplier, std::vector<int> stride,
                      std::vector<int> dilation, Padding padding,
                      ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          depth_multiplier_(depth_multiplier),
          stride_(std::move(stride)),
          dilation_(std::move(dilation)),
          padding_(padding),
          activation_(activation) {
    }

    std::unique_ptr<Op> clone(const TensorMap &map) const {
        return make_unique<DepthwiseConv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), depth_multiplier_, stride_, dilation_, padding_, activation_);
    }

    const Tensor *input() const {
        return Op::input(0);
    }
    const Tensor *filter() const {
        return Op::input(1);
    }
    const Tensor *bias() const {
        return Op::input(2);
    }
    Tensor *input() {
        return Op::input(0);
    }
    Tensor *filter() {
        return Op::input(1);
    }
    Tensor *bias() {
        return Op::input(2);
    }

    Bounds infer_bounds(const Box &crop) const;
    std::vector<Box> split(const Box &crop) const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  DepthwiseConv2D " << output()->name() << std::endl;
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
        return make_unique<MaxPoolOp>(
            apply(map, input()), apply(map, output()), stride_, filter_size_, padding_, activation_);
    }

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
        return make_unique<PadOp>(
            apply(map, input(0)), apply(map, input(1)), apply(map, output()));
    }

    Bounds infer_bounds(const Box &crop) const;
    std::vector<Box> split(const Box &crop) const;

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
        return make_unique<ReshapeOp>(apply(map, input()), apply(map, output()), new_shape_);
    }

    Bounds infer_bounds(const Box &crop) const;
    std::vector<Box> split(const Box &crop) const;

    void execute(const Box &crop);

    void dump(std::ostream &os) const {
        os << "  Reshape " << output()->name() << std::endl;
    }
};

}  // namespace interpret_nn

#endif  // OPS_H_
