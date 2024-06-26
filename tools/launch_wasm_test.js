// argv[0] = path to node
// argv[1] = path to this script
// argv[2] = path to the Halide-generated js file we want to test
// argv[3] = the Halide::Target string for the code being tested (e.g. wasm-32-wasmrt, wasm-32-wasmrt-webgpu)
const target_js_path = process.argv[2];
const halide_target_string = process.argv[3];

console.log("target_js_path is ", target_js_path)
console.log("halide_target_string is ", halide_target_string)
if (halide_target_string.includes("webgpu")) {
    const webgpu_node_bindings = process.env["HL_WEBGPU_NODE_BINDINGS"];
    if (!webgpu_node_bindings) {
        console.log("You must define the env var HL_WEBGPU_NODE_BINDINGS=/path/to/dawn.node when running WebGPU tests for Halide");
    }
    const provider = require(webgpu_node_bindings);
    const gpu = provider.create([]);
    // Not const: we want to redefine the global 'navigator' var
    navigator = { gpu: gpu };
}
require(target_js_path);
