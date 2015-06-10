#ifndef NO_COMPARE_INDEXING_SUITE_H
#define NO_COMPARE_INDEXING_SUITE_H

#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <boost/python/suite/indexing/detail/indexing_suite_detail.hpp>

// based on http://boost.2283326.n4.nabble.com/A-vector-of-objects-with-private-operators-td2698705.html
template <class T>
class no_compare_indexing_suite :
        public boost::python::vector_indexing_suite<T, false,
        no_compare_indexing_suite<T> >
{
public:
    static bool contains(T &container, typename T::value_type const &key)
    {
        PyErr_SetString(PyExc_NotImplementedError,
                        "containment checking not supported on this container");
        throw boost::python::error_already_set();
    }
};

#endif // NO_COMPARE_INDEXING_SUITE_H
