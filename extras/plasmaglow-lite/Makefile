CC ?= cc
CFLAGS ?= -O2
CPPFLAGS += -Iinclude
CFLAGS += -std=c99 -Wall -Wextra -Werror
LDLIBS = -lX11 -l:libXNVCtrl.so.0 -lm

all: plasmaglow-lite vibrance

plasmaglow-lite: plasmaglow-lite.c

vibrance: vibrance.c

.PHONY: all
