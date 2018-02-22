IM2COL=$1
# Columns are: schedule C W H N stride pad_width pad_height filter_width filter_height byte zero
$IM2COL 8 16 16 1 1 0 0 1 1 0
$IM2COL 8 16 16 1 1 1 1 3 3 0
$IM2COL 8 16 16 1 2 1 1 3 3 0
$IM2COL 8 16 16 1 2 2 2 5 5 0

$IM2COL 32 7 7 1 1 0 0 1 1 0
$IM2COL 32 7 7 1 1 1 1 3 3 0
$IM2COL 32 7 7 1 2 1 1 3 3 0
$IM2COL 32 7 7 4 2 2 2 5 5 0

$IM2COL 8 16 16 1 1 0 0 1 1 5
$IM2COL 8 16 16 1 1 1 1 3 3 5
$IM2COL 8 16 16 1 2 1 1 3 3 5
