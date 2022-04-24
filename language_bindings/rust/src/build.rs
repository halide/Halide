use std::{env, io};
use std::fs;
use std::io::{ErrorKind, Result};
use std::ops::Add;
use std::path::PathBuf;
use std::process::{Command, Output};

/// this builder must have a path to a complied Halide folder it is used to build multiple generators quickly
///
/// more stuff
///
/// examples?
pub struct GenBuilder {
    halide_path: PathBuf,
    gen_path: PathBuf,

    rs_output: PathBuf,

    //generators: Vec<Generator<'static>>,
    debug: bool,
    target: String,
}

///This represents halide generator it is built using [GenBuilder]
///
/// more stuff
///
/// Examples?
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
    ///instantiate a GenBuilder
    ///
    /// ```ignore
    /// let Hal = GenBuilder::new(
    ///       "Path/to/Halide",
    ///        "Path/to/your/generators"
    ///    )
    /// ```
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
    ///Override the output directory.
    ///
    /// by default this is the ENV "out_dir" but can be anywere
    /// useful to place .rs and headers in a folder in the src of your project but means cargo does not clean these artifacts
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
    ///Create a generator based on its name
    ///
    /// We have strict naming conventions
    ///     The generator source must be GenName.cpp
    ///     The internal cpp class must also be GenName
    ///
    ///```ignore
    /// let gen = genBuilder.new_gen(iir_blur);
    /// ```
    pub fn new_gen(self, gen_name: String) -> Generator<'static> {
        println!(
            "cargo:rerun-if-changed={}",
            self.gen_path
                .join(gen_name.as_str())
                .with_extension("cpp")
                .to_str()
                .unwrap()
        );
        //self.generators.push(
        Generator {
            gen_name: gen_name.to_string(),
            gen_exe: PathBuf::new()
                .join("target")
                .join(gen_name.to_string())
                .with_extension("generator"),
            halide_path: self.halide_path.clone(),
            gen_path: self.gen_path.join(gen_name).with_extension("cpp"),
            rs_output: self.rs_output.clone(),
            gcc_flags: vec!["-lHalide", "-ldl", "-lpthread", "-lz"], //Todo add adders
            debug: self.debug,                                       //Todo add functionality
            target: self.target,                                     //todo add setter
        }
        //);self
    }
    ///Sets the console debug for Halide generators by default this is false
    pub fn debug(mut self, b:bool) ->Self{
        self.debug=b;
        self
    }
    /*
    pub fn make_runtime(self) {
        if self.generators.is_empty() {
            //todo make gen-gen
        }
        else {
           let _gen = self.generators[0].make_runtime();
        }
    }

     */
}
///This is a generator
///
/// more stuff
///
/// ```ignore
///     let a = 5;
/// ```
///
impl Generator<'static> {
    ///Make the generator executable useing halide gengen and g++
    pub fn compile(&self) -> Output {

        let mut cmd_compile = Command::new("g++");
        cmd_compile.args(["-std=c++17"]);

        cmd_compile.args(["-I", self.halide_path.join("include").to_str().unwrap()]);
        cmd_compile.args(["-I", self.halide_path.join("tools").to_str().unwrap()]);
        cmd_compile.args(["-L", self.halide_path.join("lib").to_str().unwrap()]);

        cmd_compile.args(["-o", self.gen_exe.to_str().unwrap()]);

        let temp = self
            .halide_path
            .join("tools")
            .join("GenGen")
            .with_extension("cpp");

        cmd_compile.args([
            "-g",
            self.gen_path.to_str().unwrap(),
            temp.to_str().unwrap(),
        ]);

        cmd_compile.args(self.gcc_flags.clone());
        //compile.args(["-Wl,-rpath","-Wl,/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide/distrib/lib/"]);
        cmd_compile.output().expect("Make generator failed")

    }
    /// runs the previously made executable
    ///
    /// panics if unable to find or run the executable
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
    /// The Halide runtime is required for useing halide and contains the buffer_t and other useful functions
    pub fn make_runtime(&self) -> Result<()> {
        println!("Cmd to runt: {:?}", self.gen_exe.to_str().unwrap());
        let mut cmd = Command::new(self.gen_exe.to_str().unwrap());
        cmd.args(["-r", "runtime", "-o", self.rs_output.to_str().unwrap(),"target=host"]);
        cmd.env("LD_LIBRARY_PATH", self.halide_path.join("lib"));
        let output = cmd.output().expect("failed to make runtime");
        let result;
        if output.status.success() {
            result = self.rename_runtime();
            if result.is_ok(){
                println!("cargo:rustc-link-lib=static={}", "runtime");
            }
        }
        else {
            let errors = String::from("failed to make runtime with gen: ").add(self.gen_name.as_str());
            result = Err(io::Error::new(ErrorKind::Other, errors.as_str()));
        }
        result
    }

    fn rename_runtime(&self)->Result<()>{
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
    ///rename the gen outputs to be what rust and bindgen expect on linex distros
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
    /// run bindgen on a generator
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

    ///This complies-> runs-> renames-> binds the generator
    ///
    /// panics if any step fails if it panics it is useful to call the functions separately and print there results or outputs
    pub fn build_bind(&self){

        assert!(self.compile().status.success());
    
        assert!(self.run_gen().status.success());
    
        assert!(self.bind().is_ok());
    
        assert!(self.rename().is_ok());
        assert!(self.make_runtime().is_ok());
    }

}
