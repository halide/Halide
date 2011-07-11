open Img
open Bigarray

let _ =
  let im = load_png "test.png" in 
  for c = 0 to 2 do 
    for y = 0 to 15 do
      for x = 0 to 15 do 
        Array3.set im c y x ((Array3.get im c y x) + 1)
      done
    done
  done;
  save_png im "out.png"
