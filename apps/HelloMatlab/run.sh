#!/bin/bash

command -v octave >/dev/null 2>&1 || { echo >&2 "Octave not found.  Aborting."; exit 0; }

rm -f blurred.png
octave run.m

if [ -f blurred.png ]
then
    echo "Success!"
    exit 0
fi

echo "Failed to produce blurred.png!"
exit 1


    
