#ifndef HANNK_OPS_H_
#define HANNK_OPS_H_

#include <array>

#include "interpreter/model.h"
#include "util/small_vector.h"

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

    BoundsMap map_bounds(int input_idx, int output_idx) const override;
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

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    void execute() override;

    std::string name() const override {
        return std::string("BinaryOp(") + to_string(op_) + ")";
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

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "ConcatenationOp";
    }
};

class ConvOp : public Op {
    std::array<int, 2> stride_;
    std::array<int, 2> dilation_;
    Padding padding_;
    ActivationFunction activation_;

    // calculated in prepare()
    int vector_reduction_ = 0;
    int vector_tile_ = 0;

public:
    ConvOp(const TensorPtr &input, const TensorPtr &filter, const TensorPtr &bias, const TensorPtr &output,
           std::array<int, 2> stride, std::array<int, 2> dilation, Padding padding,
           ActivationFunction activation)
        : Op({input, filter, bias}, {output}),
          stride_(stride),
          dilation_(dilation),
          padding_(padding),
          activation_(activation) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

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

    std::array<int, 2> stride() const { return stride_; }
    std::array<int, 2> dilation() const { return dilation_; }
    Padding padding() const { return padding_; }
    ActivationFunction activation() const { return activation_; }

    halide_type_t filter_type() const;
    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    bool prepare() override;

    void execute() override;

    std::string name() const override {
        return "ConvOp";
    }
};

class DepthwiseConv2DOp : public Op {
    int depth_multiplier_;
    std::array<int, 2> stride_;
    std::array<int, 2> dilation_;
    Padding padding_;
    ActivationFunction activation_;

    // calculated in prepare()
    int channel_alignment_ = 0;

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

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    int depth_multiplier() const {
        return depth_multiplier_;
    }
    void set_depth_multiplier(int depth_multiplier) {
        depth_multiplier_ = depth_multiplier;
    }

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

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    std::array<int, 2> stride() const { return stride_; }
    std::array<int, 2> dilation() const { return dilation_; }
    Padding padding() const { return padding_; }
    ActivationFunction activation() const { return activation_; }

    bool prepare() override;

    void execute() override;

    std::string name() const override {
        return "DepthwiseConv2DOp";
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

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    void execute() override;

    std::string name() const override {
        return "ElementwiseProgramOp";
    }
};

class GatherOp : public Op {
    const int axis_;
    const int batch_dims_;

public:
    GatherOp(TensorPtr input, TensorPtr indices, TensorPtr output, int axis, int batch_dims)
        : Op({input, indices}, {output}), axis_(axis), batch_dims_(batch_dims) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "GatherOp";
    }
};

class L2NormalizationOp : public Op {
    const int axis_;

public:
    L2NormalizationOp(const TensorPtr &input, const TensorPtr &output, int axis)
        : Op({input}, {output}), axis_(axis) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "L2NormalizationOp";
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

    const TensorPtr &padding() const {
        return Op::input(1);
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "PadOp";
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

    Operator op() const {
        return op_;
    }
    Padding padding() const {
        return padding_;
    }

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    void execute() override;

    std::string name() const override {
        return std::string("Pool2DOp(") + to_string(op_) + ")";
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

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    void execute() override;

    std::string name() const override {
        return std::string("ReductionOp(") + to_string(op_) + ")";
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

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "ReshapeOp";
    }
};

class ShapeOp : public Op {
public:
    ShapeOp(const TensorPtr &input, const TensorPtr &output)
        : Op({input}, {output}) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "ShapeOp";
    }
};

class SoftmaxOp : public Op {
    const float beta_;
    const int axis_;

public:
    SoftmaxOp(const TensorPtr &input, const TensorPtr &output, float beta, int axis)
        : Op({input}, {output}), beta_(beta), axis_(axis) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "SoftmaxOp";
    }
};

class SpaceDepthOp : public Op {
    int block_size_;

public:
    SpaceDepthOp(const TensorPtr &input, const TensorPtr &output, int block_size)
        : Op({input}, {output}), block_size_(block_size) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return block_size_ > 0 ? "SpaceToDepthOp" : "DepthToSpaceOp";
    }
};

class SplitOp : public Op {
    int axis_;
    bool is_no_op_ = false;

public:
    SplitOp(const TensorPtr &input, std::vector<TensorPtr> outputs, int axis)
        : Op({input}, std::move(outputs)), axis_(axis) {
    }

    int axis() const {
        return axis_;
    }
    void set_no_op() {
        is_no_op_ = true;
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "SplitOp";
    }
};

class TileConvFilterOp : public Op {
public:
    TileConvFilterOp(const TensorPtr &input, const TensorPtr &output)
        : Op({input}, {output}) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "TileConvFilterOp";
    }
};

