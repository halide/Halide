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

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;
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

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;
};

class AddOp : public ElementwiseOp {
    ActivationFunction activation_;

public:
    explicit AddOp(Tensor *input1, Tensor *input2, Tensor *output,
                   ActivationFunction activation)
        : ElementwiseOp({input1, input2}, output), activation_(activation) {
    }

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<AddOp>(
            Map(map, Input(0)), Map(map, Input(1)),
            Map(map, Output()), activation_);
    }

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  Add " << Output()->Name() << std::endl;
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

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<AveragePoolOp>(
            Map(map, Input()), Map(map, Output()), stride_,
            filter_size_, padding_, activation_);
    }

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  AveragePool " << Output()->Name() << std::endl;
    }
};

class ConcatenationOp : public Op {
    int axis_;
    ActivationFunction activation_;

public:
    ConcatenationOp(std::vector<Tensor *> inputs, Tensor *output,
                    int axis, ActivationFunction activation)
        : Op(std::move(inputs), {output}), axis_(axis), activation_(activation) {}

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        std::vector<Tensor *> inputs;
        for (int i = 0; i < InputCount(); i++) {
            inputs.push_back(Map(map, Input(i)));
        }
        return make_unique<ConcatenationOp>(
            inputs, Map(map, Output()), axis_, activation_);
    }

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  Concatenation " << Output()->Name() << std::endl;
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

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<Conv2DOp>(
            Map(map, Input()), Map(map, Filter()), Map(map, Bias()),
            Map(map, Output()), stride_, dilation_, padding_, activation_);
    }

    const Tensor *Filter() const {
        return Op::Input(1);
    }
    const Tensor *Bias() const {
        return Op::Input(2);
    }
    Tensor *Filter() {
        return Op::Input(1);
    }
    Tensor *Bias() {
        return Op::Input(2);
    }

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  Conv2D " << Output()->Name() << std::endl;
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

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<DepthwiseConv2DOp>(
            Map(map, Input()), Map(map, Filter()), Map(map, Bias()),
            Map(map, Output()), depth_multiplier_, stride_, dilation_, padding_, activation_);
    }

    const Tensor *Input() const {
        return Op::Input(0);
    }
    const Tensor *Filter() const {
        return Op::Input(1);
    }
    const Tensor *Bias() const {
        return Op::Input(2);
    }
    Tensor *Input() {
        return Op::Input(0);
    }
    Tensor *Filter() {
        return Op::Input(1);
    }
    Tensor *Bias() {
        return Op::Input(2);
    }

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  DepthwiseConv2D " << Output()->Name() << std::endl;
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

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<MaxPoolOp>(
            Map(map, Input()), Map(map, Output()), stride_, filter_size_, padding_, activation_);
    }

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  MaxPool " << Output()->Name() << std::endl;
    }
};

class PadOp : public Op {
public:
    explicit PadOp(Tensor *input, Tensor *padding, Tensor *output)
        : Op({input, padding}, {output}) {
    }

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<PadOp>(
            Map(map, Input(0)), Map(map, Input(1)), Map(map, Output()));
    }

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  Pad " << Output()->Name() << std::endl;
    }
};

class ReshapeOp : public Op {
    std::vector<int> new_shape_;

public:
    ReshapeOp(Tensor *input, Tensor *output, std::vector<int> new_shape)
        : Op({input}, {output}), new_shape_(std::move(new_shape)) {
    }

    std::unique_ptr<Op> Clone(const TensorMap &map) const {
        return make_unique<ReshapeOp>(Map(map, Input()), Map(map, Output()), new_shape_);
    }

    Bounds InferBounds(const Box &crop) const;
    std::vector<Box> Split(const Box &crop) const;

    void Execute(const Box &crop);

    void Dump(std::ostream &os) const {
        os << "  Reshape " << Output()->Name() << std::endl;
    }
};

}  // namespace interpret_nn

#endif  // OPS_H_
