open Bigarray

(* construct an empty bigarray of identical size/kind/layout *)
let clone_bigarray arr =
  Array1.create
    (Array1.kind arr)
    (Array1.layout arr)
    (Array1.dim arr)

(* clone and copy the contents of a bigarray *)
let copy_bigarray arr =
  let cp = clone_bigarray arr in
    Array1.blit arr cp;
    cp
