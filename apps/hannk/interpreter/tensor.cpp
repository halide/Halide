#include "interpreter/tensor.h"
#include "interpreter/model.h"

namespace hannk {

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

bool Tensor::is_allocated() const {
    return buffer_.data() != nullptr;
}

void Tensor::set_external_host(void *host) {
    // No: it's ok to set this to different values over time,
    // so don't assert that host is currently null (or already equal to the new value)
    // assert(!is_allocated());

    assert(is_external());
    assert(!buffer_.owns_host_memory());
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

void Tensor::set_external_buffer(HalideBuffer<void> external_buffer) {
    HCHECK(!is_allocated());
    HCHECK(!is_dynamic());
    HCHECK(is_external());

    // TODO: NDEBUG
    for (int i = 0; i < buffer_.dimensions(); i++) {
        HCHECK(external_buffer.dim(i).min() == buffer_.dim(i).min());
        HCHECK(external_buffer.dim(i).max() == buffer_.dim(i).max());
        HCHECK(external_buffer.dim(i).extent() >= buffer_.dim(i).extent());
    }
    buffer_ = std::move(external_buffer);
}

void Tensor::allocate() {
    if (buffer_.data()) {
        return;
    }

    if (is_dynamic() || is_external()) {
        return;
    }

    buffer_.allocate();
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

}  // namespace hannk
