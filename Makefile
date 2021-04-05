INCLUDE_DIRS = -Ilib -Ilib/asio/include -Ilib/json
NIX_LIBS = -pthread -static-libgcc
WIN32_LIBS = -lwsock32 -lws2_32 -pthread -static-libgcc
WIN64_LIBS = -lwsock32 -lws2_32 -pthread -static-libgcc -Wl,--high-entropy-va

CPP_FLAGS = -Os -s -ffunction-sections -fdata-sections -DNDEBUG #-O2 -flto=8 
LD_FLAGS = -Wl,-gc-sections

reset2snes: main.cpp USB2SNES.cpp USB2SNES.h
	g++ -o $@ main.cpp USB2SNES.cpp $(INCLUDE_DIRS) $(CPP_FLAGS) $(NIX_LIBS)

reset2snes.exe: main.cpp USB2SNES.cpp USB2SNES.h
	x86_64-w64-mingw32-g++ -static -o $@ main.cpp USB2SNES.cpp $(INCLUDE_DIRS) $(CPP_FLAGS) $(LD_FLAGS) $(WIN64_LIBS)

all: reset2snes reset2snes.exe
