#!/bin/awk -f

function abs(x) {return x < 0 ?  -x : x;}

BEGIN{}

{
  if ($2 == prev_prediction) {
    printf("%s %s %f %f %f %f\n", $1, prev_filename, $2, $3, prev_actual, abs($3 - prev_actual));
  }
}

{ prev_filename = $1 }
{ prev_prediction = $2 }
{ prev_actual = $3 }

END {}
