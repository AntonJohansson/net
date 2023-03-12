@echo off

if not exist build\ (
	mkdir build
)

if not exist third_party\raylib\ (
	git submodule update --init
)

if not exist build\raylib\ (
	mkdir build\raylib
	cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=build -S third_party\raylib -B build\raylib
)

pushd build
cl /LINK /SUBSYSTEM:WINDOWS /F 16000000 /ZI /MD /Ox /std:c17 ..\src\client.c ..\src\game.c ..\src\draw.c ..\src\audio.c /I raylib\raylib\include raylib\raylib\Release\raylib.lib winmm.lib gdi32.lib opengl32.lib kernel32.lib user32.lib shell32.lib /D DRAW /D CLIENT /D _USE_MATH_DEFINES
popd
