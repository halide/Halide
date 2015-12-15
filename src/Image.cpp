#include "Image.h"

namespace Halide {

void ImageBase::prepare_for_direct_pixel_access() {
    // Make sure buffer has been copied to host. This is a no-op
    // if there's no device involved.
    buffer.copy_to_host();

    // We're probably about to modify the pixels, so to be
    // conservative we'd better set host dirty. If you're sure
    // you're not going to modify this memory via the Image
    // object, then you can call set_host_dirty(false) on the
    // underlying buffer.
    buffer.set_host_dirty(true);

    if (buffer.defined()) {
        // Cache some of the things we need to do to get efficient
        // (low-dimensional) pixel access.
        origin = buffer.host_ptr();
        ssize_t offset = 0;
        if (buffer.dimensions() > 0) {
            stride_0 = buffer.dim(0).stride();
            offset += buffer.dim(0).min() * stride_0;
        }
        if (buffer.dimensions() > 1) {
            stride_1 = buffer.dim(1).stride();
            offset += buffer.dim(1).min() * stride_1;
        }
        if (buffer.dimensions() > 2) {
            stride_2 = buffer.dim(2).stride();
            offset += buffer.dim(2).min() * stride_2;
        }
        if (buffer.dimensions() > 3) {
            stride_3 = buffer.dim(3).stride();
            offset += buffer.dim(3).min() * stride_3;
        }
        elem_size = buffer.type().bytes();
        offset *= elem_size;
        // The host pointer points to the mins vec, but we want to
        // point to the origin of the coordinate system.
        origin = (void *)((uint8_t *)origin - offset);
        dims = buffer.dimensions();
    } else {
        origin = NULL;
        stride_0 = stride_1 = stride_2 = stride_3 = 0;
        dims = 0;
    }
}

bool ImageBase::add_implicit_args_if_placeholder(std::vector<Expr> &args,
                                                 Expr last_arg,
                                                 int total_args,
                                                 bool placeholder_seen) const {
    const Internal::Variable *var = last_arg.as<Internal::Variable>();
    bool is_placeholder = var != NULL && Var::is_placeholder(var->name);
    if (is_placeholder) {
        user_assert(!placeholder_seen)
            << "Only one placeholder ('_') allowed in argument list for Image.\n";
        placeholder_seen = true;

        // The + 1 in the conditional is because one provided argument is an placeholder
        for (int i = 0; i < (dims - total_args + 1); i++) {
            args.push_back(Var::implicit(i));
        }
    } else {
        args.push_back(last_arg);
    }

    if (!is_placeholder && !placeholder_seen &&
        (int)args.size() == total_args &&
        (int)args.size() < dims) {
        user_error << "Can't construct a " << args.size()
                   << "-argument reference to Image \"" << buffer.name()
                   << "\" with " << dims << " dimensions.\n";
    }
    return is_placeholder;
}

namespace {
std::string make_image_name(const std::string &name, ImageBase *im) {
    if (name.empty()) {
        return Internal::make_entity_name(im, "Halide::Image<?", 'i');
    } else {
        return name;
    }
}
}

ImageBase::ImageBase(Type t, const std::vector<int> &size, const std::string &name) :
    buffer(Buffer(t, size, NULL, make_image_name(name, this))) {
    prepare_for_direct_pixel_access();
}

ImageBase::ImageBase(Type t, const Buffer &buf) : buffer(buf) {
    if (t != buffer.type()) {
        user_error << "Can't construct Image of type " << t
                   << " from buffer \"" << buf.name()
                   << "\" of type " << buffer.type() << '\n';
    }
    prepare_for_direct_pixel_access();
}

ImageBase::ImageBase(Type t, const Realization &r) : buffer(r) {
    if (t != buffer.type()) {
        user_error << "Can't construct Image of type " << t
                   << " from buffer \"" << buffer.name()
                   << "\" of type " << buffer.type() << '\n';
    }
    prepare_for_direct_pixel_access();
}

ImageBase::ImageBase(Type t, const halide_buffer_t *b, const std::string &name) :
    buffer(b, make_image_name(name, this)) {
    if (t != buffer.type()) {
        user_error << "Can't construct Image of type " << t
                   << " from halide_buffer_t of type "
                   << Type(b->type) << '\n';
    }
    prepare_for_direct_pixel_access();
}

const std::string &ImageBase::name() {
    return buffer.name();
}

void ImageBase::copy_to_host() {
    buffer.copy_to_host();
}

void ImageBase::set_host_dirty(bool dirty) {
    buffer.set_host_dirty(dirty);
}

bool ImageBase::defined() const {
    return buffer.defined();
}

int ImageBase::dimensions() const {
    return dims;
}

Buffer::Dimension ImageBase::dim(int idx) const {
    return buffer.dim(idx);
}

void ImageBase::set_min(const std::vector<int> &m) {
    user_assert(defined()) << "set_min of undefined Image\n";
    buffer.set_min(m);
    // Move the origin
    prepare_for_direct_pixel_access();
}

int ImageBase::width() const {
    if (dimensions() < 1) return 1;
    return dim(0).extent();
}

int ImageBase::height() const {
    if (dimensions() < 2) return 1;
    return dim(1).extent();
}

int ImageBase::channels() const {
    if (dimensions() < 3) return 1;
    return dim(2).extent();
}

int ImageBase::left() const {
    if (dimensions() < 1) return 0;
    return dim(0).min();
}

int ImageBase::right() const {
    if (dimensions() < 1) return 0;
    return dim(0).max();
}

int ImageBase::top() const {
    if (dimensions() < 2) return 0;
    return dim(1).min();
}

int ImageBase::bottom() const {
    if (dimensions() < 2) return 0;
    return dim(1).max();
}

Expr ImageBase::operator()() const {
    user_error << "Can't construct a zero-argument reference to Image \"" << buffer.name()
               << "\" with " << dims << " dimensions.\n";
    std::vector<Expr> args;
    return Internal::Call::make(buffer, args);
}

Expr ImageBase::operator()(Expr x) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    placeholder_seen |= add_implicit_args_if_placeholder(args, x, 1, placeholder_seen);

