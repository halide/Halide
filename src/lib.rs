pub mod halide_build {

    use std::path::PathBuf;
    use std::process::{Command, Output};

    pub struct Halide {
        pub halide_path: PathBuf,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,
        pub generators: Vec<HalideGen>,
    }

    pub struct HalideGen {
        pub halide_path: PathBuf,
        pub gen_name: String,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,
    }
    impl Halide {}
    impl HalideGen {
        pub fn compile_gen(&mut self) -> Output {
            let mut out = Command::new("g++");

            out.args(["-O3", "-O3", "-std=c++17"]);

            self.halide_path.push("distrib");
            self.halide_path.push("include");

            out.args(["-I", self.halide_path.to_str().unwrap()]);

            self.halide_path.pop();
            self.halide_path.push("tools");
            out.args(["-I", self.halide_path.to_str().unwrap()]);

            self.gen_path.push(self.gen_name.as_str());
            self.gen_path.set_extension("cpp");
            self.halide_path.push("GenGen.cpp");
            out.args(["-g",
                self.gen_path.to_str().unwrap(),
                self.halide_path.to_str().unwrap(),
            ]);

            self.gen_path.set_extension("gen");
            out.args(["-o", self.gen_path.to_str().unwrap()]);

            self.halide_path.pop();
            self.halide_path.pop();
            self.halide_path.push("lib");
            out.args(["-L", self.halide_path.to_str().unwrap()]);

            let mut temp: String = "-Wl,-rpath,".to_string();
            temp.push_str(self.halide_path.to_str().unwrap());
            out.arg(temp);

            out.args(["-lHalide", "-ldl", "-lpthread", "-lz"]);

            out.output().expect("Building the generator failed")
        }
        pub fn run_gen(&mut self) -> Output {
            let mut out = Command::new(self.gen_path.to_str().unwrap());
            out.args(["-g", self.gen_name.as_str()]);
            out.args(["-f", self.gen_name.as_str()]);
            self.gen_path.pop();
            out.args(["-o", self.gen_path.to_str().unwrap()]);
            out.arg("target=host-no_runtime");
            out.output().expect("failed to run gen")
        }

        pub fn rename_files(&mut self) -> std::io::Result<()> {
            self.gen_path.push(self.gen_name.as_str());
            self.gen_path.set_extension("a");
            let mut temp = self.gen_path.clone();
            temp.pop();
            let mut temp2 ="lib".to_string();
            temp2.push_str(self.gen_name.as_str());
            temp.push(temp2);
            temp.set_extension("a");
            println!("gen_path: {:?}", self.gen_path);
            println!("gen_path 2: {:?}",temp);
            std::fs::rename(self.gen_path.to_str().unwrap(),temp.to_str().unwrap())
        }

        pub fn bind(&mut self) -> std::io::Result<()> {
            self.gen_path.set_extension("h");
            self.rs_out_path.push(self.gen_name.as_str());
            self.rs_out_path.set_extension("rs");
            let bindings = bindgen::Builder::default()
                .header(self.gen_path.to_str().unwrap().to_string())
                .allowlist_function(self.gen_name.as_str())
                .blocklist_item("halide_buffer_t")
                .generate().expect("unable to generate");
            bindings.write_to_file(self.rs_out_path.as_path())
        }
    }
}

#[cfg(test)]
mod tests {
    use crate::halide_build::*;
    use std::io;
    use std::io::prelude::*;
    use std::path::PathBuf;
    #[test]
    fn it_works() {
        let mut hs = Halide {
            halide_path: PathBuf::from("/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide"),
            gen_path: PathBuf::from("/home/rootbutcher2/CLionProjects/halide_build/Halide_gens"),
            rs_out_path: PathBuf::from("/home/rootbutcher2/CLionProjects/halide_build/rs_out"),
            generators: vec![],
        };
        hs.generators.push(HalideGen {
            halide_path: hs.halide_path,
            gen_name: "iir_blur".to_string(),
            gen_path: hs.gen_path,
            rs_out_path: hs.rs_out_path,
        });
        let out1 = hs.generators[0].compile_gen();
        println!("status gen: {}", out1.status);
        io::stdout().write_all(&out1.stdout).unwrap();
        //io::stderr().write_all(&out1.stderr).unwrap();
        assert!(out1.status.success());

        let out2 = hs.generators[0].run_gen();
        println!("status run gen: {}", out2.status);
        io::stdout().write_all(&out2.stdout).unwrap();
        //io::stderr().write_all(&out2.stderr).unwrap();
        assert!(out2.status.success());

        let out3 = hs.generators[0].rename_files();
        println!("status run rename: {:?}", out3);
        assert!(out3.is_ok());

        let out4 = hs.generators[0].bind();
        println!("status run rename: {:?}", out4);
        assert!(out4.is_ok());
    }
}
