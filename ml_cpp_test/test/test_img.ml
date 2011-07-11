open Img
open Bigarray

let _ =
  let im = load_png "test.png" in 
  for c = 0 to (Array3.dim1 im)-1 do 
    for y = 0 to (Array3.dim2 im)-1 do
      for x = 0 to (Array3.dim3 im)-1 do 
        Array3.set im c y x (255 - (Array3.get im c y x))
      done
    done
  done;
  save_png im "out.png"
