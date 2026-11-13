# Dead Arrival - build (Linux)
# Uses the raylib already installed on your system; nothing is cloned or
# built automatically. Options:
#
#   make                    system raylib: pkg-config if available,
#                           else plain -lraylib (+ /usr/local paths)
#   make RAYLIB_STATIC=1    link the prebuilt deps/libraylib.a, headers from
#                           raylib-src/ (the CI layout; CI builds the lib)
#   make RAYLIB_CFLAGS=... RAYLIB_LIBS=...    fully manual override

CC      ?= gcc
CFLAGS  ?= -O2
CFLAGS  += -Wall -Wno-unused-variable -Wno-unused-function -std=c11

SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:.c=.o)
BIN     := deadarrival

# raylib's desktop (GLFW/X11/alsa) dependencies. Required when linking a
# static libraylib; harmless extras when raylib is a shared lib.
RL_DESKTOP := -lGL -lX11 -lxcb -lXau -lXdmcp -lxkbcommon -lXrandr -lXinerama \
              -lXi -lXcursor -lasound -lpthread -ldl -lm

ifeq ($(RAYLIB_STATIC),1)
  RAYLIB_MODE   := static deps/libraylib.a
  RAYLIB_CFLAGS ?= -Iraylib-src/src
  RAYLIB_LIBS   ?= -Ldeps -lraylib $(RL_DESKTOP)
  RL_LINK_DEPS  := deps/libraylib.a
else ifneq ($(shell pkg-config --exists raylib 2>/dev/null && echo yes),)
  RAYLIB_MODE   := system raylib via pkg-config
  RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib)
  RAYLIB_LIBS   ?= $(shell pkg-config --libs raylib) $(RL_DESKTOP)
  RL_LINK_DEPS  :=
else
  RAYLIB_MODE   := system raylib via -lraylib (+/usr/local)
  RAYLIB_CFLAGS ?= -I/usr/local/include
  RAYLIB_LIBS   ?= -L/usr/local/lib -lraylib $(RL_DESKTOP)
  RL_LINK_DEPS  :=
endif

$(info raylib: $(RAYLIB_MODE))

ifeq ($(RAYLIB_STATIC),1)
  ifeq ($(wildcard deps/libraylib.a raylib-src/src/raylib.h),)
    $(error RAYLIB_STATIC=1 needs deps/libraylib.a and raylib-src/src/raylib.h - \
build them first (see .github/workflows/build.yml) or run plain `make` to use system raylib)
  endif
endif

.PHONY: all selftest clean

all: $(BIN)

$(BIN): $(OBJ) $(RL_LINK_DEPS)
	$(CC) $(LDFLAGS) $(OBJ) -o $(BIN) $(RAYLIB_LIBS)

src/%.o: src/%.c src/game.h
	$(CC) $(CFLAGS) -Isrc $(RAYLIB_CFLAGS) -c $< -o $@

selftest: $(BIN)
	./$(BIN) --selftest

clean:
	rm -f $(OBJ) $(BIN)
