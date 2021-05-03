#ifndef HANNK_OPS_H_
#define HANNK_OPS_H_

#include <array>

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
    BinaryOp(const TensorPtr &a, const TensorPtr &b, const TensorPtr &output, Operator op, ActivationFunction activation = ActivationFunction::None)
        : ElementwiseOp({a, b}, {output}), op_(op), activation_(activation) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<BinaryOp>(
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
    bool is_no_op_ = false;

public:
    ConcatenationOp(std::vector<TensorPtr> inputs, const TensorPtr &output, int axis)
        : Op(std::move(inputs), {output}), axis_(axis) {
    }

    int axis() const {
        return axis_;
    }
    void set_no_op() {
        is_no_op_ = true;
    }

    OpPtr clone(TensorMap &map) const {
        std::vector<TensorPtr> inputs;
        for (int i = 0; i < input_count(); i++) {
            inputs.push_back(apply(map, input(i)));
        }
        return make_op<ConcatenationOp>(
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
    std::array<int, 2> stride_;
    std::array<int, 2> dilation_;
    Padding padding_;
    ActivationFunction activation_;

public:
    Conv2DOp(const TensorPtr &input, const TensorPtr &filter, const TensorPtr &bias, const TensorPtr &output,
             std::array<int, 2> stride, std::array<int, 2> dilation, Padding padding,
             ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          stride_(stride),
          dilation_(dilation),
          padding_(padding),
          activation_(activation) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<Conv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), stride_, dilation_, padding_, activation_);
    }

    void accept(OpVisitor *v);

    const TensorPtr &filter() const {
        return Op::input(1);
    }
    const TensorPtr &bias() const {
        return Op::input(2);
    }
    const TensorPtr &filter() {
        return Op::input(1);
    }
    void set_filter(TensorPtr filter) {
        Op::set_input(1, std::move(filter));
    }
    const TensorPtr &bias() {
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
    std::array<int, 2> stride_;
    std::array<int, 2> dilation_;
    Padding padding_;
    ActivationFunction activation_;

public:
    DepthwiseConv2DOp(const TensorPtr &input, const TensorPtr &filter, const TensorPtr &bias, const TensorPtr &output,
                      int depth_multiplier, std::array<int, 2> stride, std::array<int, 2> dilation,
                      Padding padding, ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          depth_multiplier_(depth_multiplier),
          stride_(stride),
          dilation_(dilation),
          padding_(padding),
          activation_(activation) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<DepthwiseConv2DOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), depth_multiplier_, stride_, dilation_,
            padding_, activation_);
    }

    void accept(OpVisitor *v);

    const TensorPtr &filter() const {
        return Op::input(1);
    }
    const TensorPtr &bias() const {
        return Op::input(2);
    }
    const TensorPtr &filter() {
        return Op::input(1);
    }
    const TensorPtr &bias() {
        return Op::input(2);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  DepthwiseConv2D " << output()->name() << std::endl;
    }
};

class ElementwiseProgramOp : public ElementwiseOp {
private:
    Halide::Runtime::Buffer<int16_t> program_;

public:
    ElementwiseProgramOp(std::vector<TensorPtr> inputs, const TensorPtr &output, HalideBuffer<int16_t> program)
        : ElementwiseOp(std::move(inputs), {output}), program_(program) {
    }
    ElementwiseProgramOp(std::vector<TensorPtr> inputs, std::vector<TensorPtr> outputs, HalideBuffer<int16_t> program)
        : ElementwiseOp(std::move(inputs), std::move(outputs)), program_(program) {
    }

    OpPtr clone(TensorMap &map) const {
        std::vector<TensorPtr> inputs, outputs;
        for (int i = 0; i < input_count(); i++) {
            inputs.push_back(apply(map, input(i)));
        }
        for (int i = 0; i < output_count(); i++) {
            inputs.push_back(apply(map, output(i)));
        }
        return make_op<ElementwiseProgramOp>(
            std::move(inputs), std::move(outputs), program_);
    }

    void accept(OpVisitor *v);

    void execute();

    void dump(std::ostream &os) const {
        os << "  ElementwiseProgram" << std::endl;
    }
};

class FullyConnectedOp : public Op {
    ActivationFunction activation_;

public:
    FullyConnectedOp(const TensorPtr &input, const TensorPtr &filter, const TensorPtr &bias, const TensorPtr &output,
                     ActivationFunction activation = ActivationFunction::None)
        : Op({input, filter, bias}, {output}), activation_(activation) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<FullyConnectedOp>(
            apply(map, input()), apply(map, filter()), apply(map, bias()),
            apply(map, output()), activation_);
    }

    void accept(OpVisitor *v);

    const TensorPtr &filter() const {
        return Op::input(1);
    }
    const TensorPtr &bias() const {
        return Op::input(2);
    }
    const TensorPtr &filter() {
        return Op::input(1);
    }
    const TensorPtr &bias() {
        return Op::input(2);
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  FullyConnected " << output()->name() << std::endl;
    }
};

