#!/bin/sh

for i in {1..100}; do
    ./build/client-stress $i &
done
