


pub mod Halide_Build{
    use std::ffi::OsString;
    use std::path::PathBuf;
    use std::io;
    use std::io::prelude::*;
    use std::ops::Add;
    use std::process::{Command, Output};

    pub struct Halide{
        pub halide_path: PathBuf,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,
        pub generators: Vec<Halide_Gen>,
    }

    pub struct Halide_Gen{
        pub halide_path: PathBuf,
        pub gen_name: String,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,
    }
    impl Halide{

    }
    impl Halide_Gen {
        pub fn compile_gen(&mut self) -> Output {
            let mut out = Command::new("g++");

            out.args(["-O3", "-O3","-std=c++17"]);

            self.halide_path.push("distrib");
            self.halide_path.push("include");

            out.args(["-I",self.halide_path.to_str().unwrap()]);

            self.halide_path.pop();
            self.halide_path.push("tools");
            out.args(["-I",self.halide_path.to_str().unwrap()]);

            self.gen_path.push(self.gen_name.as_str());
            self.gen_path.set_extension("cpp");
            self.halide_path.push("GenGen.cpp");
            out.args(["-g",self.gen_path.to_str().unwrap(),self.halide_path.to_str().unwrap()]);

            self.gen_path.set_extension("gen");
            out.args(["-o",self.gen_path.to_str().unwrap()]);

            self.halide_path.pop();
            self.halide_path.pop();
            self.halide_path.push("lib");
            out.args(["-L",self.halide_path.to_str().unwrap()]);

            let mut temp:String = "-Wl,-rpath,".to_string();
            temp.push_str(self.halide_path.to_str().unwrap());
            out.arg(temp);

            out.args(["-lHalide","-ldl","-lpthread","-lz"]);

            out.output().expect("Building the gererator failed")
        }
    pub fn run_gen(&self) -> Output {

        let mut out = Command::new(self.gen_path.to_str().unwrap());
        out.args(["-g",self.gen_name.as_str()]);
        out.args(["-f", self.gen_name.as_str()]);

        out.args(["-o","/home/rootbutcher2/CLionProjects/Halide_Build/Halide_gens"]);
        out.arg("target=host-no_runtime");
        out.output().expect("failed to run gen")
    }
    }
}

#[cfg(test)]
mod tests {
    use std::path::{Path, PathBuf};
    use crate::Halide_Build::*;
    use std::io;
    use std::io::prelude::*;
    #[test]
    fn it_works() {
        let mut Hs= Halide{
            halide_path: PathBuf::from("/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide"),
            gen_path: PathBuf::from("/home/rootbutcher2/CLionProjects/Halide_Build/Halide_gens"),
            rs_out_path: PathBuf::from("/home/rootbutcher2/CLionProjects/Halide_Build/rs_out"),
            generators: vec![]
        };
        Hs.generators.push(Halide_Gen{
            halide_path: Hs.halide_path,
            gen_name: "iir_blur".to_string(),
            gen_path: Hs.gen_path,
            rs_out_path: Hs.rs_out_path
        });
        let out1 = Hs.generators[0].compile_gen();
        println!("status: {}", out1.status);
        io::stdout().write_all(&out1.stdout).unwrap();
        io::stderr().write_all(&out1.stderr).unwrap();
        //assert!(output.status.success());

        let out2 = Hs.generators[0].run_gen();
        println!("status: {}", out2.status);
        io::stdout().write_all(&out2.stdout).unwrap();
        io::stderr().write_all(&out2.stderr).unwrap();
        assert!(out2.status.success());
    }
}
