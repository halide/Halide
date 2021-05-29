#include "interpreter/model.h"
#include "interpreter/ops.h"
#include "util/error_util.h"

#include <cmath>
#include <list>

namespace hannk {

const TensorPtr &apply(TensorMap &map, const TensorPtr &t) {
    auto i = map.find(t);
    if (i != map.end()) {
        return i->second;
    }

    if (t->is_constant()) {
        // Share constant tensors across users.
        return t;
    } else {
        // Remember this cloned tensor for later applications of the mapping.
        return map[t] = std::make_shared<Tensor>(*t);
    }
}

namespace {

HalideBuffer<void> make_buffer(halide_type_t type, const Box &bounds) {
    // TODO: Avoid this dynamic allocation. Halide's API requires std::vector here.
    SmallVector<halide_dimension_t, max_rank> dims(bounds.size());
    int stride = 1;
    for (int i = 0; i < (int)bounds.size(); i++) {
        dims[i].min = bounds[i].min;
        dims[i].extent = bounds[i].extent();
        dims[i].stride = stride;
        stride *= dims[i].extent;
    }
    return HalideBuffer<void>(type, nullptr, (int)dims.size(), dims.data());
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
}

Tensor::Tensor(std::string name, halide_type_t type, const Box &bounds, QuantizationInfo quantization)
    : Tensor(name, make_buffer(type, bounds), quantization) {
}

Tensor::Tensor(const Tensor &copy)
    : name_(copy.name()), buffer_(make_buffer(copy.type(), copy.bounds())),
      quantization_(copy.quantization_), is_constant_(copy.is_constant_), is_external_(copy.is_external_),
      is_input_(copy.is_input_), is_output_(copy.is_output_), is_dynamic_(copy.is_dynamic_),
      storage_(copy.storage_) {
    if (copy.is_allocated()) {
        assert(!is_dynamic());
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

void Tensor::set_external_host(void *host) {
    // No: it's ok to set this to different values over time,
    // so don't assert that host is currently null (or already equal to the new value)
    // assert(!is_allocated());

    assert(is_external());
    assert(!buffer_.owns_host_memory());
    // TODO: we don't allow aliasing of external tensors right now.
    // If we do, we need to maintain and update storage_ appropriately.
    assert(storage_ == nullptr);
    buffer_.raw_buffer()->host = (uint8_t *)host;
}

namespace {

// Copy a Halide buffer without the internal reference counting.
// This reduces overhead of buffer copies, and is unnecessary because
// we do our own reference counting.
template<typename T>
HalideBuffer<T> drop_reference(const HalideBuffer<T> &buf) {
    return HalideBuffer<T>(buf.type(), buf.data(), buf.dimensions(), buf.raw_buffer()->dim);
}

}  // namespace

void Tensor::allocate() {
    if (buffer_.data()) {
        return;
    }

    if (is_dynamic() || is_external()) {
        return;
    }

    storage()->allocate();
    HalideBuffer<void> buffer = drop_reference(storage()->buffer());
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

void Tensor::resize(const Box &new_shape) {
    assert(is_dynamic());
    assert(!is_external());

    SmallVector<halide_dimension_t, max_rank> new_dims;

    const halide_dimension_t *old_dims = buffer_.raw_buffer()->dim;

    bool all_same = (buffer_.dimensions() == (int)new_shape.size());
    // Resizing a dynamic tensor shouldn't (AFAICT) ever change the
    // number of dimensions -- just the extents -- but let's guard
    // against that just in case, because it's easy to do.
    assert(all_same);

    int stride = 1;
    for (const auto &d : new_shape) {
        const int d_min = d.min;
        const int d_extent = d.extent();
        if (all_same && (d_min != old_dims->min || d_extent != old_dims->extent)) {
            all_same = false;
        }
        new_dims.emplace_back(d_min, d_extent, stride);
        stride *= d_extent;
    }
    if (all_same) {
        return;
    }

    HalideBuffer<void> new_buffer(buffer_.type(), nullptr, (int)new_dims.size(), new_dims.data());
    new_buffer.allocate();
    if (buffer_.data()) {
        new_buffer.copy_from(buffer_);
    }
    buffer_ = std::move(new_buffer);
    storage_ = nullptr;
}

bool Tensor::is_alias() const {
    // TODO: This check could incorrectly return true, if the tensor has been
    // allocated already via storage(), but isn't an alias.
    return storage_ != nullptr;
}

void Tensor::set_alias_of(const TensorPtr &t, const SmallVector<int, max_rank> &storage_offset) {
    assert(!is_dynamic() && !is_external());

    storage_ = t->storage();
    storage_offset_ = storage_offset;

    Box offset_bounds = bounds();
    for (int i = 0; i < (int)storage_offset_.size(); i++) {
        offset_bounds[i] += storage_offset_[i];
    }
    storage_->add_use(type(), offset_bounds);
}

void Tensor::replace_all_consumers_with(const TensorPtr &other) {
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
    if (is_external()) {
        os << " external";
    }
    if (is_dynamic()) {
        os << " dynamic";
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
    inputs_[idx] = std::move(t);
    if (inputs_[idx]) {
        inputs_[idx]->add_consumer(this);
    }
}

void Op::set_output(int idx, TensorPtr t) {
    if (outputs_[idx]) {
        outputs_[idx]->remove_producer(this);
    }
    outputs_[idx] = std::move(t);
    if (outputs_[idx]) {
        outputs_[idx]->add_producer(this);
    }
}

void Op::set_input(TensorPtr t) {
    set_input(0, std::move(t));
}

void Op::set_output(TensorPtr t) {
    set_output(0, std::move(t));
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

void OpGroup::add(OpPtr to_add, const Op *before) {
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

OpPtr OpGroup::clone(TensorMap &tensor_map) const {
    std::vector<TensorPtr> inputs;
    for (int i = 0; i < input_count(); i++) {
        inputs.push_back(apply(tensor_map, input(i)));
    }
    std::vector<TensorPtr> outputs;
    for (int i = 0; i < output_count(); i++) {
        outputs.push_back(apply(tensor_map, output(i)));
    }

    std::vector<OpPtr> ops;
    for (int i = 0; i < op_count(); i++) {
        ops.push_back(op(i)->clone(tensor_map));
    }

    return make_op<OpGroup>(std::move(inputs), std::move(outputs), std::move(ops));
}

void OpGroup::accept(OpVisitor *v) {
    v->visit(this);
}

void OpGroup::dump(std::ostream &os) const {
    os << "Ops: " << std::endl;
    for (const auto &i : ops_) {
        i->dump(os);
    }
    os << std::endl;
}

}  // namespace hannk
