#!/bin/sh

CC=gcc
BUILD=build
THIRD_PARTY=third_party
CLIENT=${BUILD}/client
SERVER=${BUILD}/server
CFLAGS="-fsanitize=address -g -O3 -lpthread -lm -std=gnu2x -Wno-constant-logical-operand -I${BUILD}/raylib/raylib/include"

# Create build directory if needed
[ ! -d ${BUILD} ] && mkdir ${BUILD}

# Initialize git submodules if needed
[ ! -d ${THIRD_PARTY}/raylib ] && git submodule update --init

# Build raylib if needed
[ ! -d ${BUILD}/raylib ] && mkdir ${BUILD}/raylib && cmake -DUSE_WAYLAND=on -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${BUILD} -S ${THIRD_PARTY}/raylib -B ${BUILD}/raylib && make -j16 -C ${BUILD}/raylib && make install -C ${BUILD}/raylib

${CC} -o ${SERVER}-nodraw ${CFLAGS} src/server.c src/game.c ${BUILD}/lib/libraylib.a &
${CC} -o ${SERVER}        ${CFLAGS} src/server.c src/game.c src/draw.c src/audio.c ${BUILD}/lib/libraylib.a -DDRAW &
${CC} -o ${CLIENT}        ${CFLAGS} src/client.c src/game.c src/draw.c src/audio.c ${BUILD}/lib/libraylib.a -DDRAW -DCLIENT &

wait
