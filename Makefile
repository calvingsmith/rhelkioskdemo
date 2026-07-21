CC ?= cc
PKG_CONFIG ?= pkg-config
BUILD_DIR := build
APP_NAME := gnomekiosk-demo
APP := $(BUILD_DIR)/$(APP_NAME)
SHELL_APP_NAME := gnomekiosk-demo-shell
SHELL_APP := $(BUILD_DIR)/$(SHELL_APP_NAME)
SRC := src/main.c
CPPFLAGS +=
CFLAGS += -std=c11 -Wall -Wextra
LDLIBS += -lm

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 2>/dev/null)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 2>/dev/null)

# `make`       -> gnome-kiosk build: no D-Bus/layout-helper code compiled in at all.
# `make shell` -> gnome-shell build: adds exact position + reliable minimize via
#                 the "Kiosk Layout Helper" extension (gnome-shell-extension/).
all: $(APP)

shell: $(SHELL_APP)

$(APP): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS) $(LDLIBS)

$(SHELL_APP): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DWITH_LAYOUT_HELPER $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS) $(LDLIBS)

clean:
	rm -f $(APP) $(SHELL_APP)

.PHONY: all shell clean
