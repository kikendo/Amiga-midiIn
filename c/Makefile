# Build with the AmigaPorts / Bebbo m68k-amigaos-gcc (tested target: 6.5.0b)
# and vasm, which comes with it.
#
#   make              build midiIn and the tests for plain 68000 (the AHI and Paula 14-bit
#                     engines still need a 68020 at run time; the program
#                     checks)
#   make CPU=-m68020  build the C code for 68020 and up
#   make clean
#
# If the tools are not on your PATH:
#   make CC=/opt/amiga/bin/m68k-amigaos-gcc VASM=/opt/amiga/bin/vasmm68k_mot
#
# The AHI headers are not part of this repository. If your toolchain has
# them (devices/ahi.h in its include folder) nothing needs doing. Otherwise
# point AHI_INC at the Developer/Include/C folder of the AHI developer
# archive (m68k-amigaos-ahidev.lha), or copy that folder to c/sdk/ahi:
#   make AHI_INC=/path/to/Developer/Include/C
#
# The C runtime option has to come last on the command line (see CRT), so it
# is kept out of CFLAGS.

ifeq ($(origin CC),default)
CC      := m68k-amigaos-gcc
endif
VASM    ?= vasmm68k_mot
CPU     ?= -m68000
CRT     ?= -noixemul
CFLAGS  ?= -O2 -std=gnu99 -Wall -Wextra -fomit-frame-pointer
AHI_INC ?= sdk/ahi
AHI_CFLAGS := $(if $(wildcard $(AHI_INC)/devices/ahi.h),-I$(AHI_INC))
BUILD   := build

AUDIO   := src/audio
SND_OBJS := $(BUILD)/snd.o $(BUILD)/snd_ahi.o $(BUILD)/snd_paula4.o \
            $(BUILD)/snd_paula14.o $(BUILD)/paula4.o $(BUILD)/paula14.o \
            $(BUILD)/softmix.o $(BUILD)/play14.o $(BUILD)/hookentry.o

APP_SRCS := $(wildcard src/app/*.c) $(wildcard src/gui/*.c)
APP_OBJS := $(patsubst src/%.c,$(BUILD)/%.o,$(APP_SRCS))
APP_CFLAGS := -Iinclude -Wno-multichar -Wno-clobbered
# libnix's libm: double maths through mathieeedoubbas.library, as the E code
APP_LIBS := -lm

all: $(BUILD)/midiIn $(BUILD)/paula4_demo $(BUILD)/snd_demo

# the program
$(BUILD)/midiIn: $(APP_OBJS) $(SND_OBJS)
	$(CC) $(CPU) -o $@ $^ $(APP_LIBS) $(CRT)

$(BUILD)/app/%.o: src/app/%.c $(wildcard src/app/*.h) $(wildcard src/gui/*.h) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPU) $(CFLAGS) $(APP_CFLAGS) $(AHI_CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/gui/%.o: src/gui/%.c $(wildcard src/app/*.h) $(wildcard src/gui/*.h) | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CPU) $(CFLAGS) $(APP_CFLAGS) $(AHI_CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/paula4_demo: $(BUILD)/paula4_demo.o $(BUILD)/paula4.o
	$(CC) $(CPU) -o $@ $^ $(CRT)

$(BUILD)/snd_demo: $(BUILD)/snd_demo.o $(SND_OBJS)
	$(CC) $(CPU) -o $@ $^ $(CRT)

$(BUILD)/paula4.o: $(AUDIO)/paula4.c $(AUDIO)/paula4.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/snd.o: $(AUDIO)/snd.c $(AUDIO)/snd.h $(AUDIO)/snd_backend.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/snd_paula4.o: $(AUDIO)/snd_paula4.c $(AUDIO)/snd_backend.h $(AUDIO)/paula4.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/paula14.o: $(AUDIO)/paula14.c $(AUDIO)/paula14.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/snd_paula14.o: $(AUDIO)/snd_paula14.c $(AUDIO)/snd_backend.h $(AUDIO)/paula14.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/snd_ahi.o: $(AUDIO)/snd_ahi.c $(AUDIO)/snd.h $(AUDIO)/snd_backend.h $(AUDIO)/softmix.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) $(AHI_CFLAGS) -c -o $@ $< $(CRT)

# softmix.s and play14.s are 68020 code whatever CPU the C code is built for
$(BUILD)/softmix.o: $(AUDIO)/softmix.s | $(BUILD)
	$(VASM) -Fhunk -m68020 -quiet -o $@ $<

$(BUILD)/play14.o: $(AUDIO)/play14.s | $(BUILD)
	$(VASM) -Fhunk -m68020 -quiet -o $@ $<

$(BUILD)/hookentry.o: $(AUDIO)/hookentry.s | $(BUILD)
	$(VASM) -Fhunk -m68000 -quiet -o $@ $<

$(BUILD)/paula4_demo.o: tests/paula4_demo.c $(AUDIO)/paula4.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD)/snd_demo.o: tests/snd_demo.c $(AUDIO)/snd.h | $(BUILD)
	$(CC) $(CPU) $(CFLAGS) -c -o $@ $< $(CRT)

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)

.PHONY: all clean
