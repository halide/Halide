import halide as hl

x = hl.Var('x')
y = hl.Var('y')

# Just as with Func.compile_to_something(), you must explicitly specify
# the Arguments here for now. TODO: add type hinting everywhere as an alternative.
_FooGenArgs = [
    hl.Argument("input_buf", hl.ArgumentKind.InputBuffer, hl.UInt(8), 2),
    hl.Argument("input_scalar", hl.ArgumentKind.InputScalar, hl.Int(32), 0),
    hl.Argument("output_buf", hl.ArgumentKind.OutputBuffer, hl.UInt(8), 2),
]

# "foo_bar" is the build-system name of the Generator
@hl.generator("foo_bar", _FooGenArgs)
def FooGen(context, input_buf, input_scalar):
    output_buf = hl.Func("output_buf")
    output_buf[x, y] = input_buf[x, y] + input_scalar
    return output_buf

if __name__ == "__main__":
    hl.main()
