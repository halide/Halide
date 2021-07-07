#include "interpreter/tensor.h"

namespace hannk {

namespace {

HalideBuffer<void> make_unallocated_buffer(halide_type_t type, const Box &bounds) {
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
    : buffer(type, nullptr, rank, dimensions) {
}

size_t TensorStorage::storage_size() const {
    return buffer.size_in_bytes();
}

Tensor::Tensor(std::string name, HalideBuffer<void> buffer, QuantizationInfo quantization)
    : name_(std::move(name)),
      buffer_(std::move(buffer)),
      quantization_(std::move(quantization)) {
}

Tensor::Tensor(std::string name, halide_type_t type, const Box &bounds, QuantizationInfo quantization)
    : Tensor(name, make_unallocated_buffer(type, bounds), quantization) {
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

TensorStoragePtr Tensor::storage() {
    if (!storage_) {
        halide_buffer_t *raw_buf = buffer_.raw_buffer();
        // TensorStorage always allocates as uint.
        halide_type_t storage_type(halide_type_uint, raw_buf->type.bytes() * 8);
        storage_ = std::make_shared<TensorStorage>(storage_type, raw_buf->dimensions, raw_buf->dim);
    }
    return storage_;
}

void Tensor::set_external_buffer(HalideBuffer<void> external_buffer) {
    assert(!is_dynamic());
    assert(is_external());
    assert(storage_ == nullptr);

    // No: it's ok to set this to different values over time,
    // so don't assert that host is currently null (or already equal to the new value)
    // assert(!is_allocated());

    for (int i = 0; i < buffer_.dimensions(); i++) {
        assert(external_buffer.dim(i).min() == buffer_.dim(i).min());
        assert(external_buffer.dim(i).extent() == buffer_.dim(i).extent());
    }
    buffer_ = std::move(external_buffer);
}

void Tensor::allocate_from_arena_pointer(void *host) {
    assert(!is_dynamic());
    assert(!is_external());
    assert(!is_allocated());

    auto &storage_buffer = storage()->buffer;
    halide_buffer_t *raw_storage_buffer = storage_buffer.raw_buffer();
    assert(raw_storage_buffer->host == nullptr || raw_storage_buffer->host == host);
    raw_storage_buffer->host = (uint8_t *)host;

    finish_buffer_allocation();
}

void Tensor::allocate_from_heap() {
    assert(!is_dynamic());
    assert(!is_external());
    assert(!is_allocated());

    auto &storage_buffer = storage()->buffer;
    assert(!storage_buffer.data());
    storage_buffer.allocate();

    finish_buffer_allocation();
}

void Tensor::finish_buffer_allocation() {
    auto &storage_buffer = storage()->buffer;
    halide_buffer_t *raw_storage_buffer = storage_buffer.raw_buffer();
    assert(raw_storage_buffer->host);

    // Note that this may have a different type than storage_buffer,
    // though the *size* of the types must match!
    assert(raw_storage_buffer->type.bytes() == buffer_.type().bytes());
    HalideBuffer<void> final_buffer(buffer_.type(), raw_storage_buffer->host,
                                    raw_storage_buffer->dimensions, raw_storage_buffer->dim);

    for (int i = 0; i < final_buffer.dimensions(); i++) {
        Interval dim_i(buffer_.dim(i).min(), buffer_.dim(i).max());
        if (i < (int)storage_offset_.size()) {
            dim_i += storage_offset_[i];
        }
        assert(final_buffer.dim(i).min() <= dim_i.min);
        assert(final_buffer.dim(i).max() >= dim_i.max);

        final_buffer.crop(i, dim_i.min, dim_i.extent());
        final_buffer.translate(i, -dim_i.min);
        assert(final_buffer.dim(i).min() == buffer_.dim(i).min());
        assert(final_buffer.dim(i).max() == buffer_.dim(i).max());
    }

    buffer_ = std::move(final_buffer);

    assert(is_allocated());
}

void Tensor::resize_dynamic(const Box &new_shape) {
    assert(is_dynamic());
    assert(!is_external());
    assert(storage_ == nullptr);
    // No: we might need to resize a dynamic Tensor more than once.
    // assert(!is_allocated());

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

void Tensor::set_alias_of(const TensorPtr &t, const SmallVector<int, max_rank> &storage_offset) {
    assert(!is_dynamic());
    assert(!is_external());
    assert(!is_alias());
    // No: 't' may (or may not) already have is_alias_ = true,
    // but both will be considered an alias after this call.
    // assert(!t->is_alias_);

    storage_ = t->storage();
    storage_offset_ = storage_offset;

#ifndef NDEBUG
    // Reality-check.
    Box offset_bounds = bounds();
    for (int i = 0; i < (int)storage_offset_.size(); i++) {
        offset_bounds[i] += storage_offset_[i];
    }
    auto &shared_buffer = storage_->buffer;
    assert(shared_buffer.type().bytes() == type().bytes());
    assert(shared_buffer.dimensions() == (int)offset_bounds.size());
    assert(!shared_buffer.data());

    // Check that the storage is big enough for this buffer.
    for (int i = 0; i < shared_buffer.dimensions(); i++) {
        assert(offset_bounds[i].min >= shared_buffer.dim(i).min());
        assert(offset_bounds[i].max <= shared_buffer.dim(i).max());
    }
#endif

    is_alias_ = true;
    t->is_alias_ = true;
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
    if (is_alias()) {
        os << " alias";
    }

    os << " " << name() << std::endl;
}

}  // namespace hannk
