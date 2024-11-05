# Load this module into LLDB by running:
#     command script import /path/to/Halide/tools/lldbhalide.py

import lldb


def normalize(raw):
    return raw.lstrip('"').rstrip('"').replace(r'\n', ' ').replace('  ', ' ')


def addr(value):
    if ptr := value.GetValueAsUnsigned(0):
        return f"0x{ptr:x}"
    if ptr := value.AddressOf().GetValueAsUnsigned(0):
        return f"0x{ptr:x}"
    raise ValueError(f'Could not determine address for: {value}')


def expr_summary(value, _):
    if value is None or not value.IsValid():
        return f"<invalid>"
    try:
        raw = value.target.EvaluateExpression(
            f"Halide::Internal::lldb_string(*(Halide::Expr*){addr(value)})",
            lldb.SBExpressionOptions()
        ).GetSummary()
        return normalize(raw)
    except Exception as e:
        return f"<expr/error:{value},{e}>"


def baseexpr_summary(value, _):
    if value is None or not value.IsValid():
        return f"<invalid>"

    try:
        raw = value.target.EvaluateExpression(
            f"Halide::Internal::lldb_string((const Halide::Internal::BaseExprNode*){addr(value)})",
            lldb.SBExpressionOptions()
        ).GetSummary()
        return normalize(raw)
    except Exception as e:
        return f"<baseexpr/error:{value},{e}>"


def stmt_summary(value, _):
    if value is None or not value.IsValid():
        return "<invalid>"

    try:
        raw = value.target.EvaluateExpression(
            f"Halide::Internal::lldb_string(*(Halide::Internal::Stmt*){addr(value)})",
            lldb.SBExpressionOptions()
        ).GetSummary()
        return normalize(raw)
    except Exception as e:
        return f"<stmt/error:{value},{e}>"


class IRChildrenProvider:
    def __init__(self, valobj, _):
        self.inner = valobj.GetChildMemberWithName("ptr")
        self.update()

    def update(self):
        pass

    def num_children(self):
        return self.inner.GetNumChildren()

    def get_child_index(self, name):
        return self.inner.GetIndexOfChildWithName(name)

    def get_child_at_index(self, index):
        return self.inner.GetChildAtIndex(index)


def __lldb_init_module(debugger, _):
    base_exprs = ["Add", "And", "Broadcast", "Call", "Cast", "Div", "EQ", "GE", "GT", "LE", "LT", "Let", "Load", "Max",
                  "Min", "Mod", "Mul", "NE", "Not", "Or", "Ramp", "Reinterpret", "Select", "Shuffle", "Sub", "Variable",
                  "VectorReduce"]

    for expr in base_exprs:
        debugger.HandleCommand(
            f"type summary add Halide::Internal::{expr} --python-function lldbhalide.baseexpr_summary"
        )

    debugger.HandleCommand(
        "type summary add Halide::Expr --python-function lldbhalide.expr_summary"
    )
    debugger.HandleCommand(
        'type synthetic add Halide::Expr -l lldbhalide.IRChildrenProvider'
    )

    debugger.HandleCommand(
        "type summary add Halide::Internal::Stmt --python-function lldbhalide.stmt_summary"
    )
    debugger.HandleCommand(
        'type synthetic add Halide::Internal::Stmt -l lldbhalide.IRChildrenProvider'
    )

    debugger.HandleCommand("type summary add halide_type_t -s '${var.code%S} bits=${var.bits%u} lanes=${var.lanes%u}'")
    debugger.HandleCommand("type summary add Halide::Internal::RefCount -s ${var.count.Value%S}")
