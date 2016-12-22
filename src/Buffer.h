#ifndef HALIDE_BUFFER_H
#define HALIDE_BUFFER_H

#include "runtime/HalideBuffer.h"
#include "IntrusivePtr.h"
#include "Expr.h"
#include "Util.h"
#include "DeviceInterface.h"

namespace Halide {

template<typename T = void> class Buffer;

namespace Internal {

struct BufferContents {
    mutable RefCount ref_count;
    std::string name;
    Runtime::Buffer<> buf;
};

Expr buffer_accessor(const Buffer<> &buf, const std::vector<Expr> &args);

template<typename ...Args>
struct all_ints_and_optional_name : std::false_type {};

template<typename First, typename ...Rest>
struct all_ints_and_optional_name<First, Rest...> :
        meta_and<std::is_convertible<First, int>,
                 all_ints_and_optional_name<Rest...>> {};

template<> struct all_ints_and_optional_name<std::string> : std::true_type {};
template<> struct all_ints_and_optional_name<const char *> : std::true_type {};
template<> struct all_ints_and_optional_name<> : std::true_type {};

template<typename T>
std::string get_name_from_end_of_parameter_pack(T&&) {
    return "";
}

inline std::string get_name_from_end_of_parameter_pack(const std::string &n) {
    return n;
}

inline std::string get_name_from_end_of_parameter_pack() {
    return "";
}

template<typename First,
         typename Second,
         typename ...Args>
std::string get_name_from_end_of_parameter_pack(First first, Second second, Args&&... rest) {
    return get_name_from_end_of_parameter_pack(second, std::forward<Args>(rest)...);
}

inline void get_shape_from_start_of_parameter_pack_helper(std::vector<int> &, const std::string &) {
}

inline void get_shape_from_start_of_parameter_pack_helper(std::vector<int> &) {
}

template<typename ...Args>
void get_shape_from_start_of_parameter_pack_helper(std::vector<int> &result, int x, Args&&... rest) {
    result.push_back(x);
    get_shape_from_start_of_parameter_pack_helper(result, std::forward<Args>(rest)...);
}


template<typename ...Args>
std::vector<int> get_shape_from_start_of_parameter_pack(Args&&... args) {
    std::vector<int> result;
    get_shape_from_start_of_parameter_pack_helper(result, std::forward<Args>(args)...);
    return result;
}

template<typename T, typename T2>
using add_const_if_T_is_const = typename std::conditional<std::is_const<T>::value, const T2, T2>::type;

}

/** A Halide::Buffer is a named shared reference to a
 * Halide::Runtime::Buffer.
 *
 * A Buffer<T1> can refer to a Buffer<T2> if T1 is const whenever T2
 * is const, and either T1 = T2 or T1 is void. A Buffer<void> can
 * refer to any Buffer of any non-const type, and the default
 * template parameter is T = void.
 */
template<typename T>
class Buffer {
    Internal::IntrusivePtr<Internal::BufferContents> contents;

    template<typename T2> friend class Buffer;

    template<typename T2>
    static void assert_can_convert_from(const Buffer<T2> &other) {
        Runtime::Buffer<T>::assert_can_convert_from(*(other.get()));
    }

public:

    typedef T ElemType;

    /** Make a null Buffer, which points to no Runtime::Buffer */
    Buffer() {}

    /** Make a Buffer from a Buffer of a different type */
    template<typename T2>
    Buffer(const Buffer<T2> &other) :
        contents(other.contents) {
        assert_can_convert_from(other);
    }

    /** Move construct from a Buffer of a different type */
    template<typename T2>
    Buffer(Buffer<T2> &&other) {
        assert_can_convert_from(other);
        contents = std::move(other.contents);
    }

    /** Construct a Buffer that captures and owns an rvalue Runtime::Buffer */
    template<int D>
    Buffer(Runtime::Buffer<T, D> &&buf, const std::string &name = "") :
        contents(new Internal::BufferContents) {
        contents->buf = std::move(buf);
        if (name.empty()) {
            contents->name = Internal::make_entity_name(this, "Halide::Buffer<?", 'b');
        } else {
            contents->name = name;
        }
    }

