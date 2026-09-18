# ShoryuMux — HORI XInput fight stick -> keystrokes / agent approvals
#
#   make core      build the shoryumuxd daemon            -> build/shoryumuxd
#   make app       macOS: build the menu-bar app bundle   -> build/ShoryuMux.app
#   make           core (+ app on macOS)
#   make install   copy default config to ~/.config/shoryumux/ (if absent)
#   make clean
#
# Windows: cmake -B build && cmake --build build --config Release

UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)

CORE_COMMON := core/main.c core/config.c core/actions.c core/xinput_report.c
CORE_BIN    := build/shoryumuxd
CONFIG_DIR  := $(HOME)/.config/shoryumux
CFLAGS      ?= -O2 -Wall

ifeq ($(UNAME_S),Darwin)
CC          ?= clang
CORE_SRC    := $(CORE_COMMON) core/compat_posix.c core/input_macos.c core/output_macos.c
CORE_LIBS   := -framework IOKit -framework CoreFoundation -framework ApplicationServices -framework CoreGraphics
else ifeq ($(UNAME_S),Linux)
CC          ?= gcc
CORE_SRC    := $(CORE_COMMON) core/compat_posix.c core/input_linux.c core/output_linux.c
USB_CFLAGS  := $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
USB_LIBS    := $(shell pkg-config --libs libusb-1.0 2>/dev/null)
ifeq ($(USB_LIBS),)
USB_CFLAGS  := -I/usr/include/libusb-1.0
USB_LIBS    := -lusb-1.0
endif
CFLAGS      += $(USB_CFLAGS)
CORE_LIBS   := $(USB_LIBS)
else
$(error Unsupported uname '$(UNAME_S)'. On Windows use: cmake -B build && cmake --build build)
endif

.PHONY: all core app install clean

ifeq ($(UNAME_S),Darwin)
all: core app
else
all: core
endif

core: $(CORE_BIN)

$(CORE_BIN): $(CORE_SRC) core/shoryu.h | build
	$(CC) $(CFLAGS) -o $@ $(CORE_SRC) $(CORE_LIBS)
	@echo "built $@"

build:
	@mkdir -p build

app:
ifeq ($(UNAME_S),Darwin)
	@bash scripts/build-app.sh
else
	@echo "menu-bar app is macOS-only; other platforms use the CLI daemon"
endif

install: core
	@mkdir -p "$(CONFIG_DIR)"
	@if [ ! -f "$(CONFIG_DIR)/config.conf" ]; then \
		cp core/config.conf.default "$(CONFIG_DIR)/config.conf"; \
		echo "installed default config -> $(CONFIG_DIR)/config.conf"; \
	else \
		echo "config already exists -> $(CONFIG_DIR)/config.conf (left untouched)"; \
	fi

clean:
	rm -rf build
	rm -rf app/.build
