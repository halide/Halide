#ifndef HALIDE_BUFFER_H
#define HALIDE_BUFFER_H

#include "DeviceInterface.h"
#include "Expr.h"
#include "IntrusivePtr.h"
#include "Util.h"
#include "runtime/HalideBuffer.h"

namespace Halide {

template<typename T = void>
class Buffer;

namespace Internal {

struct BufferContents {
    mutable RefCount ref_count;
    std::string name;
    Runtime::Buffer<> buf;
};

Expr buffer_accessor(const Buffer<> &buf, const std::vector<Expr> &args);

template<typename... Args>
struct all_ints_and_optional_name : std::false_type {};

template<typename First, typename... Rest>
struct all_ints_and_optional_name<First, Rest...> : meta_and<std::is_convertible<First, int>,
                                                             all_ints_and_optional_name<Rest...>> {};

template<typename T>
struct all_ints_and_optional_name<T> : meta_or<std::is_convertible<T, std::string>,
                                               std::is_convertible<T, int>> {};

template<>
struct all_ints_and_optional_name<> : std::true_type {};

template<typename T,
         typename = typename std::enable_if<!std::is_convertible<T, std::string>::value>::type>
std::string get_name_from_end_of_parameter_pack(T &&) {
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
         typename... Args>
std::string get_name_from_end_of_parameter_pack(First first, Second second, Args &&... rest) {
    return get_name_from_end_of_parameter_pack(second, std::forward<Args>(rest)...);
}

inline void get_shape_from_start_of_parameter_pack_helper(std::vector<int> &, const std::string &) {
}

inline void get_shape_from_start_of_parameter_pack_helper(std::vector<int> &) {
}

template<typename... Args>
void get_shape_from_start_of_parameter_pack_helper(std::vector<int> &result, int x, Args &&... rest) {
    result.push_back(x);
    get_shape_from_start_of_parameter_pack_helper(result, std::forward<Args>(rest)...);
}

template<typename... Args>
std::vector<int> get_shape_from_start_of_parameter_pack(Args &&... args) {
    std::vector<int> result;
    get_shape_from_start_of_parameter_pack_helper(result, std::forward<Args>(args)...);
    return result;
}

template<typename T, typename T2>
using add_const_if_T_is_const = typename std::conditional<std::is_const<T>::value, const T2, T2>::type;

}  // namespace Internal

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

    template<typename T2>
    friend class Buffer;

    template<typename T2>
    static void assert_can_convert_from(const Buffer<T2> &other) {
        if (!other.defined()) {
            // Avoid UB of deferencing offset of a null contents ptr
            static_assert((!std::is_const<T2>::value || std::is_const<T>::value),
                          "Can't convert from a Buffer<const T> to a Buffer<T>");
            static_assert(std::is_same<typename std::remove_const<T>::type,
                                       typename std::remove_const<T2>::type>::value ||
                              std::is_void<T>::value ||
                              std::is_void<T2>::value,
                          "type mismatch constructing Buffer");
        } else {
            Runtime::Buffer<T>::assert_can_convert_from(*(other.get()));
        }
    }

public:
    typedef T ElemType;

    // This class isn't final (and is subclassed from the Python binding
    // code, at least) so it needs a virtual dtor.
    virtual ~Buffer() = default;

    /** Make a null Buffer, which points to no Runtime::Buffer */
    Buffer() = default;

    /** Trivial copy constructor. */
    Buffer(const Buffer &that) = default;

    /** Trivial copy assignment operator. */
    Buffer &operator=(const Buffer &that) = default;

