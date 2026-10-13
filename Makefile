# scgt2026 renderer: cross-build glide2x.dll (32-bit) with MinGW-w64
CXX      = i686-w64-mingw32-g++
CXXFLAGS = -std=c++20 -O2 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -Irenderer -Ithird_party -Wall -Wno-unused-function -Wno-missing-field-initializers -fno-strict-aliasing
LDFLAGS  = -shared -static -static-libgcc -static-libstdc++ -s -Wl,--enable-stdcall-fixup
SRC      = $(wildcard renderer/*.cxx)
OBJ      = $(patsubst renderer/%.cxx,build/%.o,$(SRC))
OUT      = build/glide2x.dll

all: $(OUT)

build/%.o: renderer/%.cxx $(wildcard renderer/*.hxx)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OUT): $(OBJ) renderer/glide2x.def
	$(CXX) -o $@ $(OBJ) renderer/glide2x.def $(LDFLAGS) -ld3d9

shaders:
	python3 tools/build_shaders.py

clean:
	rm -rf build
