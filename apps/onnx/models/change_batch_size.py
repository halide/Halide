import onnx
import argparse

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+')
    parser.add_argument('--batch_size', type=int)
    args = parser.parse_args()

    for f in args.files:
        print('Processing ', f)
        onnx_model = onnx.load(f)
        for i in onnx_model.graph.input:
            i.type.tensor_type.shape.dim[0].dim_value = args.batch_size
        for o in onnx_model.graph.output:
            o.type.tensor_type.shape.dim[0].dim_value = args.batch_size
        onnx.save(onnx_model, f[:-5] + '_batch.onnx')

if __name__ == '__main__':
    main()
              
