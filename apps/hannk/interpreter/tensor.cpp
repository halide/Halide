#include "interpreter/tensor.h"

namespace hannk {

namespace {

bool is_dense(const HalideBuffer<const void> &buffer) {
    return buffer.size_in_bytes() == buffer.number_of_elements() * buffer.type().bytes();
}

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

bool Tensor::is_dense() const {
    return ::hannk::is_dense(buffer());
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
        assert(storage_offset_.empty());
    }
    return storage_;
}

void Tensor::set_external_buffer(HalideBuffer<void> external_buffer) {
    assert(!is_dynamic());
    assert(is_external());

    // No: it's ok to set this to different values over time,
    // so don't assert that host is currently null (or already equal to the new value)
    // assert(!is_allocated());

    storage()->buffer = std::move(external_buffer);
    finish_buffer_allocation();

    if (alias_info_ != nullptr) {
        for (const auto &weak : alias_info_->aliases) {
            TensorPtr tp = weak.lock();  // null if the weak_ptr has expired
            if (tp != nullptr && tp.get() != this) {
                assert(!tp->is_external());
                tp->finish_buffer_allocation();
            }
        }
    }
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

    if (alias_type() == AliasType::Reshaped) {
        assert(raw_storage_buffer->number_of_elements() == buffer_.number_of_elements());
        assert(raw_storage_buffer->type == buffer_.type());
        assert(storage_offset_.empty());
        buffer_ = HalideBuffer<void>(buffer_.type(), raw_storage_buffer->host,
                                     buffer_.raw_buffer()->dimensions, buffer_.raw_buffer()->dim);
    } else {
        // Note that this may have a different type than storage_buffer,
        // though the *size* of the types must match!
        assert(raw_storage_buffer->type.bytes() == buffer_.type().bytes());
        HalideBuffer<void> final_buffer(buffer_.type(), raw_storage_buffer->host,
                                        raw_storage_buffer->dimensions, raw_storage_buffer->dim);

        for (int i = 0; i < final_buffer.dimensions(); i++) {
            const auto d = buffer_.dim(i);
            Interval dim_i(d.min(), d.max());
            if (i < (int)storage_offset_.size()) {
                dim_i += storage_offset_[i];
            }
            assert(final_buffer.dim(i).min() <= dim_i.min);
            assert(final_buffer.dim(i).max() >= dim_i.max);

            final_buffer.crop(i, dim_i.min, dim_i.extent());
            final_buffer.translate(i, -dim_i.min);
            assert(final_buffer.dim(i).min() == d.min());
            assert(final_buffer.dim(i).max() == d.max());
        }

        buffer_ = std::move(final_buffer);
    }

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

bool Tensor::has_external_alias() const {
    if (alias_info_ != nullptr) {
        for (const auto &weak : alias_info_->aliases) {
            TensorPtr tp = weak.lock();  // null if the weak_ptr has expired
            if (tp != nullptr && tp->is_external()) {
                return true;
            }
        }
    }
    return false;
}

// Return true iff 'this' can be an alias of 'source' of the given type.
bool Tensor::can_alias(const TensorPtr &source, AliasType alias_type) const {
    if (alias_type == AliasType::None || this == source.get()) {
        return false;  // Bad inputs, just say no
    }

    if (this->is_allocated()) {
        // Can't alias a tensor that already has an allocation.
        return false;
    }

    // If either tensor is dynamic, can't alias them.
    if (this->is_dynamic() || source->is_dynamic()) {
        return false;
    }

    if (this->type().bytes() != source->type().bytes()) {
        // We can't alias tensors with types of different size.
        return false;
    }

    if (this->alias_type() != AliasType::None || this->alias_info_ != nullptr) {
        // Can't alias a tensor multiple times.
        return false;
    }

    if (source->alias_type() != AliasType::None && source->alias_type() != alias_type) {
        // The source of the aliasing must either be unaliased or of the type we want
        return false;
    }

    if (alias_type == AliasType::Offset) {
        // AliasType::Offset allows for NO external tensors in the alias group
        if (this->is_external() || source->is_external()) {
            return false;
        }

        if (this->rank() != source->rank()) {
            // AliasType::Offset can't alias tensors with different rank.
            return false;
        }
    } else if (alias_type == AliasType::Reshaped) {
        // AliasType::Reshaped allows for at most one external tensor in the alias group
        const bool this_external = this->is_external() || this->has_external_alias();
        const bool source_external = source->is_external() || source->has_external_alias();
        if (this_external && source_external) {
            return false;
        }
    }

    return true;
}

/*static*/ void Tensor::make_offset_alias(TensorPtr alias, TensorPtr source, const TensorOffset &storage_offset) {
    // std::cout << "make_offset_alias:\n";
    // std::cout << "   alias: "; alias->dump(std::cout);
    // std::cout << "   source: "; source->dump(std::cout);

    assert(alias->can_alias(source, AliasType::Offset));

    if (source->alias_info_ == nullptr) {
        source->alias_info_ = std::make_shared<AliasInfo>(source, AliasType::Offset);
    } else {
        assert(source->alias_info_->alias_type == AliasType::Offset);
    }

    assert(alias->alias_info_ == nullptr);
    alias->alias_info_ = source->alias_info_;
    alias->alias_info_->aliases.push_back(alias);

    assert(alias->storage_ == nullptr);
    alias->storage_ = source->storage();
    assert(alias->storage_offset_.empty());
    alias->storage_offset_ = storage_offset;

#ifndef NDEBUG
    // Reality-check.
    Box offset_bounds = alias->bounds();
    for (int i = 0; i < (int)alias->storage_offset_.size(); i++) {
        offset_bounds[i] += alias->storage_offset_[i];
    }
    auto &shared_buffer = alias->storage_->buffer;
    assert(shared_buffer.type().bytes() == alias->type().bytes());
    assert(shared_buffer.dimensions() == (int)offset_bounds.size());
    assert(!shared_buffer.data());

    // Check that the storage is big enough for this buffer.
    for (int i = 0; i < shared_buffer.dimensions(); i++) {
        assert(offset_bounds[i].min >= shared_buffer.dim(i).min());
        assert(offset_bounds[i].max <= shared_buffer.dim(i).max());
    }
#endif
}

/*static*/ void Tensor::make_reshape_alias(TensorPtr alias, TensorPtr source) {
    // std::cout << "make_reshape_alias:\n";
    // std::cout << "   alias: "; alias->dump(std::cout);
    // std::cout << "   source: "; source->dump(std::cout);

    assert(alias->can_alias(source, AliasType::Reshaped));

    if (alias->is_external()) {
        assert(!source->has_external_alias());
    } else if (source->is_external()) {
        assert(!alias->has_external_alias());
    }

    if (source->alias_info_ == nullptr) {
        source->alias_info_ = std::make_shared<AliasInfo>(source, AliasType::Reshaped);
    } else {
        assert(source->alias_info_->alias_type == AliasType::Reshaped);
    }

    assert(alias->alias_info_ == nullptr);
    alias->alias_info_ = source->alias_info_;
    alias->alias_info_->aliases.push_back(alias);

    assert(alias->storage_ == nullptr);
    alias->storage_ = source->storage();
    assert(alias->storage_offset_.empty());

#ifndef NDEBUG
    assert(alias->storage_offset_.empty());
    assert(alias->buffer().type().bytes() == source->buffer().type().bytes());
    assert(alias->buffer().number_of_elements() == source->buffer().number_of_elements());
#endif
}

void Tensor::dump(std::ostream &os) const {
    os << "  \"" << name() << "\" this:@" << (const void *)this;

    os << " " << buffer_.type() << " x ";

    const auto dump_dims = [&os](const halide_buffer_t *b) {
        os << '{';
        for (int i = 0; i < b->dimensions; i++) {
            if (i > 0) {
                os << ", ";
            }
            os << b->dim[i];
        }
        os << '}';
    };

    dump_dims(buffer_.raw_buffer());

    if (is_allocated()) {
        os << " allocated";
    } else {
        os << " unallocated";
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
    if (alias_type() != AliasType::None) {
        os << (alias_type() == AliasType::Offset ? " alias_offset{" : " alias_reshaped{");
        for (const auto &weak : alias_info_->aliases) {
            TensorPtr tp = weak.lock();  // null if the weak_ptr has expired
            os << " " << (void *)tp.get();
        }
        os << " } storage_offset{";
        for (size_t i = 0; i < storage_offset_.size(); i++) {
            if (i > 0) {
                os << ", ";
            }
            os << storage_offset_[i];
        }
        os << "}";
    }
    if (is_dense()) {
        os << " dense";
    } else {
        os << " sparse";
    }

    os << " storage:@" << (void *)storage_.get();
    if (storage_) {
        os << ' ';
        dump_dims(storage_->buffer.raw_buffer());
    }

    os << std::endl;
}

}  // namespace hannk