    /** Constructors that match Runtime::Buffer with two differences:
     * 1) They take a Type instead of a halide_type_t
     * 2) There is an optional last string argument that gives the buffer a specific name
     */
    // @{
    template<typename ...Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(Type t,
                    int first, Args... rest) :
        Buffer(Runtime::Buffer<T>(t, Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
               Internal::get_name_from_end_of_parameter_pack(rest...)) {}

    explicit Buffer(const buffer_t &buf,
                    const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(buf), name) {}

    template<typename ...Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(int first, Args... rest) :
        Buffer(Runtime::Buffer<T>(Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
               Internal::get_name_from_end_of_parameter_pack(rest...)) {}

    Buffer(Type t,
           const std::vector<int> &sizes,
           const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(t, sizes), name) {}

    template<typename Array, size_t N>
    explicit Buffer(Array (&vals)[N],
                    const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(vals), name) {}

    template<typename ...Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    int first, Args&&... rest) :
        Buffer(Runtime::Buffer<T>(t, data, Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
               Internal::get_name_from_end_of_parameter_pack(rest...)) {}

    template<typename ...Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(T *data,
                    int first, Args&&... rest) :
        Buffer(Runtime::Buffer<T>(data, Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
               Internal::get_name_from_end_of_parameter_pack(rest...)) {}

    explicit Buffer(T *data,
                    const std::vector<int> &sizes,
                    const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(data, sizes), name) {}

    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    const std::vector<int> &sizes,
                    const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(t, data, sizes), name) {}

    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    int d,
                    const halide_dimension_t *shape,
                    const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(t, data, d, shape), name) {}

    explicit Buffer(T *data,
                    int d,
                    const halide_dimension_t *shape,
                    const std::string &name = "") :
        Buffer(Runtime::Buffer<T>(data, d, shape), name) {}


    static Buffer<T> make_scalar(const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_scalar(), name);
    }

    static Buffer<> make_scalar(Type t, const std::string &name = "") {
        return Buffer<>(Runtime::Buffer<>::make_scalar(t), name);
    }

    static Buffer<T> make_interleaved(int width, int height, int channels, const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_interleaved(width, height, channels),
                        name);
    }

    static Buffer<> make_interleaved(Type t, int width, int height, int channels, const std::string &name = "") {
        return Buffer<>(Runtime::Buffer<>::make_interleaved(t, width, height, channels),
                        name);
    }

    static Buffer<T> make_interleaved(T *data, int width, int height, int channels, const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_interleaved(data, width, height, channels),
                         name);
    }

    static Buffer<Internal::add_const_if_T_is_const<T, void>>
    make_interleaved(Type t, T *data, int width, int height, int channels, const std::string &name = "") {
        using T2 = Internal::add_const_if_T_is_const<T, void>;
        return Buffer<T2>(Runtime::Buffer<T2>::make_interleaved(t, data, width, height, channels),
                          name);
    }

    template<typename T2>
    static Buffer<T> make_with_shape_of(Buffer<T2> src,
                                        void *(*allocate_fn)(size_t) = nullptr,
                                        void (*deallocate_fn)(void *) = nullptr,
                                        const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_with_shape_of(*src.get(), allocate_fn, deallocate_fn),
                         name);
    }

    template<typename T2>
    static Buffer<T> make_with_shape_of(const Runtime::Buffer<T2> &src,
                                        void *(*allocate_fn)(size_t) = nullptr,
                                        void (*deallocate_fn)(void *) = nullptr,
                                        const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_with_shape_of(src, allocate_fn, deallocate_fn),
                         name);
    }
    // @}

    /** Buffers are optionally named. */
    // @{
    void set_name(const std::string &n) {
        contents->name = n;
    }

    const std::string &name() const {
        return contents->name;
    }
    // @}

    /** Check if two Buffer objects point to the same underlying Buffer */
    template<typename T2>
    bool same_as(const Buffer<T2> &other) {
        return (const void *)(contents.get()) == (const void *)(other.contents.get());
    }

    /** Check if this Buffer refers to an existing
     * Buffer. Default-constructed Buffer objects do not refer to any
     * existing Buffer. */
    bool defined() const {
        return contents.defined();
    }

    /** Get a pointer to the underlying Runtime::Buffer */
    // @{
    Runtime::Buffer<T> *get() {
        return &contents->buf.as<T>();
    }
    const Runtime::Buffer<T> *get() const {
        return &contents->buf.as<T>();
    }
    // @}

    // We forward numerous methods from the underlying Buffer
#define HALIDE_BUFFER_FORWARD_CONST(method)                             \
    template<typename ...Args>                                          \
    auto method(Args&&... args) const ->                                \
        decltype(((const Runtime::Buffer<T> &)Runtime::Buffer<T>()).method(std::forward<Args>(args)...)) { \
        return get()->method(std::forward<Args>(args)...);              \
    }