    /** Make a Buffer from a Buffer of a different type */
    template<typename T2>
    Buffer(const Buffer<T2> &other)
        : contents(other.contents) {
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
    Buffer(Runtime::Buffer<T, D> &&buf, const std::string &name = "")
        : contents(new Internal::BufferContents) {
        contents->buf = std::move(buf);
        if (name.empty()) {
            contents->name = Internal::make_entity_name(this, "Halide:.*:Buffer<.*>", 'b');
        } else {
            contents->name = name;
        }
    }

    /** Constructors that match Runtime::Buffer with two differences:
     * 1) They take a Type instead of a halide_type_t
     * 2) There is an optional last string argument that gives the buffer a specific name
     */
    // @{
    template<typename... Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(Type t,
                    int first, Args... rest)
        : Buffer(Runtime::Buffer<T>(t, Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
                 Internal::get_name_from_end_of_parameter_pack(rest...)) {
    }

    explicit Buffer(const halide_buffer_t &buf,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(buf), name) {
    }

    explicit Buffer(const buffer_t &buf,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(buf), name) {
    }

    template<typename... Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(int first, Args... rest)
        : Buffer(Runtime::Buffer<T>(Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
                 Internal::get_name_from_end_of_parameter_pack(rest...)) {
    }

    explicit Buffer(Type t,
                    const std::vector<int> &sizes,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(t, sizes), name) {
    }

    explicit Buffer(Type t,
                    const std::vector<int> &sizes,
                    const std::vector<int> &storage_order,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(t, sizes, storage_order), name) {
    }

    explicit Buffer(const std::vector<int> &sizes,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(sizes), name) {
    }

    explicit Buffer(const std::vector<int> &sizes,
                    const std::vector<int> &storage_order,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(sizes, storage_order), name) {
    }

    template<typename Array, size_t N>
    explicit Buffer(Array (&vals)[N],
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(vals), name) {
    }

    template<typename... Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    int first, Args &&... rest)
        : Buffer(Runtime::Buffer<T>(t, data, Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
                 Internal::get_name_from_end_of_parameter_pack(rest...)) {
    }

    template<typename... Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    const std::vector<int> &sizes,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(t, data, sizes, name)) {
    }

    template<typename... Args,
             typename = typename std::enable_if<Internal::all_ints_and_optional_name<Args...>::value>::type>
    explicit Buffer(T *data,
                    int first, Args &&... rest)
        : Buffer(Runtime::Buffer<T>(data, Internal::get_shape_from_start_of_parameter_pack(first, rest...)),
                 Internal::get_name_from_end_of_parameter_pack(rest...)) {
    }

    explicit Buffer(T *data,
                    const std::vector<int> &sizes,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(data, sizes), name) {
    }

    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    const std::vector<int> &sizes,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(t, data, sizes), name) {
    }

    explicit Buffer(Type t,
                    Internal::add_const_if_T_is_const<T, void> *data,
                    int d,
                    const halide_dimension_t *shape,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(t, data, d, shape), name) {
    }

    explicit Buffer(T *data,
                    int d,
                    const halide_dimension_t *shape,
                    const std::string &name = "")
        : Buffer(Runtime::Buffer<T>(data, d, shape), name) {
    }

    static Buffer<T> make_scalar(const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_scalar(), name);
    }

    static Buffer<> make_scalar(Type t, const std::string &name = "") {
        return Buffer<>(Runtime::Buffer<>::make_scalar(t), name);
    }

