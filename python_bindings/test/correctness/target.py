import halide as hl


def test_target():
    # Target("") should be exactly like get_host_target().
    t1 = hl.get_host_target()
    t2 = hl.Target("")
    assert t1 == t2, "Default ctor failure"
    assert t1.supported()

    # to_string roundtripping
    t1 = hl.Target()
    ts = t1.to_string()
    assert ts == "arch_unknown-0-os_unknown"

    # Note, this should *not* validate, since validate_target_string
    # now returns false if any of arch-bits-os are undefined
    assert not hl.Target.validate_target_string(ts)

    # Don't attempt to roundtrip this: trying to create
    # a Target with unknown portions will now assert-fail.
    #
    # t2 = hl.Target(ts)
    # assert t2 == t1

    # repr() and str()
    assert str(t1) == "arch_unknown-0-os_unknown"
    assert repr(t1) == "<halide.Target arch_unknown-0-os_unknown>"

    assert t1.os == hl.TargetOS.OSUnknown
    assert t1.arch == hl.TargetArch.ArchUnknown
    assert t1.bits == 0

    # Full specification round-trip:
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [hl.TargetFeature.SSE41])
    ts = t1.to_string()
    assert ts == "x86-32-linux-sse41"
    assert hl.Target.validate_target_string(ts)

    # Full specification (without features) round-trip:
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32)
    ts = t1.to_string()
    assert ts == "x86-32-linux"
    assert hl.Target.validate_target_string(ts)

    # Full specification round-trip, crazy features
    t1 = hl.Target(
        hl.TargetOS.Android,
        hl.TargetArch.ARM,
        32,
        [
            hl.TargetFeature.JIT,
            hl.TargetFeature.SSE41,
            hl.TargetFeature.AVX,
            hl.TargetFeature.AVX2,
            hl.TargetFeature.CUDA,
            hl.TargetFeature.OpenCL,
            hl.TargetFeature.OpenGLCompute,
            hl.TargetFeature.Debug,
        ],
    )
    ts = t1.to_string()
    assert ts == "arm-32-android-avx-avx2-cuda-debug-jit-opencl-openglcompute-sse41"
    assert hl.Target.validate_target_string(ts)

    # Expected failures:
    ts = "host-unknowntoken"
    assert not hl.Target.validate_target_string(ts)

    ts = "x86-23"
    assert not hl.Target.validate_target_string(ts)

    # bits == 0 is allowed only if arch_unknown and os_unknown are specified,
    # and no features are set
    ts = "x86-0"
    assert not hl.Target.validate_target_string(ts)

    ts = "0-arch_unknown-os_unknown-sse41"
    assert not hl.Target.validate_target_string(ts)

    # "host" is only supported as the first token
    ts = "opencl-host"
    assert not hl.Target.validate_target_string(ts)

    # set_feature
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [hl.TargetFeature.SSE41])
    assert t1.has_feature(hl.TargetFeature.SSE41)
    assert not t1.has_feature(hl.TargetFeature.AVX)
    t1.set_feature(hl.TargetFeature.AVX)
    t1.set_feature(hl.TargetFeature.SSE41, False)
    assert t1.has_feature(hl.TargetFeature.AVX)
    assert not t1.has_feature(hl.TargetFeature.SSE41)

    # set_features
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [hl.TargetFeature.SSE41])
    assert t1.has_feature(hl.TargetFeature.SSE41)
    assert not t1.has_feature(hl.TargetFeature.AVX)
    t1.set_features([hl.TargetFeature.SSE41], False)
    t1.set_features([hl.TargetFeature.AVX, hl.TargetFeature.AVX2], True)
    assert t1.has_feature(hl.TargetFeature.AVX)
    assert t1.has_feature(hl.TargetFeature.AVX2)
    assert not t1.has_feature(hl.TargetFeature.SSE41)

    # with_feature
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [hl.TargetFeature.SSE41])
    t2 = t1.with_feature(hl.TargetFeature.NoAsserts).with_feature(
        hl.TargetFeature.NoBoundsQuery
    )
    ts = t2.to_string()
    assert ts == "x86-32-linux-no_asserts-no_bounds_query-sse41"

    # without_feature
    t1 = hl.Target(
        hl.TargetOS.Linux,
        hl.TargetArch.X86,
        32,
        [hl.TargetFeature.SSE41, hl.TargetFeature.NoAsserts],
    )
    # Note that NoBoundsQuery wasn't set here, so 'without' is a no-op
    t2 = t1.without_feature(hl.TargetFeature.NoAsserts).without_feature(
        hl.TargetFeature.NoBoundsQuery
    )
    ts = t2.to_string()
    assert ts == "x86-32-linux-sse41"

    # natural_vector_size
    # SSE4.1 is 16 bytes wide
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [hl.TargetFeature.SSE41])
    assert t1.natural_vector_size(hl.UInt(8)) == 16
    assert t1.natural_vector_size(hl.Int(16)) == 8
    assert t1.natural_vector_size(hl.UInt(32)) == 4
    assert t1.natural_vector_size(hl.Float(32)) == 4

    # has_gpu_feature
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [hl.TargetFeature.OpenCL])
    t2 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, [])
    assert t1.has_gpu_feature()
    assert not t2.has_gpu_feature()

    # has_large_buffers & maximum_buffer_size
    t1 = hl.Target(
        hl.TargetOS.Linux,
        hl.TargetArch.X86,
        64,
        [hl.TargetFeature.LargeBuffers],
    )
    t2 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 64, [])
    assert t1.has_large_buffers()
    assert t1.maximum_buffer_size() == 9223372036854775807
    assert not t2.has_large_buffers()
    assert t2.maximum_buffer_size() == 2147483647

    # supports_device_api
    t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 64, [hl.TargetFeature.CUDA])
    t2 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 64)
    assert t1.supports_device_api(hl.DeviceAPI.CUDA)
    assert not t2.supports_device_api(hl.DeviceAPI.CUDA)

    # supports_type (deprecated version)
    t1 = hl.Target(hl.TargetOS.OSX, hl.TargetArch.X86, 64, [hl.TargetFeature.Metal])
    t2 = hl.Target(hl.TargetOS.OSX, hl.TargetArch.X86, 64)
    assert not t1.supports_type(hl.Float(64))
    assert t2.supports_type(hl.Float(64))

    # supports_type (preferred version)
    t1 = hl.Target(hl.TargetOS.OSX, hl.TargetArch.X86, 64, [hl.TargetFeature.Metal])
    t2 = hl.Target(hl.TargetOS.OSX, hl.TargetArch.X86, 64)
    assert not t1.supports_type(hl.Float(64), hl.DeviceAPI.Metal)
    assert not t2.supports_type(hl.Float(64), hl.DeviceAPI.Metal)

    # target_feature_for_device_api
    assert (
        hl.target_feature_for_device_api(hl.DeviceAPI.OpenCL) == hl.TargetFeature.OpenCL
    )

    # with_feature with non-convertible lists
    try:
        t1 = hl.Target(hl.TargetOS.Linux, hl.TargetArch.X86, 32, ["this is a string"])
    except TypeError as e:
        assert "incompatible constructor arguments" in str(e)
    else:
        assert False, "Did not see expected exception!"


if __name__ == "__main__":
    test_target()
