#!/usr/bin/python3

# to be called via nose, for example
# nosetests-3.4 -v path_to/tests/test_extern.py


from halide import *
import numpy as np


def test_extern():
    """
    Shows an example of Halide calling a C library loaded
    in the Python process via ctypes
    """


    x = Var("x")

    data = np.random.random(10).astype(np.float64)
    expected_result = np.sort(data)
    output_data = np.empty(10, dtype=np.float64)

    sort_func = Func("extern_sort_func")
    # gsl_sort,
    # see http://www.gnu.org/software/gsl/manual/html_node/Sorting-vectors.html#Sorting-vectors

    input = ImageParam(Float(64), 1, "input_data")

    params = ExternFuncArgumentsVector()
    p1 = ExternFuncArgument(input) # data
    params.append(p1)

    output_types = TypesVector()
    output_types.append(Int(32))

    dimensionality = 1

    extern_name = "the_sort_func"
    sort_func.define_extern(extern_name, params, output_types, dimensionality)

    try:
        sort_func.compile_jit()
    except RuntimeError:
        pass
    else:
        raise Exception("compile_jit should have raised a 'Symbol not found' RuntimeError")


    import ctypes
    sort_lib = ctypes.CDLL("the_sort_function.so")
    print(sort_lib.the_sort_func)

    try:
        sort_func.compile_jit()
    except RuntimeError:
        print("ctypes CDLL did not work out")
    else:
        print("ctypes CDLL worked !")

    lib_path = "the_sort_function.so"
    #lib_path = "/home/rodrigob/code/references/" \
    #           "Halide_master/python_bindings/tests/the_sort_function.nohere.so"
    load_error = load_library_into_llvm(lib_path)
    assert load_error == False


    sort_func.compile_jit()

    # now that things are loaded, we try to call them
    input.set(data)
    sort_func.realize(output_data)

    assert numpy.isclose(expected_result, output_data)

    return

if __name__ == "__main__":

    test_extern()