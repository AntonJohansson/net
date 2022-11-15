#!/bin/sh

CC=clang
BUILDDIR=build
CLIENT=${BUILDDIR}/client
SERVER=${BUILDDIR}/server

[ ! -d ${BUILDDIR} ] && mkdir ${BUILDDIR}

if [ "$1" = "nodraw" ]; then
    ${CC} -o ${SERVER}-nodraw -fsanitize=address src/server.c            -g -O0 -lpthread          -lm -std=gnu2x &
else
    ${CC} -o ${SERVER}        -fsanitize=address src/server.c src/draw.c -g -O0 -lpthread -lraylib -lm -std=gnu2x -DDRAW &
    ${CC} -o ${CLIENT}        -fsanitize=address src/client.c src/draw.c -g -O0 -lpthread -lraylib -lm -std=gnu2x -DCLIENT &
fi

wait
