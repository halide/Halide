(* convert whatever we've loaded to an Rgb24.t image *)
let im = match Images.load "test.png" [] with
  | Images.Index8 i -> Index8.to_rgb24 i
  | Images.Index16 i -> Index16.to_rgb24 i
  | Images.Rgb24 i -> i
  | Images.Rgba32 i -> Rgb24.of_rgba32 i
  | Images.Cmyk32 i -> raise Images.Wrong_image_type

(* Copy a scanline string into a char Array *)
let to_bytes scanline =
  let arr = Array.make (String.length scanline) '\000' in
  let idx = 0 in
  let i = ref idx in
  let f b =
    arr.(!i) <- b;
    Printf.printf "%d: %d\n" !i (Char.code b);
    i := !i + 1;
  in
    String.iter f scanline;
    arr

let bytearr = to_bytes (Rgb24.get_scanline im 0)
