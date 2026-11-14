# ============================================================================
# Dead Arrival - plain GNU Make build (no CMake required)
#
# raylib selection order (Linux/macOS):
#   1. system raylib 6.2+ found via pkg-config       -> RAYLIB_MODE=system-pkgconfig
#   2. system raylib 6.2+ headers+lib in /usr[/local] -> RAYLIB_MODE=system-header
#   3. bundled copy under RAYLIB_PATH (deps/raylib)  -> RAYLIB_MODE=bundled
#      ('make deps' clones and builds raylib 6.2 exactly)
#
# Force a mode with e.g.:  make RAYLIB_MODE=bundled
# ============================================================================

CC          ?= cc
RAYLIB_PATH ?= deps/raylib
RAYLIB_TAG  ?= 6.2
BUILD       := build
BIN         := $(BUILD)/dead_arrival

SRC_DIR  := src
SRCS     := $(wildcard $(SRC_DIR)/*.c)
OBJS     := $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SRCS))
DEPS     := $(OBJS:.o=.d)

CFLAGS   ?= -O2
CFLAGS   += -std=c99 -Wall -Wextra -Wno-missing-field-initializers -I$(SRC_DIR)
LDFLAGS  ?=
LDLIBS   := -lGL -lX11 -lXcursor -lXrandr -lXi -lXinerama -lm -lpthread -ldl -lrt

ifeq ($(OS),Windows_NT)
    LDLIBS := -lopengl32 -lgdi32 -lwinmm -lshell32
endif

# ---- raylib detection ------------------------------------------------------
ifneq ($(OS),Windows_NT)
    PKG_CONFIG ?= pkg-config
    # system raylib via pkg-config, version in [6.2, 7.0)
    RL_PC_OK := $(shell $(PKG_CONFIG) --atleast-version=6.2 raylib 2>/dev/null && \
                         ! $(PKG_CONFIG) --atleast-version=7.0 raylib 2>/dev/null && echo 1)
    # system raylib installed without pkg-config: header in a standard prefix
    RL_H := $(firstword $(wildcard /usr/local/include/raylib.h /usr/include/raylib.h))
    ifneq ($(RL_H),)
        # raylib 6.x headers expose RAYLIB_VERSION_MAJOR / _MINOR integers
        RL_VER := $(shell m=$$(sed -n 's/^#define RAYLIB_VERSION_MAJOR \([0-9][0-9]*\).*/\1/p' $(RL_H) | head -1); \
                          n=$$(sed -n 's/^#define RAYLIB_VERSION_MINOR \([0-9][0-9]*\).*/\1/p' $(RL_H) | head -1); \
                          [ -n "$$m" ] && echo "$$m.$$n")
        # accept 6.2 .. 6.x
        RL_OK  := $(shell m=$$(sed -n 's/^#define RAYLIB_VERSION_MAJOR \([0-9][0-9]*\).*/\1/p' $(RL_H) | head -1); \
                          n=$$(sed -n 's/^#define RAYLIB_VERSION_MINOR \([0-9][0-9]*\).*/\1/p' $(RL_H) | head -1); \
                          [ -n "$$m" ] && [ "$$m" -eq 6 ] && [ "$$n" -ge 2 ] && echo 1)
    else
        RL_VER :=
        RL_OK  :=
    endif
endif

ifeq ($(RAYLIB_MODE),)
    ifeq ($(RL_PC_OK),1)
        RAYLIB_MODE := system-pkgconfig
    else ifeq ($(RL_OK),1)
        RAYLIB_MODE := system-header
    else
        RAYLIB_MODE := bundled
    endif
endif

ifeq ($(RAYLIB_MODE),system-pkgconfig)
    RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
    RAYLIB_LINK   := $(shell $(PKG_CONFIG) --libs raylib)
    $(info == raylib: system install ($(shell $(PKG_CONFIG) --modversion raylib)) via pkg-config)
else ifeq ($(RAYLIB_MODE),system-header)
    RL_PREFIX     := $(patsubst %/include/raylib.h,%,$(RL_H))
    RAYLIB_CFLAGS := -I$(RL_PREFIX)/include
    RAYLIB_LINK   := -L$(RL_PREFIX)/lib -lraylib
    $(info == raylib: system install $(RL_VER) at $(RL_PREFIX))
else
    RAYLIB_MODE   := bundled
    RAYLIB_CFLAGS := -I$(RAYLIB_PATH)/src
    RAYLIB_LINK   := $(RAYLIB_PATH)/src/libraylib.a
    $(info == raylib: bundled copy at $(RAYLIB_PATH) - run 'make deps' to fetch raylib $(RAYLIB_TAG))
endif

CFLAGS += $(RAYLIB_CFLAGS)

.PHONY: all deps run clean assets

all: $(BIN)

# ---- link + compile rules (bundled raylib is built as a prerequisite) ------
ifeq ($(RAYLIB_MODE),bundled)

$(BIN): $(OBJS) $(RAYLIB_PATH)/src/libraylib.a
	@mkdir -p $(BUILD)
	$(CC) $(OBJS) $(RAYLIB_PATH)/src/libraylib.a -o $(BIN) $(LDFLAGS) $(LDLIBS)
	@echo "== Build OK: $(BIN) (run it from the repo root so ./assets is found)"

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(RAYLIB_PATH)/src/libraylib.a
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(RAYLIB_PATH)/src/libraylib.a: deps-cloned
	$(MAKE) -C $(RAYLIB_PATH)/src PLATFORM=PLATFORM_DESKTOP -j$$(nproc 2>/dev/null || echo 2)

deps-cloned:
	@if [ ! -d "$(RAYLIB_PATH)/src" ]; then \
		echo "== Fetching raylib $(RAYLIB_TAG) =="; \
		mkdir -p $(RAYLIB_PATH); \
		git clone --depth 1 --branch $(RAYLIB_TAG) https://github.com/raysan5/raylib.git $(RAYLIB_PATH); \
	fi
	@touch $@

else

$(BIN): $(OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(OBJS) $(RAYLIB_LINK) -o $(BIN) $(LDFLAGS) $(LDLIBS)
	@echo "== Build OK: $(BIN) (run it from the repo root so ./assets is found)"

$(BUILD)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

endif

# regenerate procedural textures & sounds if any are missing
assets:
	python3 tools/gen_assets.py --check

run: $(BIN)
	$(BIN)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
