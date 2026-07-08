CC ?= cc
PKG_CONFIG ?= pkg-config
BUILD_DIR := build
APP_NAME := gnomekiosk-demo
APP := $(BUILD_DIR)/$(APP_NAME)
SRC := src/main.c
CPPFLAGS +=
CFLAGS += -std=c11 -Wall -Wextra
LDLIBS += -lm

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 2>/dev/null)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 2>/dev/null)

all: $(APP)

$(APP): $(SRC)
	mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS) $(LDLIBS)

clean:
	rm -f $(APP)

.PHONY: all clean
