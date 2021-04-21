#include "interpreter/model.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

TensorPtr apply(TensorMap &map, const TensorPtr t) {
    auto i = map.find(t);
    if (i != map.end()) {
        return i->second;
    }

    if (t->is_constant()) {
        // Share constant tensors across users.
        return t;
    } else {
        // Remember this cloned tensor for later applications of the mapping.
        TensorPtr clone = std::make_shared<Tensor>(*t);
        map[t] = clone;
        return clone;
    }
}

namespace {

HalideBuffer<void> make_buffer(halide_type_t type, const Box &bounds) {
    std::vector<int> extents(bounds.size());
    for (int i = 0; i < (int)bounds.size(); i++) {
        extents[i] = bounds[i].extent();
    }
    HalideBuffer<void> buffer(type, nullptr, extents);
    for (int i = 0; i < (int)bounds.size(); i++) {
        buffer.translate(i, bounds[i].min);
    }
    return buffer;
}

}  // namespace

TensorStorage::TensorStorage() {
}
TensorStorage::TensorStorage(HalideBuffer<void> buffer)
    : buffer_(buffer) {
    assert(!buffer_.data());
}

void TensorStorage::add_use(halide_type_t type, const Box &bounds) {
    if (buffer_.dimensions() == 0) {
        buffer_ = make_buffer(type, bounds);
    } else {
        assert(buffer_.type() == type);
        assert(buffer_.dimensions() == (int)bounds.size());
        assert(!buffer_.data());

        // Check that the storage is big enough for this buffer.
        for (int i = 0; i < rank(); i++) {
            assert(bounds[i].min >= buffer_.dim(i).min());
            assert(bounds[i].max <= buffer_.dim(i).max());
        }
    }
}

bool TensorStorage::is_allocated() const {
    return buffer_.data() != nullptr;
}

void TensorStorage::allocate() {
    if (!buffer_.data()) {
        buffer_ = HalideBuffer<void>::make_with_shape_of(buffer_);
    }
}

Tensor::Tensor(std::string name, HalideBuffer<void> buffer, QuantizationInfo quantization)
    : name_(std::move(name)),
      buffer_(std::move(buffer)),
      quantization_(std::move(quantization)) {
    is_constant_ = buffer_.data() != nullptr;
}

Tensor::Tensor(std::string name, halide_type_t type, const Box &bounds, QuantizationInfo quantization)
    : Tensor(name, make_buffer(type, bounds), quantization) {
}

Tensor::Tensor(const Tensor &copy)
    : name_(copy.name()), buffer_(make_buffer(copy.type(), copy.bounds())),
      quantization_(copy.quantization_), is_constant_(copy.is_constant_),
      is_input_(copy.is_input_), is_output_(copy.is_output_), storage_(copy.storage_) {
    if (copy.is_allocated()) {
        allocate();
        // This should have used the same buffer as the copy's storage.
        assert(buffer_.data() == copy.buffer_.data());
    } else {
        assert(!buffer_.data());
    }
}

void Tensor::add_consumer(Op *op) {
    consumers_.push_back(op);
}

void Tensor::add_producer(Op *op) {
    producers_.push_back(op);
}

void Tensor::remove_consumer(Op *op) {
    consumers_.remove(op);
}

void Tensor::remove_producer(Op *op) {
    producers_.remove(op);
}

std::shared_ptr<TensorStorage> Tensor::storage() {
    if (!storage_) {
        storage_ = std::make_shared<TensorStorage>(buffer_);
    }
    return storage_;
}

bool Tensor::is_allocated() const {
    return buffer_.data() != nullptr;
}

void Tensor::allocate() {
    if (buffer_.data()) {
        return;
    }

    storage()->allocate();
    HalideBuffer<void> buffer = storage()->buffer();
    for (int i = 0; i < buffer.dimensions(); i++) {
        Interval dim_i(buffer_.dim(i).min(), buffer_.dim(i).max());
        if (i < (int)storage_offset_.size()) {
            dim_i += storage_offset_[i];
        }
        assert(buffer.dim(i).min() <= dim_i.min);
        assert(buffer.dim(i).max() >= dim_i.max);
        buffer.crop(i, dim_i.min, dim_i.extent());
        buffer.translate(i, -dim_i.min);
        assert(buffer.dim(i).min() == buffer_.dim(i).min());
        assert(buffer.dim(i).max() == buffer_.dim(i).max());
    }
    buffer_ = buffer;
}

