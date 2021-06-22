#include "interpreter/tensor.h"

namespace hannk {

namespace {

HalideBuffer<void> make_buffer(halide_type_t type, const Box &bounds) {
    TensorDimensions dims(bounds.size());
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

TensorStorage::TensorStorage(halide_type_t type, int rank, const halide_dimension_t *dimensions)
    : buffer_(type, nullptr, rank, dimensions) {
}

void TensorStorage::add_use(halide_type_t type, const Box &bounds) {
    if (buffer_.dimensions() == 0) {
        buffer_ = make_buffer(type, bounds);
    } else {
        assert(buffer_.type() == type);
        assert(buffer_.dimensions() == (int)bounds.size());
        assert(!buffer_.data());

        // Check that the storage is big enough for this buffer.
        for (int i = 0; i < buffer_.dimensions(); i++) {
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
        storage_ = std::make_shared<TensorStorage>(buffer_.type(), buffer_.dimensions(), buffer_.raw_buffer()->dim);
    }
    return storage_;
}

bool Tensor::is_allocated() const {
    return buffer_.data() != nullptr;
}

void Tensor::set_external_buffer(HalideBuffer<void> external_buffer) {
    assert(!is_dynamic());
    assert(is_external());

    // No: it's ok to set this to different values over time,
    // so don't assert that host is currently null (or already equal to the new value)
    // assert(!is_allocated());

    // TODO: we don't allow aliasing of external tensors right now.
    // If we do, we need to maintain and update storage_ appropriately.
    assert(storage_ == nullptr);

    for (int i = 0; i < buffer_.dimensions(); i++) {
        assert(external_buffer.dim(i).min() == buffer_.dim(i).min());
        assert(external_buffer.dim(i).extent() == buffer_.dim(i).extent());
    }
    buffer_ = std::move(external_buffer);
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

void Tensor::resize_dynamic(const Box &new_shape) {
    assert(is_dynamic());
    assert(!is_external());

    TensorDimensions new_dims;

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

}  // namespace hannk
