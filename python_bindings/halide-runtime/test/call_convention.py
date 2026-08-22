"""Exercise the full AOT calling convention through halide.runtime.

This drives the `callconv` kernel entirely from its introspected metadata
(`kernel.arguments`): input scalars of every type, a 2-D input buffer, and three
outputs of differing element types -- including a Tuple output that lowers to
several output buffers. Because everything is computed elementwise, the numpy
reference is independent of Halide's axis ordering.
"""

import sys

import numpy as np

import halide.runtime as hlr

# Halide type string (as reported by kernel.arguments) -> numpy dtype.
_DTYPE = {
    "bool": np.uint8,
    "int8": np.int8,
    "int16": np.int16,
    "int32": np.int32,
    "int64": np.int64,
    "uint8": np.uint8,
    "uint16": np.uint16,
    "uint32": np.uint32,
    "uint64": np.uint64,
    "float32": np.float32,
    "float64": np.float64,
}

# Deliberately spans signed/unsigned, narrow/wide, and float types. Values are
# small enough that the int64 accumulator never overflows.
SCALARS = {
    "s_bool": True,
    "s_i8": -5,
    "s_i16": 300,
    "s_i32": -70000,
    "s_i64": 5_000_000_000,
    "s_u8": 7,
    "s_u16": 40000,
    "s_u32": 3_000_000_000,
    "s_u64": 10_000_000_000,
    "s_f32": 1.5,  # exactly representable in float32
    "s_f64": 2.25,
}


def run(kernel, shape, input_buf):
    """Call `kernel` with a value for every argument, driven by its metadata."""
    args = {a["name"]: a for a in kernel.arguments}
    call_args = {}
    for name, a in args.items():
        if a["kind"] == "input_buffer":
            call_args[name] = input_buf
        elif a["kind"] == "output_buffer":
            call_args[name] = np.zeros(shape, dtype=_DTYPE[a["type"]])
        else:  # input_scalar
            call_args[name] = SCALARS[name]
    # Note: "packed.0"/"packed.1" are not valid Python identifiers, but keyword
    # expansion of a dict accepts arbitrary string keys.
    kernel(**call_args)
    return call_args


def main():
    # Two kernels compiled from the same generator with different values of its
    # enum GeneratorParam `combine`: the default (add) and a `combine=xor` build.
    add_path, xor_path = sys.argv[1], sys.argv[2]
    kernel = hlr.load(add_path, name="callconv")

    # Introspection describes the calling convention: name, kind, type, dims.
    args = {a["name"]: a for a in kernel.arguments}
    assert args["input"]["kind"] == "input_buffer"
    assert args["input"]["type"] == "uint8" and args["input"]["dimensions"] == 2
    assert args["s_bool"]["kind"] == "input_scalar"
    assert args["s_u64"]["type"] == "uint64" and args["s_u64"]["dimensions"] == 0
    assert args["total"]["kind"] == "output_buffer" and args["total"]["type"] == "int64"
    assert args["scaled"]["type"] == "float64"
    # The Tuple output shows up as two separate output buffers.
    assert "packed.0" in args and "packed.1" in args
    assert args["packed.0"]["type"] == "uint8" and args["packed.1"]["type"] == "int32"

    shape = (3, 4)
    input_buf = np.arange(12, dtype=np.uint8).reshape(shape)

    call_args = run(kernel, shape, input_buf)

    in64 = input_buf.astype(np.int64)
    scalar_sum = (
        1  # s_bool
        - 5
        + 300
        - 70000
        + 5_000_000_000  # signed
        + 7
        + 40000
        + 3_000_000_000
        + 10_000_000_000  # unsigned
    )
    expected_total = in64 + scalar_sum
    np.testing.assert_array_equal(call_args["total"], expected_total)

    expected_scaled = (
        np.float32(1.5).astype(np.float64) * in64.astype(np.float64) + 2.25
    )
    np.testing.assert_array_equal(call_args["scaled"], expected_scaled)

    # Default `combine=add`: packed.0 = input + s_u8.
    np.testing.assert_array_equal(
        call_args["packed.0"], (input_buf + 7).astype(np.uint8)
    )
    np.testing.assert_array_equal(
        call_args["packed.1"], expected_total.astype(np.int32)
    )

    # Reject multi-byte arrays whose declared byte order differs from the host,
    # rather than silently writing native-endian data into them.
    invalid_args = call_args.copy()
    invalid_args["total"] = np.zeros(shape, dtype=">i8")
    try:
        kernel(**invalid_args)
    except ValueError as e:
        assert "Non-native byte order" in str(e)
    else:
        assert False, "non-native-endian output was accepted"

    # Reject byte strides that cannot be represented as an integral Halide
    # element stride.
    invalid_args = call_args.copy()
    invalid_args["packed.1"] = np.ndarray(
        shape, dtype=np.int32, buffer=bytearray(32), strides=(1, 1)
    )
    try:
        kernel(**invalid_args)
    except ValueError as e:
        assert "stride" in str(e)
    else:
        assert False, "non-integral element stride was accepted"

    # The enum GeneratorParam is a compile-time choice: the `combine=xor` build is
    # a different kernel with the *same* calling convention but different behavior.
    xor_kernel = hlr.load(xor_path, name="callconv_xor")
    assert [a["name"] for a in xor_kernel.arguments] == list(args), (
        "the enum GeneratorParam must not change the runtime calling convention"
    )
    xor_args = run(xor_kernel, shape, input_buf)
    np.testing.assert_array_equal(
        xor_args["packed.0"], (input_buf ^ 7).astype(np.uint8)
    )
    # Everything not selected by the enum is unchanged.
    np.testing.assert_array_equal(xor_args["total"], expected_total)

    # No compiler was needed for any of this.
    assert "halide.halide_" not in sys.modules

    print("Success!")


if __name__ == "__main__":
    main()
