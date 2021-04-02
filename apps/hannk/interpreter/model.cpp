#include "interpreter/model.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

Tensor *apply(const TensorMap &map, const Tensor *t) {
    auto i = map.find(t);
    if (i != map.end()) {
        return i->second;
    }
    // TODO: Try to do this without const_cast?
    return const_cast<Tensor *>(t);
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


TensorStorage::TensorStorage() {}
TensorStorage::TensorStorage(HalideBuffer<void> buffer) : buffer_(buffer) {
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
    : name_(copy.name()), buffer_(make_buffer(copy.type(), copy.box())),
      quantization_(copy.quantization_), is_constant_(copy.is_constant_),
      is_input_(copy.is_input_), is_output_(copy.is_output_), storage_(copy.storage_) {
    if (copy.is_allocated()) {
        allocate();
        // This should have used the same buffer as the copy's storage.
        assert(buffer_.data() == copy.buffer_.data());
    } else {
        assert(!storage_.buffer_.data());
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

void Tensor::set_alias_of(Tensor *t, std::vector<int> storage_offset) {
    storage_ = t->storage();
    storage_offset_ = std::move(storage_offset);

    Box bounds = box();
    for (int i = 0; i < (int)storage_offset_.size(); i++) {
        bounds[i] += storage_offset_[i];
    }
    storage_->add_use(type(), bounds);
}

void Tensor::replace_all_consumers_with(Tensor *other) {
    // We need to make a copy of the list of consumers so it doesn't get invalidated
    // by set_input below.
    auto consumers = consumers_;
    for (Op *i : consumers) {
        for (int j = 0; j < i->input_count(); j++) {
            if (i->input(j) == this) {
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

Op::Op(std::vector<Tensor *> inputs, std::vector<Tensor *> outputs)
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

void Op::set_input(int idx, Tensor *t) {
    if (inputs_[idx]) {
        inputs_[idx]->remove_consumer(this);
    }
    inputs_[idx] = t;
    if (inputs_[idx]) {
        inputs_[idx]->add_consumer(this);
    }
}

void Op::set_output(int idx, Tensor *t) {
    if (outputs_[idx]) {
        outputs_[idx]->remove_producer(this);
    }
    outputs_[idx] = t;
    if (outputs_[idx]) {
        outputs_[idx]->add_producer(this);
    }
}

void Op::set_input(Tensor *t) {
    set_input(0, t);
}

void Op::set_output(Tensor *t) {
    set_output(0, t);
}

Model::Model(const Model &copy) {
    // Clone the tensors, making a mapping from old tensor to new tensor.
    TensorMap map;
    for (const auto &i : copy.tensors) {
        auto cloned = ::hannk::make_unique<Tensor>(*i);
        map[i.get()] = cloned.get();
        tensors.push_back(std::move(cloned));
    }

    // Now copy the ops, using the tensor map we made above.
    for (const auto &i : copy.ops) {
        ops.push_back(i->clone(map));
    }
}

void Model::insert(std::unique_ptr<Tensor> to_insert, const Tensor *after) {
    for (auto i = tensors.begin(); i != tensors.end(); ++i) {
        if (i->get() == after) {
            tensors.insert(++i, std::move(to_insert));
            return;
        }
    }
    tensors.push_back(std::move(to_insert));
}

void Model::insert(std::unique_ptr<Op> to_insert, const Op *before) {
    for (auto i = ops.begin(); i != ops.end(); ++i) {
        if (i->get() == before) {
            ops.insert(i, std::move(to_insert));
            return;
        } else {
            for (int j = 0; j < to_insert->output_count(); ++j) {
                for (int k = 0; k < (*i)->input_count(); ++k) {
                    if ((*i)->input(k) == to_insert->output(j)) {
                        // i consumes an output of to_insert.
                        ops.insert(i, std::move(to_insert));
                        return;
                    }
                }
            }
        }
    }
    ops.push_back(std::move(to_insert));
}

void Model::remove(const Op *op) {
    for (auto i = ops.begin(); i != ops.end(); ++i) {
        if (i->get() == op) {
            ops.erase(i);
            return;
        }
    }
}

void Model::accept(OpVisitor *v) {
    // TODO: Major hack, don't use iterators because visitors might invalidate them.
    for (int i = 0; i < (int)ops.size(); i++) {
        ops[i]->accept(v);
    }
}

void Model::dump(std::ostream &os) {
    os << "Tensors: " << std::endl;
    for (const auto &i : tensors) {
        i->dump(os);
    }

    os << "Ops: " << std::endl;
    for (const auto &i : ops) {
        i->dump(os);
    }
    os << std::endl;
}

}  // namespace hannk
