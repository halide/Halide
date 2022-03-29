import halide as hl

x = hl.Var('x')
y = hl.Var('y')
c = hl.Var('c')

@hl.generator("simplepy")
class ComplexPy:
    vectorize = hl.GeneratorParam(True)
    intermediate_level = hl.GeneratorParam(hl.LoopLevel.root())

    typed_buffer_input = hl.InputBuffer(hl.UInt(8), 3)
    untyped_buffer_input = hl.InputBuffer(None, 3)
    simple_input = hl.InputFunc(None, 3)
    float_arg = hl.InputScalar(hl.Float(32))
    int_arg = hl.InputScalar(hl.Int(32))

    simple_output = hl.OutputFunc(hl.Float(32), 3)
    tuple_output = hl.OutputFunc(None, 3)
    typed_buffer_output = hl.OutputBuffer(hl.Float(32), 3)
    untyped_buffer_output = hl.OutputBuffer(None, 3)
    static_compiled_buffer_output = hl.OutputBuffer(hl.UInt(8), 3)

    # Just an intermediate Func we need to share between generate() and schedule()
    intermediate = hl.Func()

    def __init__(self, context):
        super().__init__(context)
        # Python Generators don't have configure() methods;
        # just add the extra stuff you need in the __init__ method
        self.extra_func_input = hl.InputFunc(hl.UInt(16), 3)
        self.extra_func_output = hl.OutputFunc(hl.Float(64), 2)

    def generate(self):
        self.simple_output[x, y, c] = hl.f32(self.simple_input[x, y, c])
        self.typed_buffer_output[x, y, c] = hl.f32(self.typed_buffer_input[x, y, c])
        self.untyped_buffer_output[x, y, c] = hl.cast(self.untyped_buffer_output.type(), self.untyped_buffer_input[x, y, c])

        # Gratuitous intermediate for the purpose of exercising
        # GeneratorParam<LoopLevel>
        self.intermediate[x, y, c] = self.simple_input[x, y, c] * self.float_arg

        self.tuple_output[x, y, c] = (self.intermediate[x, y, c], self.intermediate[x, y, c] + self.int_arg)

        # This should be compiled into the Generator product itself,
        # and not produce another input for the Stub or AOT filter.
        static_compiled_buffer = hl.Buffer(hl.UInt(8), [4, 4, 1])
        for xx in range(4):
            for yy in range(4):
                for cc in range(1):
                    static_compiled_buffer[xx, yy, cc] = xx + yy + cc + 42

        self.static_compiled_buffer_output = static_compiled_buffer
        self.extra_func_output[x, y] = hl.f64(self.extra_func_input[x, y, 0] + 1)

    def schedule(self):
        self.intermediate.compute_at(self.intermediate_level.value());
        self.intermediate.specialize(self.vectorize.value()).vectorize(x, self.natural_vector_size(hl.Float(32)));

if __name__ == "__main__":
    hl.main()

