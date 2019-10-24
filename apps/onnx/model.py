
import model_cpp


class Model():
    def __init__(self):
        self.pipeline = None

    def BuildFromOnnxModel(self, onnx_model):
        assert onnx_model

        if type(onnx_model) is str:
            onnx_model = onnx_model.encode()
            self.pipeline = model_cpp.ConvertOnnxModel(onnx_model)
        elif type(onnx_model) is bytes:
            self.pipeline = model_cpp.ConvertOnnxModel(onnx_model)
        else:
            model_str = onnx_model.SerializeToString()
            self.pipeline = model_cpp.ConvertOnnxModel(model_str)

    def OptimizeSchedule(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.AutoSchedule(self.pipeline)

    def run(self, inputs, device=''):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Run(self.pipeline, inputs, device)

    def Benchmark(self, num_iters=5, device=''):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        return model_cpp.Benchmark(self.pipeline, num_iters, device)

    def PrintLoopNest(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        model_cpp.PrintLoopNest(self.pipeline)

    def PrintLoweredStatement(self):
        if not self.pipeline:
            raise Exception("model not initialized, call BuildFromOnnxModel first")
        model_cpp.PrintLoweredStatement(self.pipeline)
