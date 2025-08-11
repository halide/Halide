// argv[0] = path to node
// argv[1] = path to this script
// argv[2] = path to the Halide-generated js file we want to test
// argv[3] = the Halide::Target string for the code being tested (e.g. wasm-32-wasmrt, wasm-32-wasmrt-webgpu)
const target_js_path = process.argv[2];
const halide_target_string = process.argv[3];

if (halide_target_string.includes("webgpu")) {
    const provider = require("dawn");
    const gpu = provider.create([]);
    // Not const: we want to redefine the global 'navigator' var
    navigator = {gpu: gpu};
}

require(target_js_path);
