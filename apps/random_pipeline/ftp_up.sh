#!/bin/bash

echo "
  verbose
  open 192.168.86.250
  user anonymous pass
  cd samples
  put $1
  bye
" | ftp -n
