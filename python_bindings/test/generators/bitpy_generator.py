import halide as hl

x = hl.Var("x")
y = hl.Var("y")


@hl.generator(name="bitpy")
class BitGenerator:
    # We can use `bool` as an alias for `hl.Bool()` if we like
    bit_input = hl.InputBuffer(bool, 1)
    bit_constant = hl.InputScalar(bool)

    bit_output = hl.OutputBuffer(hl.Bool(), 1)

    def generate(self):
        g = self
        g.bit_output[x] = g.bit_input[x] | g.bit_constant


if __name__ == "__main__":
    hl.main()
