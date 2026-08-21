# Loads Halide's GDB pretty-printers when gdb is launched from the repository
# root. GDB only auto-loads a local .gdbinit if you have allowed this directory,
# e.g. by adding to your ~/.gdbinit:
#     add-auto-load-safe-path /path/to/Halide/.gdbinit
# Otherwise, load the helpers manually with:
#     source ./tools/gdbhalide.py
source ./tools/gdbhalide.py
