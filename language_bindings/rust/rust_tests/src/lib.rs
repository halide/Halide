//!DO NOT PUBLISH
//!
//! This crate is used to test functions primarily in halide_runtime by running:
//! ```-ignore
//! cargo test
//! ```
//!
//! This crate also tests halide_build as a halide libruntime.a is required to test halide runtime
//! Test can't be located inside runtime crate as this would introduce halide_build as a dependency.

///Halide_runtime integration tests
#[cfg(test)]
mod runtime_tests;