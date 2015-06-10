#ifndef MAKE_ARRAY_H
#define MAKE_ARRAY_H

#include <string>
#include <typeinfo>
#include <boost/python.hpp>
#include <boost/python/suite/indexing/indexing_suite.hpp>


/** Helper method for boost::python
 * from http://stackoverflow.com/questions/18882089/wrapping-arrays-in-boost-python

struct Foo
{
  int vals[3];
  boost::array<std::string, 5> strs;

  Foo()  { std::cout << "Foo()"  << std::endl; }
  ~Foo() { std::cout << "~Foo()" << std::endl; }
};

int more_vals[2];

BOOST_PYTHON_MODULE(example)
{
  namespace python = boost::python;

  python::class_<Foo>("Foo")
    .add_property("vals", make_array(&Foo::vals))
    .add_property("strs", make_array(&Foo::strs))
    ;
  python::def("more_vals", make_array(&more_vals));
}
*/

namespace detail {

template <typename> struct array_trait;

/// @brief Type that proxies to an array.
template <typename T>
class array_proxy
{
public:
  // Types
  typedef T           value_type;
  typedef T*          iterator;
  typedef T&          reference;
  typedef std::size_t size_type;

  /// @brief Empty constructor.
  array_proxy()
    : ptr_(0),
      length_(0)
  {}

  /// @brief Construct with iterators.
  template <typename Iterator>
  array_proxy(Iterator begin, Iterator end)
    : ptr_(&*begin),
      length_(std::distance(begin, end))
  {}

  /// @brief Construct with with start and size.
  array_proxy(reference begin, std::size_t length)
    : ptr_(&begin),
      length_(length)
  {}

  // Iterator support.
  iterator begin()               { return ptr_; }
  iterator end()                 { return ptr_ + length_; }

  // Element access.
  reference operator[](size_t i) { return ptr_[i]; }

  // Capacity.
  size_type size()               { return length_; }

private:
  T* ptr_;
  std::size_t length_;
};

/// @brief Make an array_proxy.
template <typename T>
array_proxy<typename array_trait<T>::element_type>
make_array_proxy(T& array)
{
  return array_proxy<typename array_trait<T>::element_type>(
    array[0],
    array_trait<T>::static_size);
}

/// @brief Policy type for referenced indexing, meeting the DerivedPolicies
///        requirement of boost::python::index_suite.
///
/// @note Requires Container to support:
///          - value_type and size_type types,
///          - value_type is default constructable and copyable,
///          - element access via operator[],
///          - Default constructable, iterator constructable,
///          - begin(), end(), and size() member functions
template <typename Container>
class ref_index_suite
  : public boost::python::indexing_suite<Container,
      ref_index_suite<Container> >
{
public:

  typedef typename Container::value_type data_type;
  typedef typename Container::size_type  index_type;
  typedef typename Container::size_type  size_type;

  // Element access and manipulation.

  /// @brief Get element from container.
  static data_type&
  get_item(Container& container, index_type index)
  {
    return container[index];
  }

  /// @brief Set element from container.
  static void
  set_item(Container& container, index_type index, const data_type& value)
  {
    container[index] = value;
  }

  /// @brief Reset index to default value.
  static void
  delete_item(Container& container, index_type index)
  {
    set_item(container, index, data_type());
  };

  // Slice support.

  /// @brief Get slice from container.
  ///
  /// @return Python object containing
  static boost::python::object
  get_slice(Container& container, index_type from, index_type to)
  {
    using boost::python::list;
    if (from > to) return list();

    // Return copy, as container only references its elements.
    list list;
    while (from != to) list.append(container[from++]);
    return list;
  };

  /// @brief Set a slice in container with a given value.
  static void
  set_slice(
    Container& container, index_type from,
    index_type to, const data_type& value
  )
  {
    // If range is invalid, return early.
    if (from > to) return;

    // Populate range with value.
    while (from < to) container[from++] = value;
  }

  /// @brief Set a slice in container with another range.
  template <class Iterator>
  static void
  set_slice(
    Container& container, index_type from,
    index_type to, Iterator first, Iterator last
  )
  {
    // If range is invalid, return early.
    if (from > to) return;

    // Populate range with other range.
    while (from < to) container[from++] = *first++;
  }

  /// @brief Reset slice to default values.
  static void
  delete_slice(Container& container, index_type from, index_type to)
  {
    set_slice(container, from, to, data_type());
  }

  // Capacity.

  /// @brief Get size of container.
  static std::size_t
  size(Container& container) { return container.size(); }

  /// @brief Check if a value is within the container.
  template <class T>
  static bool
  contains(Container& container, const T& value)
  {
    return std::find(container.begin(), container.end(), value)
        != container.end();
  }

  /// @brief Minimum index supported for container.
  static index_type
  get_min_index(Container& /*container*/)
  {
      return 0;
  }

  /// @brief Maximum index supported for container.
  static index_type
  get_max_index(Container& container)
  {
    return size(container);
  }

  // Misc.

  /// @brief Convert python index (could be negative) to a valid container
  ///        index with proper boundary checks.
  static index_type
  convert_index(Container& container, PyObject* object)
  {
    namespace python = boost::python;
    python::extract<long> py_index(object);

    // If py_index cannot extract a long, then type the type is wrong so
    // set error and return early.
    if (!py_index.check())
    {
      PyErr_SetString(PyExc_TypeError, "Invalid index type");
      python::throw_error_already_set();
      return index_type();
    }

    // Extract index.
    long index = py_index();

    // Adjust negative index.
    if (index < 0)
        index += container.size();

    // Boundary check.
    if (index >= long(container.size()) || index < 0)
    {
      PyErr_SetString(PyExc_IndexError, "Index out of range");
      python::throw_error_already_set();
    }

    return index;
  }
};

/// @brief Trait for arrays.
template <typename T>
struct array_trait_impl;

// Specialize for native array.
template <typename T, std::size_t N>
struct array_trait_impl<T[N]>
{
  typedef T element_type;
  enum { static_size = N };
  typedef array_proxy<element_type> proxy_type;
  typedef boost::python::default_call_policies policy;
  typedef boost::mpl::vector<array_proxy<element_type> > signature;
};

// Specialize boost::array to use the native array trait.
template <typename T, std::size_t N>
struct array_trait_impl<boost::array<T, N> >
  : public array_trait_impl<T[N]>
{};

// @brief Specialize for member objects to use and modify non member traits.
template <typename T, typename C>
struct array_trait_impl<T (C::*)>
  : public array_trait_impl<T>
{
  typedef boost::python::with_custodian_and_ward_postcall<
      0, // return object (custodian)
      1  // self or this (ward)
    > policy;

  // Append the class to the signature.
  typedef typename boost::mpl::push_back<
    typename array_trait_impl<T>::signature, C&>::type signature;
};

/// @brief Trait class used to deduce array information, policies, and
///        signatures
template <typename T>
struct array_trait:
  public array_trait_impl<typename boost::remove_pointer<T>::type>
{
  typedef T native_type;
};

/// @brief Functor used used convert an array to an array_proxy for
///        non-member objects.
template <typename Trait>
struct array_proxy_getter
{
public:
  typedef typename Trait::native_type native_type;
  typedef typename Trait::proxy_type proxy_type;

