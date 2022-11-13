#!/bin/sh

CC=clang
BUILDDIR=build
CLIENT=${BUILDDIR}/client
CLIENT_STRESS=${BUILDDIR}/client-stress
SERVER=${BUILDDIR}/server

[ ! -d ${BUILDDIR} ] && mkdir ${BUILDDIR}

${CC} -o ${SERVER}        -fsanitize=address src/server.c src/draw.c -g -O0 -lpthread -lraylib -lm -std=gnu2x -DDRAW &
${CC} -o ${CLIENT}        -fsanitize=address src/client.c src/draw.c -g -O0 -lpthread -lraylib -lm -std=gnu2x -DCLIENT &
${CC} -o ${CLIENT_STRESS} -fsanitize=address src/client.c -g -O0            -lpthread          -lm -std=gnu2x -DSTRESS &

wait
