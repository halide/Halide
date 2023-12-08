# Generator template for making issues.

## Making the generator and emitting the code.

This folder contains an app-like structure, but is meant for making
reproducible codegen issues. By default the generator will emit `stmt`,
`stmt_html`, `conceptual_stmt`, `conceptual_stmt_html`, `device_code`,
`assembly`, `bitcode`.

To use this template, you ...
 1. Duplicate this folder and rename it (reference the issue number in the
    folder name if you have one).
 2. Edit the `generator.cpp` file to produce the issue you wish to
    demonstrate.
 3. Run `make HL_TARGET=... codegen`, with your target triple (e.g.,
    `HL_TARGET=host-cuda`).
 4. Open an issue and paste the generator code.

As such, the everyone with the Halide repository checked out, can copy-paste
your generator code and reproduce the issue with relative ease.

## Running the code.

If you additionally wish to run the code as part of the issue, there is a starter
main-file provided `test.cpp`. There is an additional rule in the Makefile,
which you can run with:

```sh
make HL_TARGET=... run
```

If this is part of the problem demonstration, include this code in the issue.
