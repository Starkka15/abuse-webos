#!/bin/bash

# WebOS cross-compilation script for Abuse
# Uses arm-2011.03 toolchain + HPwebOS PDK

TOOLCHAIN=/home/stark/arm-2011.03
PDK=/home/stark/HPwebOS/PDK

export PATH=$TOOLCHAIN/bin:$(pwd):$PATH
export CC=arm-none-linux-gnueabi-gcc
export CXX=arm-none-linux-gnueabi-g++
export AR=arm-none-linux-gnueabi-ar
export RANLIB=arm-none-linux-gnueabi-ranlib

export CFLAGS="-mcpu=cortex-a8 -mfpu=neon -mfloat-abi=softfp -O2 -I$PDK/include -I$PDK/include/SDL -D__webos__"
export CXXFLAGS="$CFLAGS"
export CPPFLAGS="-I$PDK/include -I$PDK/include/SDL"
export LDFLAGS="-L$PDK/device/lib -lpdl -lGLES_CM -lEGL"
export LIBS="-lSDL -lSDL_mixer -lpdl -lGLES_CM -lEGL"

export SDL_CFLAGS="-I$PDK/include/SDL -D_GNU_SOURCE=1 -D_REENTRANT"
export SDL_LIBS="-L$PDK/device/lib -lSDL"

./configure \
    --host=arm-none-linux-gnueabi \
    --prefix=/media/cryptofs/apps/usr/palm/applications/org.abuse.game \
    --with-assetdir=/media/cryptofs/apps/usr/palm/applications/org.abuse.game/data \
    --disable-sdltest

echo ""
echo "Now run: make"
