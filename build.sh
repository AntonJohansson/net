#!/bin/sh

CC=clang
BUILDDIR=build
CLIENT=${BUILDDIR}/client
SERVER=${BUILDDIR}/server
CFLAGS="-fsanitize=address -g -O2 -lpthread -lm -std=gnu2x -Wno-constant-logical-operand"

[ ! -d ${BUILDDIR} ] && mkdir ${BUILDDIR}

if [ "$1" = "nodraw" ]; then
    ${CC} -o ${SERVER}-nodraw ${CFLAGS} src/server.c                              &
else
    ${CC} -o ${SERVER}        ${CFLAGS} src/server.c src/draw.c -lraylib -DDRAW   &
    ${CC} -o ${CLIENT}        ${CFLAGS} src/client.c src/draw.c -lraylib -DCLIENT &
fi

wait
