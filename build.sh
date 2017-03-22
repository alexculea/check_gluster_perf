#!/bin/bash

target=$1

if [ "$target" == "debug" ]; then
    g++ -g -o main.out main.cpp
elif [ "$target" == "release" ]; then
    g++ -o main.out main.cpp
else 
    echo "Invalid or no build target specified."
    exit 1
fi;