#define HALIDE_BUFFER_FORWARD(method)                                   \
    template<typename ...Args>                                          \
    auto method(Args&&... args) ->                                      \
        decltype(Runtime::Buffer<T>().method(std::forward<Args>(args)...)) { \
        return get()->method(std::forward<Args>(args)...);              \
    }

    /** Does the same thing as the equivalent Halide::Runtime::Buffer method */
    // @{
    HALIDE_BUFFER_FORWARD(raw_buffer)
    HALIDE_BUFFER_FORWARD_CONST(raw_buffer)
    HALIDE_BUFFER_FORWARD_CONST(dimensions)
    HALIDE_BUFFER_FORWARD_CONST(dim)
    HALIDE_BUFFER_FORWARD_CONST(width)
    HALIDE_BUFFER_FORWARD_CONST(height)
    HALIDE_BUFFER_FORWARD_CONST(channels)
    HALIDE_BUFFER_FORWARD_CONST(min)
    HALIDE_BUFFER_FORWARD_CONST(extent)
    HALIDE_BUFFER_FORWARD_CONST(stride)
    HALIDE_BUFFER_FORWARD_CONST(number_of_elements)
    HALIDE_BUFFER_FORWARD_CONST(size_in_bytes)
    HALIDE_BUFFER_FORWARD_CONST(begin)
    HALIDE_BUFFER_FORWARD_CONST(end)
    HALIDE_BUFFER_FORWARD(data)
    HALIDE_BUFFER_FORWARD_CONST(data)
    HALIDE_BUFFER_FORWARD_CONST(contains)
    HALIDE_BUFFER_FORWARD(crop)
    HALIDE_BUFFER_FORWARD(slice)
    HALIDE_BUFFER_FORWARD(embed)
    HALIDE_BUFFER_FORWARD(set_min)
    HALIDE_BUFFER_FORWARD(translate)
    HALIDE_BUFFER_FORWARD(transpose)
    HALIDE_BUFFER_FORWARD(add_dimension)
    HALIDE_BUFFER_FORWARD(copy_to_host)
    HALIDE_BUFFER_FORWARD(copy_to_device)
    HALIDE_BUFFER_FORWARD_CONST(has_device_allocation)
    HALIDE_BUFFER_FORWARD_CONST(host_dirty)
    HALIDE_BUFFER_FORWARD_CONST(device_dirty)
    HALIDE_BUFFER_FORWARD(set_host_dirty)
    HALIDE_BUFFER_FORWARD(set_device_dirty)
    HALIDE_BUFFER_FORWARD(device_sync)
    HALIDE_BUFFER_FORWARD(device_malloc)
    HALIDE_BUFFER_FORWARD(allocate)
    HALIDE_BUFFER_FORWARD(deallocate)
    HALIDE_BUFFER_FORWARD(device_deallocate)
    HALIDE_BUFFER_FORWARD(device_free)
    HALIDE_BUFFER_FORWARD(fill)
    HALIDE_BUFFER_FORWARD_CONST(for_each_element)
    HALIDE_BUFFER_FORWARD(for_each_value)

#undef HALIDE_BUFFER_FORWARD
#undef HALIDE_BUFFER_FORWARD_CONST

    Type type() const {
        return contents->buf.type();
    }

    Buffer<T> copy() const {
        return Buffer<T>(std::move(contents->buf.copy()));
    }

    template<typename T2>
    void copy_from(const Buffer<T2> &other) {
        contents->buf.copy_from(*other.get());
    }

    template<typename ...Args>
    auto operator()(int first, Args&&... args) ->
        decltype((*get())(first, std::forward<Args>(args)...)) {
        return (*get())(first, std::forward<Args>(args)...);
    }

    template<typename ...Args>
    auto operator()(int first, Args&&... args) const ->
        decltype((*get())(first, std::forward<Args>(args)...)) {
        return (*get())(first, std::forward<Args>(args)...);
    }

    auto operator()(const int *pos) ->
        decltype((*get())(pos)) {
        return (*get())(pos);
    }

    auto operator()(const int *pos) const ->
        decltype((*get())(pos)) {
        return (*get())(pos);
    }

    auto operator()() ->
        decltype((*get())()) {
        return (*get())();
    }

    auto operator()() const ->
        decltype((*get())()) {
        return (*get())();
    }
    // @}

    /** Make an Expr that loads from this concrete buffer at a computed coordinate. */
    // @{
    template<typename ...Args>
    Expr operator()(Expr first, Args... rest) {
        std::vector<Expr> args = {first, rest...};
        return (*this)(args);
    };

    template<typename ...Args>
    Expr operator()(const std::vector<Expr> &args) {
        return buffer_accessor(Buffer<>(*this), args);
    };
    // @}


    /** Copy to the GPU, using the device API that is the default for the given Target. */
    int copy_to_device(const Target &t = get_jit_target_from_environment()) {
        return contents->buf.copy_to_device(get_default_device_interface_for_target(t));
    }

    /** Copy to the GPU, using the given device API */
    int copy_to_device(const DeviceAPI &d, const Target &t = get_jit_target_from_environment()) {
        return contents->buf.copy_to_device(get_device_interface_for_device_api(d, t));
    }

    /** Allocate on the GPU, using the device API that is the default for the given Target. */
    int device_malloc(const Target &t = get_jit_target_from_environment()) {
        return contents->buf.device_malloc(get_default_device_interface_for_target(t));
    }

    /** Allocate storage on the GPU, using the given device API */
    int device_malloc(const DeviceAPI &d, const Target &t = get_jit_target_from_environment()) {
        return contents->buf.device_malloc(get_device_interface_for_device_api(d, t));
    }


};

}

#endif
