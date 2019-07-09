"""Wrap our operator (and gradient) in autograd.""" 

# We need to import torch before loading the custom modules
import torch as th
import halide_ops as ops

def _dispatch(op, optype=th.float32, cuda=False):
  opname = op
  if cuda:
    opname += "_cuda"

  if optype == th.float64:
    opname += "_double"
  fn_ = getattr(ops, opname)
  if not hasattr(ops, opname):
    raise ValueError("Module has no operator %s" % opname)  
  return fn_

# Register our ops with autograd so we can backprop through it
class AddFunction(th.autograd.Function):
  def __init__(self):
    super(AddFunction, self).__init__()

  @staticmethod
  def forward(ctx, input_a, input_b):
    tp = input_a.dtype
    cuda = input_a.is_cuda
    assert tp == input_b.dtype, "inputs should have the same type"
    assert cuda == input_b.is_cuda, "inputs should be on the same device (cpu/gpu)"

    # Fetch the correct Halide operator for the type/device used
    fn_ = _dispatch("add", optype=tp, cuda=cuda)

    out = input_a.new()
    out.resize_(input_a.shape)
    fn_(input_a, input_b, out)
    return out

  @staticmethod
  def backward(ctx, d_out):
    tp = d_out.dtype
    cuda = d_out.is_cuda

    # Fetch the correct Halide operator for the type/device used
    fn_ = _dispatch("add_grad", optype=tp, cuda=cuda)

    d_input_a = d_out.new()
    d_input_b = d_out.new()
    d_input_a.resize_(d_out.shape)
    d_input_b.resize_(d_out.shape)
    ops.add_grad(d_out.contiguous(), d_input_a, d_input_b)
    return d_input_a, d_input_b


# Define a module that uses our autograd function
class Add(th.nn.Module):
  def __init__(self):
    super(Add, self).__init__()
    self._adder = AddFunction()

  def forward(self, a, b):
    return self._adder.apply(a, b)

a = th.zeros(4, 6, 8, 8, requires_grad=True)
b = th.zeros(4, 6, 8, 8, requires_grad=True)
out = th.zeros(4, 6, 8, 8)

adder = Add()
out = adder(a, b)
loss = out.sum()
loss.backward()
