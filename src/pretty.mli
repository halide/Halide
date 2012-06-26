type t                          (* a printable document *)

val empty : t                   (* empty document *)
val break:  t                   (* space or newline *)
val text:   string -> t         (* create document from string *)
val printf: ('a, unit, string, t) format4 -> 'a

val cat : t -> t -> t       (* clients define let (^^) = Pretty.cat *)

val nest : int -> t -> t

val group : t -> t

val to_string: int -> t -> string
val to_file:   out_channel -> int -> t -> unit
