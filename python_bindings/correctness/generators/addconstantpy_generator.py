import halide as hl

x = hl.Var('x')
y = hl.Var('y')
z = hl.Var('z')

@hl.generator(name = "addconstantpy")
class AddConstantGenerator:
    class GeneratorParams:
        extra_int = 0

    class Inputs:
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
        input_2d = hl.InputBuffer(hl.Int(8), 2)
        input_3d = hl.InputBuffer(hl.Int(8), 3)

    class Outputs:
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
        output_2d = hl.OutputBuffer(hl.Int(8), 2)
        output_3d = hl.OutputBuffer(hl.Int(8), 3)

    def generate(self, gp, i, o):
        o.output_uint8[x] = i.input_uint8[x] + i.scalar_uint8
        o.output_uint16[x] = i.input_uint16[x] + i.scalar_uint16
        o.output_uint32[x] = i.input_uint32[x] + i.scalar_uint32
        o.output_uint64[x] = i.input_uint64[x] + i.scalar_uint64
        o.output_int8[x] = i.input_int8[x] + i.scalar_int8
        o.output_int16[x] = i.input_int16[x] + i.scalar_int16
        o.output_int32[x] = i.input_int32[x] + i.scalar_int32
        o.output_int64[x] = i.input_int64[x] + i.scalar_int64
        o.output_float[x] = i.input_float[x] + i.scalar_float
        o.output_double[x] = i.input_double[x] + i.scalar_double
        o.output_2d[x, y] = i.input_2d[x, y] + i.scalar_int8
        o.output_3d[x, y, z] = i.input_3d[x, y, z] + i.scalar_int8 + gp.extra_int

if __name__ == "__main__":
    hl.main()

