import halide as hl

x = hl.Var('x')
y = hl.Var('y')

@hl.generator("simplepy")
class SimplePy:
    offset = hl.GeneratorParam(0)
    compute_level = hl.GeneratorParam(hl.LoopLevel.root())

    buffer_input = hl.InputBuffer(hl.UInt(8), 2)
    func_input = hl.InputFunc(hl.Int(32), 2)
    float_arg = hl.InputScalar(hl.Float(32))

    simple_output = hl.OutputFunc(hl.Float(32), 2)

    def generate(self):
        self.simple_output[x, y] = hl.f32(self.func_input[x, y] + self.offset + self.buffer_input[x, y]) + self.float_arg

    def schedule(self):
        self.simple_output.compute_at(self.compute_level.value())

if __name__ == "__main__":
    hl.main()

