open Bigarray
external load_png : string -> (int, int8_unsigned_elt, c_layout) Array3.t = "load_png"
external save_png : (int, int8_unsigned_elt, c_layout) Array3.t -> string -> unit = "save_png"

    
