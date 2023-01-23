import halide as hl
import numpy as np
import gc
import sys


def test_ndarray_to_buffer(reverse_axes=True):
    a0 = np.ones((200, 300), dtype=np.int32)

    # Buffer always shares data (when possible) by default,
    # and maintains the shape of the data source. (note that
    # the ndarray is col-major by default!)
    b0 = hl.Buffer(a0, "float32_test_buffer", reverse_axes)
    assert b0.type() == hl.Int(32)
    assert b0.name() == "float32_test_buffer"
    assert b0.all_equal(1)

    if reverse_axes:
        assert b0.dim(0).min() == 0
        assert b0.dim(0).max() == 299
        assert b0.dim(0).extent() == 300
        assert b0.dim(0).stride() == 1

        assert b0.dim(1).min() == 0
        assert b0.dim(1).max() == 199
        assert b0.dim(1).extent() == 200
        assert b0.dim(1).stride() == 300

        a0[12, 34] = 56
        assert b0[34, 12] == 56

        b0[56, 34] = 12
        assert a0[34, 56] == 12
    else:
        assert b0.dim(0).min() == 0
        assert b0.dim(0).max() == 199
        assert b0.dim(0).extent() == 200
        assert b0.dim(0).stride() == 300

        assert b0.dim(1).min() == 0
        assert b0.dim(1).max() == 299
        assert b0.dim(1).extent() == 300
        assert b0.dim(1).stride() == 1

        a0[12, 34] = 56
        assert b0[12, 34] == 56

        b0[56, 34] = 12
        assert a0[56, 34] == 12


def test_buffer_to_ndarray(reverse_axes=True):
    buf0 = hl.Buffer(hl.Int(16), [4, 6])
    assert buf0.type() == hl.Int(16)
    buf0.fill(0)
    buf0[1, 2] = 42
    assert buf0[1, 2] == 42

    # This is subtle: the default behavior when converting
    # a Buffer to an np.array (or ndarray, etc) is to reverse the
    # order of the axes, since Halide prefers column-major and
    # the rest of Python prefers row-major. By calling reverse_axes()
    # before that conversion, we end up doing a *double* reverse, i.e,
    # not reversing at all. So the 'not' here is correct.
    buf = buf0.reverse_axes() if not reverse_axes else buf0

    # Should share storage with buf
    array_shared = np.array(buf, copy=False)
    assert array_shared.dtype == np.int16
    if reverse_axes:
        assert array_shared.shape == (6, 4)
        assert array_shared[2, 1] == 42
    else:
        assert array_shared.shape == (4, 6)
        assert array_shared[1, 2] == 42

    # Should *not* share storage with buf
    array_copied = np.array(buf, copy=True)
    assert array_copied.dtype == np.int16
    if reverse_axes:
        assert array_copied.shape == (6, 4)
        assert array_copied[2, 1] == 42
    else:
        assert array_copied.shape == (4, 6)
        assert array_copied[1, 2] == 42

    # Should affect array_shared but not array_copied
    buf0[1, 2] = 3
    if reverse_axes:
        assert array_shared[2, 1] == 3
        assert array_copied[2, 1] == 42
    else:
        assert array_shared[1, 2] == 3
        assert array_copied[1, 2] == 42

    # Ensure that Buffers that have nonzero mins get converted correctly,
    # since the Python Buffer Protocol doesn't have the 'min' concept
    cropped_buf0 = buf0.copy()
    cropped_buf0.crop(dimension=0, min=1, extent=2)
    cropped_buf = cropped_buf0.reverse_axes() if not reverse_axes else cropped_buf0

    # Should share storage with cropped (and buf)
    cropped_array_shared = np.array(cropped_buf, copy=False)
    assert cropped_array_shared.dtype == np.int16
    if reverse_axes:
        assert cropped_array_shared.shape == (6, 2)
        assert cropped_array_shared[2, 0] == 3
    else:
        assert cropped_array_shared.shape == (2, 6)
        assert cropped_array_shared[0, 2] == 3

    # Should *not* share storage with anything
    cropped_array_copied = np.array(cropped_buf, copy=True)
    assert cropped_array_copied.dtype == np.int16
    if reverse_axes:
        assert cropped_array_copied.shape == (6, 2)
        assert cropped_array_copied[2, 0] == 3
    else:
        assert cropped_array_copied.shape == (2, 6)
        assert cropped_array_copied[0, 2] == 3

    cropped_buf0[1, 2] = 5
    assert cropped_buf0[1, 2] == 5
    if reverse_axes:
        assert cropped_buf[1, 2] == 5
        assert cropped_array_shared[2, 0] == 5
        assert cropped_array_copied[2, 0] == 3
    else:
        assert cropped_buf[2, 1] == 5
        assert cropped_array_shared[0, 2] == 5
        assert cropped_array_copied[0, 2] == 3


