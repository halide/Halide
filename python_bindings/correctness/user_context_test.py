import array
import user_context


def test():
    output = bytearray("\0\0\0\0", "ascii")
    user_context.user_context(None, ord('q'), output)
    assert output == bytearray("qqqq", "ascii")


if __name__ == "__main__":
    test()
