import halide as hl
import numpy as np


def test_extern():
    """
    Shows an example of Halide calling a C library loaded
    in the Python process via ctypes
    """

    # Requires Makefile support to build the external function in linkable form
    print("TODO: test_extern not yet implemented in Python; skipping...")
    return 0

    x = hl.Var("x")

    data = np.random.random(10).astype(np.float64)
    expected_result = np.sort(data)
    output_data = np.empty(10, dtype=np.float64)

    sort_func = hl.Func("extern_sort_func")
    # gsl_sort,
    # see http://www.gnu.org/software/gsl/manual/html_node/Sorting-vectors.html#Sorting-vectors

    input = hl.ImageParam(hl.Float(64), 1, "input_data")

    extern_name = "the_sort_func"
    params = [hl.ExternFuncArgument(input)]
    output_types = [hl.Int(32)]
    dimensionality = 1
    sort_func.define_extern(extern_name, params, output_types, dimensionality)

    try:
        sort_func.compile_jit()
    except hl.HalideError:
        assert "cannot be converted to a bool" in str(e)
    else:
        assert False, "Did not see expected exception!"

    import ctypes

    sort_lib = ctypes.CDLL("the_sort_function.so")
    print(sort_lib.the_sort_func)

    try:
        sort_func.compile_jit()
    except hl.HalideError:
        assert "cannot be converted to a bool" in str(e)
    else:
        assert False, "Did not see expected exception!"

    lib_path = "the_sort_function.so"
    load_error = load_library_into_llvm(lib_path)
    assert load_error == False

    sort_func.compile_jit()

    # now that things are loaded, we try to call them
    input.set(data)
    sort_func.realize(output_data)

    assert np.isclose(expected_result, output_data)

    return


if __name__ == "__main__":

    test_extern()
