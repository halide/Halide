
(* Recursively remove all calls in a statement using the given
   environment and schedule tree *)
val lower_stmt :
  Ir.stmt -> Ir.environment -> Schedule.schedule_tree -> Ir.stmt

(* Generate the realization of some function over the region specified
   in the schedule tree. *)
val lower_function :
  Schedule.StringMap.key ->
  Ir.environment -> Schedule.schedule_tree -> Ir.stmt