def _assert_fn(e):
    assert e


def _is_64bits():
    return sys.maxsize > 2**32


def test_for_each_element():
    buf = hl.Buffer(hl.Float(32), [3, 4])
    for x in range(3):
        for y in range(4):
            buf[x, y] = x + y
    # Can't use 'assert' in a lambda, but can call a fn that uses it.
    buf.for_each_element(
        lambda pos, buf=buf: _assert_fn(buf[pos[0], pos[1]] == pos[0] + pos[1])
    )


def test_fill_all_equal():
    buf = hl.Buffer(hl.Int(32), [3, 4])
    buf.fill(3)
    assert buf.all_equal(3)
    buf[1, 2] = 4
    assert not buf.all_equal(3)


def test_bufferinfo_sharing():
    # Don't bother testing this on 32-bit systems (our "huge" size is too large there)
    if not _is_64bits():
        print("skipping test_bufferinfo_sharing()")
        return

    # Torture-test to ensure that huge Python Buffer Protocol allocations are properly
    # shared (rather than copied), and also that the lifetime is held appropriately.
    a0 = np.ones((20000, 30000), dtype=np.int32)
    b0 = hl.Buffer(a0)
    del a0
    for i in range(200):
        b1 = hl.Buffer(b0)
        b0 = b1
        b1 = None
        gc.collect()

    b0[56, 34] = 12
    assert b0[56, 34] == 12


def test_float16():
    array_in = np.zeros((256, 256, 3), dtype=np.float16, order="F")
    hl_img = hl.Buffer(array_in)
    array_out = np.array(hl_img, copy=False)


# TODO: https://github.com/halide/Halide/issues/6849
# def test_bfloat16():
#     try:
#         from tensorflow.python.lib.core import _pywrap_bfloat16
#         bfloat16 = _pywrap_bfloat16.TF_bfloat16_type()
#         array_in = np.zeros((256, 256, 3), dtype=bfloat16, order='F')
#         hl_img = hl.Buffer(array_in)
#         array_out = np.array(hl_img, copy = False)
#     except ModuleNotFoundError as e:
#         print("skipping test_bfloat16() because tensorflow was not found: %s" % str(e))
#         return
#     else:
#         assert False, "This should not happen"


def test_int64():
    array_in = np.zeros((256, 256, 3), dtype=np.int64, order="F")
    hl_img = hl.Buffer(array_in)
    array_out = np.array(hl_img, copy=False)


def test_make_interleaved():
    w = 7
    h = 13
    c = 3

    b = hl.Buffer.make_interleaved(type=hl.UInt(8), width=w, height=h, channels=c)

    assert b.dim(0).min() == 0
    assert b.dim(0).extent() == w
    assert b.dim(0).stride() == c

    assert b.dim(1).min() == 0
    assert b.dim(1).extent() == h
    assert b.dim(1).stride() == w * c

    assert b.dim(2).min() == 0
    assert b.dim(2).extent() == c
    assert b.dim(2).stride() == 1

    a = np.array(b, copy=False)
    # NumPy shape order is opposite that of Halide shape order
    assert a.shape == (c, h, w)
    assert a.strides == (1, w * c, c)
    assert a.dtype == np.uint8


def test_interleaved_ndarray():
    w = 7
    h = 13
    c = 3

    a = np.ndarray(dtype=np.uint8, shape=(w, h, c), strides=(c, w * c, 1))

    assert a.shape == (w, h, c)
    assert a.strides == (c, w * c, 1)
    assert a.dtype == np.uint8

    b = hl.Buffer(a)
    assert b.type() == hl.UInt(8)

    assert b.dim(0).min() == 0
    assert b.dim(0).extent() == c
    assert b.dim(0).stride() == 1

    assert b.dim(1).min() == 0
    assert b.dim(1).extent() == h
    assert b.dim(1).stride() == w * c

    assert b.dim(2).min() == 0
    assert b.dim(2).extent() == w
    assert b.dim(2).stride() == c


