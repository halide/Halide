use std::env;
use std::fs::{remove_file,rename};
use std::io;
use std::path::PathBuf;
use std::process::Command;

pub mod halide_build {
    use std::{default, fs};
    use std::env;
    use std::fs::{remove_file,rename};
    use std::io::Result;
    use std::ops::Add;
    use std::path::{Path,PathBuf};
    use std::process::{Command, Output};


    pub struct Halide {
        halide_path: PathBuf,
        gen_path: PathBuf,

        rs_output: PathBuf,

        //Generators: Vec<Generator<'static>>,
        debug: bool,
    }
    //#[derive(Clone)]
    pub struct Generator<'a> {
        gen_name: String,
        gen_exe: PathBuf,
        halide_path: PathBuf,
        gen_path: PathBuf,

        rs_output: PathBuf,

        gcc_flags: Vec<&'a str>,
        debug: bool,
    }

    impl Halide {
        pub fn new<T: Into<PathBuf>>(
            halide_path: T,
            gen_path: T,
            //rs_output:T
        ) -> Halide {
            Halide {
                halide_path: halide_path.into().join("distrib"),
                gen_path:gen_path.into(),
                rs_output:PathBuf::from(env::var("OUT_DIR").unwrap()),
                //Generators: Vec!{},
                debug: false
            }
        }

        pub fn out_dir<T:Into<PathBuf>>(mut self,out:T)->Self{
            self.rs_output = out.into();
            self
        }
        //Todo template all strings
        pub fn newGen(self, gen_name:String) ->Generator<'static>{
            println!("cargo:rerun-if-changed={}",self.gen_path.join(gen_name.as_str()).with_extension("cpp").to_str().unwrap());
            //self.Generators.push(
            Generator {
                    gen_name: gen_name.to_string(),
                    gen_exe: PathBuf::new().join("target").join(gen_name.to_string()).with_extension("generator"),
                    halide_path: self.halide_path.clone(),
                    gen_path: self.gen_path.join(gen_name).with_extension("cpp"),
                    rs_output: self.rs_output.clone(),
                    gcc_flags: vec!["-lHalide","-ldl","-lpthread","-lz"],
                    debug: self.debug
            }
            //);self
        }


    }

    impl Generator <'static>{
        pub fn make(&self) -> Output {
            let mut compile = Command::new("g++");
            compile.args(["-std=c++17",]);

            compile.args(["-I",self.halide_path.join("include").to_str().unwrap()]);
            compile.args(["-I", self.halide_path.join("tools").to_str().unwrap()]);
            compile.args(["-L",self.halide_path.join("lib").to_str().unwrap()]);

            compile.args(["-o", self.gen_exe.to_str().unwrap()]);

            let mut temp = self.halide_path.join("tools").join("GenGen").with_extension("cpp");
            compile.args(["-g",self.gen_path.to_str().unwrap(),temp.to_str().unwrap()]);
            compile.args(self.gcc_flags.clone());
            //compile.args(["-Wl,-rpath","-Wl,/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide/distrib/lib/"]);
            compile.output().expect("Make generator failed")

        }
        pub fn run_gen(&self) -> Output {
            //assert!(!self.gen_exe.is_none());
            //todo change to build target
            println!("cargo:rustc-link-search=native={}", self.rs_output.to_str().unwrap());

            let mut gen = Command::new(self.gen_exe.to_str().unwrap());
            gen.args(["-g",self.gen_name.as_str()]);
            gen.args(["-f", self.gen_name.as_str()]);

            //Todo change to build dir
            gen.args(["-o",self.rs_output.to_str().unwrap()]);
            gen.env("LD_LIBRARY_PATH", self.halide_path.join("lib"));

            //Todo add debug
            gen.args(["target=host-no_runtime"]);

            gen.output().expect("failed to run")

        }

        pub fn rename_move(&self ) -> Result<()> {
            println!("file name: {:?}",self.rs_output.join(self.gen_name.as_str()).with_extension("a").to_str().unwrap());
            println!("file out: {:?}",self.rs_output.join(String::from("lib").add(self.gen_name.as_str())).with_extension("a").to_str().unwrap());
            fs::rename(
                self.rs_output.join(self.gen_name.as_str()).with_extension("a").to_str().unwrap(),
                self.rs_output.join(String::from("lib").add(self.gen_name.as_str())).with_extension("a").to_str().unwrap()
            )
        }

        pub fn bind(&self) -> Result<()> {
            let bindings = bindgen::Builder::default()
                .header(self.rs_output.join(self.gen_name.as_str()).with_extension("h").to_str().unwrap())
                .allowlist_function(self.gen_name.as_str())
                .blocklist_item("halide_buffer_t")
                .generate().expect("unable to gen bindings").write_to_file(self.rs_output.join(self.gen_name.as_str()).with_extension("rs"));

            println!("cargo:rustc-link-lib=static={}", self.gen_name);
            bindings
        }
    }
}
    #[cfg(test)]
    mod tests {
        use std::env;
        use std::io;
        use std::io::prelude::*;
        use crate::halide_build::Halide;

        #[test]
        fn it_works() {
            let mut H = Halide::new(
                "/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide",
                "/home/rootbutcher2/CLionProjects/halide_build/Halide_gens"
            );
            let G = H.newGen("iir_blur".to_string());

            let out = G.make();
            println!("Gen Creation Status: {}", out.status);
            io::stdout().write_all(&out.stdout).unwrap();
            io::stderr().write_all(&out.stderr).unwrap();
            assert!(out.status.success());

            let out2 = G.run_gen();
            println!("Gen Run Status: {}", out2.status);
            io::stdout().write_all(&out2.stdout).unwrap();
            io::stderr().write_all(&out2.stderr).unwrap();
            assert!(out2.status.success());

            let out3 = G.rename_move();
            println!("move results: {:?}", out3);
            assert!(out3.is_ok());

            let out4 = G.bind();
            println!("bind results: {:?}", out4);
            assert!(out4.is_ok());
        }
    }
