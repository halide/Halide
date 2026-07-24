import os
import tempfile

import halide as hl


def _read_text(path):
    with open(path, "r") as f:
        return f.read()


def _read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def make_pipeline():
    x = hl.Var("x")
    producer = hl.Func("producer")
    consumer = hl.Func("consumer")
    producer[x] = x * 2
    consumer[x] = producer[x] + producer[x + 1]
    producer.compute_root()
    return hl.Pipeline(consumer)


def main():
    # The three scopes are exposed as an enum.
    assert hl.RuntimeVisibility.Import is not None
    assert hl.RuntimeVisibility.Export is not None
    assert hl.RuntimeVisibility.Internal is not None

    tmpdir = tempfile.mkdtemp()
    host = hl.Target("host")

    # A standalone runtime can be compiled with export/internal prefixes; the
    # emitted object's symbol table should contain the renamed public symbol.
    rt = os.path.join(tmpdir, "rt.o")
    hl.compile_standalone_runtime(
        rt,
        host,
        {
            hl.RuntimeVisibility.Export: "my_ns_",
            hl.RuntimeVisibility.Internal: "my_ns_internal_",
        },
    )
    assert b"my_ns_malloc" in _read_bytes(rt)

    # The namespace_map argument is optional (backward compatible): a plain
    # standalone runtime keeps the stock halide_ names.
    rt_default = os.path.join(tmpdir, "rt_default.o")
    hl.compile_standalone_runtime(rt_default, host)
    assert b"halide_malloc" in _read_bytes(rt_default)

    # A pipeline can be given a matching import prefix and compiled (NoRuntime),
    # after which its runtime calls reference the renamed symbols.
    aot = hl.Target("host-no_runtime")
    p = make_pipeline()
    p.apply_runtime_namespace(aot, {hl.RuntimeVisibility.Import: "my_ns_"})
    ll = os.path.join(tmpdir, "kern.ll")
    p.compile_to({hl.OutputFileType.llvm_assembly: ll}, [], "kern", aot)
    text = _read_text(ll)
    assert "@my_ns_malloc" in text
    assert "@halide_malloc" not in text

    # Runtime namespacing is unsupported for JIT and must raise.
    error = False
    try:
        make_pipeline().apply_runtime_namespace(
            hl.Target("host-jit"), {hl.RuntimeVisibility.Export: "my_ns_"}
        )
    except hl.HalideError:
        error = True
    assert error, "expected an error when requesting a runtime namespace for JIT"

    print("Success!")


if __name__ == "__main__":
    main()
