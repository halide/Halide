from __future__ import absolute_import, division, print_function, unicode_literals
import unittest
from model import Model
from onnx import helper
from onnx import TensorProto
import numpy as np


class ModelTest(unittest.TestCase):
    def setUp(self):
        pass

    def test_empty_model(self):
        model = Model()
        with self.assertRaises(Exception):
            model.GenerateSchedule()
        with self.assertRaises(Exception):
            model.PrintLoopNest()
        with self.assertRaises(Exception):
            model.PrintLoweredStatement()

    def test_small_model(self):
        # Create one input
        X = helper.make_tensor_value_info('IN', TensorProto.FLOAT, [2, 3])
        # Create one output
        Y = helper.make_tensor_value_info('OUT', TensorProto.FLOAT, [2, 3])
        # Create a node
        node_def = helper.make_node('Abs', ['IN'], ['OUT'])

        # Create the model
        graph_def = helper.make_graph([node_def], "test-model", [X], [Y])
        onnx_model = helper.make_model(graph_def,
                                       producer_name='onnx-example')

        model = Model()
        model.BuildFromOnnxModel(onnx_model)
        schedule = model.OptimizeSchedule()
        schedule = schedule.replace('\n', ' ')
        expected_schedule = r'// --- BEGIN machine-generated schedule // Target: .+// MachineParams: .+// Delete this line if not using Generator Pipeline pipeline = get_pipeline\(\);.+Func OUT = pipeline.get_func\(1\);.+{.+}.+'
        self.assertRegex(schedule, expected_schedule)

        input = np.random.rand(2, 3) - 0.5
        outputs = model.run([input])
        self.assertEqual(1, len(outputs))
        output = outputs[0]
        expected = np.abs(input)
        np.testing.assert_allclose(expected, output)

    def test_scalars(self):
        # Create 2 inputs
        X = helper.make_tensor_value_info('A', TensorProto.INT32, [])
        Y = helper.make_tensor_value_info('B', TensorProto.INT32, [])
        # Create one output
        Z = helper.make_tensor_value_info('C', TensorProto.INT32, [])
        # Create a node
        node_def = helper.make_node('Add', ['A', 'B'], ['C'])

        # Create the model
        graph_def = helper.make_graph([node_def], "scalar-model", [X, Y], [Z])
        onnx_model = helper.make_model(graph_def,
                                       producer_name='onnx-example')

        model = Model()
        model.BuildFromOnnxModel(onnx_model)
        schedule = model.OptimizeSchedule()
        schedule = schedule.replace('\n', ' ')
        expected_schedule = r'// --- BEGIN machine-generated schedule // Target: .+// MachineParams: .+// Delete this line if not using Generator Pipeline pipeline = get_pipeline\(\);.+Func C = pipeline.get_func\(2\);.+{.+}.+'
        self.assertRegex(schedule, expected_schedule)

        input1 = np.random.randint(-10, 10, size=())
        input2 = np.random.randint(-10, 10, size=())
        outputs = model.run([input1, input2])
        self.assertEqual(1, len(outputs))
        output = outputs[0]
        expected = input1 + input2
        np.testing.assert_allclose(expected, output)

    def test_model_with_initializer(self):
        X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [3, 1])
        Z2 = helper.make_tensor_value_info('Z2', TensorProto.FLOAT, [2, 3, 6])

        expand_node_def = helper.make_node('Expand', ['X', 'Y'], ['Z1'])
        cast_node_def = helper.make_node('Scale', ['Z1'], ['Z2'])

        graph_def = helper.make_graph([expand_node_def, cast_node_def],
            "test-node",
            [X],
            [Z2],
            initializer=[
                helper.make_tensor('Y', TensorProto.INT64, (3,), (2, 1, 6))])
        onnx_model = helper.make_model(graph_def,
                                       producer_name='onnx-example')
        model = Model()
        model.BuildFromOnnxModel(onnx_model)
        input_data = np.random.rand(3, 1).astype(np.float32)
        outputs = model.run([input_data])
        expected = input_data * np.ones([2, 1, 6], dtype=np.float32)
        np.testing.assert_allclose(expected, outputs[0])

    def test_tensors_rank_zero(self):
        X = helper.make_tensor_value_info('X', TensorProto.FLOAT, [3, 2])
        S1 = helper.make_tensor_value_info('S1', TensorProto.INT64, [])
        S2 = helper.make_tensor_value_info('S2', TensorProto.FLOAT, [])

        size_node = helper.make_node('Size', ['X'], ['S1'])

        graph_def = helper.make_graph([size_node],
            "rank_zero_test",
            [X],
            [S1, S2],
            initializer=[
                helper.make_tensor('S2', TensorProto.FLOAT, (), (3.14,))])
        onnx_model = helper.make_model(graph_def,
                                       producer_name='onnx-example')
        model = Model()
        model.BuildFromOnnxModel(onnx_model)
        input_data = np.random.rand(3, 2).astype(np.float32)
        outputs = model.run([input_data])
        self.assertEqual(6, outputs[0])
        self.assertAlmostEqual(3.14, outputs[1])
