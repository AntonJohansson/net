#!/bin/sh

alacritty --working-directory $(pwd) -e ./build/server &
sleep 0.1
alacritty --working-directory $(pwd) -e ./build/client &
sleep 0.1
alacritty --working-directory $(pwd) -e ./build/client &
