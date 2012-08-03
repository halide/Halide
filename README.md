Getting Started
----------------
What you probably want as a user of Halide is one of our [binary releases](http://github.com/halide/Halide/downloads).

Once you have that, check out the [Getting Started tutorial](https://github.com/halide/Halide/wiki/Getting-Started).

Using Halide just requires including a single header and linking against a self-contained static library which includes our entire compiler. Given that, you can JIT or statically compile Halide programs without needing anything else.

Building the Halide compiler from source currently requires some occasionally hairy external dependencies. We're working on streamlining the process for major platforms (Mac & Ubuntu now, Windows to follow) as we speak. You're still very welcome to spelunk through the code, but again, you can do anything short of modify the compiler with one of the [binary releases](http://github.com/halide/Halide/downloads).

Be aware that this is very much a research compiler. There are plenty of rough edges, and a simple syntax error in one place might cause a segfault somewhere else. We're working hard to make it more user-friendly, and if you do have trouble please feel free to raise it on our [bug tracker](http://github.com/halide/Halide/issues), or on the [halide-dev list](https://lists.csail.mit.edu/mailman/listinfo/halide-dev).

Resources
----------
- Check out [our first paper](http://people.csail.mit.edu/jrk/halide12).
- Subscribe to [halide-announce](https://lists.csail.mit.edu/mailman/listinfo/halide-announce) to hear about releases. - Subscribe to [halide-dev](https://lists.csail.mit.edu/mailman/listinfo/halide-dev) to discuss technical issues.
