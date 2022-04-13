


pub mod Halide_Build{
    use std::ffi::OsString;
    use std::path::PathBuf;
    use std::io;
    use std::io::prelude::*;
    use std::process::{Command, Output};

    pub struct Halide{
        pub halide_path: PathBuf,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,
        pub generators: Vec<Halide_Gen>,
    }

    pub struct Halide_Gen{
        pub(crate) halide_path: PathBuf,
        pub(crate) gen_name: String,
        pub(crate) gen_path: PathBuf,
        pub(crate) rs_out_path: PathBuf,
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
            gen_name: "iir_blur.cpp".to_string(),
            gen_path: Hs.gen_path,
            rs_out_path: Hs.rs_out_path
        });
        let output = Hs.generators[0].compile_gen();
        println!("status: {}", output.status);
        io::stdout().write_all(&output.stdout).unwrap();
        io::stderr().write_all(&output.stderr).unwrap();
        assert!(output.status.success());
    }
}
