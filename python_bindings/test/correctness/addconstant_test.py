import addconstantcpp, addconstantpy
import addconstantcpp_with_offset_42, addconstantpy_with_offset_42
import addconstantcpp_with_negative_offset, addconstantpy_with_negative_offset
import numpy

TESTS_AND_OFFSETS = [
    (addconstantcpp.addconstantcpp, 0),
    (addconstantpy.addconstantpy, 0),
    (addconstantcpp_with_offset_42.addconstantcpp_with_offset_42, 42),
    (addconstantpy_with_offset_42.addconstantpy_with_offset_42, 42),
    (addconstantcpp_with_negative_offset.addconstantcpp_with_negative_offset, -1),
    (addconstantpy_with_negative_offset.addconstantpy_with_negative_offset, -1),
]

ERROR_THRESHOLD = 0.0001


def test(addconstant_impl_func, offset):
    scalar_u1 = True
    scalar_u8 = 3
    scalar_u16 = 49153
    scalar_u32 = 65537
    scalar_u64 = 5724968371
    scalar_i8 = -7
    scalar_i16 = -30712
    scalar_i32 = 98901
    scalar_i64 = -8163465847
    scalar_float = 3.14159
    scalar_double = 1.61803

    input_u8 = numpy.array([0, 1, 2], dtype=numpy.uint8)
    input_u16 = numpy.array([0, 256, 512], dtype=numpy.uint16)
    input_u32 = numpy.array([0, 65536, 131072], dtype=numpy.uint32)
    input_u64 = numpy.array([0, 4294967296, 8589934592], dtype=numpy.uint64)
    input_i8 = numpy.array([1, -2, 3], dtype=numpy.int8)
    input_i16 = numpy.array([1, -256, 512], dtype=numpy.int16)
    input_i32 = numpy.array([1, -65536, 131072], dtype=numpy.int32)
    input_i64 = numpy.array([0, -4294967296, 8589934592], dtype=numpy.int64)
    input_float = numpy.array([3.14, 2.718, 1.618], dtype=numpy.float32)
    input_double = numpy.array([3.14, 2.718, 1.618], dtype=numpy.float64)
    input_half = numpy.array([3.14, 2.718, 1.618], dtype=numpy.float16)
    input_2d = numpy.array([[1, 2, 3], [4, 5, 6]], dtype=numpy.int8, order="F")
    input_3d = numpy.array([[[1, 2], [3, 4]], [[5, 6], [7, 8]]], dtype=numpy.int8)

    output_u8 = numpy.zeros((3,), dtype=numpy.uint8)
    output_u16 = numpy.zeros((3,), dtype=numpy.uint16)
    output_u32 = numpy.zeros((3,), dtype=numpy.uint32)
    output_u64 = numpy.zeros((3,), dtype=numpy.uint64)
    output_i8 = numpy.zeros((3,), dtype=numpy.int8)
    output_i16 = numpy.zeros((3,), dtype=numpy.int16)
    output_i32 = numpy.zeros((3,), dtype=numpy.int32)
    output_i64 = numpy.zeros((3,), dtype=numpy.int64)
    output_float = numpy.zeros((3,), dtype=numpy.float32)
    output_double = numpy.zeros((3,), dtype=numpy.float64)
    output_half = numpy.zeros((3,), dtype=numpy.float16)
    output_2d = numpy.zeros((2, 3), dtype=numpy.int8, order="F")
    output_3d = numpy.zeros((2, 2, 2), dtype=numpy.int8)

    addconstant_impl_func(
        scalar_u1,
        scalar_u8,
        scalar_u16,
        scalar_u32,
        scalar_u64,
        scalar_i8,
        scalar_i16,
        scalar_i32,
        scalar_i64,
        scalar_float,
        scalar_double,
        input_u8,
        input_u16,
        input_u32,
        input_u64,
        input_i8,
        input_i16,
        input_i32,
        input_i64,
        input_float,
        input_double,
        input_half,
        input_2d,
        input_3d,
        output_u8,
        output_u16,
        output_u32,
        output_u64,
        output_i8,
        output_i16,
        output_i32,
        output_i64,
        output_float,
        output_double,
        output_half,
        output_2d,
        output_3d,
    )

    combinations = [
        ("u8", input_u8, output_u8, scalar_u8),
        ("u16", input_u16, output_u16, scalar_u16),
        ("u32", input_u32, output_u32, scalar_u32),
        ("u64", input_u64, output_u64, scalar_u64),
        ("i8", input_i8, output_i8, scalar_i8),
        ("i16", input_i16, output_i16, scalar_i16),
        ("i32", input_i32, output_i32, scalar_i32),
        ("i64", input_i64, output_i64, scalar_i64),
        ("float", input_float, output_float, scalar_float),
        ("double", input_double, output_double, scalar_double),
        ("half", input_half, output_half, scalar_float),
    ]

    for _, input, output, scalar in combinations:
        for i, o in zip(input, output):
            scalar_as_numpy = numpy.array(scalar).astype(input.dtype)
            assert abs(o - (i + scalar_as_numpy)) < ERROR_THRESHOLD

    for x in range(input_2d.shape[0]):
        for y in range(input_2d.shape[1]):
            assert output_2d[x, y] == input_2d[x, y] + scalar_i8

    for x in range(input_3d.shape[0]):
        for y in range(input_3d.shape[1]):
            for z in range(input_3d.shape[2]):
                assert output_3d[x, y, z] == input_3d[x, y, z] + scalar_i8 + offset

    try:
        # Expected requirement failure #1
        scalar_i32 = 0
        addconstant_impl_func(
            scalar_u1,
            scalar_u8,
            scalar_u16,
            scalar_u32,
            scalar_u64,
            scalar_i8,
            scalar_i16,
            scalar_i32,
            scalar_i64,
            scalar_float,
            scalar_double,
            input_u8,
            input_u16,
            input_u32,
            input_u64,
            input_i8,
            input_i16,
            input_i32,
            input_i64,
            input_float,
            input_double,
            input_half,
            input_2d,
            input_3d,
            output_u8,
            output_u16,
            output_u32,
            output_u64,
            output_i8,
            output_i16,
            output_i32,
            output_i64,
            output_float,
            output_double,
            output_half,
            output_2d,
            output_3d,
        )
    except RuntimeError as e:
        assert str(e) == "Halide Runtime Error: -27", e
    else:
        assert False, "Did not see expected exception!"

    try:
        # Expected requirement failure #2 -- note that for AOT-compiled
        # code in Python, the error message is stricly numeric (the text
        # of the error isn't currently propagated int he exception).
        scalar_i32 = -1
        addconstant_impl_func(
            scalar_u1,
            scalar_u8,
            scalar_u16,
            scalar_u32,
            scalar_u64,
            scalar_i8,
            scalar_i16,
            scalar_i32,
            scalar_i64,
            scalar_float,
            scalar_double,
            input_u8,
            input_u16,
            input_u32,
            input_u64,
            input_i8,
            input_i16,
            input_i32,
            input_i64,
            input_float,
            input_double,
            input_half,
            input_2d,
            input_3d,
            output_u8,
            output_u16,
            output_u32,
            output_u64,
            output_i8,
            output_i16,
            output_i32,
            output_i64,
            output_float,
            output_double,
            output_half,
            output_2d,
            output_3d,
        )
    except RuntimeError as e:
        assert str(e) == "Halide Runtime Error: -27", e
    else:
        assert False, "Did not see expected exception!"


if __name__ == "__main__":
    for t, o in TESTS_AND_OFFSETS:
        test(t, o)