class GatherOp : public Op {
    int axis_;

public:
    GatherOp(TensorPtr input, TensorPtr indices, TensorPtr output, int axis)
        : Op({input, indices}, {output}), axis_(axis) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<GatherOp>(apply(map, input(0)), apply(map, input(1)), apply(map, output()), axis_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Gather " << output()->name() << std::endl;
    }
};

class L2NormalizationOp : public Op {
public:
    L2NormalizationOp(const TensorPtr &input, const TensorPtr &output)
        : Op({input}, {output}) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<L2NormalizationOp>(apply(map, input()), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  L2Normalization " << output()->name() << std::endl;
    }
};

class PadOp : public Op {
public:
    PadOp(const TensorPtr &input, const TensorPtr &padding, const TensorPtr &output)
        : Op({input, padding}, {output}) {
        if (input->rank() == 0 || !padding->is_constant()) {
            output->set_dynamic();
        }
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<PadOp>(
            apply(map, input(0)), apply(map, input(1)), apply(map, output()));
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Pad " << output()->name() << std::endl;
    }
};

class Pool2DOp : public Op {
public:
    enum Operator {
        Average,
        Max,
    };

    static const char *to_string(Operator op);

protected:
    std::array<int, 2> stride_;
    std::array<int, 2> filter_size_;
    Padding padding_;
    Operator op_;
    ActivationFunction activation_;

public:
    Pool2DOp(const TensorPtr &input, const TensorPtr &output, std::array<int, 2> stride,
             std::array<int, 2> filter_size, Padding padding, Operator op,
             ActivationFunction activation)
        : Op({input}, {output}),
          stride_(stride),
          filter_size_(filter_size),
          padding_(padding),
          op_(op),
          activation_(activation) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<Pool2DOp>(
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
    ReductionOp(const TensorPtr &input, const TensorPtr &indices, const TensorPtr &output, Operator op)
        : Op({input, indices}, {output}), op_(op) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<ReductionOp>(
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
    SmallVector<int, max_rank> calc_new_shape() const;

public:
    ReshapeOp(const TensorPtr &input, const TensorPtr &shape_tensor, const TensorPtr &output)
        : Op({input, shape_tensor}, {output}) {
        if (shape_tensor && !shape_tensor->is_constant()) {
            output->set_dynamic();
        }
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<ReshapeOp>(apply(map, input()), apply(map, input(1)), apply(map, output()));
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
    ShapeOp(const TensorPtr &input, const TensorPtr &output)
        : Op({input}, {output}) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<ShapeOp>(apply(map, input()), apply(map, output()));
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
    SoftmaxOp(const TensorPtr &input, const TensorPtr &output, float beta)
        : Op({input}, {output}), beta_(beta) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<SoftmaxOp>(apply(map, input()), apply(map, output()), beta_);
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
    SpaceDepthOp(const TensorPtr &input, const TensorPtr &output, int block_size)
        : Op({input}, {output}), block_size_(block_size) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<SpaceDepthOp>(apply(map, input()), apply(map, output()), block_size_);
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        const char *name = block_size_ > 0 ? "SpaceToDepth" : "DepthToSpace";
        os << "  " << name << " " << output()->name() << std::endl;
    }
};

class SplitOp : public Op {
    int axis_;
    bool is_no_op_ = false;

public:
    SplitOp(const TensorPtr &input, std::vector<TensorPtr> outputs, int axis)
        : Op({input}, std::move(outputs)), axis_(axis) {
    }

    OpPtr clone(TensorMap &map) const {
        std::vector<TensorPtr> outputs;
        for (int i = 0; i < output_count(); i++) {
            outputs.push_back(apply(map, output(i)));
        }
        return make_op<SplitOp>(
            apply(map, input()), std::move(outputs), axis_);
    }

    int axis() const {
        return axis_;
    }
    void set_no_op() {
        is_no_op_ = true;
    }

    void accept(OpVisitor *v);

    BoundsMap map_bounds(int input_idx, int output_idx) const;

    void execute();

    void dump(std::ostream &os) const {
        os << "  Split" << std::endl;
    }
};

class TileConvFilterOp : public Op {
public:
    TileConvFilterOp(const TensorPtr &input, const TensorPtr &output)
        : Op({input}, {output}) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<TileConvFilterOp>(apply(map, input()), apply(map, output()));
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
    UnaryOp(const TensorPtr &input, const TensorPtr &output, Operator op)
        : ElementwiseOp({input}, {output}), op_(op) {
    }

    OpPtr clone(TensorMap &map) const {
        return make_op<UnaryOp>(
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
    virtual void visit(ElementwiseProgramOp *op) {
    }
    virtual void visit(FullyConnectedOp *op) {
    }
    virtual void visit(GatherOp *op) {
    }
    virtual void visit(L2NormalizationOp *op) {
    }
    virtual void visit(PadOp *op) {
    }
    virtual void visit(Pool2DOp *op) {
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
    virtual void visit(SplitOp *op) {
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
