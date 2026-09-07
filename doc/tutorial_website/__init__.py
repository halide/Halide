"""Generates the Halide tutorial's Sphinx pages from tutorial/lesson_*.cpp.

Captures the output of interesting statements (Func::realize(), Pipeline::
print_loop_nest(), etc.) by driving either GDB or LLDB in batch mode against
the already-built lesson binaries, then renders each lesson as a MyST page
with the captured output inlined as collapsible <details> blocks.
"""
