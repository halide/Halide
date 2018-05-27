#!/bin/bash

echo "
  verbose
  open 192.168.86.2
  user anonymous pass
  put $1
  bye
" | ftp -n
