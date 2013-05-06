(* Based on modules from https://code.google.com/p/quest-tester/

Copyright (c) 2004, 2005 Christian Lindig <lindig@eecs.harvard.edu>. All
rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.

Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the following
disclaimer in the documentation and/or other materials provided
with the distribution.

THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR AND COPYRIGHT HOLDER BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*)

type t =
  | Empty
  | Cons    of t * t
  | Text    of string
  | Nest    of int * t
  | Break   of string
  | Group   of t

let space               = " "

let cat left right      = Cons(left,right)
let empty               = Empty
let text s              = Text(s)
let printf fmt          = Printf.kprintf (fun msg -> Text(msg)) fmt
let nest i t            = Nest(i,t)
let break               = Break(space)
let group t             = Group(t)

type mode       = Flat | Brk
let strlen      = String.length
let rec fits w  = function (* tail recursive *)
  | _ when w < 0           -> false
  | []                     -> true
  | (i,m,Empty)       :: z -> fits w z
  | (i,m,Cons(x,y))   :: z -> fits w ((i,m,x)::(i,m,y)::z)
  | (i,m,Nest(j,x))   :: z -> fits w ((i+j,m,x)::z)
  | (i,m,Text(s))     :: z -> fits (w - strlen s) z
  | (i,Flat,Break(s)) :: z -> fits (w - strlen s) z
  | (i,Brk,Break(_))  :: z -> true
  | (i,m,Group(x))    :: z -> fits w ((i,m,x)::z)

let rec layout: ('a -> string -> 'a) -> 'a -> int -> t -> 'a = 
  fun emit out w x -> 
  let nl  acc i = emit (emit acc "\n") (String.make i ' ') in
  let rec loop acc w k = function (* tail recursive *)
    | []                          -> acc
    | (i,m,Empty)       :: z -> loop acc w k z
    | (i,m,Cons(x,y))   :: z -> loop acc w k ((i,m,x)::(i,m,y)::z)
    | (i,m,Nest(j,x))   :: z -> loop acc w k ((i+j,m,x)::z)
    | (i,m,Text(s))     :: z -> loop (emit acc s) w (k + strlen s) z
    | (i,Flat,Break(s)) :: z -> loop (emit acc s) w (k + strlen s) z
    | (i,Brk,Break(s))  :: z -> loop (nl   acc i) w i z 
    | (i,Flat,Group(x)) :: z -> loop acc w k ((i,Flat,x)::z)  (*optimization*)
    | (i,m,Group(x))    :: z -> if fits (w-k) ((i,Flat,x)::z) (*scan beyond x!*)
                                then loop acc w k ((i,Flat,x)::z)
                                else loop acc w k ((i,Brk, x)::z)
  in
    loop out w 0 [(0,Brk,x)]

let to_string w t = 
  let buf     = Buffer.create 256     in      (* grows as needed *)
  let emit () = Buffer.add_string buf in
  let ()      = layout emit () w t   in
      Buffer.contents buf

let to_file oc w t = 
  let emit () = output_string oc in
    layout emit () w t

module Test = struct

let (^^) = cat
let (^/) x y    = if x = empty then y 
                  else if y = empty then x 
                  else x ^^ break ^^ y
let nest        = nest 4
let group x     = group (text "<" ^^ x ^^ text ">")
let (^+) x y    = x ^^ nest (break ^^ y)                  
let (<<) f g x  = f (g x)

let rec list sep f xs =
  let rec loop acc = function
    | []    -> acc
    | [x]   -> acc ^^ f x 
    | x::xs -> loop (acc ^^ f x ^^ sep) xs
  in
    loop empty xs 

let bracket l x r =
  group (l ^^ nest (break ^^ x) ^/ r)

let rec repeat n x = match n with
  | 0 -> []
  | n -> x :: repeat (n-1) x

let words n = list break text (repeat n "foobar")

let ifthen cond body = 
  text "if (" ^^ cond ^^ text ")" ^^ bracket (text "{") body (text "}")

end
