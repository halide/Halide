This app is an interpreter of machine learning pipelines, where many of the ops are implemented in Halide.

There are several front ends for the interpreter:
- TFlite flat buffer parser
- TFlite delegate
- Direct API

This app is a work in progress. Currently, only quantized uint8 networks are supported.
All of the [TensorFlow hosted models](https://www.tensorflow.org/lite/guide/hosted_models)
are working and producing good performance.

### Benchmarks
The comparison data below was produced with TensorFlow v.2.4.0 (the latest release as of this writing):

x86 OSX laptop w/ AVX2:

| Network | TFlite (ms)|Halide (ms)| Speedup
| ---- | ---- | ---- | ---- |
| inception_v1_224_quant | 72.5 | 28.4 | 2.55 |
| inception_v2_224_quant | 100 | 38.1 | 2.62 |
| inception_v3_quant | 267 | 105.3 | 2.54 |
| inception_v4_299_quant | 566 | 227 | 2.49 |
| mobilenet_v1_0.25_128_quant | 1.9 | 0.68 | 2.78 |
| mobilenet_v1_1.0_224_quant | 38.4 | 12.7 | 3.02 |
| mobilenet_v2_1.0_224_quant | 30.6 | 9.85 | 3.11 |

Qualcomm Snapdragon 855 A76 core (Pixel 4):

| Network | TFlite (ms)|Halide (ms)| Speedup
| ---- | ---- | ---- | ---- |
| inception_v1_224_quant | 24.7 | 25.0 | 0.99 |
| inception_v2_224_quant | 49.8 | 33.5 | 1.49 |
| inception_v3_quant | 97 | 87.6 | 1.11 |
| inception_v4_299_quant | 198 | 183.4 | 1.09 |
| mobilenet_v1_0.25_128_quant	| 0.97 | 0.63 | 1.54 |
| mobilenet_v1_1.0_128_quant |4.64 | 4.44 | 1.05 |
| mobilenet_v1_1.0_224_quant | 12.9 | 11.6 | 1.11 |
| mobilenet_v2_1.0_224_quant | 11.8 | 9.72 | 1.21 |

### Planned but still TODO
- More op support
- More data type support
- Multicore parallelism
- Hexagon HVX support
- More intelligent scheduling across ops, to save memory and improve locality

### Usage

#### benchmark
benchmark is a binary that runs the provided .tflite flat buffer files and reports the time taken for each.

Usage:

    benchmark a.tflite [b.tflite ...]

#### compare_vs_tflite
This binary runs each provided network 3 times:
- Directly via TFlite
- Directly via HANNK
- Via HANNK TFlite delegate

The app reports timing for each, and compares the results, reporting significant differences.

Usage:

    compare_vs_tflite a.tflite [b.tflite ...]

