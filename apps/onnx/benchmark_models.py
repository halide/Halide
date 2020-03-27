""" Given a list of prototxt files, compile them to Halide and benchmark them.
"""
import argparse
import os

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()

    for f in args.files:
        print('Processing', f)
        model_name = os.path.splitext(os.path.basename(f))[0]
        # Compile the generator
        cmd = ('bin/host/onnx_converter.generator '
               '-e static_library,h,registration,python_extension '
               '-g onnx_model_generator '
               '-o bin/host '
               '-f {} '
               '-p ../gradient_autoscheduler/bin/libgradient_autoscheduler.so '
               '-s Li2018 '
               'target=host '
               'auto_schedule=true '
               'model_file_path={}').format(model_name, f)
        print(cmd)
        os.system(cmd)

if __name__ == '__main__':
    main()
