


pub mod Halide_Build{
    use std::ffi::OsString;
    use std::path::PathBuf;
    use std::process::Command;

    pub struct Halide{
        halide_path: PathBuf,
        gen_path: PathBuf,
        rs_out_path: PathBuf,
        generators: Vec<Halide_Gen>,
    }

    struct Halide_Gen{
        halide_path: PathBuf,
        gen_name: String,
        gen_path: PathBuf,
        rs_out_path: PathBuf,
    }

    impl Halide_Gen {
        fn compile_gen(&mut self) -> std::io::Result<Output> {
            let mut out = Command::new("g++");

            out.args(["-O3", "-O3","-std=c++17"]);

            self.halide_path.push("distrib");
            self.halide_path.push("include");

            out.args(["-I",self.halide_path.as_os_str()]);

            self.halide_path.pop();
            self.halide_path.push("tools");
            out.args(["-I",self.halide_path.as_os_str()]);

            self.gen_path.push(gen_name);
            self.halide_path.push("GenGen.cpp");
            out.args(["-g",self.gen_path.as_os_str(),self.halide_path.as_os_str()]);

            self.gen_path.set_extension(".gen");
            out.args(["-o",self.gen_path.as_os_str()]);

            self.halide_path.pop();
            self.halide_path.pop();
            self.halide_path.push("lib");
            out.args(["-L",self.halide_path.as_os_str()]);

            let mut temp:String = "-Wl,-rpath,".to_string();
            temp.push_str(self.halide_path.to_str().unwrao());
            out.arg(temp);

            out.args(["-lHalide","-ldl","-lpthread","-lz"]);

            out.output()
        }

    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        let result = 2 + 2;
        assert_eq!(result, 4);
    }
}
