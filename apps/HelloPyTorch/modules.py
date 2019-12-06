"""Wrap our operator (and gradient) in autograd."""

# We need to import torch before loading the custom modules
import torch as th
import halide_ops as ops

# TODO(mgharbi): maybe find a way to wrap function and module directly in C++
# instead of generating the C++ wrapper on the fly?

def _dispatch(opname, optype=th.float32, cuda=False):
    """
    Helper function that matches an opname and type to the Halide backend.

    This is based on the naming convention we use in this example. Functions are
    named: <opname>[_cuda]_<optype>.

    Args:
      opname(str): name of the base Halide function.
      optype(torch.dtype): pytorch's tensor datatype.
      cuda(bool): whether the operator should use cuda.

    Returns:
      op: a python function wrapping the requested Halide operator.
    """

    assert type(opname) == str, "opname should be a string"
    assert type(optype) == th.dtype, "optype should be a tensor datatype (torch.dtype)"

    if cuda:
        opname += "_cuda"

    if optype == th.float32:
        opname += "_float32"
    elif optype == th.float64:
        opname += "_float64"
    else:
        raise ValueError("Optype %s not recognized %s" % optype)
    op = getattr(ops, opname)
    if not hasattr(ops, opname):
        raise ValueError("Module has no operator %s" % opname)
    return op

def _forward_common(ctx, input_a, input_b):
    tp = input_a.dtype
    cuda = input_a.is_cuda
    assert tp == input_b.dtype, "inputs should have the same type"
    assert cuda == input_b.is_cuda, "inputs should be on the same device (cpu/gpu)"

    ctx.save_for_backward(input_a, input_b)

    fn_ = _dispatch("add", optype=tp, cuda=cuda)

    # Create an output tensor with the proper dimensions
    out = input_a.new()
    out.resize_(input_a.shape)

    fn_(input_a, input_b, out)
    return out

def _backward_common(ctx, d_out, backward_op):
    tp = d_out.dtype
    cuda = d_out.is_cuda

    input_a = ctx.saved_tensors[0]
    input_b = ctx.saved_tensors[1]

    # Fetch the correct Halide operator for the type/device used
    fn_ = _dispatch(backward_op, optype=tp, cuda=cuda)

    d_input_a = d_out.new()
    d_input_b = d_out.new()
    d_input_a.resize_(d_out.shape)
    d_input_b.resize_(d_out.shape)

    fn_(input_a, input_b, d_out.contiguous(), d_input_a, d_input_b)
    return d_input_a, d_input_b

# TODO(srj): surely there's a better way to do this,
# but PyTorch seems to make it tricky to pass in
# extra info to the backward() method.
class AddFunction_Grad(th.autograd.Function):
  """Version using the manually-written backprop"""
  def __init__(self):
      super(AddFunction_Grad, self).__init__()

  @staticmethod
  def forward(ctx, input_a, input_b):
      return _forward_common(ctx, input_a, input_b)

  @staticmethod
  def backward(ctx, d_out):
      return _backward_common(ctx, d_out, "add_grad")

class AddFunction_HalideGrad(th.autograd.Function):
  """Version using the Halide-generated backprop"""
  def __init__(self):
      super(AddFunction_HalideGrad, self).__init__()

  @staticmethod
  def forward(ctx, input_a, input_b):
      return _forward_common(ctx, input_a, input_b)

  @staticmethod
  def backward(ctx, d_out):
      return _backward_common(ctx, d_out, "add_halidegrad")

class Add(th.nn.Module):
    """Defines a module that uses our autograd function.

    This is so we can use it as an operator.
    """
    def __init__(self, backward_op):
        super(Add, self).__init__()
        if backward_op == "add_grad":
          self._adder = AddFunction_Grad()
        elif backward_op == "add_halidegrad":
          self._adder = AddFunction_HalideGrad()
        else:
          assert False

    def forward(self, a, b):
        return self._adder.apply(a, b)
