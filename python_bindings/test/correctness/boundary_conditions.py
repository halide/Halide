import halide as hl

test_exterior = 42
test_min = -25
test_extent = 100

x, y = hl.vars("x y")


def expect_eq(actual, expected):
    assert expected == actual, "Failed: expected %d, actual %d" % (expected, actual)


def schedule_test(f, vector_width, target, partition_policy):
    if vector_width != 1:
        f.vectorize(x, vector_width)

    f.partition(x, partition_policy);
    f.partition(y, partition_policy);

    if target.has_gpu_feature() and vector_width <= 16:
        xo, yo, xi, yi = hl.vars("xo yo xi yi")
        f.gpu_tile(x, y, xo, yo, xi, yi, 2, 2)


def realize_and_check(
    f,
    checker,
    input,
    test_min_x,
    test_extent_x,
    test_min_y,
    test_extent_y,
    vector_width,
    target,
    partition_policy,
):
    result = hl.Buffer(hl.UInt(8), [test_extent_x, test_extent_y])
    result.set_min([test_min_x, test_min_y])
    f2 = hl.lambda_func(x, y, f[x, y])
    schedule_test(f2, vector_width, target, partition_policy)
    f2.realize(result, target)
    result.copy_to_host()
    for r in range(test_min_y, test_min_y + test_extent_y):
        for c in range(test_min_x, test_min_x + test_extent_x):
            checker(input, result, c, r)


def check_constant_exterior(input, result, c, r):
    if c < 0 or r < 0 or c >= input.width() or r >= input.height():
        expect_eq(result[c, r], test_exterior)
    else:
        expect_eq(result[c, r], input[c, r])


def check_repeat_edge(input, result, c, r):
    clamped_y = min(input.height() - 1, max(0, r))
    clamped_x = min(input.width() - 1, max(0, c))
    expect_eq(result[c, r], input[clamped_x, clamped_y])


def check_repeat_image(input, result, c, r):
    mapped_x = c
    mapped_y = r
    while mapped_x < 0:
        mapped_x += input.width()
    while mapped_x > input.width() - 1:
        mapped_x -= input.width()
    while mapped_y < 0:
        mapped_y += input.height()
    while mapped_y > input.height() - 1:
        mapped_y -= input.height()
    expect_eq(result[c, r], input[mapped_x, mapped_y])


def check_mirror_image(input, result, c, r):
    mapped_x = -(c + 1) if c < 0 else c
    mapped_x = mapped_x % (2 * input.width())
    if mapped_x > (input.width() - 1):
        mapped_x = (2 * input.width() - 1) - mapped_x
    mapped_y = -(r + 1) if r < 0 else r
    mapped_y = mapped_y % (2 * input.height())
    if mapped_y > (input.height() - 1):
        mapped_y = (2 * input.height() - 1) - mapped_y
    expect_eq(result[c, r], input[mapped_x, mapped_y])


def check_mirror_interior(input, result, c, r):
    mapped_x = abs(c) % (input.width() * 2 - 2)
    if mapped_x > input.width() - 1:
        mapped_x = input.width() * 2 - 2 - mapped_x
    mapped_y = abs(r) % (input.height() * 2 - 2)
    if mapped_y > input.height() - 1:
        mapped_y = input.height() * 2 - 2 - mapped_y
    expect_eq(result[c, r], input[mapped_x, mapped_y])


def test_all(vector_width, target, partition_policy):
    # print("target is %s, partition_policy is %s " % (str(target), str(partition_policy)))

    W = 32
    H = 32
    input = hl.Buffer(hl.UInt(8), [W, H])
    for r in range(H):
        for c in range(W):
            input[c, r] = (c + r * W) & 0xFF

    input_f = hl.Func()
    input_f[x, y] = input[x, y]

    tests = [
        (hl.BoundaryConditions.constant_exterior, check_constant_exterior),
        (hl.BoundaryConditions.repeat_edge, check_repeat_edge),
        (hl.BoundaryConditions.repeat_image, check_repeat_image),
        (hl.BoundaryConditions.mirror_image, check_mirror_image),
        (hl.BoundaryConditions.mirror_interior, check_mirror_interior),
    ]

    for bc, checker in tests:
        # print('  Testing %s:%d...' % (bc.__name__, vector_width))
        func_input_args = {"f": input_f, "bounds": [(0, W), (0, H)]}
        image_input_args = {"f": input, "bounds": [(0, W), (0, H)]}
        undef_min_args = {"f": input, "bounds": [(hl.Expr(), hl.Expr()), (0, H)]}
        undef_max_args = {"f": input, "bounds": [(0, W), (hl.Expr(), hl.Expr())]}
        implicit_bounds_args = {"f": input}

        if bc == hl.BoundaryConditions.constant_exterior:
            func_input_args["exterior"] = test_exterior
            image_input_args["exterior"] = test_exterior
            undef_min_args["exterior"] = test_exterior
            undef_max_args["exterior"] = test_exterior
            implicit_bounds_args["exterior"] = test_exterior

        realize_and_check(
            bc(**func_input_args),
            checker,
            input,
            test_min,
            test_extent,
            test_min,
            test_extent,
            vector_width,
            target,
            partition_policy,
        )
        realize_and_check(
            bc(**image_input_args),
            checker,
            input,
            test_min,
            test_extent,
            test_min,
            test_extent,
            vector_width,
            target,
            partition_policy,
        )
        realize_and_check(
            bc(**undef_min_args),
            checker,
            input,
            0,
            W,
            test_min,
            test_extent,
            vector_width,
            target,
            partition_policy,
        )
        realize_and_check(
            bc(**undef_max_args),
            checker,
            input,
            test_min,
            test_extent,
            0,
            H,
            vector_width,
            target,
            partition_policy,
        )
        realize_and_check(
            bc(**implicit_bounds_args),
            checker,
            input,
            test_min,
            test_extent,
            test_min,
            test_extent,
            vector_width,
            target,
            partition_policy,
        )


if __name__ == "__main__":
    target = hl.get_jit_target_from_environment()

    vector_width_power_max = 6
    # https://github.com/halide/Halide/issues/2148
    if target.has_feature(hl.TargetFeature.Metal) or \
        target.has_feature(hl.TargetFeature.Vulkan) or \
        target.has_feature(hl.TargetFeature.OpenGLCompute) or \
        target.has_feature(hl.TargetFeature.D3D12Compute):
        vector_width_power_max = 2

    for i in range(0, vector_width_power_max):
        vector_width = 1 << i
        test_all(vector_width, target, hl.Partition.Auto)
        test_all(vector_width, target, hl.Partition.Never)
