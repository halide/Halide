from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

from onnx.backend.base import Backend as BackendBase
import onnx
import model as halide_model
import signal
import base64
import hashlib
import datetime


SAVE_MODEL = False


class WatchdogTimer(Exception):
    def __init__(self, timeout=10*60):
        self.timeout = timeout

    def __enter__(self):
        signal.signal(signal.SIGALRM, self.handler)
        signal.alarm(self.timeout)

    def __exit__(self, type, value, traceback):
        signal.alarm(0)

    def handler(self, signum, frame):
        raise self

    def __str__(self):
        return "Timeout (%ds) exceeded, aborting" % self.timeout


class HalideBackend(BackendBase):
    @classmethod
    def is_compatible(cls,
                      model,  # type: ModelProto
                      device='CPU',  # type: Text
                      **kwargs  # type: Any
                      ):  # type: (...) -> bool
        """Returns whether the model is compatible with the backend.
        For the moment, we always return True and will throw an exception
        later when preparing or running the model.
        """
        return True

    @classmethod
    def prepare(cls,
                model,  # type: ModelProto
                device='CPU',  # type: Text
                **kwargs  # type: Any
                ):
        """Prepare an ONNX model to run using the Halide backend.

        Builds and returns an internal representation of the model (which
        includes the actual Halide pipeline).

        :param model: The ONNX model to be converted.
        :param device: The device to execute this model on (Ignored for now).
        :returns: An internal object that can be used to run the model with
                  Halide.
        """
        onnx.checker.check_model(model)
        if SAVE_MODEL:
            cls.save_model(model)
        prepared = halide_model.Model()
        with WatchdogTimer(timeout=300):
            prepared.BuildFromOnnxModel(model, device)
            return prepared

    @classmethod
    def run_model(cls,
                  model,  # type: ModelProto
                  inputs,  # type: Any
                  device='CPU',  # type: Text
                  **kwargs  # type: Any
                  ):  # type: (...) -> Tuple[Any, ...]
        """Evaluate a ONNX model using the Halide backend.

        :param model: The ONNX model to be converted.
        :param inputs: A list of numpy arrays (one for each model input)
        :param device: The device to execute this model on (Ignored for now).
        :returns: A list of numpy arrays (one for each model output).
        """
        prepared = cls.prepare(model, device, **kwargs)
        with WatchdogTimer(timeout=300):
            return prepared.run(inputs)

    @classmethod
    def run_node(cls,
                 node,  # type: NodeProto
                 inputs,  # type: Any
                 device='CPU',  # type: Text
                 outputs_info=None,   # type: Optional[Sequence[Tuple[numpy.dtype, Tuple[int, ...]]]]
                 **kwargs  # type: Dict[Text, Any]
                 ):  # type: (...) -> Optional[Tuple[Any, ...]]
        """Evaluate a single ONNX node using the Halide backend.
        Not supported yet.
        """
        assert False
        return None

    @classmethod
    def supports_device(cls, device):  # type: (Text) -> bool
        """
        Checks whether the backend is compiled with particular device support.
        Always return True since we ignore the device and run on the host at the
        moment.
        """
        return True


prepare = HalideBackend.prepare
run_node = HalideBackend.run_node
run_model = HalideBackend.run_model
supports_device = HalideBackend.supports_device
is_compatible = HalideBackend.is_compatible
