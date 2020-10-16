#ifndef OPS_H_
#define OPS_H_

#include "interpret_nn.h"

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

    Bounds InferBounds(const CropShape &crop) const;
    std::vector<CropShape> Split(const CropShape &crop) const;
};

class AddOp : public ElementwiseOp {
    ActivationFunction activation_;

public:
    explicit AddOp(Tensor *input1, Tensor *input2, Tensor *output,
                   ActivationFunction activation)
        : ElementwiseOp({input1, input2}, output), activation_(activation) {
    }

    void Execute(const CropShape &crop);

    void Dump(std::ostream &os) const {
        os << "  Add " << Output()->Name() << std::endl;
    }
};

class AveragePoolOp : public Op {
    std::vector<int> stride_;
    std::vector<int> filter_size_;
    Padding padding_;
    ActivationFunction activation_;

public:
    AveragePoolOp(Tensor *input, Tensor *output, std::vector<int> stride,
                  std::vector<int> filter_size, Padding padding,
                  ActivationFunction activation)
        : Op({input}, {output}),
          stride_(std::move(stride)),
          filter_size_(std::move(filter_size)),
          padding_(padding),
          activation_(activation) {

    }

    Bounds InferBounds(const CropShape &crop) const;
    std::vector<CropShape> Split(const CropShape &crop) const;

    void Execute(const CropShape &crop);

    void Dump(std::ostream &os) const {
        os << "  AveragePool " << Output()->Name() << std::endl;
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

    Bounds InferBounds(const CropShape &crop) const;
    std::vector<CropShape> Split(const CropShape &crop) const;

    void Execute(const CropShape &crop);

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

    Bounds InferBounds(const CropShape &crop) const;
    std::vector<CropShape> Split(const CropShape &crop) const;

    void Execute(const CropShape &crop);

    void Dump(std::ostream &os) const {
        os << "  DepthwiseConv2D " << Output()->Name() << std::endl;
    }
};

class PadOp : public Op {
public:
    explicit PadOp(Tensor *input, Tensor *padding, Tensor *output)
        : Op({input, padding}, {output}) {
    }

    Bounds InferBounds(const CropShape &crop) const;
    std::vector<CropShape> Split(const CropShape &crop) const;

    void Execute(const CropShape &crop);

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

    Bounds InferBounds(const CropShape &crop) const;
    std::vector<CropShape> Split(const CropShape &crop) const;

    void Execute(const CropShape &crop);

    void Dump(std::ostream &os) const {
        os << "  Reshape " << Output()->Name() << std::endl;
    }
};

}  // namespace interpret_nn

#endif  // OPS_H_
