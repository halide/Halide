#![warn(missing_docs)]
//#![warn(missing_doc_code_examples)]

//!This crate is an example of calling Halide generated code from Rust.
//!This crate also has an example of IIR-blur Halide app that get called from rust.
//! IIR blur takes a image input, goes through the full Halide pipeline, and outputs a blurred image all using rust.
//!

use std::fs;
use std::io::{ErrorKind, Result};
use std::ops::Add;
use std::path::PathBuf;
use std::process::{Command, Output};
///Required module for use in build.rs scripts
///
/// This Module will compile and bind a generator and create a Halide runtime.
///
//TODO update rust docs
use std::{env, io};

/// This builder must have a path to a complied Halide folder. It is used to build multiple generators quickly
///
///
pub struct GenBuilder {
    halide_path: PathBuf,
    gen_path: PathBuf,

    rs_output: PathBuf,

    //generators: Vec<Generator<'static>>,
    debug: bool,
    target: String,
}

/// This represents a Halide generator that is built using [GenBuilder]
///
///
pub struct Generator<'a> {
    gen_name: String,
    gen_exe: PathBuf,
    halide_path: PathBuf,
    gen_path: PathBuf,

    rs_output: PathBuf,

    gcc_flags: Vec<&'a str>,
    debug: bool,
    target: String,
}

//Todo add runtime maker
impl GenBuilder {
    /// Instantiate a GenBuilder
    ///
    /// ```ignore
    /// let Hal = GenBuilder::new(
    ///       "Path/to/Halide",
    ///        "Path/to/your/generators"
    ///    )
    /// ```
    ///
    pub fn new<T: Into<PathBuf>>(
        halide_path: T,
        gen_path: T,
        //rs_output:T
    ) -> GenBuilder {
        GenBuilder {
            halide_path: halide_path.into().join("distrib"),
            gen_path: gen_path.into(),
            rs_output: PathBuf::from(env::var("OUT_DIR").unwrap_or("target".to_string())),
            //generators: vec![],
            debug: false,
            target: "target=host-no_runtime".to_string(),
        }
    }
    /// Override the output directory.
    ///
    /// by default this is the ENV "out_dir" but can be anywere
    /// useful to place .rs and headers in a folder in the src of your project but doing that
    /// means cargo does not clean these artifacts
    pub fn out_dir<T: Into<PathBuf>>(mut self, out: T) -> Self {
        self.rs_output = out.into();
        self
    }
    /*
    pub fn push_gen(mut self, gen: Generator<'static>)->Self{
        self.generators.push(gen);
        self
    }

     */
    //Todo template all strings
    /// Create a generator based on its name
    ///
    /// We have strict naming conventions:   
    ///    - The generator source must be gen_name.cpp   
    ///    - The internal cpp class must also be gen_name
    ///
    ///```ignore
    /// let gen = genBuilder.new_gen(iir_blur);
    /// ```
    pub fn new_gen(self, gen_name: String) -> Generator<'static> {
        let mut temp_gen_exe = PathBuf::from(self.rs_output.to_str().unwrap());
        if gen_name.is_empty() {
            temp_gen_exe.push("runtime");
        } else {
            println!(
                "cargo:rerun-if-changed={}",
                self.gen_path
                    .join(gen_name.as_str())
                    .with_extension("cpp")
                    .to_str()
                    .unwrap()
            );
            temp_gen_exe.push(gen_name.as_str());
        }
        temp_gen_exe.with_extension("generator");

        println!("temp: {:?}", temp_gen_exe.as_os_str());

        Generator {
            gen_name: gen_name.to_string(),
            gen_exe: temp_gen_exe,
            halide_path: self.halide_path.clone(),
            gen_path: self.gen_path.join(gen_name).with_extension("cpp"),
            rs_output: self.rs_output.clone(),
            gcc_flags: vec!["-lHalide", "-ldl", "-lpthread", "-lz"], //Todo add adders
            debug: self.debug,                                       //Todo add functionality
            target: self.target,                                     //todo add setter
        }
    }
    /// Sets the console debug for Halide generators by default this is false
    pub fn debug(mut self, b: bool) -> Self {
        self.debug = b;
        self
    }
    ///
    ///
    ///
    pub fn make_runtime(self) {
        let gen = self.new_gen("".to_string());

        let compilation_result = gen.compile();
        println!("Gen Creation Status: {}", compilation_result.status);
        assert!(compilation_result.status.success());

        let execution_result = gen.make_runtime();
        println!("runtime results: {:?}", execution_result);
        assert!(execution_result.is_ok());

        gen.rename_runtime();
    }
}

