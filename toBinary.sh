#!/bin/bash

# This script writes the content of the provided file to STDOUT and replaces each digit (0-9a-f) by its binary representation.

if [ "$1" == "" ]; then
   echo
   echo "usage: $0 <inputFilePath>"
   echo
   exit 1
fi

cat $1 | sed -r 's/([0-9a-f])([0-9a-f])/@\1 @\2   /g' \
   | sed -r 's/@0/0000/g' \
   | sed -r 's/@1/0001/g' \
   | sed -r 's/@2/0010/g' \
   | sed -r 's/@3/0011/g' \
   | sed -r 's/@4/0100/g' \
   | sed -r 's/@5/0101/g' \
   | sed -r 's/@6/0110/g' \
   | sed -r 's/@7/0111/g' \
   | sed -r 's/@8/1000/g' \
   | sed -r 's/@9/1001/g' \
   | sed -r 's/@a/1010/g' \
   | sed -r 's/@b/1011/g' \
   | sed -r 's/@c/1100/g' \
   | sed -r 's/@d/1101/g' \
   | sed -r 's/@e/1110/g' \
   | sed -r 's/@f/1111/g'