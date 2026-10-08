# Build with the AmigaPorts / Bebbo m68k-amigaos-gcc (tested target: 6.5.0b).
#
#   make              build for plain 68000
#   make CPU=-m68020  build for 68020 and up
#   make clean
#
# If the compiler is not on your PATH:  make CC=/opt/amiga/bin/m68k-amigaos-gcc
#
# The C runtime option has to come last on the command line (see CRT), so it
# is kept out of CFLAGS.

CC      ?= m68k-amigaos-gcc
CPU     ?= -m68000
CRT     ?= -noixemul
CFLAGS  ?= -O2 -std=gnu99 -Wall -Wextra -fomit-frame-pointer
BUILD   := build

AUDIO_OBJS := $(BUILD)/paula4.o

all: $(BUILD)/paula4_demo

$(BUILD)/paula4_demo: $(BUILD)/paula4_demo.o $(AUDIO_OBJS)
	$(CC) $(CPU) -o $@ $^ $(CRT)

$(BUILD)/paula4.o: src/audio/paula4.c src/audio/paula4.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/paula4_demo.o: tests/paula4_demo.c src/audio/paula4.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
