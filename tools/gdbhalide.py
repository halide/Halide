# Load this module into GDB by running:
#     source /path/to/Halide/tools/gdbhalide.py
#
# It registers pretty-printers so that Halide's IR (Expr, Stmt, and the
# individual node types), types, targets, modules, and schedules render in a
# human-readable form. As with the LLDB helpers in tools/lldbhalide.py, the
# heavy lifting is done in C++ by Halide::Internal::debug_string() (see
# src/IRPrinter.h): each printer asks the inferior to render the value, so GDB
# and LLDB produce identical output. This means the program must be running
# (stopped at a breakpoint) for the summaries to appear.
import contextlib

import gdb

# Concrete Expr node types (subclasses of Halide::Internal::BaseExprNode).
EXPR_NODES = [
    "IntImm",
    "UIntImm",
    "FloatImm",
    "StringImm",
    "Cast",
    "Reinterpret",
    "Variable",
    "Add",
    "Sub",
    "Mul",
    "Div",
    "Mod",
    "Min",
    "Max",
    "EQ",
    "NE",
    "LT",
    "LE",
    "GT",
    "GE",
    "And",
    "Or",
    "Not",
    "Select",
    "Load",
    "Ramp",
    "Broadcast",
    "Call",
    "Let",
    "Shuffle",
    "VectorReduce",
]

# Concrete Stmt node types (subclasses of Halide::Internal::BaseStmtNode).
STMT_NODES = [
    "LetStmt",
    "AssertStmt",
    "ProducerConsumer",
    "For",
    "Acquire",
    "Store",
    "Provide",
    "Allocate",
    "Free",
    "Realize",
    "Block",
    "Fork",
    "IfThenElse",
    "Evaluate",
    "Prefetch",
    "Atomic",
    "HoistedStorage",
]

# Value types with a Halide::Internal::debug_string() overload.
VALUE_TYPES = [
    "Halide::Type",
    "Halide::Target",
    "Halide::Module",
    "Halide::Tuple",
    "Halide::Internal::Interval",
    "Halide::Internal::ConstantInterval",
    "Halide::Internal::ModulusRemainder",
]

# Reference-counted handles: render by value and expand to the underlying node.
HANDLE_TYPES = ["Halide::Expr", "Halide::Internal::Stmt"]

# Box types whose interesting state lives behind a `contents` IntrusivePtr.
BOX_TYPES = [
    "Halide::Internal::Definition",
    "Halide::Internal::FuncSchedule",
    "Halide::Internal::ReductionDomain",
    "Halide::Internal::StageSchedule",
]

# code -> mnemonic for halide_type_t.
_TYPE_CODES = {0: "int", 1: "uint", 2: "float", 3: "handle", 4: "bfloat"}


def _render_std_string(value):
    """Turn a gdb.Value of type std::string into a plain Python string."""
    try:
        text = value.format_string(symbols=False, address=False)
    except Exception:
        text = str(value)
    text = text.strip()
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        text = text[1:-1]
    return text.replace("\\n", " ")


def _call(expr):
    """Evaluate an inferior expression returning std::string, robustly."""
    try:
        return _render_std_string(gdb.parse_and_eval(expr))
    except gdb.error as e:
        return f"<error: {e}>"


def _addr(val):
    a = val.address
    return None if a is None else int(a)


class DebugStringPrinter:
    """Renders a value via Halide::Internal::debug_string()."""

    def __init__(self, val, typename, by_pointer):
        self.val = val
        self.typename = typename
        self.by_pointer = by_pointer

    def to_string(self):
        addr = _addr(self.val)
        if addr is None:
            return None
        target = f"({self.typename} *){addr}"
        arg = target if self.by_pointer else f"*{target}"
        return _call(f"Halide::Internal::debug_string({arg})")


class HandlePrinter(DebugStringPrinter):
    """An Expr/Stmt handle: summary plus the concrete node as a child."""

    def children(self):
        try:
            ptr = self.val["ptr"]
        except gdb.error:
            return
        if ptr is None or int(ptr) == 0:
            return
        node = ptr.dereference()
        with contextlib.suppress(gdb.error):
            node = node.cast(node.dynamic_type)
        yield ("ptr", node)


class UnwrapPrinter:
    """A box type: expose the members behind its `contents` pointer."""

    def __init__(self, val, member="contents"):
        self.val = val
        self.member = member

    def children(self):
        try:
            ptr = self.val[self.member]["ptr"]
        except gdb.error:
            return
        if ptr is None or int(ptr) == 0:
            return
        yield ("contents", ptr.dereference())


class NamePrinter:
    """Summarises a value to its name() (Func, Buffer, Var, RVar)."""

    def __init__(self, val, typename):
        self.val = val
        self.typename = typename

    def to_string(self):
        addr = _addr(self.val)
        if addr is None:
            return None
        return _call(f"(({self.typename} *){addr})->name()")


class BufferNamePrinter:
    """Summarises a Halide::Buffer<T> to its name, read from its contents.

    Buffer<T>::name() is an inline template method that may not be instantiated
    in the program, so navigate to the name field directly."""

    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            ptr = self.val["contents"]["ptr"]
            if ptr is None or int(ptr) == 0:
                return "<undefined buffer>"
            return _render_std_string(ptr.dereference()["name"])
        except gdb.error as e:
            return f"<error: {e}>"


class HalideTypeTPrinter:
    def __init__(self, val):
        self.val = val

    def to_string(self):
        try:
            code = int(self.val["code"])
            bits = int(self.val["bits"])
            lanes = int(self.val["lanes"])
        except gdb.error as e:
            return f"<error: {e}>"
        base = f"{_TYPE_CODES.get(code, code)}{bits}"
        return base if lanes == 1 else f"{base}x{lanes}"


def _type_name(val):
    """Fully-qualified tag of val's type, with typedefs/refs/cv stripped."""
    t = val.type.strip_typedefs()
    if t.code == gdb.TYPE_CODE_REF:
        t = t.target()
    t = t.unqualified()
    return t.tag


def lookup(val):
    name = _type_name(val)
    if name is None:
        return None

    if name in EXPR_NODES_Q or name in STMT_NODES_Q:
        return DebugStringPrinter(val, name, by_pointer=True)
    if name in HANDLE_TYPES:
        return HandlePrinter(val, name, by_pointer=False)
    if name in VALUE_TYPES:
        return DebugStringPrinter(val, name, by_pointer=False)
    if name in BOX_TYPES:
        return UnwrapPrinter(val)
    if name in ("Halide::Func", "Halide::Var", "Halide::RVar"):
        return NamePrinter(val, name)
    if name.startswith("Halide::Buffer<"):
        return BufferNamePrinter(val)
    if name == "halide_type_t":
        return HalideTypeTPrinter(val)
    return None


# Fully-qualified node names for fast membership tests.
EXPR_NODES_Q = frozenset(f"Halide::Internal::{n}" for n in EXPR_NODES)
STMT_NODES_Q = frozenset(f"Halide::Internal::{n}" for n in STMT_NODES)


def register(objfile=None):
    where = gdb if objfile is None else objfile
    # Remove any previous copy so re-sourcing this file is idempotent.
    where.pretty_printers = [
        p for p in where.pretty_printers if getattr(p, "name", None) != "halide"
    ]
    lookup.name = "halide"
    where.pretty_printers.append(lookup)


register()
