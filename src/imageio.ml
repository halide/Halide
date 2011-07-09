open Bigarray

exception Unsupported_image_format

let load path =
  (* load the image *)
  let im = Images.load path [] in

  (* unpack metadata *)
  let (w,h) = Images.size im in

  (* convert down to rgb24 *)
  let rgb = match im with
    | Images.Index8 i -> Index8.to_rgb24 i
    | Images.Rgb24  i -> i
    | Images.Rgba32 i -> Rgb24.of_rgba32 i
    | _ -> raise Unsupported_image_format
  in

  (* extract raw bytes into 1D uint8 Bigarray *)
  let bigarray_of_string str =
    let arr = Array1.create int8_unsigned c_layout (String.length str) in
      for i=0 to (String.length str)-1 do
        arr.{i} <- Char.code str.[i]
      done;
      arr
  in

    (* NOTE: Rgb24.dump may raise out of memory for large images *)
    (w, h, bigarray_of_string(Rgb24.dump rgb))

let save arr w h path =
  (* allocate string of equal size and transfer bigarray bytes into it *)
  let bigarray_to_string arr =
    let str = String.create (Array1.dim arr) in
      for i=0 to (Array1.dim arr)-1 do
        str.[i] <- Char.chr arr.{i}
      done;
      str
  in

  (* create Rgb24 Image.t from a byte buffer *)
  let create_im w h buf =
    let rgb = Rgb24.create_with w h [] buf in
      Images.Rgb24(rgb)
  in

    Images.save path None [] (create_im w h (bigarray_to_string arr))