    Internal::check_call_arg_types(buffer.name(), &args, dims);

    return Internal::Call::make(buffer, args);
}

Expr ImageBase::operator()(Expr x, Expr y) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    placeholder_seen |= add_implicit_args_if_placeholder(args, x, 2, placeholder_seen);
    placeholder_seen |= add_implicit_args_if_placeholder(args, y, 2, placeholder_seen);

    Internal::check_call_arg_types(buffer.name(), &args, dims);

    return Internal::Call::make(buffer, args);
}

Expr ImageBase::operator()(Expr x, Expr y, Expr z) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    placeholder_seen |= add_implicit_args_if_placeholder(args, x, 3, placeholder_seen);
    placeholder_seen |= add_implicit_args_if_placeholder(args, y, 3, placeholder_seen);
    placeholder_seen |= add_implicit_args_if_placeholder(args, z, 3, placeholder_seen);

    Internal::check_call_arg_types(buffer.name(), &args, dims);

    return Internal::Call::make(buffer, args);
}

Expr ImageBase::operator()(Expr x, Expr y, Expr z, Expr w) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    placeholder_seen |= add_implicit_args_if_placeholder(args, x, 4, placeholder_seen);
    placeholder_seen |= add_implicit_args_if_placeholder(args, y, 4, placeholder_seen);
    placeholder_seen |= add_implicit_args_if_placeholder(args, z, 4, placeholder_seen);
    placeholder_seen |= add_implicit_args_if_placeholder(args, w, 4, placeholder_seen);

    Internal::check_call_arg_types(buffer.name(), &args, dims);

    return Internal::Call::make(buffer, args);
}

Expr ImageBase::operator()(std::vector<Expr> args_passed) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    for (size_t i = 0; i < args_passed.size(); i++) {
        placeholder_seen |=
            add_implicit_args_if_placeholder(args, args_passed[i],
                                             args_passed.size(), placeholder_seen);
    }

    Internal::check_call_arg_types(buffer.name(), &args, dims);
    return Internal::Call::make(buffer, args);
}

Expr ImageBase::operator()(std::vector<Var> args_passed) const {
    std::vector<Expr> args;
    bool placeholder_seen = false;
    for (size_t i = 0; i < args_passed.size(); i++) {
        placeholder_seen |=
            add_implicit_args_if_placeholder(args, args_passed[i],
                                             args_passed.size(), placeholder_seen);
    }

    Internal::check_call_arg_types(buffer.name(), &args, dims);
    return Internal::Call::make(buffer, args);
}

halide_buffer_t *ImageBase::raw_buffer() const {
    return buffer.raw_buffer();
}

}