class TransposeOp : public Op {
public:
    TransposeOp(const TensorPtr &input, const TensorPtr &dims, const TensorPtr &output)
        : Op({input, dims}, {output}) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "TransposeOp";
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

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    void execute() override;

    std::string name() const override {
        return std::string("UnaryOp(") + to_string(op_) + ")";
    }
};

class UpsampleChannelsOp : public Op {
    int factor_;

public:
    UpsampleChannelsOp(const TensorPtr &input, int factor, const TensorPtr &output)
        : Op({input}, {output}), factor_(factor) {
    }

    void accept(OpVisitor *v) const override;
    OpPtr mutate(OpMutator *m, OpPtr op) override;

    BoundsMap map_bounds(int input_idx, int output_idx) const override;

    void execute() override;

    std::string name() const override {
        return "UpsampleChannelsOp";
    }
};

class OpVisitor {
public:
    virtual ~OpVisitor() = default;

protected:
    // Only the classes in the list are allowed to call visit() (to implement accept())
    friend class BinaryOp;
    friend class ConcatenationOp;
    friend class ConvOp;
    friend class DepthwiseConv2DOp;
    friend class ElementwiseProgramOp;
    friend class GatherOp;
    friend class L2NormalizationOp;
    friend class PadOp;
    friend class Pool2DOp;
    friend class ReductionOp;
    friend class ReshapeOp;
    friend class ShapeOp;
    friend class SoftmaxOp;
    friend class SpaceDepthOp;
    friend class SplitOp;
    friend class TileConvFilterOp;
    friend class TransposeOp;
    friend class UnaryOp;
    friend class UpsampleChannelsOp;
    friend class OpGroup;

    // clang-format off
    virtual void visit(const BinaryOp *op) {}
    virtual void visit(const ConcatenationOp *op) {}
    virtual void visit(const ConvOp *op) {}
    virtual void visit(const DepthwiseConv2DOp *op) {}
    virtual void visit(const ElementwiseProgramOp *op) {}
    virtual void visit(const GatherOp *op) {}
    virtual void visit(const L2NormalizationOp *op) {}
    virtual void visit(const PadOp *op) {}
    virtual void visit(const Pool2DOp *op) {}
    virtual void visit(const ReductionOp *op) {}
    virtual void visit(const ReshapeOp *op) {}
    virtual void visit(const ShapeOp *op) {}
    virtual void visit(const SoftmaxOp *op) {}
    virtual void visit(const SpaceDepthOp *op) {}
    virtual void visit(const SplitOp *op) {}
    virtual void visit(const TileConvFilterOp *op) {}
    virtual void visit(const TransposeOp *op) {}
    virtual void visit(const UnaryOp *op) {}
    virtual void visit(const UpsampleChannelsOp *op) {}
    virtual void visit(const OpGroup *op);
    // clang-format on
};

class OpMutator {
public:
    enum Direction {
        Forward,
        Reverse
    };

    OpMutator() = default;
    explicit OpMutator(Direction d) : direction_(d) {}
    virtual ~OpMutator() = default;

protected:
    const Direction direction_ = Forward;

    // Only the classes in the list are allowed to call visit() (to implement mutate())
    friend class BinaryOp;
    friend class ConcatenationOp;
    friend class ConvOp;
    friend class DepthwiseConv2DOp;
    friend class ElementwiseProgramOp;
    friend class GatherOp;
    friend class L2NormalizationOp;
    friend class PadOp;
    friend class Pool2DOp;
    friend class ReductionOp;
    friend class ReshapeOp;
    friend class ShapeOp;
    friend class SoftmaxOp;
    friend class SpaceDepthOp;
    friend class SplitOp;
    friend class TileConvFilterOp;
    friend class TransposeOp;
    friend class UnaryOp;
    friend class UpsampleChannelsOp;
    friend class OpGroup;

    template<typename T>
    inline OpPtr visit_typed(T* self, OpPtr op) {
        /* ugh, horrible but legal */
        assert(op.get() == self);
        std::unique_ptr<T> o(static_cast<T *>(op.release()));
        return visit(std::move(o));
    }

    // clang-format off
    virtual OpPtr visit_leaf(OpPtr op) { return op; }
    virtual OpPtr visit(std::unique_ptr<BinaryOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<ConcatenationOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<ConvOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<DepthwiseConv2DOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<ElementwiseProgramOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<GatherOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<L2NormalizationOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<PadOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<Pool2DOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<ReductionOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<ReshapeOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<ShapeOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<SoftmaxOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<SpaceDepthOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<SplitOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<TileConvFilterOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<TransposeOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<UnaryOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<UpsampleChannelsOp> op) { return visit_leaf(std::move(op)); }
    virtual OpPtr visit(std::unique_ptr<OpGroup> op);
    // clang-format on
};

}  // namespace hannk

#endif  // HANNK_OPS_H_
