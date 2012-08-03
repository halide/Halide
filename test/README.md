Summary
--------
Tests are largely run through the C++ front end, and managed with the `run_test` script.

Tests are defined in `cpp/<test_name>/test.cpp`, and run with `./run_test cpp/<test_name>`. The `run_test` script also works with multiple arguments, so you can run all tests with:

    $ ./run_test cpp/*
    ......E....E......................

or a selection of simple tests, e.g.:

    $ ./run_test cpp/bounds* cpp/chunk
    .....

Results for each test, as well as the generated code and any trace data, are logged in the corresponding test directory:

    $ cd cpp/bounds
    $ ls
    a.out
    err.txt
    f0.sexp
    f1.sexp
    f2.sexp
    kernels.bc
    kernels.ptx
    log.txt
    passes.txt
    test.cpp
