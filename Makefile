CC ?= cc
PKG_CONFIG ?= pkg-config
APP := gnomekiosk-demo
SRC := src/main.c
CPPFLAGS +=
CFLAGS += -std=c11 -Wall -Wextra
LDLIBS += -lm

GTK_CFLAGS := $(shell $(PKG_CONFIG) --cflags gtk4 2>/dev/null)
GTK_LIBS := $(shell $(PKG_CONFIG) --libs gtk4 2>/dev/null)

all: $(APP)

$(APP): $(SRC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) -o $@ $< $(GTK_LIBS) $(LDLIBS)

clean:
	rm -f $(APP)

.PHONY: all clean
