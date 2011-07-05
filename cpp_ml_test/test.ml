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

let _ = 
  Callback.register "makeFoo1" (fun _ -> Foo1);
  Callback.register "makeFoo2" (fun x -> Foo2 x);
  Callback.register "makeFoo3" (fun x -> Foo3 x);
  Callback.register "makeFoo4" (fun x y -> Foo4 (x, y));
  Callback.register "eatFoo" (fun x -> eatFoo x; flush stdout);

