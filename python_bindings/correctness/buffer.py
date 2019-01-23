from __future__ import print_function
from __future__ import division

import halide as hl
import numpy as np
import gc

def test_ndarray_to_buffer():
    a0 = np.ones((200, 300), dtype=np.int32)

    # Buffer always shares data (when possible) by default,
    # and maintains the shape of the data source. (note that
    # the ndarray is col-major by default!)
    b0 = hl.Buffer(a0, "float32_test_buffer")
    assert b0.type() == hl.Int(32)
    assert b0.name() == "float32_test_buffer"
    assert b0.all_equal(1)

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


def test_buffer_to_ndarray():
    buf = hl.Buffer(hl.Int(16), [4, 4])
    assert buf.type() == hl.Int(16)
    buf.fill(0)
    buf[1, 2] = 42
    assert buf[1, 2] == 42

    # Should share storage with buf
    array_shared = np.array(buf, copy = False)
    assert array_shared.shape == (4, 4)
    assert array_shared.dtype == np.int16
    assert array_shared[1, 2] == 42

    # Should *not* share storage with buf
    array_copied = np.array(buf, copy = True)
    assert array_copied.shape == (4, 4)
    assert array_copied.dtype == np.int16
    assert array_copied[1, 2] == 42

    buf[1, 2] = 3
    assert array_shared[1, 2] == 3
    assert array_copied[1, 2] == 42

    # Ensure that Buffers that have nonzero mins get converted correctly,
    # since the Python Buffer Protocol doesn't have the 'min' concept
    cropped = buf.copy()
    cropped.crop(dimension = 0, min = 1, extent = 2)

    # Should share storage with cropped (and buf)
    cropped_array_shared = np.array(cropped, copy = False)
    assert cropped_array_shared.shape == (2, 4)
    assert cropped_array_shared.dtype == np.int16
    assert cropped_array_shared[0, 2] == 3

    # Should *not* share storage with anything
    cropped_array_copied = np.array(cropped, copy = True)
    assert cropped_array_copied.shape == (2, 4)
    assert cropped_array_copied.dtype == np.int16
    assert cropped_array_copied[0, 2] == 3

    cropped[1, 2] = 5

    assert buf[1, 2] == 3
    assert array_shared[1, 2] == 3
    assert array_copied[1, 2] == 42

    assert cropped[1, 2] == 5
    assert cropped_array_shared[0, 2] == 5
    assert cropped_array_copied[0, 2] == 3


def _assert_fn(e):
    assert e

def test_for_each_element():
    buf = hl.Buffer(hl.Float(32), [3, 4])
    for x in range(3):
        for y in range(4):
            buf[x, y] = x + y
    # Can't use 'assert' in a lambda, but can call a fn that uses it.
    buf.for_each_element(lambda pos, buf=buf: _assert_fn(buf[pos[0], pos[1]] == pos[0] + pos[1]))

def test_fill_all_equal():
    buf = hl.Buffer(hl.Int(32), [3, 4])
    buf.fill(3)
    assert buf.all_equal(3)
    buf[1, 2] = 4
    assert not buf.all_equal(3)


def test_bufferinfo_sharing():
    # Torture-test to ensure that huge Python Buffer Protocol allocations are properly
    # shared (rather than copied), and also that the lifetime is held appropriately
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
    array_in = np.zeros((256, 256, 3), dtype=np.float16, order='F')
    hl_img = hl.Buffer(array_in)
    array_out = np.array(hl_img, copy = False)

def test_int64():
    array_in = np.zeros((256, 256, 3), dtype=np.int64, order='F')
    hl_img = hl.Buffer(array_in)
    array_out = np.array(hl_img, copy = False)

def test_make_interleaved():
    w = 7
    h = 13
    c = 3

    b = hl.Buffer.make_interleaved(type = hl.UInt(8), width = w, height = h, channels = c)

    assert b.dim(0).min() == 0
    assert b.dim(0).extent() == w
    assert b.dim(0).stride() == c

    assert b.dim(1).min() == 0
    assert b.dim(1).extent() == h
    assert b.dim(1).stride() == w * c

    assert b.dim(2).min() == 0
    assert b.dim(2).extent() == c
    assert b.dim(2).stride() == 1

    a = np.array(b, copy = False)
    assert a.shape == (w, h, c)
    assert a.strides == (c, w*c, 1)
    assert a.dtype == np.uint8

def test_interleaved_ndarray():
    w = 7
    h = 13
    c = 3

    a = np.ndarray(dtype=np.uint8, shape=(w, h, c), strides=(c, w*c, 1))

    assert a.shape == (w, h, c)
    assert a.strides == (c, w*c, 1)
    assert a.dtype == np.uint8

    b = hl.Buffer(a)
    assert b.type() == hl.UInt(8)

    assert b.dim(0).min() == 0
    assert b.dim(0).extent() == w
    assert b.dim(0).stride() == c

    assert b.dim(1).min() == 0
    assert b.dim(1).extent() == h
    assert b.dim(1).stride() == w * c

    assert b.dim(2).min() == 0
    assert b.dim(2).extent() == c
    assert b.dim(2).stride() == 1

def test_reorder():
    W = 7
    H = 5
    C = 3
    Z = 2

    a = hl.Buffer(type = hl.UInt(8), sizes = [W, H, C], storage_order = [2, 0, 1])
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


if __name__ == "__main__":
    test_make_interleaved()
    test_interleaved_ndarray()
    test_ndarray_to_buffer()
    test_buffer_to_ndarray()
    test_for_each_element()
    test_fill_all_equal()
    test_bufferinfo_sharing()
    test_float16()
    test_int64()
    test_reorder()

