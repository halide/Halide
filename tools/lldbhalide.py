# Load this module into LLDB by running:
#     command script import /path/to/Halide/tools/lldbhalide.py
import functools

import lldb


def normalize(raw):
    return raw.lstrip('"').rstrip('"').replace(r'\n', ' ').replace('  ', ' ')


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
def call_lldb_string(value):
    return value.EvaluateExpression("Halide::Internal::lldb_string(*this)", lldb.SBExpressionOptions())


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
        contents = valobj.EvaluateExpression("*this->contents.get()", lldb.SBExpressionOptions())
        print(contents)
        super().__init__(contents, None)


def __lldb_init_module(debugger, _):
    base_exprs = ["Add", "And", "Broadcast", "Call", "Cast", "Div", "EQ", "GE", "GT", "LE", "LT", "Let", "Load", "Max",
                  "Min", "Mod", "Mul", "NE", "Not", "Or", "Ramp", "Reinterpret", "Select", "Shuffle", "Sub", "Variable",
                  "VectorReduce"]

    for ty in base_exprs:
        debugger.HandleCommand(
            f"type summary add Halide::Internal::{ty} --python-function lldbhalide.call_lldb_string"
        )

    for ty in ('Expr', 'Internal::Stmt'):
        debugger.HandleCommand(
            f"type summary add Halide::{ty} --python-function lldbhalide.call_lldb_string"
        )
        debugger.HandleCommand(
            f'type synthetic add Halide::{ty} -l lldbhalide.IRChildrenProvider'
        )

    for ty in ("Definition", "FuncSchedule", "ReductionDomain", "StageSchedule"):
        debugger.HandleCommand(
            f"type synthetic add Halide::Internal::{ty} -l lldbhalide.BoxChildrenProvider"
        )

    debugger.HandleCommand(
        'type synthetic add Halide::Internal::Function -l lldbhalide.FunctionChildrenProvider'
    )

    debugger.HandleCommand("type summary add Halide::Internal::Dim -s '${var.var%S}'")
    debugger.HandleCommand("type summary add Halide::RVar --python-function lldbhalide.call_name")
    debugger.HandleCommand("type summary add Halide::Var --python-function lldbhalide.call_name")

    debugger.HandleCommand("type summary add halide_type_t -s '${var.code%S} bits=${var.bits%u} lanes=${var.lanes%u}'")
    debugger.HandleCommand("type summary add Halide::Internal::RefCount -s ${var.count.Value%S}")
