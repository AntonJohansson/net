#!/bin/sh

for i in {1..100}; do
    ./build/client-stress 127.0.0.1 $i &
done
