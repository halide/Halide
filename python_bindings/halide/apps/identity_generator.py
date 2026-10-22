import halide as hl


@hl.generator(name="identity")
class Identity:
    output = hl.OutputBuffer(hl.Int(32), 1)

    def generate(self):
        x = hl.Var("x")
        self.output[x] = x


if __name__ == "__main__":
    hl.main()