  /// @brief Constructor.
  array_proxy_getter(native_type array): array_(array) {}

  /// @brief Return an array_proxy for a member array object.
  template <typename C>
  proxy_type operator()(C& c) { return make_array_proxy(c.*array_); }

  /// @brief Return an array_proxy for a non-member array object.
  proxy_type operator()() { return make_array_proxy(*array_); }
private:
  native_type array_;
};

/// @brief Conditionally register a type with Boost.Python.
template <typename Trait>
void register_array_proxy()
{
  typedef typename Trait::element_type element_type;
  typedef typename Trait::proxy_type proxy_type;

  // If type is already registered, then return early.
  namespace python = boost::python;
  bool is_registered = (0 != python::converter::registry::query(
    python::type_id<proxy_type>())->to_python_target_type());
  if (is_registered) return;

  // Otherwise, register the type as an internal type.
  std::string type_name = std::string("_") + typeid(element_type).name();
  python::class_<proxy_type>(type_name.c_str(), python::no_init)
    .def(ref_index_suite<proxy_type>());
}

/// @brief Create a callable Boost.Python object that will return an
///        array_proxy type when called.
///
/// @note This function will conditionally register array_proxy types
///       for conversion within Boost.Python.  The array_proxy will
///       extend the life of the object from which it was called.
///       For example, if `foo` is an object, and `vars` is an array,
///       then the object returned from `foo.vars` will extend the life
///       of `foo`.
template <typename Array>
boost::python::object make_array_aux(Array array)
{
  typedef array_trait<Array> trait_type;
  // Register an array proxy.
  register_array_proxy<trait_type>();

  // Create function.
  return boost::python::make_function(
      array_proxy_getter<trait_type>(array),
      typename trait_type::policy(),
      typename trait_type::signature());
}

} // namespace detail

/// @brief Create a callable Boost.Python object from an array.
template <typename T>
boost::python::object make_array(T array)
{
  return detail::make_array_aux(array);
}


#endif // MAKE_ARRAY_H
