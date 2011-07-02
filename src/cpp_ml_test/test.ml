open Callback

type foo = 
  | Foo1 
  | Foo2 of int
  | Foo3 of string
  | Foo4 of int * int 

let eatFoo = function
  | Foo1 -> Printf.printf "It's a Foo1\n"
  | Foo2 x -> Printf.printf "It's a Foo2: %d\n" x
  | Foo3 x -> Printf.printf "It's a Foo3: %s\n" x
  | Foo4 (x, y) -> Printf.printf "It's a Foo4: %d %d\n" x y

and _ = Callback.register "makeFoo1" (fun _ -> Foo1)
let _ = Callback.register "makeFoo2" (fun x -> Foo2 x)
let _ = Callback.register "makeFoo3" (fun x -> Foo3 x)
let _ = Callback.register "makeFoo4" (fun x y -> Foo4 (x, y))
let _ = Callback.register "eatFoo" (fun x -> eatFoo x; flush stdout)

