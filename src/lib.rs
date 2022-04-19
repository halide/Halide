use std::env;
use std::fs::{remove_file,rename};
use std::io;
use std::path::PathBuf;
use std::process::Command;

pub mod halide_build {
    use std::default;
    use std::env;
    use std::fs::{remove_file,rename};
    use std::io::Result;
    use std::path::{Path,PathBuf};
    use std::process::{Command, Output};


    pub struct Halide {
        halide_path: PathBuf,
        gen_path: PathBuf,

        rs_output: PathBuf,

        debug: bool,
    }
    //#[derive(Clone)]
    pub struct Generator {
        gen_name: String,
        gen_exe: PathBuf,
        halide_path: PathBuf,
        gen_path: PathBuf,

        rs_output: PathBuf,

        gcc_flags: Option<Vec<String>>,
        debug: bool,
    }

    impl Halide {
        pub fn new<T: Into<PathBuf>>(
            halide_path: T,
            gen_path: T,
            rs_output:T
        ) -> Halide {
            Halide {
                halide_path: halide_path.into().join("distrib"),
                gen_path:gen_path.into(),
                rs_output:rs_output.into(),
                debug: false
            }
        }

        pub fn newGen(self,gen_name:String)->Generator{
            Generator {
                gen_name: gen_name.to_string(),
                gen_exe: PathBuf::new().join("target").join(gen_name.to_string()).with_extension("generator"),
                halide_path: self.halide_path,
                gen_path: self.gen_path.join(gen_name).with_extension("cpp"),
                rs_output: self.rs_output,
                gcc_flags: None,
                debug: false
            }
        }
    }

    impl Generator {
        pub fn make(&self) -> Output {
            let mut compile = Command::new("g++");
            compile.args(["-std=c++17",]);

            compile.args(["-I",self.halide_path.join("include").to_str().unwrap()]);
            compile.args(["-I", self.halide_path.join("tools").to_str().unwrap()]);
            compile.args(["-L",self.halide_path.join("lib").to_str().unwrap()]);

            compile.args(["-o", self.gen_exe.to_str().unwrap()]);

            let mut temp = self.halide_path.join("tools").join("GenGen").with_extension("cpp");
            compile.args(["-g",self.gen_path.to_str().unwrap(),temp.to_str().unwrap()]);
            compile.args(["-lHalide","-ldl","-lpthread","-lz"]);
            //compile.args(["-Wl,-rpath","-Wl,/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide/distrib/lib/"]);
            compile.output().expect("Make generator failed")

        }
        pub fn run_gen(mut self) -> Output {
            //assert!(!self.gen_exe.is_none());

            let mut gen = Command::new(self.gen_exe.to_str().unwrap());
            gen.args(["-g",self.gen_name.as_str()]);
            gen.args(["-f", self.gen_name.as_str()]);
            gen.args(["-o",self.rs_output.to_str().unwrap()]);
            gen.env("LD_LIBRARY_PATH", self.halide_path.join("lib"));

            gen.args(["target=host-no_runtime"]);

            gen.output().expect("failed to run")

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
            let H = Halide::new(
                "/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide",
                "/home/rootbutcher2/CLionProjects/halide_build/Halide_gens",
                "/home/rootbutcher2/CLionProjects/halide_build/rs_out"
            );
            let G= H.newGen("iir_blur".to_string());
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
        }
    }
