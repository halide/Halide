open Util

let _ =
  let lt a b =
  if (a mod 3 = b mod 3) then
    Some (a <= b)
  else 
    None
  in
  let l = [87;24;1;6;3;7;2;4;8;1;23;5] in
  let sorted = partial_sort lt l in
  Printf.printf "%s\n" (String.concat ", " (List.map string_of_int sorted));
    
  
    