impl Generator<'static> {
    ///Make the generator executable using Halide GenGen and g++
    pub fn compile(&self) -> Output {
        let mut cmd_compile = Command::new("g++");
        cmd_compile.args(["-std=c++17"]);

        // Inlcude flags
        cmd_compile.args(["-I", self.halide_path.join("include").to_str().unwrap()]);
        cmd_compile.args(["-I", self.halide_path.join("tools").to_str().unwrap()]);

        // Linker flag
        cmd_compile.args(["-L", self.halide_path.join("lib").to_str().unwrap()]);

        let temp = self
            .halide_path
            .join("tools")
            .join("GenGen")
            .with_extension("cpp");
        if self.gen_name.is_empty() {
            cmd_compile.args(["-g", temp.to_str().unwrap()]);
        } else {
            cmd_compile.args([
                "-g",
                self.gen_path.to_str().unwrap(),
                temp.to_str().unwrap(),
            ]);
        }
        // Output flag
        cmd_compile.args(["-o", self.gen_exe.to_str().unwrap()]);

        cmd_compile.args(self.gcc_flags.clone());
        //compile.args(["-Wl,-rpath","-Wl,/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide/distrib/lib/"]);
        cmd_compile.output().expect("Make generator failed")
    }
    /// Runs the previously made executable
    ///
    /// Panics if unable to find or run the executable
    ///```ignore
    ///     gen.run_gen()
    ///```
    pub fn run_gen(&self) -> Output {
        //assert!(!self.gen_exe.is_none());
        println!(
            "cargo:rustc-link-search=native={}",
            self.rs_output.to_str().unwrap()
        );

        let mut gen = Command::new(self.gen_exe.to_str().unwrap());
        gen.args(["-g", self.gen_name.as_str()]);
        gen.args(["-f", self.gen_name.as_str()]);

        //Todo change to build dir
        gen.args(["-o", self.rs_output.to_str().unwrap()]);
        gen.env("LD_LIBRARY_PATH", self.halide_path.join("lib"));

        //Todo add debug
        if !self.debug {
            gen.arg(self.target.as_str());
        } else {
            let mut temp = String::from(self.target.as_str());
            temp.push_str("-debug");
            gen.arg(temp);
        }
        gen.output().expect("failed to run")
    }

    /// Make the Halide runtime
    ///
    /// The Halide runtime is required for using Halide and contains the buffer_t and other useful functions
    pub fn make_runtime(&self) -> Result<()> {
        println!(
            "cargo:rustc-link-search=native={}",
            self.rs_output.to_str().unwrap()
        );
        println!("Cmd to run: {:?}", self.gen_exe.to_str().unwrap());
        let mut cmd = Command::new(self.gen_exe.to_str().unwrap());
        cmd.args([
            "-r",
            "runtime",
            "-o",
            self.rs_output.to_str().unwrap(),
            "target=host",
        ]);
        cmd.env("LD_LIBRARY_PATH", self.halide_path.join("lib"));
        let output = cmd.output().expect("failed to make runtime");
        let result;
        if output.status.success() {
            result = self.rename_runtime();
            if result.is_ok() {
                println!("cargo:rustc-link-lib=static={}", "runtime");
            }
        } else {
            let errors =
                String::from("failed to make runtime with gen: ").add(self.gen_name.as_str());
            result = Err(io::Error::new(ErrorKind::Other, errors.as_str()));
        }
        result
    }

    fn rename_runtime(&self) -> Result<()> {
        println!(
            "file name: {:?}",
            self.rs_output
                .join("runtime")
                .with_extension("a")
                .to_str()
                .unwrap()
        );
        println!(
            "file out: {:?}",
            self.rs_output
                .join(String::from("lib").add("runtime"))
                .with_extension("a")
                .to_str()
                .unwrap()
        );
        fs::rename(
            self.rs_output
                .join("runtime")
                .with_extension("a")
                .to_str()
                .unwrap(),
            self.rs_output
                .join(String::from("lib").add("runtime"))
                .with_extension("a")
                .to_str()
                .unwrap(),
        )
    }

    /// Renames the gen outputs to be what rust and bindgen expect on linux distros
    ///
    /// IE halide.a -> libhalide.a
    pub fn rename(&self) -> Result<()> {
        println!(
            "file name: {:?}",
            self.rs_output
                .join(self.gen_name.as_str())
                .with_extension("a")
                .to_str()
                .unwrap()
        );
        println!(
            "file out: {:?}",
            self.rs_output
                .join(String::from("lib").add(self.gen_name.as_str()))
                .with_extension("a")
                .to_str()
                .unwrap()
        );
        fs::rename(
            self.rs_output
                .join(self.gen_name.as_str())
                .with_extension("a")
                .to_str()
                .unwrap(),
            self.rs_output
                .join(String::from("lib").add(self.gen_name.as_str()))
                .with_extension("a")
                .to_str()
                .unwrap(),
        )
    }

    /// Run bindgen on a generator
    ///
    /// This bind only creates the genname funtion binding and specificly blocklists the buffer_t
    pub fn bind(&self) -> Result<()> {
        let bindings = bindgen::Builder::default()
            .header(
                self.rs_output
                    .join(self.gen_name.as_str())
                    .with_extension("h")
                    .to_str()
                    .unwrap(),
            )
            .allowlist_function(self.gen_name.as_str())
            .blocklist_item("halide_buffer_t")
            .generate()
            .expect("unable to gen bindings")
            .write_to_file(
                self.rs_output
                    .join(self.gen_name.as_str())
                    .with_extension("rs"),
            );

        println!("cargo:rustc-link-lib=static={}", self.gen_name);
        bindings
    }

    /// This complies-> runs-> renames-> binds the generator
    ///
    /// panics if any step fails if it panics it is useful to call the functions separately and print there results or outputs
    pub fn build_bind(&self) {
        assert!(self.compile().status.success());

        assert!(self.run_gen().status.success());

        assert!(self.bind().is_ok());

        assert!(self.rename().is_ok());
        assert!(self.make_runtime().is_ok());
    }
}

mod build_tests;
