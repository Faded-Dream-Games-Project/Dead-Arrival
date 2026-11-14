# Dead Arrival - plain GNU Make build (no CMake required)
# Builds against raylib 6.2. Run `make deps` once to fetch and build raylib,
# or point RAYLIB_PATH at an existing raylib checkout / installed library.

CC       ?= gcc
RAYLIB_PATH ?= deps/raylib
RAYLIB_TAG ?= 6.2
BUILD    := build
BIN      := $(BUILD)/dead_arrival

SRC_DIR  := src
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

CFLAGS   ?= -O2
CFLAGS   += -std=c99 -Wall -Wextra -Wno-missing-field-initializers \
            -I$(SRC_DIR) -I$(RAYLIB_PATH)/src -I$(RAYLIB_PATH)/include \
            $(shell test -d /usr/local/include/raylib.h 2>/dev/null || test -f /usr/local/include/raylib.h && echo -I/usr/local/include)
LDFLAGS  ?=
LDLIBS   := -lGL -lX11 -lXcursor -lXrandr -lXi -lXinerama -lm -lpthread -ldl -lrt

ifeq ($(OS),Windows_NT)
    LDLIBS := -lopengl32 -lgdi32 -lwinmm -lshell32
endif

RAYLIB_LIB := $(RAYLIB_PATH)/src/libraylib.a

.PHONY: all deps run clean assets

all: $(BIN)

$(BIN): $(OBJS) $(RAYLIB_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(OBJS) $(RAYLIB_LIB) -o $(BIN) $(LDFLAGS) $(LDLIBS)
	@echo "== Build OK: $(BIN) (run it from the repo root so ./assets is found)"

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(RAYLIB_LIB)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(RAYLIB_LIB): deps-cloned
	$(MAKE) -C $(RAYLIB_PATH)/src PLATFORM=PLATFORM_DESKTOP -j$$(nproc 2>/dev/null || echo 2)

deps-cloned:
	@if [ ! -d "$(RAYLIB_PATH)/src" ]; then \
		echo "== Fetching raylib $(RAYLIB_TAG) =="; \
		mkdir -p $(RAYLIB_PATH); \
		git clone --depth 1 --branch $(RAYLIB_TAG) https://github.com/raysan5/raylib.git $(RAYLIB_PATH); \
	fi
	@touch $@

# Regenerate procedural textures & sounds if any are missing
assets:
	python3 tools/gen_assets.py --check

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