def test_reorder():
    W = 7
    H = 5
    C = 3
    Z = 2

    a = hl.Buffer(type=hl.UInt(8), sizes=[W, H, C], storage_order=[2, 0, 1])
    assert a.dim(0).extent() == W
    assert a.dim(1).extent() == H
    assert a.dim(2).extent() == C
    assert a.dim(2).stride() == 1
    assert a.dim(0).stride() == C
    assert a.dim(1).stride() == W * C

    b = hl.Buffer(hl.UInt(8), [W, H, C, Z], [2, 3, 0, 1])
    assert b.dim(0).extent() == W
    assert b.dim(1).extent() == H
    assert b.dim(2).extent() == C
    assert b.dim(3).extent() == Z
    assert b.dim(2).stride() == 1
    assert b.dim(3).stride() == C
    assert b.dim(0).stride() == C * Z
    assert b.dim(1).stride() == W * C * Z

    b2 = hl.Buffer(hl.UInt(8), [C, Z, W, H])
    assert b.dim(0).extent() == b2.dim(2).extent()
    assert b.dim(1).extent() == b2.dim(3).extent()
    assert b.dim(2).extent() == b2.dim(0).extent()
    assert b.dim(3).extent() == b2.dim(1).extent()
    assert b.dim(0).stride() == b2.dim(2).stride()
    assert b.dim(1).stride() == b2.dim(3).stride()
    assert b.dim(2).stride() == b2.dim(0).stride()
    assert b.dim(3).stride() == b2.dim(1).stride()

    b2.transpose([2, 3, 0, 1])
    assert b.dim(0).extent() == b2.dim(0).extent()
    assert b.dim(1).extent() == b2.dim(1).extent()
    assert b.dim(2).extent() == b2.dim(2).extent()
    assert b.dim(3).extent() == b2.dim(3).extent()
    assert b.dim(0).stride() == b2.dim(0).stride()
    assert b.dim(1).stride() == b2.dim(1).stride()
    assert b.dim(2).stride() == b2.dim(2).stride()
    assert b.dim(3).stride() == b2.dim(3).stride()


def test_overflow():
    # Don't bother testing this on 32-bit systems (our "huge" size is too large there)
    if not _is_64bits():
        print("skipping test_overflow()")
        return

    # size = INT_MAX
    w_intmax = 0x7FFFFFFF

    # When size == INT_MAX, we should not emit error
    size_intmax = np.ndarray(dtype=np.uint8, shape=(w_intmax))
    hl.Buffer(size_intmax)

    # size = INT_MAX + 1
    w_over_intmax = 0x7FFFFFFF + 1

    # We should emit the error when the size > INT_MAX
    size_over_intmax = np.ndarray(dtype=np.uint8, shape=(w_over_intmax))
    try:
        hl.Buffer(size_over_intmax)
    except ValueError as e:
        assert "Out of range dimensions in buffer conversion" in str(e)


def test_buffer_to_str():
    b = hl.Buffer()
    assert str(b) == "<undefined halide.Buffer>"
    b = hl.Buffer(hl.Int(32), [128, 256])
    assert str(b) == "<halide.Buffer of type int32 shape:[[0,128,1],[0,256,128]]>"


def test_scalar_buffers():
    buf = hl.Buffer.make_scalar(hl.Float(32))

    assert buf.dimensions() == 0

    buf.fill(0)
    buf[()] = 2.5

    assert buf[()] == 2.5

    buf.fill(32)
    assert buf[()] == 32


def test_oob():
    buf = hl.Buffer(hl.Int(16), [4, 6])
    buf.fill(0)

    # getitem below min
    try:
        print(buf[-1, 2])
    except IndexError as e:
        assert "index -1 is out of bounds for axis 0 with min=0, extent=4" in str(e)
    else:
        assert False, "Did not see expected exception!"

    # getitem above max
    try:
        print(buf[1, 6])
    except IndexError as e:
        assert "index 6 is out of bounds for axis 1 with min=0, extent=6" in str(e)

    # setitem below min
    try:
        buf[-1, 2] = 42
    except IndexError as e:
        assert "index -1 is out of bounds for axis 0 with min=0, extent=4" in str(e)
    else:
        assert False, "Did not see expected exception!"

    # setitem above max
    try:
        buf[1, 6] = 42
    except IndexError as e:
        assert "index 6 is out of bounds for axis 1 with min=0, extent=6" in str(e)


if __name__ == "__main__":
    test_make_interleaved()
    test_interleaved_ndarray()
    test_ndarray_to_buffer(reverse_axes=True)
    test_ndarray_to_buffer(reverse_axes=False)
    test_buffer_to_ndarray(reverse_axes=True)
    test_buffer_to_ndarray(reverse_axes=False)
    test_for_each_element()
    test_fill_all_equal()
    test_bufferinfo_sharing()
    # TODO: https://github.com/halide/Halide/issues/6849
    # test_bfloat16()
    test_float16()
    test_int64()
    test_reorder()
    test_overflow()
    test_buffer_to_str()
    test_scalar_buffers()
    test_oob()
