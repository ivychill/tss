#!/bin/bash
make clean
git add *
echo $1
git commit -m "$1"
git push origin master
