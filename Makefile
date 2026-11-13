# Dead Arrival - build (Linux)
# raylib is built from source into a static lib; see build_windows.sh for the
# Windows cross-compile and .github/workflows/build.yml for CI builds.

CC      = gcc
CFLAGS  = -O2 -Wall -Wno-unused-variable -Wno-unused-function -std=c11
IFLAGS  = -Isrc -Ideps/raylib/src
LDFLAGS =
LDLIBS  = -lraylib -lGL -lX11 -lXrandr -lXinerama -lXi -lXcursor -lasound -lpthread -ldl -lm

SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)
BIN     = deadarrival

# If a local raylib source tree exists, build the static lib from it.
RAYLIB_A = deps/raylib-$(shell uname -m).a

.PHONY: all clean selftest

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) -o $(BIN) -Ldeps $(LDLIBS)

src/%.o: src/%.c src/game.h
	$(CC) $(CFLAGS) $(IFLAGS) -c $< -o $@

selftest: $(BIN)
	./$(BIN) --selftest

clean:
	rm -f $(OBJ) $(BIN)
