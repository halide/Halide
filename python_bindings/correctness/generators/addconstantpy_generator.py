import halide as hl

x = hl.Var('x')
y = hl.Var('y')
z = hl.Var('z')

@hl.generator("addconstantpy")
class AddConstantGenerator:
    extra_int = hl.GeneratorParam(0)

    constant_uint1 = hl.InputScalar(hl.Bool())
    constant_uint8 = hl.InputScalar(hl.UInt(8))
    constant_uint16 = hl.InputScalar(hl.UInt(16))
    constant_uint32 = hl.InputScalar(hl.UInt(32))
    constant_uint64 = hl.InputScalar(hl.UInt(64))
    constant_int8 = hl.InputScalar(hl.Int(8))
    constant_int16 = hl.InputScalar(hl.Int(16))
    constant_int32 = hl.InputScalar(hl.Int(32))
    constant_int64 = hl.InputScalar(hl.Int(64))
    constant_float = hl.InputScalar(hl.Float(32))
    constant_double = hl.InputScalar(hl.Float(64))

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

    def generate(self):
        self.output_uint8[x] = self.input_uint8[x] + self.constant_uint8
        self.output_uint16[x] = self.input_uint16[x] + self.constant_uint16
        self.output_uint32[x] = self.input_uint32[x] + self.constant_uint32
        self.output_uint64[x] = self.input_uint64[x] + self.constant_uint64
        self.output_int8[x] = self.input_int8[x] + self.constant_int8
        self.output_int16[x] = self.input_int16[x] + self.constant_int16
        self.output_int32[x] = self.input_int32[x] + self.constant_int32
        self.output_int64[x] = self.input_int64[x] + self.constant_int64
        self.output_float[x] = self.input_float[x] + self.constant_float
        self.output_double[x] = self.input_double[x] + self.constant_double
        self.output_2d[x, y] = self.input_2d[x, y] + self.constant_int8
        self.output_3d[x, y, z] = self.input_3d[x, y, z] + self.constant_int8 + self.extra_int

if __name__ == "__main__":
    hl.main()

