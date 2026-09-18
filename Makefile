# ShoryuMux — HORI XInput fight stick -> macOS keystrokes / agent approvals
#
#   make core      build the shoryumuxd daemon            -> build/shoryumuxd
#   make app       build the menu-bar app bundle          -> build/ShoryuMux.app
#   make           build both
#   make install   copy default config to ~/.config/shoryumux/ (if absent)
#   make clean

CC       := clang
CORE_SRC := core/shoryumuxd.c
CORE_BIN := build/shoryumuxd
CORE_FW  := -framework IOKit -framework CoreFoundation -framework ApplicationServices -framework CoreGraphics
CONFIG_DIR := $(HOME)/.config/shoryumux

.PHONY: all core app install clean

all: core app

core: $(CORE_BIN)

$(CORE_BIN): $(CORE_SRC) | build
	$(CC) -O2 -Wall -o $@ $(CORE_SRC) $(CORE_FW)
	@echo "built $@"

build:
	@mkdir -p build

app:
	@bash scripts/build-app.sh

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
