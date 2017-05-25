#!/bin/bash

echo Running $1

$1
if [[ "$?" -ne "0" ]]
then
  echo "Success"
  exit 0
fi

echo "Expected Failure"
exit -1
