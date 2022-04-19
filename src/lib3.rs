pub mod halide_build {

    use std::path::{Path, PathBuf};
    use std::process::{Command, Output};
    use std::env;

    pub struct Halide {
        pub halide_path: PathBuf,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,
        pub generators: Vec<HalideGen>,
    }
    pub struct HalideGen {
        pub gen_name: String,
        pub halide_path: PathBuf,
        pub gen_path: PathBuf,
        pub rs_out_path: PathBuf,


        debug: bool,
        args: Vec<String>,

        target: String,
        //pub command : Command,

    }

    impl Halide {
        pub fn new<T: Into<PathBuf>>(halide_path: T) ->Halide{
            Halide{
                halide_path: halide_path.into(),
                gen_path: PathBuf::from(env::var_os("OUT_DIR").unwrap()),
                rs_out_path: PathBuf::from(env::var_os("OUT_DIR").unwrap()),
                generators: vec![]
            }
        }

        pub fn gen_path<T: Into<PathBuf>>(mut self, gen_path:T)->Halide{
            self.gen_path=gen_path.into();
            self
        }

        pub fn out_path<T:Into<PathBuf>>(mut self, rs_path: T)-> Halide{
            self.rs_out_path=rs_path.into();
            self
        }

        pub fn newGen<T:Into<String>>(mut self, genName: T){
            self.generators.push(HalideGen::new(genName,&self));
        }
    }
    impl HalideGen {

        pub fn new<T:Into<String>>(gen_name : T, input : &Halide) ->HalideGen{
            HalideGen{
                gen_name : gen_name.into(),
                halide_path: input.halide_path.to_path_buf(),
                gen_path: input.gen_path.to_path_buf(),
                rs_out_path: input.rs_out_path.to_path_buf(),
                debug: false,
                args: vec![],
                target: "".to_string(),
                //command: command::new("g++"),
            }
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
        let Halide_test = Halide::new("/home/rootbutcher2/CLionProjects/Halide-Rusts-tests/Halide");
        Halide_test.newGen("iir_blur");
    }
}
