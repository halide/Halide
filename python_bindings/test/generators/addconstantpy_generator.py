import halide as hl

x = hl.Var("x")
y = hl.Var("y")
z = hl.Var("z")


@hl.alias(
    addconstantpy_with_offset_42={"extra_int": 42},
    addconstantpy_with_negative_offset={"extra_int": -1},
)
@hl.generator(name="addconstantpy")
class AddConstantGenerator:
    extra_int = hl.GeneratorParam(0)

    scalar_uint1 = hl.InputScalar(hl.Bool())
    scalar_uint8 = hl.InputScalar(hl.UInt(8))
    scalar_uint16 = hl.InputScalar(hl.UInt(16))
    scalar_uint32 = hl.InputScalar(hl.UInt(32))
    scalar_uint64 = hl.InputScalar(hl.UInt(64))
    scalar_int8 = hl.InputScalar(hl.Int(8))
    scalar_int16 = hl.InputScalar(hl.Int(16))
    scalar_int32 = hl.InputScalar(hl.Int(32))
    scalar_int64 = hl.InputScalar(hl.Int(64))
    scalar_float = hl.InputScalar(hl.Float(32))
    scalar_double = hl.InputScalar(hl.Float(64))

    input_uint8 = hl.InputBuffer(hl.UInt(8), 1)
    input_uint16 = hl.InputBuffer(hl.UInt(16), 1)
    input_uint32 = hl.InputBuffer(hl.UInt(32), 1)
    input_uint64 = hl.InputBuffer(hl.UInt(64), 1)
    input_int8 = hl.InputBuffer(hl.Int(8), 1)
    input_int16 = hl.InputBuffer(hl.Int(16), 1)
    input_int32 = hl.InputBuffer(hl.Int(32), 1)
    input_int64 = hl.InputBuffer(hl.Int(64), 1)
    input_float = hl.InputBuffer(hl.Float(32), 1)
    input_double = hl.InputBuffer(hl.Float(64), 1)
    input_half = hl.InputBuffer(hl.Float(16), 1)
    input_2d = hl.InputBuffer(hl.Int(8), 2)
    input_3d = hl.InputBuffer(hl.Int(8), 3)

    output_uint8 = hl.OutputBuffer(hl.UInt(8), 1)
    output_uint16 = hl.OutputBuffer(hl.UInt(16), 1)
    output_uint32 = hl.OutputBuffer(hl.UInt(32), 1)
    output_uint64 = hl.OutputBuffer(hl.UInt(64), 1)
    output_int8 = hl.OutputBuffer(hl.Int(8), 1)
    output_int16 = hl.OutputBuffer(hl.Int(16), 1)
    output_int32 = hl.OutputBuffer(hl.Int(32), 1)
    output_int64 = hl.OutputBuffer(hl.Int(64), 1)
    output_float = hl.OutputBuffer(hl.Float(32), 1)
    output_double = hl.OutputBuffer(hl.Float(64), 1)
    output_half = hl.OutputBuffer(hl.Float(16), 1)
    output_2d = hl.OutputBuffer(hl.Int(8), 2)
    output_3d = hl.OutputBuffer(hl.Int(8), 3)

    def generate(self):
        g = self
        g.add_requirement(g.scalar_int32 != 0)  # error_args omitted for this case
        g.add_requirement(g.scalar_int32 > 0, "negative values are bad", g.scalar_int32)

        g.output_uint8[x] = g.input_uint8[x] + g.scalar_uint8
        g.output_uint16[x] = g.input_uint16[x] + g.scalar_uint16
        g.output_uint32[x] = g.input_uint32[x] + g.scalar_uint32
        g.output_uint64[x] = g.input_uint64[x] + g.scalar_uint64
        g.output_int8[x] = g.input_int8[x] + g.scalar_int8
        g.output_int16[x] = g.input_int16[x] + g.scalar_int16
        g.output_int32[x] = g.input_int32[x] + g.scalar_int32
        g.output_int64[x] = g.input_int64[x] + g.scalar_int64
        g.output_float[x] = g.input_float[x] + g.scalar_float
        g.output_double[x] = g.input_double[x] + g.scalar_double
        g.output_half[x] = g.input_half[x] + hl.cast(hl.Float(16), g.scalar_float)
        g.output_2d[x, y] = g.input_2d[x, y] + g.scalar_int8
        g.output_3d[x, y, z] = g.input_3d[x, y, z] + g.scalar_int8 + g.extra_int


if __name__ == "__main__":
    hl.main()
