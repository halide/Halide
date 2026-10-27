import halide as hl


def test_profiler_scope():
    target = hl.get_jit_target_from_environment()
    if target.arch == hl.TargetArch.WebAssembly:
        print("[SKIP] Profiler state is not accessible under WebAssembly.")
        return
    target = target.with_feature(hl.TargetFeature.Profile)

    x, y = hl.Var("x"), hl.Var("y")
    g = hl.Func("g_profiled")
    f = hl.Func("f_profiled")
    g[x, y] = x + y
    f[x, y] = g[x, y] * 2
    g.compute_root()

    size = 256
    with hl.ProfilerScope(f) as scope:
        assert scope.pipeline_stats() is None

        for _ in range(3):
            f.realize([size, size], target)

        p = scope.pipeline_stats()
        assert p is not None
        assert p.runs == 3
        assert p.name == f.name()
        assert any(fs.name == g.name() for fs in p.funcs)

        gs = scope.func_stats(g)
        assert gs is not None
        assert gs.kind == hl.ProfilerFuncKind.Func
        assert gs.num_allocs == 3
        assert gs.memory_peak == size * size * 4
        assert gs.memory_total == 3 * size * size * 4
        assert scope.func_stats(g.name()).num_allocs == gs.num_allocs
        assert scope.func_stats("nonexistent") is None

    # Snapshots outlive the scope.
    assert gs.num_allocs == 3

    try:
        scope.pipeline_stats()
        raise AssertionError("Expected an error after the scope exited")
    except RuntimeError:
        pass

    # Explicitly constructing a Pipeline works too, and the scope's exit
    # reset the stats.
    pipe = hl.Pipeline(f)
    with hl.ProfilerScope(pipe) as scope:
        pipe.realize([size, size], target)
        assert scope.pipeline_stats().runs == 1


def main():
    test_profiler_scope()


if __name__ == "__main__":
    main()
