# Load this module into LLDB by running:
#     command script import /path/to/Halide/tools/lldbhalide.py
#
# It registers type summaries and synthetic children providers so that Halide's
# IR (Expr, Stmt, and the individual node types), types, targets, modules, and
# schedules render in a human-readable form. The heavy lifting is done in C++ by
# Halide::Internal::debug_string(); see src/IRPrinter.h. The sibling module
# tools/gdbhalide.py provides the same coverage for GDB.
import functools

import lldb

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

# Value types with a Halide::Internal::debug_string() overload that renders them
# directly (no need to unwrap a handle first).
VALUE_TYPES = [
    "Halide::Type",
    "Halide::Target",
    "Halide::Module",
    "Halide::Tuple",
    "Halide::Internal::Interval",
    "Halide::Internal::ConstantInterval",
    "Halide::Internal::ModulusRemainder",
]


def normalize(raw):
    return raw.lstrip('"').rstrip('"').replace(r"\n", " ").replace("  ", " ")


def summary_string(summary_fn):
    @functools.wraps(summary_fn)
    def wrapper(value, _):
        if value is None or not value.IsValid():
            return "<invalid>"

        try:
            return normalize(summary_fn(value).GetSummary())
        except Exception as e:
            return f"<error:{e},{value}>"

    return wrapper


@summary_string
def call_name(value):
    return value.EvaluateExpression("this->name()", lldb.SBExpressionOptions())


@summary_string
def call_buffer_name(value):
    # Halide::Buffer<T>::name() is an inline template method that may not be
    # instantiated in the program, so read the name field directly instead of
    # calling it. contents is an IntrusivePtr<BufferContents>.
    return value.EvaluateExpression(
        "this->contents.ptr->name", lldb.SBExpressionOptions()
    )


@summary_string
def call_debug_string(value):
    # Render a handle or value type by value: debug_string(*this).
    return value.EvaluateExpression(
        "Halide::Internal::debug_string(*this)", lldb.SBExpressionOptions()
    )


@summary_string
def call_debug_string_ptr(value):
    # Render a concrete IR node by pointer: debug_string(this). A raw node (e.g.
    # a Halide::Internal::Add) is not implicitly convertible to Expr/Stmt, but
    # its address converts to BaseExprNode*/BaseStmtNode*, which debug_string
    # accepts.
    return value.EvaluateExpression(
        "Halide::Internal::debug_string(this)", lldb.SBExpressionOptions()
    )


class ProxyChildrenProvider:
    def __init__(self, valobj, _):
        self.inner = valobj
        self.update()

    def update(self):
        pass

    def num_children(self):
        return self.inner.GetNumChildren()

    def get_child_index(self, name):
        return self.inner.GetIndexOfChildWithName(name)

    def get_child_at_index(self, index):
        return self.inner.GetChildAtIndex(index)


class IRChildrenProvider(ProxyChildrenProvider):
    def __init__(self, valobj, _):
        super().__init__(valobj.GetChildMemberWithName("ptr"), None)


class BoxChildrenProvider(IRChildrenProvider):
    def __init__(self, valobj, _):
        super().__init__(valobj.GetChildMemberWithName("contents"), None)


class FunctionChildrenProvider(ProxyChildrenProvider):
    def __init__(self, valobj, _):
        contents = valobj.EvaluateExpression(
            "*this->contents.get()", lldb.SBExpressionOptions()
        )
        super().__init__(contents, None)


def __lldb_init_module(debugger, _):
    # Concrete IR node types render via debug_string(this).
    for ty in EXPR_NODES + STMT_NODES:
        debugger.HandleCommand(
            f"type summary add Halide::Internal::{ty} "
            f"--python-function lldbhalide.call_debug_string_ptr"
        )

    # The Expr/Stmt handles render by value and expand into their node contents.
    for ty in ("Expr", "Internal::Stmt"):
        debugger.HandleCommand(
            f"type summary add Halide::{ty} --python-function lldbhalide.call_debug_string"
        )
        debugger.HandleCommand(
            f"type synthetic add Halide::{ty} -l lldbhalide.IRChildrenProvider"
        )

    # Value types that debug_string() can render directly.
    for ty in VALUE_TYPES:
        debugger.HandleCommand(
            f"type summary add {ty} --python-function lldbhalide.call_debug_string"
        )

    for ty in ("Definition", "FuncSchedule", "ReductionDomain", "StageSchedule"):
        debugger.HandleCommand(
            f"type synthetic add Halide::Internal::{ty} -l lldbhalide.BoxChildrenProvider"
        )

    debugger.HandleCommand(
        "type synthetic add Halide::Internal::Function -l lldbhalide.FunctionChildrenProvider"
    )

    debugger.HandleCommand("type summary add Halide::Internal::Dim -s '${var.var%S}'")
    debugger.HandleCommand(
        "type summary add Halide::RVar --python-function lldbhalide.call_name"
    )
    debugger.HandleCommand(
        "type summary add Halide::Var --python-function lldbhalide.call_name"
    )

    # Func and (typed) Buffer summarise to their name; their data is inspected
    # through synthetic children (Func) or the Natvis image viewer (Buffer).
    debugger.HandleCommand(
        "type summary add Halide::Func --python-function lldbhalide.call_name"
    )
    debugger.HandleCommand(
        'type summary add -x "^Halide::Buffer<.*>$" '
        "--python-function lldbhalide.call_buffer_name"
    )

    debugger.HandleCommand(
        "type summary add halide_type_t -s '${var.code%S} bits=${var.bits%u} lanes=${var.lanes%u}'"
    )
    debugger.HandleCommand(
        "type summary add Halide::Internal::RefCount -s ${var.count.Value%S}"
    )
