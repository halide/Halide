import halide as hl


def test_rdom():
    x = hl.Var("x")
    y = hl.Var("y")

    diagonal = hl.Func("diagonal")
    diagonal[x, y] = 1

    domain_width = 10
    domain_height = 10

    r = hl.RDom([(0, domain_width), (0, domain_height)])
    r.where(r.x <= r.y)

    diagonal[r.x, r.y] += 2
    output = diagonal.realize([domain_width, domain_height])

    for iy in range(domain_height):
        for ix in range(domain_width):
            if ix <= iy:
                assert output[ix, iy] == 3
            else:
                assert output[ix, iy] == 1

    assert r.x.name() == r[0].name()
    assert r.y.name() == r[1].name()
    try:
        r[-1].name()
        raise Exception("underflowing index should raise KeyError")
    except KeyError:
        pass
    try:
        r[2].name()
        raise Exception("overflowing index should raise KeyError")
    except KeyError:
        pass
    try:
        r["foo"].name()
        raise Exception("bad index type should raise TypeError")
    except TypeError:
        pass

    return 0


if __name__ == "__main__":
    test_rdom()
