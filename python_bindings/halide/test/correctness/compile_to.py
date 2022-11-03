import os, shutil, tempfile
import halide as hl


def main():
    x = hl.Var("x")

    f = hl.Func("f")
    f[x] = 100 * x

    args = []

    tmpdir = tempfile.mkdtemp()
    try:
        p = os.path.join(tmpdir, "f.bc")
        f.compile_to_bitcode(p, args, "f")
        assert os.path.isfile(p)

        p = os.path.join(tmpdir, "f.cpp")
        f.compile_to_c(p, args, "f")
        assert os.path.isfile(p)

        p = os.path.join(tmpdir, "f.o")
        f.compile_to_object(p, args, "f")
        assert os.path.isfile(p)

        p = os.path.join(tmpdir, "f.h")
        f.compile_to_header(p, args, "f")
        assert os.path.isfile(p)

        p = os.path.join(tmpdir, "f.s")
        f.compile_to_assembly(p, args, "f")
        assert os.path.isfile(p)

        p = os.path.join(tmpdir, "f.txt")
        f.compile_to_lowered_stmt(p, args)
        assert os.path.isfile(p)

        f.compile_to_file(os.path.join(tmpdir, "f_all"), args)
        assert os.path.isfile(os.path.join(tmpdir, "f_all.h"))
        if hl.get_target_from_environment().os == hl.TargetOS.Windows:
            assert os.path.isfile(os.path.join(tmpdir, "f_all.obj"))
        else:
            assert os.path.isfile(os.path.join(tmpdir, "f_all.o"))

        p = os.path.join(tmpdir, "f.html")
        f.compile_to({hl.OutputFileType.stmt_html: p}, args, "f")
        assert os.path.isfile(p)

    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)


if __name__ == "__main__":
    main()
