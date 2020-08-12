# Adapted from https://github.com/saurabh-shandilya/onnx-utils/blob/master/onnx_edit.py

import onnx
from onnx import helper, checker
from onnx import TensorProto
import onnx.shape_inference
import re
import argparse

def create_graph_member_map(graph_member_list):
    member_map=dict();
    for n in graph_member_list:
        member_map[n.name]=n;
    return member_map

def trace_dependent_nodes(n,input_names,node_map,input_map,visited_nodes):
    if n.name in visited_nodes:
        return visited_nodes
    visited_nodes[n.name] = n
    for ninput in n.input:
        if ninput not in input_names and ninput in node_map:
            visited_nodes = trace_dependent_nodes(node_map[ninput],input_names,node_map,input_map,visited_nodes)
        if ninput in node_map:
            visited_nodes[ninput] = node_map[ninput]
        elif ninput in input_map:
            visited_nodes[ninput] = input_map[ninput]
        else:
            assert(False)
    return visited_nodes

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", help="input onnx model name")
    parser.add_argument("output", help="output onnx model name")
    parser.add_argument("--inputs", help="comma separated model input names, e.g. --inputs <nodename>,<nodename1> ")
    parser.add_argument("--outputs", help="comma separated model output names, e.g. --outputs <nodename>,<nodename1> ")
    args = parser.parse_args()

    model = onnx.load(args.input)
    model = onnx.shape_inference.infer_shapes(model)
    graph = model.graph
    onnx.checker.check_model(model)

    node_map = create_graph_member_map(graph.node)
    input_map = create_graph_member_map(graph.input)
    output_map = create_graph_member_map(graph.output)
    initializer_map = create_graph_member_map(graph.initializer)
    value_info_map = create_graph_member_map(graph.value_info)

    new_input_names = args.inputs.split(',') #['mobilenetv20_features_conv0_weight']
    new_output_names = args.outputs.split(',') #['mobilenetv20_features_linearbottleneck1_batchnorm2_fwd']

    visited_nodes = {}
    for n in new_output_names:
        visited_nodes = trace_dependent_nodes(node_map[n],new_input_names,node_map,input_map,visited_nodes)

    # Remove unused nodes
    for n in node_map:
        if n not in visited_nodes or n in new_input_names:
            graph.node.remove(node_map[n])
    for n in input_map:
        if n not in visited_nodes:
            graph.input.remove(input_map[n])
    for n in output_map:
        if n not in visited_nodes:
            graph.output.remove(output_map[n])
    for n in initializer_map:
        if n not in visited_nodes:
            graph.initializer.remove(initializer_map[n])

    for n in visited_nodes:
        # New inputs
        if n in new_input_names or type(visited_nodes[n]).__name__ == 'ValueInfoProto' or len(visited_nodes[n].input) == 0:
            if visited_nodes[n] not in graph.input:
                value_info = value_info_map[n].type.tensor_type
                shape = [d.dim_value for d in value_info.shape.dim]
                graph.input.extend([helper.make_tensor_value_info(n, value_info.elem_type, shape)])
        # New outputs
        if n in new_output_names:
            if visited_nodes[n] not in graph.output:
                value_info = value_info_map[n].type.tensor_type
                shape = [d.dim_value for d in value_info.shape.dim]
                graph.output.extend([helper.make_tensor_value_info(n, value_info.elem_type, shape)])

    onnx.checker.check_model(model)
    onnx.save(model, args.output)

if __name__ == "__main__":
    main()
