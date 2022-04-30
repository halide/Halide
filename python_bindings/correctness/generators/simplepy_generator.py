import halide as hl

x = hl.Var('x')
y = hl.Var('y')

@hl.generator(name = "simplepy")
class SimplePy:
    class GeneratorParams:
        offset = 0

    class Inputs:
        buffer_input = hl.InputBuffer(hl.UInt(8), 2)
        float_arg = hl.InputScalar(hl.Float(32))

    class Outputs:
        simple_output = hl.OutputBuffer(hl.Float(32), 2)

    def generate(self, gp, i, o):
        o.simple_output[x, y] = hl.f32(gp.offset + i.buffer_input[x, y]) + i.float_arg

    def schedule(self, gp, i, o):
        o.simple_output.compute_root()

if __name__ == "__main__":
    hl.main()

