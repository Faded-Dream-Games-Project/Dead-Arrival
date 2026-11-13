# Dead Arrival - build (Linux)
# raylib 6.2 is cloned and built from source automatically on first build
# (static link). Windows: see .github/workflows/build.yml.
# Extra system deps vs 5.5: libxcb1-dev libxkbcommon-dev libxau-dev libxdmcp-dev

CC      ?= gcc
CFLAGS  = -O2 -Wall -Wno-unused-variable -Wno-unused-function -std=c11
IFLAGS  = -Isrc -Iraylib-src/src
LDLIBS  = -lGL -lX11 -lxcb -lXau -lXdmcp -lxkbcommon -lXrandr -lXinerama -lXi -lXcursor -lasound -lpthread -ldl -lm

SRC     = $(wildcard src/*.c)
OBJ     = $(SRC:.c=.o)
BIN     = deadarrival
RLIB    = deps/libraylib.a

.PHONY: all clean selftest

all: $(BIN)

raylib-src:
	git clone --depth 1 --branch 6.2 https://github.com/raysan5/raylib.git raylib-src

$(RLIB): | raylib-src
	@mkdir -p obj deps
	@for f in raylib-src/src/*.c; do \
	  o="obj/rl_$(basename $$f).o"; \
	  $(CC) -O2 -DPLATFORM_DESKTOP -D_GLFW_X11 -Iraylib-src/src \
	    -Iraylib-src/src/external/glfw/include -c "$$f" -o "$$o" || exit 1; \
	done
	ar rcs $@ obj/rl_*.o

$(BIN): $(OBJ) $(RLIB)
	$(CC) $(OBJ) -o $(BIN) -Ldeps $(LDLIBS)

src/%.o: src/%.c src/game.h
	$(CC) $(CFLAGS) $(IFLAGS) -c $< -o $@

selftest: $(BIN)
	./$(BIN) --selftest

clean:
	rm -f $(OBJ) $(BIN)