    static Buffer<T> make_scalar(T *data, const std::string &name = "") {
        return Buffer<T>(Runtime::Buffer<T>::make_scalar(data), name);
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
        // It's already type-checked, so no need to use as<T>.
        return (Runtime::Buffer<T> *)(&contents->buf);
    }
    const Runtime::Buffer<T> *get() const {
        return (const Runtime::Buffer<T> *)(&contents->buf);
    }
    // @}

public:
    // We forward numerous methods from the underlying Buffer
#define HALIDE_BUFFER_FORWARD_CONST(method)                                                                                      \
    template<typename... Args>                                                                                                   \
    auto method(Args &&... args) const->decltype(std::declval<const Runtime::Buffer<T>>().method(std::forward<Args>(args)...)) { \
        user_assert(defined()) << "Undefined buffer calling const method " #method "\n";                                         \
        return get()->method(std::forward<Args>(args)...);                                                                       \
    }

#define HALIDE_BUFFER_FORWARD(method)                                                                                \
    template<typename... Args>                                                                                       \
    auto method(Args &&... args)->decltype(std::declval<Runtime::Buffer<T>>().method(std::forward<Args>(args)...)) { \
        user_assert(defined()) << "Undefined buffer calling method " #method "\n";                                   \
        return get()->method(std::forward<Args>(args)...);                                                           \
    }

// This is a weird-looking but effective workaround for a deficiency in "perfect forwarding":
// namely, it can't really handle initializer-lists. The idea here is that we declare
// the expected type to be passed on, and that allows the compiler to handle it.
// The weirdness comes in with the variadic macro: the problem is that the type
// we want to forward might be something like `std::vector<std::pair<int, int>>`,
// which contains a comma, which throws a big wrench in C++ macro system.
// However... since all we really need to do is capture the remainder of the macro,
// and forward it as is, we can just use ... to allow an arbitrary number of commas,
// then use __VA_ARGS__ to forward the mess as-is, and while it looks horrible, it
// works.
#define HALIDE_BUFFER_FORWARD_INITIALIZER_LIST(method, ...)                                            \
    inline auto method(const __VA_ARGS__ &a)->decltype(std::declval<Runtime::Buffer<T>>().method(a)) { \
        user_assert(defined()) << "Undefined buffer calling method " #method "\n";                     \
        return get()->method(a);                                                                       \
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
    HALIDE_BUFFER_FORWARD_CONST(left)
    HALIDE_BUFFER_FORWARD_CONST(right)
    HALIDE_BUFFER_FORWARD_CONST(top)
    HALIDE_BUFFER_FORWARD_CONST(bottom)
    HALIDE_BUFFER_FORWARD_CONST(number_of_elements)
    HALIDE_BUFFER_FORWARD_CONST(size_in_bytes)
    HALIDE_BUFFER_FORWARD_CONST(begin)
    HALIDE_BUFFER_FORWARD_CONST(end)
    HALIDE_BUFFER_FORWARD(data)
    HALIDE_BUFFER_FORWARD_CONST(data)
    HALIDE_BUFFER_FORWARD_CONST(contains)
    HALIDE_BUFFER_FORWARD(crop)
    HALIDE_BUFFER_FORWARD_INITIALIZER_LIST(crop, std::vector<std::pair<int, int>>)
    HALIDE_BUFFER_FORWARD(slice)
    HALIDE_BUFFER_FORWARD_CONST(sliced)
    HALIDE_BUFFER_FORWARD(embed)
    HALIDE_BUFFER_FORWARD_CONST(embedded)
    HALIDE_BUFFER_FORWARD(set_min)
    HALIDE_BUFFER_FORWARD(translate)
    HALIDE_BUFFER_FORWARD_INITIALIZER_LIST(translate, std::vector<int>)
    HALIDE_BUFFER_FORWARD(transpose)
    HALIDE_BUFFER_FORWARD(transposed)
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
    HALIDE_BUFFER_FORWARD(device_wrap_native)
    HALIDE_BUFFER_FORWARD(device_detach_native)
    HALIDE_BUFFER_FORWARD(allocate)
    HALIDE_BUFFER_FORWARD(deallocate)
    HALIDE_BUFFER_FORWARD(device_deallocate)
    HALIDE_BUFFER_FORWARD(device_free)
    HALIDE_BUFFER_FORWARD_CONST(all_equal)

#undef HALIDE_BUFFER_FORWARD
#undef HALIDE_BUFFER_FORWARD_CONST

    template<typename Fn, typename... Args>
    Buffer<T> &for_each_value(Fn &&f, Args... other_buffers) {
        get()->for_each_value(std::forward<Fn>(f), (*std::forward<Args>(other_buffers).get())...);
        return *this;
    }

    template<typename Fn, typename... Args>
    const Buffer<T> &for_each_value(Fn &&f, Args... other_buffers) const {
        get()->for_each_value(std::forward<Fn>(f), (*std::forward<Args>(other_buffers).get())...);
        return *this;
    }

    template<typename Fn>
    Buffer<T> &for_each_element(Fn &&f) {
        get()->for_each_element(std::forward<Fn>(f));
        return *this;
    }

    template<typename Fn>
    const Buffer<T> &for_each_element(Fn &&f) const {
        get()->for_each_element(std::forward<Fn>(f));
        return *this;
    }

    template<typename FnOrValue>
    Buffer<T> &fill(FnOrValue &&f) {
        get()->fill(std::forward<FnOrValue>(f));
        return *this;
    }

    static constexpr bool has_static_halide_type = Runtime::Buffer<T>::has_static_halide_type;

    static halide_type_t static_halide_type() {
        return Runtime::Buffer<T>::static_halide_type();
    }

    template<typename T2>
    static bool can_convert_from(const Buffer<T2> &other) {
        return Halide::Runtime::Buffer<T>::can_convert_from(*other.get());
    }

    // Note that since Runtime::Buffer stores halide_type_t rather than Halide::Type,
    // there is no handle-specific type information, so all handle types are
    // considered equivalent to void* here. (This only matters if you are making
    // a Buffer-of-handles, which is not really a real use case...)
    Type type() const {
        return contents->buf.type();
    }

    template<typename T2>
    Buffer<T2> as() const {
        return Buffer<T2>(*this);
    }

    Buffer<T> copy() const {
        return Buffer<T>(std::move(contents->buf.as<T>().copy()));
    }

    template<typename T2>
    void copy_from(const Buffer<T2> &other) {
        contents->buf.copy_from(*other.get());
    }

    template<typename... Args>
    auto operator()(int first, Args &&... args) -> decltype(std::declval<Runtime::Buffer<T>>()(first, std::forward<Args>(args)...)) {
        return (*get())(first, std::forward<Args>(args)...);
    }

    template<typename... Args>
    auto operator()(int first, Args &&... args) const -> decltype(std::declval<const Runtime::Buffer<T>>()(first, std::forward<Args>(args)...)) {
        return (*get())(first, std::forward<Args>(args)...);
    }

    auto operator()(const int *pos) -> decltype(std::declval<Runtime::Buffer<T>>()(pos)) {
        return (*get())(pos);
    }

    auto operator()(const int *pos) const -> decltype(std::declval<const Runtime::Buffer<T>>()(pos)) {
        return (*get())(pos);
    }

    auto operator()() -> decltype(std::declval<Runtime::Buffer<T>>()()) {
        return (*get())();
    }

    auto operator()() const -> decltype(std::declval<const Runtime::Buffer<T>>()()) {
        return (*get())();
    }
    // @}

    /** Make an Expr that loads from this concrete buffer at a computed coordinate. */
    // @{
    template<typename... Args>
    Expr operator()(Expr first, Args... rest) const {
        std::vector<Expr> args = {first, rest...};
        return (*this)(args);
    };

    template<typename... Args>
    Expr operator()(const std::vector<Expr> &args) const {
        return buffer_accessor(Buffer<>(*this), args);
    };
    // @}

    /** Copy to the GPU, using the device API that is the default for the given Target. */
    int copy_to_device(const Target &t = get_jit_target_from_environment()) {
        return copy_to_device(DeviceAPI::Default_GPU, t);
    }

    /** Copy to the GPU, using the given device API */
    int copy_to_device(const DeviceAPI &d, const Target &t = get_jit_target_from_environment()) {
        return contents->buf.copy_to_device(get_device_interface_for_device_api(d, t, "Buffer::copy_to_device"));
    }

    /** Allocate on the GPU, using the device API that is the default for the given Target. */
    int device_malloc(const Target &t = get_jit_target_from_environment()) {
        return device_malloc(DeviceAPI::Default_GPU, t);
    }

    /** Allocate storage on the GPU, using the given device API */
    int device_malloc(const DeviceAPI &d, const Target &t = get_jit_target_from_environment()) {
        return contents->buf.device_malloc(get_device_interface_for_device_api(d, t, "Buffer::device_malloc"));
    }

    /** Wrap a native handle, using the given device API.
     * It is a bad idea to pass DeviceAPI::Default_GPU to this routine
     * as the handle argument must match the API that the default
     * resolves to and it is clearer and more reliable to pass the
     * resolved DeviceAPI explicitly. */
    int device_wrap_native(const DeviceAPI &d, uint64_t handle, const Target &t = get_jit_target_from_environment()) {
        return contents->buf.device_wrap_native(get_device_interface_for_device_api(d, t, "Buffer::device_wrap_native"), handle);
    }
};

}  // namespace Halide

#endif