void Tensor::set_alias_of(TensorPtr t, std::vector<int> storage_offset) {
    storage_ = t->storage();
    storage_offset_ = std::move(storage_offset);

    Box offset_bounds = bounds();
    for (int i = 0; i < (int)storage_offset_.size(); i++) {
        offset_bounds[i] += storage_offset_[i];
    }
    storage_->add_use(type(), offset_bounds);
}

void Tensor::replace_all_consumers_with(TensorPtr other) {
    // We need to make a copy of the list of consumers so it doesn't get invalidated
    // by set_input below.
    auto consumers = consumers_;
    for (Op *i : consumers) {
        for (int j = 0; j < i->input_count(); j++) {
            if (i->input(j).get() == this) {
                i->set_input(j, other);
            }
        }
    }
}

void Tensor::dump(std::ostream &os) const {
    os << "  " << buffer_.type() << " x ";

    const auto *b = buffer_.raw_buffer();
    os << '{';
    for (int i = 0; i < b->dimensions; i++) {
        if (i > 0) {
            os << ", ";
        }
        os << b->dim[i];
    }
    os << '}';

    if (is_allocated()) {
        os << " allocated";
    }
    if (is_constant()) {
        os << " constant";
    }

    os << " " << name() << std::endl;
}

Op::Op(std::vector<TensorPtr> inputs, std::vector<TensorPtr> outputs)
    : inputs_(std::move(inputs)), outputs_(std::move(outputs)) {
    for (auto &i : inputs_) {
        if (!i) continue;
        i->add_consumer(this);
    }
    for (auto &i : outputs_) {
        if (!i) continue;
        i->add_producer(this);
    }
}

Op::~Op() {
    for (auto &i : inputs_) {
        if (!i) continue;
        i->remove_consumer(this);
    }
    for (auto &i : outputs_) {
        if (!i) continue;
        i->remove_producer(this);
    }
}

void Op::set_input(int idx, TensorPtr t) {
    if (inputs_[idx]) {
        inputs_[idx]->remove_consumer(this);
    }
    inputs_[idx] = t;
    if (inputs_[idx]) {
        inputs_[idx]->add_consumer(this);
    }
}

void Op::set_output(int idx, TensorPtr t) {
    if (outputs_[idx]) {
        outputs_[idx]->remove_producer(this);
    }
    outputs_[idx] = t;
    if (outputs_[idx]) {
        outputs_[idx]->add_producer(this);
    }
}

void Op::set_input(TensorPtr t) {
    set_input(0, t);
}

void Op::set_output(TensorPtr t) {
    set_output(0, t);
}

void OpGroup::execute() {
    for (int i = 0; i < op_count(); i++) {
        op(i)->execute();
    }
}

BoundsMap OpGroup::map_bounds(int input_idx, int output_idx) const {
    BoundsMap result(input(input_idx)->rank(), output(output_idx)->rank());
    // TODO
    return result;
}

void OpGroup::add(std::unique_ptr<Op> to_add, const Op *before) {
    for (auto i = ops_.begin(); i != ops_.end(); ++i) {
        if (i->get() == before) {
            ops_.insert(i, std::move(to_add));
            return;
        } else {
            for (int j = 0; j < to_add->output_count(); ++j) {
                for (int k = 0; k < (*i)->input_count(); ++k) {
                    if ((*i)->input(k) == to_add->output(j)) {
                        // i consumes an output of to_add.
                        ops_.insert(i, std::move(to_add));
                        return;
                    }
                }
            }
        }
    }
    ops_.push_back(std::move(to_add));
}

void OpGroup::remove(const Op *op) {
    for (auto i = ops_.begin(); i != ops_.end(); ++i) {
        if (i->get() == op) {
            ops_.erase(i);
            return;
        }
    }
}

std::unique_ptr<Op> OpGroup::clone(TensorMap &tensor_map) const {
    std::vector<TensorPtr> inputs;
    for (int i = 0; i < input_count(); i++) {
        inputs.push_back(apply(tensor_map, input(i)));
    }
    std::vector<TensorPtr> outputs;
    for (int i = 0; i < output_count(); i++) {
        outputs.push_back(apply(tensor_map, output(i)));
    }

    std::vector<std::unique_ptr<Op>> ops;
    for (int i = 0; i < op_count(); i++) {
        ops.push_back(op(i)->clone(tensor_map));
    }

    return ::hannk::make_unique<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));
}

void OpGroup::accept(OpVisitor *v) {
    for (int i = 0; i < op_count(); i++) {
        op(i)->accept(v);
    }
}

void OpGroup::dump(std::ostream &os) const {
    os << "Ops: " << std::endl;
    for (const auto &i : ops_) {
        i->dump(os);
    }
    os << std::endl;
}

}  // namespace hannk
