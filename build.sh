#!/bin/sh

CC=clang
BUILDDIR=build
CLIENT=${BUILDDIR}/client
CLIENT_STRESS=${BUILDDIR}/client-stress
SERVER=${BUILDDIR}/server

[ ! -d ${BUILDDIR} ] && mkdir ${BUILDDIR}

${CC} -o ${SERVER}        src/server.c src/draw.c -g -O2 -lraylib -lm -std=gnu2x -DDRAW &
${CC} -o ${CLIENT}        src/client.c src/draw.c -g -O2 -lraylib -lm -std=gnu2x -DCLIENT &
${CC} -o ${CLIENT_STRESS} src/client.c -g -O2          -lm -std=gnu2x -DSTRESS &

wait
