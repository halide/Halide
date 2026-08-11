use std::env;
use std::path::PathBuf;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    tauri_build::build();
    generate_trace_packet_bindings()
}

/// Derives Rust type layouts for `halide_trace_packet_t` (and, transitively, the
/// `halide_trace_event_code_t` enum it references) directly from Halide's runtime header, so the
/// wire-format struct used by `trace.rs` stays mechanically in sync with the real C++ definition
/// instead of being hand-copied.
///
/// This only derives *layout*: it deliberately does not generate bindings for any of
/// `halide_trace_packet_t`'s C++ methods (e.g., `coordinates()`, `value()`, `func()`).
fn generate_trace_packet_bindings() -> Result<(), Box<dyn std::error::Error>> {
    let manifest_dir = env::var("CARGO_MANIFEST_DIR")?;
    let header_path = PathBuf::from(&manifest_dir).join("../../../src/runtime/HalideRuntime.h");
    let header = header_path.canonicalize().map_err(|e| {
        format!(
            "HalideRuntime.h not found at {}: {e}",
            header_path.display()
        )
    })?;

    // Flag to cargo that we only need to rerun bindgen if HalideRuntime.h's contents have changed.
    println!("cargo:rerun-if-changed={}", header.display());

    let bindings = bindgen::Builder::default()
        .header(header.to_string_lossy())
        .clang_args(["-x", "c++", "-std=c++17"])
        .allowlist_type("halide_trace_packet_t")
        .with_codegen_config(bindgen::CodegenConfig::TYPES | bindgen::CodegenConfig::VARS)
        .derive_default(true)
        .generate()?;

    // Write generated bindings to this build's hashed OUT_DIR (as opposed to committing to source).
    let out_path = PathBuf::from(env::var("OUT_DIR")?);
    bindings.write_to_file(out_path.join("halide_trace_bindings.rs"))?;

    Ok(())
}
