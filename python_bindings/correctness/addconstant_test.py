import addconstant
import numpy


ERROR_THRESHOLD = 0.0001


def test():
    constant_u1 = True
    constant_u8 = 3
    constant_u16 = 49153
    constant_u32 = 65537
    constant_u64 = 5724968371
    constant_i8 = -7
    constant_i16 = -30712
    constant_i32 = -98901
    constant_i64 = -8163465847
    constant_float = 3.14159
    constant_double = 1.61803

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
    input_2d = numpy.array([[1, 2, 3], [4, 5, 6]], dtype=numpy.int8, order='F')
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
    output_2d = numpy.zeros((2, 3), dtype=numpy.int8, order='F')
    output_3d = numpy.zeros((2, 2, 2), dtype=numpy.int8)

    addconstant.addconstant(
        constant_u1,
        constant_u8, constant_u16, constant_u32, constant_u64,
        constant_i8, constant_i16, constant_i32, constant_i64,
        constant_float, constant_double,
        input_u8, input_u16, input_u32, input_u64,
        input_i8, input_i16, input_i32, input_i64,
        input_float, input_double, input_2d, input_3d,
        output_u8, output_u16, output_u32, output_u64,
        output_i8, output_i16, output_i32, output_i64,
        output_float, output_double, output_2d, output_3d,
    )

    combinations = [
        ("u8", input_u8, output_u8, constant_u8),
        ("u16", input_u16, output_u16, constant_u16),
        ("u32", input_u32, output_u32, constant_u32),
        ("u64", input_u64, output_u64, constant_u64),
        ("i8", input_i8, output_i8, constant_i8),
        ("i16", input_i16, output_i16, constant_i16),
        ("i32", input_i32, output_i32, constant_i32),
        ("i64", input_i64, output_i64, constant_i64),
        ("float", input_float, output_float, constant_float),
        ("double", input_double, output_double, constant_double),
    ]

    for _, input, output, constant in combinations:
        for i, o in zip(input, output):
            assert abs(o - (i + constant)) < ERROR_THRESHOLD

    for x in range(input_2d.shape[0]):
        for y in range(input_2d.shape[1]):
            assert output_2d[x, y] == input_2d[x, y] + constant_i8

    for x in range(input_3d.shape[0]):
        for y in range(input_3d.shape[1]):
            for z in range(input_3d.shape[2]):
                assert output_3d[x, y, z] == input_3d[x, y, z] + constant_i8


if __name__ == "__main__":
  test()
