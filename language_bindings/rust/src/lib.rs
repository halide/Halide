#![warn(missing_docs)]
//#![warn(missing_doc_code_examples)]

//!This crate is an example of calling Halide generated code from Rust.
//!This crate also has an example of IIR-blur halide app that get called from rust.
//! IIR blur takes a image input, goes through the full halide pipeline, and outputs a blurred image all using rust.
//!

///Required module for use in build.rs scripts
///
/// This Module will compile and bind a generator and create a Halide runtime.
///
pub mod build;

///tests for build module
///
///
#[cfg(test)]
mod build_tests;

///The runtime module contains useful functions for use in your program
///
///Most importantly has constructors for a Halide_buffer_t. This module requires the build module to be run in the build.rs in order to function.
///
pub mod runtime;

///tests for runtime
///
/// not currently functional
#[cfg(test)]
mod runtime_tests;
